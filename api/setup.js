// Разовая настройка после деплоя: прописывает вебхук и меню команд.
// Открывается как /api/setup?key=<WEBHOOK_SECRET>
import { tg, baseUrl } from '../lib/telegram.js';

const TOKEN = process.env.TELEGRAM_BOT_TOKEN;
const SECRET = process.env.WEBHOOK_SECRET;

const COMMANDS = [
  { command: 'meme',    description: 'Случайный мем про дистанции' },
  { command: 'top',     description: 'Топ непреодолимых дистанций' },
  { command: 'spravka', description: 'Именная справка о преодолении' },
  { command: 'help',    description: 'Что я умею' },
];

export default async function handler(req, res) {
  if (!TOKEN) return res.status(500).json({ ok: false, error: 'TELEGRAM_BOT_TOKEN не задан' });
  // Без секрета не работаем: иначе вебхук смог бы переставить кто угодно.
  if (!SECRET) return res.status(500).json({ ok: false, error: 'WEBHOOK_SECRET не задан' });

  const key = new URL(req.url, 'http://x').searchParams.get('key');
  if (key !== SECRET) return res.status(401).json({ ok: false, error: 'неверный key' });

  const base = baseUrl(req);
  try {
    const me = await tg(TOKEN, 'getMe', {});
    await tg(TOKEN, 'setWebhook', {
      url: base + '/api/telegram',
      secret_token: SECRET,
      allowed_updates: ['message', 'callback_query', 'inline_query'],
      drop_pending_updates: true,
      max_connections: 40,
    });
    await tg(TOKEN, 'setMyCommands', { commands: COMMANDS });
    const info = await tg(TOKEN, 'getWebhookInfo', {});

    return res.status(200).json({
      ok: true,
      bot: '@' + me.username,
      webhook: info.url,
      pending: info.pending_update_count,
      commands: COMMANDS.length,
      note: 'Inline-режим включается у @BotFather: /setinline',
    });
  } catch (err) {
    return res.status(500).json({ ok: false, error: err?.message || String(err) });
  }
}

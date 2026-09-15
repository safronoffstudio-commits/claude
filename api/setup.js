// Настройка вебхука. Открывается с телефона:
//   /api/setup?token=<ТОКЕН БОТА>      — если WEBHOOK_SECRET ещё не заведён
//   /api/setup?key=<WEBHOOK_SECRET>    — обычный путь
import { tg, baseUrl, authorize } from '../lib/telegram.js';

const TOKEN = process.env.TELEGRAM_BOT_TOKEN;
const SECRET = process.env.WEBHOOK_SECRET;

const COMMANDS = [
  { command: 'meme',    description: 'Случайный мем про дистанции' },
  { command: 'top',     description: 'Топ непреодолимых дистанций' },
  { command: 'spravka', description: 'Именная справка о преодолении' },
  { command: 'help',    description: 'Что я умею' },
];

export default async function handler(req, res) {
  if (!TOKEN) {
    return res.status(500).json({
      ok: false,
      error: 'TELEGRAM_BOT_TOKEN не задан на Vercel',
      что_делать: 'Vercel → Settings → Environment Variables → добавить, затем Deployments → Redeploy',
    });
  }
  if (!(await authorize(req))) {
    return res.status(401).json({
      ok: false,
      error: 'нужен ?key=<WEBHOOK_SECRET> или ?token=<токен бота>',
    });
  }

  const base = baseUrl(req);
  try {
    const me = await tg(TOKEN, 'getMe', {});
    const hook = {
      url: base + '/api/telegram',
      allowed_updates: ['message', 'callback_query', 'inline_query'],
      drop_pending_updates: true,
      max_connections: 40,
    };
    // Без секрета вебхук тоже работает, но принимает POST от кого угодно.
    if (SECRET) hook.secret_token = SECRET;

    await tg(TOKEN, 'setWebhook', hook);
    await tg(TOKEN, 'setMyCommands', { commands: COMMANDS });
    const info = await tg(TOKEN, 'getWebhookInfo', {});

    return res.status(200).json({
      ok: true,
      бот: '@' + me.username,
      вебхук: info.url,
      в_очереди: info.pending_update_count,
      защита_секретом: SECRET ? 'включена' : 'ВЫКЛЮЧЕНА — заведи WEBHOOK_SECRET и открой эту ссылку ещё раз',
      дальше: 'Открой @' + me.username + ' и напиши /start',
    });
  } catch (err) {
    return res.status(500).json({ ok: false, error: err?.message || String(err) });
  }
}

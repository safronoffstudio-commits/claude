// Нужен лендингу, чтобы подставить ссылку на бота.
import { tg } from '../lib/telegram.js';

export default async function handler(req, res) {
  try {
    const me = await tg(process.env.TELEGRAM_BOT_TOKEN, 'getMe', {});
    res.setHeader('cache-control', 'public, max-age=300');
    return res.status(200).json({ ok: true, username: me.username, name: me.first_name });
  } catch {
    return res.status(200).json({ ok: false });
  }
}

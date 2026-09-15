// Тонкая обёртка над Bot API.

const BASE = 'https://api.telegram.org/bot';

export async function tg(token, method, body) {
  const res = await fetch(BASE + token + '/' + method, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body || {}),
  });
  let data;
  try {
    data = await res.json();
  } catch {
    throw new Error(method + ': не удалось разобрать ответ (' + res.status + ')');
  }
  if (!data.ok) throw new Error(method + ': ' + (data.description || res.status));
  return data.result;
}

// Базовый публичный URL деплоя — нужен, чтобы Telegram сам забрал картинку.
export function baseUrl(req) {
  if (process.env.PUBLIC_URL) return process.env.PUBLIC_URL.replace(/\/+$/, '');
  const host =
    (req.headers['x-forwarded-host'] || req.headers.host || process.env.VERCEL_URL || '')
      .toString()
      .split(',')[0]
      .trim();
  const proto = (req.headers['x-forwarded-proto'] || 'https').toString().split(',')[0].trim();
  return proto + '://' + host;
}

export function memeImageUrl(base, { seed, name, target, brand, force }) {
  const p = new URLSearchParams({ s: seed });
  if (name) p.set('n', name);
  if (target) p.set('t', target);
  if (brand) p.set('b', brand);
  if (force) p.set('f', force);
  return base + '/api/og?' + p.toString();
}

// Экранирование под parse_mode: MarkdownV2
export function mdEscape(s) {
  return String(s).replace(/([_*\[\]()~`>#+\-=|{}.!\\])/g, '\\$1');
}

export const moreKeyboard = (label = 'Ещё дистанцию') => ({
  inline_keyboard: [[{ text: label, callback_data: 'more' }]],
});

// Vercel обычно отдаёт уже разобранный JSON, но если по какой-то причине
// пришла строка, буфер или непрочитанный поток — молча потерять апдейт нельзя:
// Telegram получит 200 и не покажет никакой ошибки, а бот будет казаться мёртвым.
export async function readUpdate(req) {
  const b = req.body;
  if (b && typeof b === 'object' && !Buffer.isBuffer(b)) return b;
  let raw = null;
  if (typeof b === 'string') raw = b;
  else if (Buffer.isBuffer(b)) raw = b.toString('utf8');
  else {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    raw = Buffer.concat(chunks).toString('utf8');
  }
  try {
    return JSON.parse(raw);
  } catch {
    console.error('не смог разобрать тело апдейта:', String(raw).slice(0, 200));
    return {};
  }
}

// Доступ к служебным ручкам. Пускаем по WEBHOOK_SECRET или по самому токену
// бота: кто знает токен — и так полностью владеет ботом, слабее не становится.
// Нужно, чтобы чинить бота с телефона, когда секрет ещё не заведён.
export async function authorize(req) {
  const TOKEN = process.env.TELEGRAM_BOT_TOKEN;
  const SECRET = process.env.WEBHOOK_SECRET;
  const qs = new URL(req.url, 'http://x').searchParams;
  let key = qs.get('key');
  let token = qs.get('token');
  if (req.method === 'POST') {
    const body = await readUpdate(req);
    key = key || body.key || null;
    token = token || body.token || null;
  }
  if (SECRET && key && key === SECRET) return true;
  if (TOKEN && token && token === TOKEN) return true;
  return false;
}

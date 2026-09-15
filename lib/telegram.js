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

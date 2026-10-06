// Пересылает ответы с сайта тебе в Telegram.
//
// Переменные окружения (Vercel → Project → Settings → Environment Variables):
//   TELEGRAM_BOT_TOKEN — токен бота от @BotFather
//   TELEGRAM_CHAT_ID   — твой числовой id в Telegram (например, от @userinfobot)
//
// GET  /api/notify — проверка настройки (ничего не отправляет)
// POST /api/notify — { event: 'open' | 'yes' | 'final', ... } → сообщение в Telegram

const LIMITS = { name: 40, activity: 40, activities: 12, wish: 500, body: 32 * 1024 };

export default async function handler(req, res) {
  const token = (process.env.TELEGRAM_BOT_TOKEN || '').trim();
  const chatId = (process.env.TELEGRAM_CHAT_ID || '').trim();

  if (req.method === 'GET') {
    reply(res, 200, await diagnose(token, chatId));
    return;
  }
  if (req.method !== 'POST') {
    res.setHeader('Allow', 'GET, POST');
    reply(res, 405, { ok: false, error: 'method_not_allowed' });
    return;
  }
  if (!token || !chatId) {
    reply(res, 500, { ok: false, error: 'not_configured' });
    return;
  }

  let body;
  try {
    body = await readJson(req);
  } catch {
    reply(res, 400, { ok: false, error: 'bad_request' });
    return;
  }

  const text = buildMessage(body);
  if (!text) {
    reply(res, 400, { ok: false, error: 'bad_event' });
    return;
  }

  const tg = await telegram(token, 'sendMessage', { chat_id: chatId, text, parse_mode: 'HTML' });
  if (!tg.ok) {
    console.error('Telegram sendMessage failed:', tg.description);
    reply(res, 502, { ok: false, error: 'telegram', description: tg.description });
    return;
  }
  reply(res, 200, { ok: true });
}

function buildMessage(body) {
  if (!body || typeof body !== 'object' || Array.isArray(body)) return null;

  const name = clean(body.name, LIMITS.name);
  const who = name ? ` · ${esc(name)}` : '';
  const attempts = toCount(body.attempts);
  const lines = [];
  if (body.test === true) lines.push('🧪 <b>ТЕСТ</b> — это проверка, не настоящий ответ', '');

  switch (body.event) {
    case 'open':
      lines.push(`👀 Приглашение открыли${who}`, 'Ждём ответ…');
      break;

    case 'yes':
      lines.push(`💘 <b>ДА!</b> Свидание будет${who}`, '', `🙈 Попыток нажать «Нет»: ${attempts}`);
      if (body.viaNo === true) lines.push('😂 Кнопка «Нет» сдалась и сама превратилась в «Да»');
      lines.push('', 'Сейчас выбирается дата…');
      break;

    case 'final': {
      const date = formatDate(body.date);
      if (!date) return null;
      const activities = (Array.isArray(body.activities) ? body.activities : [])
        .slice(0, LIMITS.activities)
        .map((item) => clean(item, LIMITS.activity))
        .filter(Boolean);
      const wish = clean(body.wish, LIMITS.wish, { multiline: true });

      lines.push(`💌 <b>Свидание назначено!</b>${who}`, '');
      lines.push(`🗓 <b>Дата:</b> ${date}`);
      lines.push(`🕖 <b>Время:</b> ${formatTime(body.time)}`);
      if (activities.length) lines.push(`✨ <b>Планы:</b> ${activities.map(esc).join(', ')}`);
      if (wish) lines.push(`💬 <b>Пожелание:</b> ${esc(wish)}`);
      lines.push('', `🙈 Попыток нажать «Нет»: ${attempts}`);
      break;
    }

    default:
      return null;
  }

  return lines.join('\n');
}

async function diagnose(token, chatId) {
  const where = 'Vercel → Settings → Environment Variables, потом Deployments → Redeploy';
  if (!token) return { ok: false, message: `Не задан TELEGRAM_BOT_TOKEN. Добавь его: ${where}.` };
  if (!chatId) return { ok: false, message: `Не задан TELEGRAM_CHAT_ID. Добавь его: ${where}.` };

  const me = await telegram(token, 'getMe', {});
  if (!me.ok) {
    return {
      ok: false,
      message: 'Telegram не принял TELEGRAM_BOT_TOKEN — проверь, что токен от @BotFather скопирован целиком.',
      telegram: me.description,
    };
  }

  const bot = `@${me.result.username}`;
  // «Печатает…» на секунду — так проверяем, что бот может тебе писать, не отправляя сообщений.
  const probe = await telegram(token, 'sendChatAction', { chat_id: chatId, action: 'typing' });
  if (!probe.ok) {
    return {
      ok: false,
      bot,
      message: `Бот не может написать в чат ${chatId}. Открой ${bot} в Telegram, нажми «Запустить» (Start) и проверь TELEGRAM_CHAT_ID.`,
      telegram: probe.description,
    };
  }

  return { ok: true, bot, message: 'Всё настроено ✅ Ответы будут приходить тебе в личку от бота.' };
}

async function telegram(token, method, payload) {
  try {
    const res = await fetch(`https://api.telegram.org/bot${token}/${method}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(8000),
    });
    const data = await res.json().catch(() => null);
    if (data && typeof data === 'object') return data;
    return { ok: false, description: `HTTP ${res.status}` };
  } catch (err) {
    const timeout = err && err.name === 'TimeoutError';
    return { ok: false, description: timeout ? 'Telegram не ответил вовремя' : 'Не удалось связаться с Telegram' };
  }
}

async function readJson(req) {
  // На Vercel JSON уже разобран в req.body; в остальных случаях читаем поток сами.
  let body = req.body;
  if (body === undefined) {
    const chunks = [];
    let size = 0;
    for await (const chunk of req) {
      size += chunk.length;
      if (size > LIMITS.body) throw new Error('body_too_large');
      chunks.push(chunk);
    }
    body = Buffer.concat(chunks).toString('utf8');
  }
  if (Buffer.isBuffer(body)) body = body.toString('utf8');
  if (typeof body === 'string') body = body ? JSON.parse(body) : {};
  return body;
}

function reply(res, status, data) {
  res.statusCode = status;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.setHeader('Cache-Control', 'no-store');
  res.end(JSON.stringify(data, null, 2));
}

function formatDate(value) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(typeof value === 'string' ? value : '');
  if (!m) return null;
  const [y, mo, d] = [Number(m[1]), Number(m[2]), Number(m[3])];
  const date = new Date(Date.UTC(y, mo - 1, d));
  if (date.getUTCFullYear() !== y || date.getUTCMonth() !== mo - 1 || date.getUTCDate() !== d) return null;
  return date
    .toLocaleDateString('ru-RU', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric', timeZone: 'UTC' })
    .replace(/\s*г\.$/, '');
}

function formatTime(value) {
  if (value === 'any') return 'не важно — решай ты 😉';
  const m = /^([01]?\d|2[0-3]):([0-5]\d)$/.exec(typeof value === 'string' ? value : '');
  return m ? `${m[1].padStart(2, '0')}:${m[2]}` : 'не указано';
}

function clean(value, max, { multiline = false } = {}) {
  if (typeof value !== 'string') return '';
  let text = value.replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '');
  text = multiline
    ? text.replace(/\r\n?/g, '\n').replace(/[ \t]+/g, ' ').replace(/\n{3,}/g, '\n\n')
    : text.replace(/\s+/g, ' ');
  const chars = Array.from(text.trim());
  return chars.length > max ? `${chars.slice(0, max).join('')}…` : chars.join('');
}

function toCount(value) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.min(Math.max(Math.trunc(n), 0), 9999) : 0;
}

function esc(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

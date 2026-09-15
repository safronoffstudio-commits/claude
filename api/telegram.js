import { generateMeme, memeToText } from '../lib/memes.js';
import { newSeed } from '../lib/rng.js';
import { tg, baseUrl, memeImageUrl, moreKeyboard } from '../lib/telegram.js';

const TOKEN = process.env.TELEGRAM_BOT_TOKEN;
const SECRET = process.env.WEBHOOK_SECRET;

const HELLO = [
  'Здорово. Я меряю дистанции.',
  '',
  'Настоящие — до Луны, до Владивостока, до дна Марианской впадины.',
  'И те, что человечеству не даются: от кровати до холодильника.',
  '',
  'Что умею:',
  '/meme — случайный мем',
  '/top — топ непреодолимых дистанций',
  '/spravka — именная справка о преодолении',
  '',
  'Или просто напиши, до чего тебе не дойти: «до зала», «до зарплаты».',
  'Ещё меня можно звать в любой чат: набери @БОТ и что угодно.',
].join('\n');

const HELP = [
  'Команды:',
  '/meme — случайный мем про дистанции',
  '/top — топ-5 непреодолимого',
  '/spravka — справка на твоё имя',
  '',
  'Любой текст превращается в мем: напиши «до спортзала» — и получишь.',
  'В любом чате работает inline: @БОТ до дедлайна',
].join('\n');

// Имя бота нужно для подписи на картинке. Кэшируем между вызовами.
let brandCache = null;
async function brand() {
  if (brandCache === null) {
    try {
      const me = await tg(TOKEN, 'getMe', {});
      brandCache = me.username ? '@' + me.username : '';
    } catch {
      brandCache = '';
    }
  }
  return brandCache;
}

async function sendMeme(chatId, base, opts = {}) {
  const seed = newSeed();
  const b = await brand();
  const meme = generateMeme(seed, opts);
  const photo = memeImageUrl(base, {
    seed, name: opts.name, target: opts.target, force: opts.force, brand: b,
  });

  try {
    await tg(TOKEN, 'sendPhoto', {
      chat_id: chatId,
      photo,
      reply_markup: moreKeyboard(),
    });
  } catch (err) {
    // Картинка не уехала — отдаём текстом, бот не должен молчать.
    console.error('sendPhoto упал, отдаю текстом:', err.message);
    await tg(TOKEN, 'sendMessage', {
      chat_id: chatId,
      text: memeToText(meme).replace(/[*_]/g, ''),
      reply_markup: moreKeyboard(),
    });
  }
}

async function handleMessage(msg, base) {
  const chatId = msg.chat.id;
  const name = msg.from?.first_name || null;
  const text = (msg.text || '').trim();
  const cmd = text.match(/^\/([a-z_]+)/i)?.[1]?.toLowerCase();

  if (cmd === 'start') {
    const b = await brand();
    await tg(TOKEN, 'sendMessage', {
      chat_id: chatId,
      text: HELLO.replace('@БОТ', b || '@бот'),
      reply_markup: moreKeyboard('Давай мем'),
    });
    return;
  }
  if (cmd === 'help') {
    const b = await brand();
    await tg(TOKEN, 'sendMessage', { chat_id: chatId, text: HELP.replace('@БОТ', b || '@бот') });
    return;
  }
  if (cmd === 'meme') return sendMeme(chatId, base, { name });
  if (cmd === 'top') return sendMeme(chatId, base, { name, force: 'top' });
  if (cmd === 'spravka') return sendMeme(chatId, base, { name: name || 'Аноним', force: 'cert' });

  // Неизвестная команда — не делаем вид, что это текст для мема
  if (cmd) {
    await tg(TOKEN, 'sendMessage', { chat_id: chatId, text: 'Такого не умею. Смотри /help' });
    return;
  }

  if (!text) return;
  return sendMeme(chatId, base, { name, target: text });
}

async function handleCallback(cq, base) {
  const chatId = cq.message?.chat?.id;
  try {
    await tg(TOKEN, 'answerCallbackQuery', { callback_query_id: cq.id });
  } catch { /* кнопка могла протухнуть — не страшно */ }
  if (chatId) await sendMeme(chatId, base, { name: cq.from?.first_name || null });
}

async function handleInline(q, base) {
  const target = (q.query || '').trim();
  const name = q.from?.first_name || null;
  const b = await brand();

  const results = [];
  for (let i = 0; i < 5; i++) {
    const seed = newSeed();
    const meme = generateMeme(seed, { name, target: target || null });
    const url = memeImageUrl(base, { seed, name, target: target || null, brand: b });
    results.push({
      type: 'photo',
      id: seed,
      photo_url: url,
      thumbnail_url: url,
      photo_width: 1200,
      photo_height: 800,
      title: meme.title,
      description: meme.punch,
    });
  }

  await tg(TOKEN, 'answerInlineQuery', {
    inline_query_id: q.id,
    results,
    cache_time: 0,
    is_personal: true,
    button: { text: 'Открыть бота', start_parameter: 'inline' },
  });
}

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    return res.status(200).json({ ok: true, hint: 'это вебхук для Telegram' });
  }
  if (!TOKEN) {
    console.error('TELEGRAM_BOT_TOKEN не задан');
    return res.status(200).json({ ok: false });
  }
  if (SECRET && req.headers['x-telegram-bot-api-secret-token'] !== SECRET) {
    return res.status(401).json({ ok: false });
  }

  const update = req.body || {};
  const base = baseUrl(req);

  try {
    if (update.message) await handleMessage(update.message, base);
    else if (update.callback_query) await handleCallback(update.callback_query, base);
    else if (update.inline_query) await handleInline(update.inline_query, base);
  } catch (err) {
    // Telegram повторяет апдейт при не-200 — отвечаем 200 всегда.
    console.error('ошибка обработки апдейта:', err?.message || err);
  }

  return res.status(200).json({ ok: true });
}

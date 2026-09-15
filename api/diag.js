// Диагностика: /api/diag  — короткий отчёт без секретов
//               /api/diag?key=<WEBHOOK_SECRET> — полный, с ошибкой вебхука от Telegram
import { tg, baseUrl } from '../lib/telegram.js';

const TOKEN = process.env.TELEGRAM_BOT_TOKEN;
const SECRET = process.env.WEBHOOK_SECRET;

export default async function handler(req, res) {
  const base = baseUrl(req);
  const steps = [];
  const add = (ok, name, detail) => steps.push({ ok, шаг: name, детали: detail });

  // 1. Переменные окружения
  add(!!TOKEN, 'TELEGRAM_BOT_TOKEN', TOKEN ? 'задан' : 'НЕ ЗАДАН — добавь в Vercel → Settings → Environment Variables и сделай Redeploy');
  add(!!SECRET, 'WEBHOOK_SECRET', SECRET ? 'задан' : 'НЕ ЗАДАН — без него не открыть /api/setup');

  const key = new URL(req.url, 'http://x').searchParams.get('key');
  const full = !!SECRET && key === SECRET;

  if (!TOKEN || !SECRET) {
    return res.status(200).json({
      итог: 'Не хватает переменных окружения',
      шаги: steps,
      что_делать: 'Добавь недостающие переменные в Vercel (Production), нажми Redeploy, потом открой /api/setup?key=<секрет>',
    });
  }

  if (!full) {
    return res.status(200).json({
      итог: 'Переменные на месте. Для полного отчёта открой /api/diag?key=<WEBHOOK_SECRET>',
      шаги: steps,
    });
  }

  // 2. Токен живой?
  let me = null;
  try {
    me = await tg(TOKEN, 'getMe', {});
    add(true, 'Токен', 'рабочий, бот @' + me.username);
  } catch (err) {
    add(false, 'Токен', 'Telegram отверг токен: ' + err.message + ' — перевыпусти у @BotFather');
  }

  // 3. Что Telegram думает про вебхук — здесь обычно и лежит причина
  let info = null;
  if (me) {
    try {
      info = await tg(TOKEN, 'getWebhookInfo', {});
      const want = base + '/api/telegram';
      if (!info.url) {
        add(false, 'Вебхук', 'НЕ ПРОПИСАН — открой /api/setup?key=<секрет>');
      } else if (info.url !== want) {
        add(false, 'Вебхук', 'смотрит на ' + info.url + ', а деплой живёт на ' + want + ' — открой /api/setup?key=<секрет>');
      } else {
        add(true, 'Вебхук', 'прописан верно: ' + info.url);
      }
      if (info.last_error_message) {
        add(false, 'Последняя ошибка доставки', info.last_error_message +
          ' (' + new Date((info.last_error_date || 0) * 1000).toISOString() + ')');
      } else if (info.url) {
        add(true, 'Ошибки доставки', 'Telegram не жалуется');
      }
      if (info.pending_update_count > 0) {
        add(false, 'Очередь', info.pending_update_count + ' необработанных апдейтов — функция падает или недоступна');
      }
    } catch (err) {
      add(false, 'Вебхук', 'не удалось спросить Telegram: ' + err.message);
    }
  }

  // 4. Рендер картинки жив?
  try {
    const r = await fetch(base + '/api/og?s=diag', { headers: { 'user-agent': 'diag' } });
    const type = r.headers.get('content-type') || '';
    if (r.ok && type.startsWith('image/')) {
      add(true, 'Генератор картинок', 'работает (' + type + ')');
    } else {
      const body = await r.text().catch(() => '');
      add(false, 'Генератор картинок', 'HTTP ' + r.status + ' ' + type + ' ' + body.slice(0, 200));
    }
  } catch (err) {
    add(false, 'Генератор картинок', 'не отвечает: ' + err.message);
  }

  // 5. Доступен ли деплой снаружи? Telegram обязан забрать картинку сам.
  const protectedHint = 'Если включена Deployment Protection (Vercel → Settings → Deployment Protection), ' +
    'Telegram не сможет ни достучаться до вебхука, ни забрать картинку. Для этого проекта её надо выключить.';

  const broken = steps.filter((s) => !s.ok);
  return res.status(200).json({
    итог: broken.length ? 'Найдено проблем: ' + broken.length : 'Всё на месте — бот должен отвечать',
    домен: base,
    проблемы: broken.map((s) => s.шаг + ': ' + s.детали),
    шаги: steps,
    подсказка: protectedHint,
  });
}

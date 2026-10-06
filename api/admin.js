// Админка: список ответов и удаление. Пароль — переменная окружения ADMIN_PASSWORD.
// POST /api/admin — { password, action: 'list' }  или  { password, action: 'delete', id }

import { createHash, timingSafeEqual } from 'node:crypto';
import { readJson, reply } from '../lib/http.js';
import { getStore, parsePathname } from '../lib/store.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    res.setHeader('Allow', 'POST');
    reply(res, 405, { ok: false, error: 'method_not_allowed' });
    return;
  }

  let body;
  try {
    body = await readJson(req);
  } catch {
    reply(res, 400, { ok: false, error: 'bad_request' });
    return;
  }

  const expected = (process.env.ADMIN_PASSWORD || '').trim();
  if (!expected) {
    reply(res, 503, { ok: false, error: 'admin_not_configured' });
    return;
  }
  if (!samePassword(body && body.password, expected)) {
    await new Promise((resolve) => setTimeout(resolve, 600)); // не даём быстро перебирать пароли
    reply(res, 401, { ok: false, error: 'wrong_password' });
    return;
  }

  const store = await getStore();
  if (!store) {
    reply(res, 503, { ok: false, error: 'storage_not_configured' });
    return;
  }

  try {
    if (body.action === 'delete') {
      if (typeof body.id !== 'string' || !/^[a-z0-9]{8,32}$/.test(body.id)) {
        reply(res, 400, { ok: false, error: 'bad_id' });
        return;
      }
      await store.remove(body.id);
      reply(res, 200, { ok: true });
      return;
    }
    reply(res, 200, { ok: true, storage: store.kind, visitors: await collect(store) });
  } catch (err) {
    console.error('Admin storage error:', err);
    reply(res, 502, { ok: false, error: 'storage_failed', message: String((err && err.message) || err).slice(0, 200) });
  }
}

function samePassword(given, expected) {
  const a = createHash('sha256').update(typeof given === 'string' ? given.trim() : '').digest();
  const b = createHash('sha256').update(expected).digest();
  return timingSafeEqual(a, b);
}

// Собирает события по людям: когда открыли, когда сказали «да», что выбрали в итоге.
async function collect(store) {
  const people = new Map();
  for (const item of await store.list()) {
    const meta = parsePathname(item.pathname);
    if (!meta) continue;
    if (!people.has(meta.visitorId)) people.set(meta.visitorId, []);
    people.get(meta.visitorId).push({ ...meta, item });
  }

  const visitors = [...people.entries()]
    .map(([id, events]) => {
      events.sort((a, b) => a.at - b.at);
      const of = (type) => events.filter((e) => e.event === type);
      const opens = of('open');
      const yeses = of('yes');
      const finals = of('final');
      return {
        id,
        openedAt: opens.length ? opens[0].at : null,
        yesAt: yeses.length ? yeses[0].at : null,
        finalAt: finals.length ? finals[finals.length - 1].at : null,
        lastAt: events[events.length - 1].at,
        opens: opens.length,
        finals: finals.length,
        // Читаем один файл на человека: самый полный из имеющихся.
        source: finals[finals.length - 1] || yeses[0] || events[events.length - 1],
      };
    })
    .sort((a, b) => b.lastAt - a.lastAt)
    .slice(0, 200);

  await mapLimit(visitors, 6, async (visitor) => {
    let record = null;
    try {
      record = await store.read(visitor.source.item);
    } catch (err) {
      console.error('Reading an answer failed:', err);
    }
    delete visitor.source;
    visitor.name = (record && record.name) || '';
    visitor.test = Boolean(record && record.test);
    visitor.attempts = (record && record.attempts) || 0;
    visitor.viaNo = Boolean(record && record.viaNo);
    visitor.answer = record && record.event === 'final'
      ? { date: record.date, time: record.time, activities: record.activities || [], wish: record.wish || '' }
      : null;
  });

  return visitors;
}

async function mapLimit(items, limit, fn) {
  let next = 0;
  const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length) await fn(items[next++]);
  });
  await Promise.all(workers);
}

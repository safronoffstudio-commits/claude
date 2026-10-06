// Принимает события со страницы приглашения и сохраняет их для админки.
// POST /api/answer — { event: 'open' | 'yes' | 'final', visitorId, ... }

import { readJson, reply } from '../lib/http.js';
import { getStore } from '../lib/store.js';

const LIMITS = { name: 40, activity: 40, activities: 12, wish: 500 };
const EVENTS = new Set(['open', 'yes', 'final']);

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

  const parsed = sanitize(body);
  if (!parsed) {
    reply(res, 400, { ok: false, error: 'bad_event' });
    return;
  }

  const store = await getStore();
  if (!store) {
    reply(res, 503, { ok: false, error: 'storage_not_configured' });
    return;
  }

  try {
    await store.add(parsed.visitorId, parsed.record.event, parsed.record);
  } catch (err) {
    console.error('Saving the answer failed:', err);
    reply(res, 502, { ok: false, error: 'storage_failed' });
    return;
  }
  reply(res, 200, { ok: true });
}

function sanitize(body) {
  if (!body || typeof body !== 'object' || Array.isArray(body)) return null;
  if (!EVENTS.has(body.event)) return null;
  if (typeof body.visitorId !== 'string' || !/^[a-z0-9]{8,32}$/.test(body.visitorId)) return null;

  const record = {
    event: body.event,
    at: new Date().toISOString(),
    name: clean(body.name, LIMITS.name),
    test: body.test === true,
    attempts: toCount(body.attempts),
    viaNo: body.viaNo === true,
  };

  if (body.event === 'final') {
    const date = validDate(body.date);
    const time = validTime(body.time);
    if (!date || !time) return null;
    record.date = date;
    record.time = time;
    record.activities = (Array.isArray(body.activities) ? body.activities : [])
      .slice(0, LIMITS.activities)
      .map((item) => clean(item, LIMITS.activity))
      .filter(Boolean);
    record.wish = clean(body.wish, LIMITS.wish, { multiline: true });
  }

  return { visitorId: body.visitorId, record };
}

function validDate(value) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(typeof value === 'string' ? value : '');
  if (!m) return null;
  const [y, mo, d] = [Number(m[1]), Number(m[2]), Number(m[3])];
  const date = new Date(Date.UTC(y, mo - 1, d));
  if (date.getUTCFullYear() !== y || date.getUTCMonth() !== mo - 1 || date.getUTCDate() !== d) return null;
  return value;
}

function validTime(value) {
  if (value === 'any') return 'any';
  const m = /^([01]?\d|2[0-3]):([0-5]\d)$/.exec(typeof value === 'string' ? value : '');
  return m ? `${m[1].padStart(2, '0')}:${m[2]}` : null;
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

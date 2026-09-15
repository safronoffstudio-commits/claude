// Проверяем сквозную детерминированность: вебхук выбирает сид и отдаёт Telegram
// ссылку на /api/og, а тот собирает мем заново. Оба обязаны совпасть.
import assert from 'node:assert/strict';
import { generateMeme, memeToText } from '../lib/memes.js';
import { newSeed } from '../lib/rng.js';
import { memeImageUrl, baseUrl, readUpdate, authorize } from '../lib/telegram.js';
import { Readable } from 'node:stream';

let checks = 0;
const fail = [];

function roundTrip(opts) {
  const seed = newSeed();
  const fromWebhook = generateMeme(seed, opts);
  const url = memeImageUrl('https://x.test', { seed, ...opts, brand: '@b' });

  // то, что сделает api/og.js
  const sp = new URL(url).searchParams;
  const fromOg = generateMeme(sp.get('s'), {
    name: (sp.get('n') || '').slice(0, 24) || null,
    target: (sp.get('t') || '').slice(0, 64) || null,
    force: (sp.get('f') || '').slice(0, 12) || null,
  });

  checks++;
  const a = JSON.stringify(fromWebhook);
  const b = JSON.stringify(fromOg);
  if (a !== b) fail.push({ opts, seed, a: a.slice(0, 160), b: b.slice(0, 160) });
  return fromWebhook;
}

for (let i = 0; i < 300; i++) {
  roundTrip({ name: 'Санёк' });
  roundTrip({ name: null });
  roundTrip({ name: 'Ann', target: 'до зарплаты' });
  roundTrip({ name: 'Вася', target: 'до спортзала, честно' });
  roundTrip({ name: 'Оля', force: 'top' });
  roundTrip({ name: 'Пётр', force: 'cert' });
}

assert.equal(fail.length, 0, 'расхождение вебхук/рендерер: ' + JSON.stringify(fail[0], null, 2));
console.log(`детерминированность: ${checks} проверок, расхождений 0`);

// force реально выбирает шаблон
for (let i = 0; i < 50; i++) {
  assert.equal(generateMeme(newSeed(), { name: 'A', force: 'top' }).template, 'top');
  assert.equal(generateMeme(newSeed(), { name: 'A', force: 'cert' }).template, 'cert');
}
console.log('force: /top и /spravka всегда дают нужный шаблон');

// без имени справка не выпадает (на картинке было бы «Аноним» без спроса)
for (let i = 0; i < 400; i++) {
  assert.notEqual(generateMeme(newSeed(), {}).template, 'cert');
}
console.log('без имени шаблон «справка» не выпадает');

// произвольный текст всегда доезжает до картинки
for (let i = 0; i < 200; i++) {
  const m = generateMeme(newSeed(), { name: 'A', target: 'до дедлайна' });
  // шаблон «шкала» намеренно поднимает цель в верхний регистр
  const blob = JSON.stringify(m).toLowerCase();
  assert.ok(blob.includes('до дедлайна'), 'текст юзера потерялся в шаблоне ' + m.template);
}
console.log('пользовательский текст попадает в мем во всех шаблонах');

// текстовый фолбэк не пустой и не разваливается
for (let i = 0; i < 200; i++) {
  const m = generateMeme(newSeed(), { name: 'Кто-то', target: i % 2 ? 'до отпуска' : null });
  const t = memeToText(m);
  assert.ok(t.length > 20 && !t.includes('undefined') && !t.includes('NaN'), 'плохой текст: ' + t);
}
console.log('текстовый фолбэк валиден');

// baseUrl корректно берёт хост из заголовков Vercel
assert.equal(baseUrl({ headers: { 'x-forwarded-host': 'a.vercel.app', 'x-forwarded-proto': 'https' } }), 'https://a.vercel.app');
assert.equal(baseUrl({ headers: { host: 'b.vercel.app' } }), 'https://b.vercel.app');
console.log('baseUrl определяется верно');

// Тело апдейта может прийти объектом, строкой, буфером или непрочитанным потоком.
// Потерять его нельзя: Telegram получит 200 и не покажет никакой ошибки,
// а бот со стороны будет выглядеть мёртвым.
{
  const upd = { message: { chat: { id: 1 }, text: '/start' } };
  const raw = JSON.stringify(upd);
  assert.deepEqual(await readUpdate({ body: upd }), upd, 'объект');
  assert.deepEqual(await readUpdate({ body: raw }), upd, 'строка');
  assert.deepEqual(await readUpdate({ body: Buffer.from(raw) }), upd, 'буфер');
  assert.deepEqual(await readUpdate(Readable.from([Buffer.from(raw)])), upd, 'поток');
  assert.deepEqual(await readUpdate({ body: 'не json' }), {}, 'мусор не роняет обработчик');
  console.log('разбор тела апдейта: объект/строка/буфер/поток/мусор');
}

// длинный/грязный ввод не ломает генератор
for (const bad of ['', '   ', '/start лишнее', 'а'.repeat(500), '@user до цели', '\n\n\t']) {
  const m = generateMeme('seed', { name: 'X', target: bad });
  assert.ok(m.title && m.punch, 'сломался на вводе: ' + JSON.stringify(bad.slice(0, 20)));
}
console.log('грязный ввод обрабатывается');
// Служебные ручки пускают по секрету или по токену бота, и никак иначе.
{
  const oldT = process.env.TELEGRAM_BOT_TOKEN, oldS = process.env.WEBHOOK_SECRET;
  process.env.TELEGRAM_BOT_TOKEN = '111:AAtok';
  process.env.WEBHOOK_SECRET = 'sekret';
  const get = (q) => ({ url: '/api/setup' + q, method: 'GET' });
  const post = (o) => {
    const st = Readable.from([Buffer.from(JSON.stringify(o))]);
    st.url = '/api/setup'; st.method = 'POST';
    return st;
  };
  assert.equal(await authorize(get('?token=111:AAtok')), true);
  assert.equal(await authorize(get('?key=sekret')), true);
  assert.equal(await authorize(post({ token: '111:AAtok' })), true);
  assert.equal(await authorize(post({ key: 'sekret' })), true);
  for (const bad of [get(''), get('?token=nope'), get('?key=nope')]) {
    assert.equal(await authorize(bad), false, 'пустил лишнего');
  }
  assert.equal(await authorize(post({})), false, 'пустил лишнего');
  process.env.TELEGRAM_BOT_TOKEN = oldT; process.env.WEBHOOK_SECRET = oldS;
  console.log('доступ к /api/setup и /api/diag: только по токену или секрету');
}

console.log('\nВСЕ ПРОВЕРКИ ПРОЙДЕНЫ');

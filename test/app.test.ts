import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { describe, it } from 'node:test';
import { GenerationError, type ReelsGenerator } from '../src/ai/generator.js';
import { safeNext } from '../src/app.js';
import { getUserByEmail } from '../src/auth.js';
import { formatTimestamp } from '../src/transcript.js';
import { Client, createTestApp } from './helpers.js';

const TRANSCRIPT = readFileSync(new URL('./fixtures/podcast-ru.txt', import.meta.url), 'utf8');

const GENERATE_FORM = {
  title: 'Подкаст с Анной',
  transcript: TRANSCRIPT,
  count: '3',
  language: 'ru',
  platform: 'reels',
  tone: 'expert',
  ctaGoal: 'comment',
  ctaDetail: 'ЦЕНА',
  context: '',
};

async function generate(client: Client, form: Record<string, string> = GENERATE_FORM): Promise<string> {
  const res = await client.post('/app/generations', form);
  assert.equal(res.status, 303, await res.text());
  const location = res.headers.get('location')!;
  assert.match(location, /^\/app\/g\/[\w-]+$/);
  return location.split('/').pop()!;
}

describe('marketing pages', () => {
  it('serves the landing page in the visitor’s language', async () => {
    const { app } = createTestApp();
    const client = new Client(app);
    const en = await client.get('/', { headers: { 'accept-language': 'en-US,en;q=0.9' } });
    assert.equal(en.status, 200);
    assert.match(await en.text(), /One long video → five ready-to-shoot Reels/);

    const ru = await client.get('/', { headers: { 'accept-language': 'ru-RU,ru;q=0.9,en;q=0.8' } });
    assert.match(await ru.text(), /Одно длинное видео → пять готовых Reels/);

    const switched = await client.get('/lang/ru?next=/pricing');
    assert.equal(switched.headers.get('location'), '/pricing');
    const pricing = await client.get('/pricing', { headers: { 'accept-language': 'en' } });
    assert.match(await pricing.text(), /Простые тарифы/);
  });

  it('sends security headers, robots and a sitemap', async () => {
    const { app } = createTestApp();
    const res = await app.request('http://localhost:3000/');
    assert.match(res.headers.get('content-security-policy') ?? '', /script-src 'self'/);
    assert.equal(res.headers.get('x-frame-options'), 'SAMEORIGIN');
    assert.match(await (await app.request('http://localhost:3000/robots.txt')).text(), /Disallow: \/app/);
    assert.match(await (await app.request('http://localhost:3000/sitemap.xml')).text(), /<loc>http:\/\/localhost:3000\/pricing<\/loc>/);
    assert.equal((await app.request('http://localhost:3000/healthz')).status, 200);
    assert.equal((await app.request('http://localhost:3000/nope')).status, 404);
    const huge = await app.request('http://localhost:3000/stripe/webhook', {
      method: 'POST',
      headers: { 'stripe-signature': 't=1,v1=x', 'content-type': 'application/json' },
      body: 'x'.repeat(5 * 1024 * 1024),
    });
    assert.equal(huge.status, 413);
  });

  it('only accepts local redirect targets', () => {
    assert.equal(safeNext('/app/g/1?x=1'), '/app/g/1?x=1');
    assert.equal(safeNext('//evil.example'), undefined);
    assert.equal(safeNext('/\\evil.example'), undefined);
    assert.equal(safeNext('https://evil.example'), undefined);
  });
});

describe('accounts', () => {
  it('signs up, logs out and logs back in', async () => {
    const { app } = createTestApp();
    const client = new Client(app);
    assert.equal((await client.get('/app')).headers.get('location'), '/login?next=%2Fapp');

    const signup = await client.signup('Anna@Example.com');
    assert.equal(signup.headers.get('location'), '/app');
    assert.match(await (await client.get('/app')).text(), /0 of 3 generations used|0 из 3 генераций/);

    await client.post('/logout');
    assert.equal(client.hasCookie('hc_session'), false);
    assert.equal((await client.get('/app')).status, 302);

    const wrong = await client.post('/login', { email: 'anna@example.com', password: 'nope-nope' });
    assert.equal(wrong.status, 401);
    const ok = await client.post('/login', { email: 'ANNA@example.com', password: 'correct horse battery', next: '/app/account' });
    assert.equal(ok.headers.get('location'), '/app/account');
  });

  it('validates signups', async () => {
    const { app } = createTestApp();
    const client = new Client(app);
    assert.equal((await client.post('/signup', { email: 'not-an-email', password: 'long enough pw' })).status, 400);
    assert.equal((await client.post('/signup', { email: 'a@example.com', password: 'short' })).status, 400);
    await client.signup('a@example.com');
    const dup = await new Client(app).post('/signup', { email: 'A@example.com', password: 'long enough pw' });
    assert.equal(dup.status, 409);
  });

  it('rejects cross-site form posts', async () => {
    const { app } = createTestApp();
    const client = new Client(app);
    const res = await client.post('/signup', { email: 'a@example.com', password: 'long enough pw' }, { headers: { origin: 'https://evil.example' } });
    assert.equal(res.status, 403);
  });

  it('resets a forgotten password through an emailed one-time link', async () => {
    const { app, emails } = createTestApp();
    const client = new Client(app);
    await client.signup('anna@example.com');
    await client.post('/logout');

    const unknown = await client.post('/forgot', { email: 'nobody@example.com' });
    assert.equal(unknown.status, 200);
    assert.equal(emails.length, 0);

    await client.post('/forgot', { email: 'anna@example.com' });
    assert.equal(emails.length, 1);
    const link = /http:\/\/localhost:3000(\/reset\/[\w-]+)/.exec(emails[0]!.text)![1]!;

    assert.match(await (await client.get(link)).text(), /name="password"/);
    const reset = await client.post(link, { password: 'brand new password' });
    assert.equal(reset.headers.get('location'), '/app');

    const reused = await new Client(app).post(link, { password: 'another password' });
    assert.equal(reused.status, 400);
    const fresh = new Client(app);
    assert.equal((await fresh.post('/login', { email: 'anna@example.com', password: 'correct horse battery' })).status, 401);
    assert.equal((await fresh.post('/login', { email: 'anna@example.com', password: 'brand new password' })).status, 303);
  });

  it('changes the password and deletes the account', async () => {
    const { app, db } = createTestApp();
    const client = new Client(app);
    await client.signup('anna@example.com');
    assert.equal((await client.post('/app/account/password', { current: 'wrong', password: 'whatever123' })).status, 400);
    const changed = await client.post('/app/account/password', { current: 'correct horse battery', password: 'new password 1' });
    assert.equal(changed.headers.get('location'), '/app/account?password=changed');

    assert.equal((await client.post('/app/account/delete', { confirm: 'nope' })).status, 400);
    assert.equal((await client.post('/app/account/delete', { confirm: 'DELETE' })).headers.get('location'), '/');
    assert.equal(getUserByEmail(db, 'anna@example.com'), undefined);
  });

  it('shows the admin dashboard to admins only', async () => {
    const { app } = createTestApp();
    const user = new Client(app);
    await user.signup('user@example.com');
    assert.equal((await user.get('/admin')).status, 404);
    const admin = new Client(app);
    await admin.signup('admin@example.com');
    const res = await admin.get('/admin');
    assert.equal(res.status, 200);
    assert.match(await res.text(), /MRR/);
  });
});

describe('generating scripts', () => {
  it('turns a transcript into scripts, exports and shares them', async () => {
    const { app, jobs } = createTestApp();
    const client = new Client(app);
    await client.signup('anna@example.com');

    const id = await generate(client);
    await jobs.idle();

    assert.deepEqual(await (await client.get(`/app/g/${id}/status`)).json(), { status: 'done' });
    const page = await (await client.get(`/app/g/${id}`)).text();
    assert.match(page, /id="reel-1"/);
    assert.match(page, /id="reel-3"/);
    assert.doesNotMatch(page, /id="reel-4"/);
    assert.match(page, /Подкаст с Анной/);

    const md = await client.get(`/app/g/${id}/export.md`);
    assert.equal(md.headers.get('content-type'), 'text/markdown; charset=utf-8');
    assert.match(md.headers.get('content-disposition') ?? '', /attachment/);
    assert.match(await md.text(), /^# Подкаст с Анной/);

    await client.post(`/app/g/${id}/share`, { enabled: '1' });
    const shareUrl = /value="http:\/\/localhost:3000(\/s\/[\w-]+)"/.exec(await (await client.get(`/app/g/${id}`)).text())![1]!;
    const shared = await new Client(app).get(shareUrl);
    assert.equal(shared.status, 200);
    assert.match(await shared.text(), /made-with/);

    await client.post(`/app/g/${id}/share`, { enabled: '0' });
    assert.equal((await new Client(app).get(shareUrl)).status, 404);

    const history = await (await client.get('/app')).text();
    assert.match(history, new RegExp(`/app/g/${id}`));
  });

  it('keeps each user’s scripts private', async () => {
    const { app, jobs } = createTestApp();
    const anna = new Client(app);
    await anna.signup('anna@example.com');
    const id = await generate(anna);
    await jobs.idle();

    const other = new Client(app);
    await other.signup('other@example.com');
    assert.equal((await other.get(`/app/g/${id}`)).status, 404);
    assert.equal((await other.get(`/app/g/${id}/export.md`)).status, 404);
    await other.post(`/app/g/${id}/delete`);
    assert.equal((await anna.get(`/app/g/${id}`)).status, 200);
  });

  it('enforces transcript length and the monthly quota of the plan', async () => {
    const { app, jobs } = createTestApp();
    const client = new Client(app);
    await client.signup('anna@example.com');

    const short = await client.post('/app/generations', { ...GENERATE_FORM, transcript: 'Слишком коротко.' });
    assert.equal(short.status, 400);
    const long = await client.post('/app/generations', { ...GENERATE_FORM, transcript: `${TRANSCRIPT}\n`.repeat(20) });
    assert.equal(long.status, 400);
    assert.match(await long.text(), /~40/);

    // Free plan: at most 3 Reels per generation and 3 generations per month.
    const id = await generate(client, { ...GENERATE_FORM, count: '5' });
    await generate(client);
    await generate(client);
    await jobs.idle();
    const page = await (await client.get(`/app/g/${id}`)).text();
    assert.doesNotMatch(page, /id="reel-4"/);

    const blocked = await client.post('/app/generations', GENERATE_FORM);
    assert.equal(blocked.status, 402);
    assert.match(await blocked.text(), /Upgrade to keep going|Перейдите на тариф выше/);
  });

  it('counts long recordings as one generation per started hour', async () => {
    const { app, db, jobs } = createTestApp();
    const client = new Client(app);
    await client.signup('pro@example.com');
    db.prepare("UPDATE users SET plan = 'pro' WHERE email = ?").run('pro@example.com');

    // ~2.3 hours of speech → 3 generations.
    const long = Array.from({ length: 1500 }, (_, i) => `${formatTimestamp(i * 6)}\n${'Слово '.repeat(15)}`).join('\n');
    const id = await generate(client, { ...GENERATE_FORM, transcript: long });
    await jobs.idle();
    assert.equal((db.prepare('SELECT credits FROM generations WHERE id = ?').get(id) as { credits: number }).credits, 3);
    assert.match(await (await client.get('/app')).text(), /3 of 150|3 из 150/);

    // Not enough left for another long one, but a short one still fits.
    db.prepare('UPDATE generations SET credits = 148 WHERE id = ?').run(id);
    const blocked = await client.post('/app/generations', { ...GENERATE_FORM, transcript: long });
    assert.equal(blocked.status, 402);
    assert.match(await blocked.text(), /counts as 3 generations|списывает 3 генерации/);
    await generate(client);
  });

  it('does not count failed generations and lets the user retry', async () => {
    let fail = true;
    const flaky: ReelsGenerator = {
      name: 'flaky',
      async generate(req) {
        if (fail) throw new GenerationError('overloaded', 'busy');
        const { createMockGenerator } = await import('../src/ai/mock.js');
        return createMockGenerator().generate(req);
      },
    };
    const { app, jobs } = createTestApp({ generator: flaky });
    const client = new Client(app);
    await client.signup('anna@example.com');

    const id = await generate(client);
    await jobs.idle();
    const failedPage = await (await client.get(`/app/g/${id}`)).text();
    assert.match(failedPage, /overloaded|перегружен/);
    assert.match(await (await client.get('/app')).text(), /0 of 3|0 из 3/);

    fail = false;
    const retry = await client.post(`/app/g/${id}/retry`);
    assert.equal(retry.headers.get('location'), `/app/g/${id}`);
    await jobs.idle();
    assert.deepEqual(await (await client.get(`/app/g/${id}/status`)).json(), { status: 'done' });
    assert.match(await (await client.get('/app')).text(), /1 of 3|1 из 3/);
  });
});

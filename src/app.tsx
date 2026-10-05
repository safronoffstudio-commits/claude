import { Hono, type Context, type MiddlewareHandler } from 'hono';
import { deleteCookie, getCookie, setCookie } from 'hono/cookie';
import { bodyLimit } from 'hono/body-limit';
import { csrf } from 'hono/csrf';
import { HTTPException } from 'hono/http-exception';
import { secureHeaders } from 'hono/secure-headers';
import { serveStatic } from '@hono/node-server/serve-static';
import { getConnInfo } from '@hono/node-server/conninfo';
import {
  createPasswordReset,
  createSession,
  createUser,
  deleteSession,
  deleteUserSessions,
  EmailTakenError,
  findPasswordReset,
  getSessionUser,
  getUserByEmail,
  isValidEmail,
  markPasswordResetUsed,
  MIN_PASSWORD,
  normalizeEmail,
  SESSION_COOKIE,
  SESSION_TTL_SEC,
  setLocale,
  setPassword,
  verifyPassword,
} from './auth.js';
import { BillingDisabledError, WebhookSignatureError, type Billing } from './billing.js';
import type { Config } from './config.js';
import type { Db, UserRow } from './db.js';
import type { Mailer } from './email.js';
import { generationToMarkdown } from './export.js';
import {
  createGeneration,
  deleteGeneration,
  getSharedGeneration,
  getUserGeneration,
  listGenerations,
  parseOptions,
  parseResult,
  requeueGeneration,
  setSharing,
} from './generations.js';
import {
  CTA_GOALS,
  dict,
  formatLimitChars,
  isLocale,
  localeFromHeader,
  OUTPUT_LANGUAGES,
  PLATFORMS,
  TONES,
  type Dict,
  type Locale,
} from './i18n.js';
import type { JobQueue } from './jobs.js';
import { creditsFor, getPlan, isPaidPlan, PAID_PLANS, type PlanId } from './plans.js';
import { RateLimiter } from './ratelimit.js';
import { adminStats } from './stats.js';
import { normalizeTranscript } from './transcript.js';
import { getUsage } from './usage.js';
import { AccountPage } from './views/account.js';
import { AdminPage } from './views/admin.js';
import { ForgotPage, LoginPage, ResetPage, SignupPage } from './views/auth.js';
import { DashboardPage } from './views/dashboard.js';
import { GenerationPage, SharedPage } from './views/generation.js';
import { LandingPage, PricingPage } from './views/landing.js';
import type { PageContext } from './views/layout.js';
import { ErrorPage, LegalPage } from './views/pages.js';

export interface AppDeps {
  config: Config;
  db: Db;
  jobs: JobQueue;
  billing: Billing;
  mailer: Mailer;
  publicDir?: string;
  assetVersion?: string;
}

type Env = { Variables: { user: UserRow | null; locale: Locale; t: Dict } };
type C = Context<Env>;

const LANG_COOKIE = 'hc_lang';
const MIN_TRANSCRIPT_CHARS = 500;

/** Only same-site relative paths are allowed as post-login redirects. */
export function safeNext(value: unknown): string | undefined {
  if (typeof value !== 'string' || !value.startsWith('/') || value.startsWith('//') || value.startsWith('/\\')) return undefined;
  return value;
}

function field(body: Record<string, unknown>, key: string): string {
  const value = body[key];
  return typeof value === 'string' ? value : '';
}

function pick<T extends string>(values: readonly T[], value: string, fallback: T): T {
  return (values as readonly string[]).includes(value) ? (value as T) : fallback;
}

function deriveTitle(text: string): string {
  const words = text
    .replace(/\[[\d:]+\]/g, ' ')
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 8)
    .join(' ');
  return words.length > 60 ? `${words.slice(0, 60)}…` : `${words}…`;
}

function attachmentName(title: string): string {
  const base = title.replace(/[^\p{L}\p{N}]+/gu, '-').replace(/^-+|-+$/g, '').slice(0, 60) || 'hookcut';
  return `${base}.md`;
}

export function createApp(deps: AppDeps) {
  const { config, db, jobs, billing, mailer } = deps;
  const app = new Hono<Env>();
  const assetVersion = deps.assetVersion ?? String(Date.now());
  const publicDir = deps.publicDir ?? './public';

  const limits = {
    signup: new RateLimiter(10, 60 * 60 * 1000),
    loginIp: new RateLimiter(30, 15 * 60 * 1000),
    loginEmail: new RateLimiter(10, 15 * 60 * 1000),
    forgot: new RateLimiter(5, 60 * 60 * 1000),
    generate: new RateLimiter(10, 60 * 1000),
  };

  const isAdmin = (user: UserRow | null) => Boolean(user && config.adminEmails.includes(user.email.toLowerCase()));

  function clientIp(c: C): string {
    if (config.trustProxy) {
      const forwarded = c.req.header('x-forwarded-for')?.split(',')[0]?.trim();
      if (forwarded) return forwarded;
    }
    try {
      return getConnInfo(c).remote.address ?? 'unknown';
    } catch {
      return 'unknown';
    }
  }

  function pageContext(c: C): PageContext {
    const user = c.get('user');
    const url = new URL(c.req.url);
    return {
      t: c.get('t'),
      locale: c.get('locale'),
      user,
      isAdmin: isAdmin(user),
      appUrl: config.appUrl,
      path: url.pathname + url.search,
      assetVersion,
    };
  }

  function startSession(c: C, userId: number): void {
    setCookie(c, SESSION_COOKIE, createSession(db, userId), {
      httpOnly: true,
      secure: config.secureCookies,
      sameSite: 'Lax',
      path: '/',
      maxAge: SESSION_TTL_SEC,
    });
  }

  function availablePlans(): PlanId[] {
    return PAID_PLANS.filter((plan) => billing.isPlanAvailable(plan));
  }

  // --- infrastructure -------------------------------------------------------

  app.use(
    '*',
    secureHeaders({
      contentSecurityPolicy: {
        defaultSrc: ["'self'"],
        scriptSrc: ["'self'"],
        styleSrc: ["'self'", "'unsafe-inline'"],
        imgSrc: ["'self'", 'data:'],
        connectSrc: ["'self'"],
        // Billing forms redirect to Stripe-hosted pages.
        formAction: ["'self'", 'https://checkout.stripe.com', 'https://billing.stripe.com'],
        frameAncestors: ["'none'"],
        baseUri: ["'self'"],
        objectSrc: ["'none'"],
      },
      strictTransportSecurity: config.secureCookies ? 'max-age=31536000; includeSubDomains' : false,
    }),
  );

  // Applies to every route, the Stripe webhook included. Generous enough for a 5-hour transcript.
  app.use('*', bodyLimit({ maxSize: 4 * 1024 * 1024, onError: (c) => c.text('Payload too large', 413) }));

  for (const file of ['styles.css', 'app.js', 'favicon.svg']) {
    app.get(`/${file}`, async (c, next) => {
      await next();
      c.header('Cache-Control', c.req.query('v') ? 'public, max-age=31536000, immutable' : 'public, max-age=3600');
    }, serveStatic({ path: `${publicDir}/${file}` }));
  }

  app.get('/healthz', (c) => {
    db.prepare('SELECT 1').get();
    return c.text('ok');
  });

  // Stripe signs the raw body, so this route reads it before any parsing.
  app.post('/stripe/webhook', async (c) => {
    const signature = c.req.header('stripe-signature');
    if (!signature) return c.json({ error: 'missing signature' }, 400);
    try {
      const result = await billing.handleWebhook(await c.req.text(), signature);
      return c.json({ received: true, result });
    } catch (err) {
      if (err instanceof BillingDisabledError) return c.json({ error: 'billing disabled' }, 503);
      if (err instanceof WebhookSignatureError) return c.json({ error: 'bad signature' }, 400);
      throw err;
    }
  });

  app.use(
    '*',
    csrf({
      origin: (origin) => {
        if (origin === config.appUrl) return true;
        // Local development is often opened as 127.0.0.1 or on another port.
        return config.env !== 'production' && /^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/.test(origin);
      },
    }),
  );

  // Session + locale for every page.
  app.use('*', async (c, next) => {
    let user: UserRow | null = null;
    const token = getCookie(c, SESSION_COOKIE);
    if (token) {
      const session = getSessionUser(db, token);
      if (session) {
        user = session.user;
        if (session.renewed) {
          setCookie(c, SESSION_COOKIE, token, { httpOnly: true, secure: config.secureCookies, sameSite: 'Lax', path: '/', maxAge: SESSION_TTL_SEC });
        }
      } else {
        deleteCookie(c, SESSION_COOKIE, { path: '/' });
      }
    }
    const cookieLocale = getCookie(c, LANG_COOKIE);
    const locale: Locale = isLocale(cookieLocale) ? cookieLocale : user ? user.locale : localeFromHeader(c.req.header('accept-language'));
    c.set('user', user);
    c.set('locale', locale);
    c.set('t', dict(locale));
    await next();
  });

  const requireUser: MiddlewareHandler<Env> = async (c, next) => {
    if (!c.get('user')) {
      const url = new URL(c.req.url);
      return c.redirect(`/login?next=${encodeURIComponent(url.pathname + url.search)}`);
    }
    await next();
  };

  // --- marketing ----------------------------------------------------------------

  app.get('/', (c) => c.html(<LandingPage ctx={pageContext(c)} availablePlans={availablePlans()} />));
  app.get('/pricing', (c) => c.html(<PricingPage ctx={pageContext(c)} availablePlans={availablePlans()} />));
  app.get('/terms', (c) => c.html(<LegalPage ctx={pageContext(c)} doc="terms" />));
  app.get('/privacy', (c) => c.html(<LegalPage ctx={pageContext(c)} doc="privacy" />));

  app.get('/lang/:locale', (c) => {
    const locale = c.req.param('locale');
    if (isLocale(locale)) {
      setCookie(c, LANG_COOKIE, locale, { path: '/', maxAge: 365 * 24 * 3600, sameSite: 'Lax', secure: config.secureCookies });
      const user = c.get('user');
      if (user) setLocale(db, user.id, locale);
    }
    return c.redirect(safeNext(c.req.query('next')) ?? '/');
  });

  app.get('/robots.txt', (c) =>
    c.text(`User-agent: *\nAllow: /\nDisallow: /app\nDisallow: /admin\nDisallow: /billing\nDisallow: /s/\n\nSitemap: ${config.appUrl}/sitemap.xml\n`),
  );

  app.get('/sitemap.xml', (c) => {
    const urls = ['/', '/pricing', '/terms', '/privacy'].map((p) => `<url><loc>${config.appUrl}${p}</loc></url>`).join('');
    c.header('Content-Type', 'application/xml; charset=utf-8');
    return c.body(`<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${urls}</urlset>`);
  });

  // --- auth -----------------------------------------------------------------------

  app.get('/signup', (c) => {
    if (c.get('user')) return c.redirect('/app');
    return c.html(<SignupPage ctx={pageContext(c)} next={safeNext(c.req.query('next'))} />);
  });

  app.post('/signup', async (c) => {
    const t = c.get('t');
    const body = await c.req.parseBody();
    const email = normalizeEmail(field(body, 'email'));
    const password = field(body, 'password');
    const next = safeNext(field(body, 'next'));
    const fail = (error: string, status: 400 | 409 | 429 = 400) =>
      c.html(<SignupPage ctx={pageContext(c)} error={error} email={email} next={next} />, status);

    if (!limits.signup.take(clientIp(c))) return fail(t.auth.errors.tooMany, 429);
    if (!isValidEmail(email)) return fail(t.auth.errors.invalidEmail);
    if (password.length < MIN_PASSWORD) return fail(t.auth.errors.weakPassword);
    try {
      const user = await createUser(db, email, password, c.get('locale'));
      startSession(c, user.id);
      return c.redirect(next ?? '/app', 303);
    } catch (err) {
      if (err instanceof EmailTakenError) return fail(t.auth.errors.emailTaken, 409);
      throw err;
    }
  });

  app.get('/login', (c) => {
    if (c.get('user')) return c.redirect(safeNext(c.req.query('next')) ?? '/app');
    const notice = c.req.query('reset') === '1' ? c.get('t').account.passwordChanged : undefined;
    return c.html(<LoginPage ctx={pageContext(c)} next={safeNext(c.req.query('next'))} notice={notice} />);
  });

  app.post('/login', async (c) => {
    const t = c.get('t');
    const body = await c.req.parseBody();
    const email = normalizeEmail(field(body, 'email'));
    const password = field(body, 'password');
    const next = safeNext(field(body, 'next'));
    const fail = (error: string, status: 401 | 429) => c.html(<LoginPage ctx={pageContext(c)} error={error} email={email} next={next} />, status);

    if (!limits.loginIp.take(clientIp(c)) || !limits.loginEmail.take(email)) return fail(t.auth.errors.tooMany, 429);
    const user = getUserByEmail(db, email);
    if (!user || !(await verifyPassword(password, user.password_hash))) return fail(t.auth.errors.invalidCredentials, 401);
    limits.loginEmail.reset(email);
    startSession(c, user.id);
    return c.redirect(next ?? '/app', 303);
  });

  app.post('/logout', (c) => {
    const token = getCookie(c, SESSION_COOKIE);
    if (token) deleteSession(db, token);
    deleteCookie(c, SESSION_COOKIE, { path: '/' });
    return c.redirect('/', 303);
  });

  app.get('/forgot', (c) => c.html(<ForgotPage ctx={pageContext(c)} />));

  app.post('/forgot', async (c) => {
    const t = c.get('t');
    if (!limits.forgot.take(clientIp(c))) return c.html(<ForgotPage ctx={pageContext(c)} error={t.auth.errors.tooMany} />, 429);
    const email = normalizeEmail(field(await c.req.parseBody(), 'email'));
    const user = isValidEmail(email) ? getUserByEmail(db, email) : undefined;
    if (user) {
      const token = createPasswordReset(db, user.id);
      const userT = dict(user.locale);
      // Not awaited: the response must look the same whether or not the account exists.
      mailer
        .send({ to: user.email, subject: userT.auth.resetEmailSubject, text: userT.auth.resetEmailBody(`${config.appUrl}/reset/${token}`) })
        .catch((err) => console.error('[email] password reset failed:', err));
    }
    return c.html(<ForgotPage ctx={pageContext(c)} sent />);
  });

  app.get('/reset/:token', (c) => {
    const token = c.req.param('token');
    return c.html(<ResetPage ctx={pageContext(c)} token={token} valid={Boolean(findPasswordReset(db, token))} />);
  });

  app.post('/reset/:token', async (c) => {
    const t = c.get('t');
    const token = c.req.param('token');
    const reset = findPasswordReset(db, token);
    if (!reset) return c.html(<ResetPage ctx={pageContext(c)} token={token} valid={false} />, 400);
    const password = field(await c.req.parseBody(), 'password');
    if (password.length < MIN_PASSWORD) {
      return c.html(<ResetPage ctx={pageContext(c)} token={token} valid error={t.auth.errors.weakPassword} />, 400);
    }
    await setPassword(db, reset.userId, password);
    markPasswordResetUsed(db, token);
    deleteUserSessions(db, reset.userId);
    startSession(c, reset.userId);
    return c.redirect('/app', 303);
  });

  // --- the product ----------------------------------------------------------------

  app.use('/app/*', requireUser);
  app.use('/app', requireUser);

  function renderDashboard(c: C, error?: string, form?: Record<string, string>, status: 200 | 400 | 402 | 429 = 200) {
    const user = c.get('user')!;
    return c.html(
      <DashboardPage ctx={pageContext(c)} usage={getUsage(db, user)} generations={listGenerations(db, user.id)} error={error} form={form} />,
      status,
    );
  }

  app.get('/app', (c) => {
    const t = c.get('t');
    const error = c.req.query('error') === 'quota' ? t.app.errors.quota : undefined;
    return renderDashboard(c, error);
  });

  app.post('/app/generations', async (c) => {
    const user = c.get('user')!;
    const t = c.get('t');
    const body = await c.req.parseBody();
    const form = Object.fromEntries(
      ['title', 'transcript', 'count', 'language', 'platform', 'tone', 'ctaGoal', 'ctaDetail', 'context'].map((k) => [k, field(body, k)]),
    );
    const plan = getPlan(user.plan);

    if (!limits.generate.take(`user:${user.id}`)) return renderDashboard(c, t.app.errors.tooFast, form, 429);
    const usage = getUsage(db, user);
    if (usage.remaining <= 0) return renderDashboard(c, t.app.errors.quota, form, 402);

    const transcript = normalizeTranscript(form.transcript!);
    if (transcript.text.length < MIN_TRANSCRIPT_CHARS) return renderDashboard(c, t.app.errors.tooShort, form, 400);
    if (transcript.text.length > plan.maxChars) {
      return renderDashboard(c, t.app.errors.tooLong(formatLimitChars(c.get('locale'), plan.maxChars)), form, 400);
    }
    const credits = creditsFor(transcript.text.length);
    if (credits > usage.remaining) return renderDashboard(c, t.app.errors.notEnough(credits, usage.remaining), form, 402);

    const count = Math.min(plan.maxReels, Math.max(1, Number.parseInt(form.count!, 10) || 3));
    const row = createGeneration(
      db,
      user.id,
      form.title!.trim().slice(0, 120) || deriveTitle(transcript.text),
      {
        count,
        language: pick(OUTPUT_LANGUAGES, form.language!, 'auto'),
        platform: pick(PLATFORMS, form.platform!, 'reels'),
        tone: pick(TONES, form.tone!, 'expert'),
        ctaGoal: pick(CTA_GOALS, form.ctaGoal!, 'subscribe'),
        ctaDetail: form.ctaDetail!.trim().slice(0, 200),
        context: form.context!.trim().slice(0, 1500),
        hasTimecodes: transcript.hasTimecodes,
        durationSec: transcript.durationSec,
        format: transcript.format,
      },
      transcript.text,
      credits,
    );
    jobs.enqueue(row.id);
    return c.redirect(`/app/g/${row.id}`, 303);
  });

  app.get('/app/g/:id', (c) => {
    const user = c.get('user')!;
    const row = getUserGeneration(db, user.id, c.req.param('id'));
    if (!row) return c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404);
    return c.html(
      <GenerationPage
        ctx={pageContext(c)}
        row={row}
        options={parseOptions(row)}
        output={parseResult(row)}
        canRetry={getUsage(db, user).remaining >= row.credits}
      />,
    );
  });

  app.get('/app/g/:id/status', (c) => {
    const row = getUserGeneration(db, c.get('user')!.id, c.req.param('id'));
    if (!row) return c.json({ error: 'not found' }, 404);
    c.header('Cache-Control', 'no-store');
    return c.json({ status: row.status });
  });

  app.get('/app/g/:id/export.md', (c) => {
    const row = getUserGeneration(db, c.get('user')!.id, c.req.param('id'));
    const output = row && parseResult(row);
    if (!row || !output) return c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404);
    const filename = attachmentName(row.title);
    c.header('Content-Type', 'text/markdown; charset=utf-8');
    c.header('Content-Disposition', `attachment; filename="hookcut.md"; filename*=UTF-8''${encodeURIComponent(filename)}`);
    return c.body(generationToMarkdown(row.title, output, c.get('t')));
  });

  app.post('/app/g/:id/share', async (c) => {
    const id = c.req.param('id');
    const enabled = field(await c.req.parseBody(), 'enabled') === '1';
    setSharing(db, c.get('user')!.id, id, enabled);
    return c.redirect(`/app/g/${id}`, 303);
  });

  app.post('/app/g/:id/retry', (c) => {
    const user = c.get('user')!;
    const id = c.req.param('id');
    const row = getUserGeneration(db, user.id, id);
    if (!row) return c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404);
    if (getUsage(db, user).remaining < row.credits) return c.redirect('/app?error=quota', 303);
    if (requeueGeneration(db, user.id, id)) jobs.enqueue(id);
    return c.redirect(`/app/g/${id}`, 303);
  });

  app.post('/app/g/:id/delete', (c) => {
    deleteGeneration(db, c.get('user')!.id, c.req.param('id'));
    return c.redirect('/app', 303);
  });

  app.get('/s/:shareId', (c) => {
    const row = getSharedGeneration(db, c.req.param('shareId'));
    const output = row && parseResult(row);
    if (!row || !output) return c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404);
    const owner = db.prepare('SELECT plan FROM users WHERE id = ?').get(row.user_id) as { plan: PlanId } | undefined;
    return c.html(<SharedPage ctx={pageContext(c)} row={row} output={output} whiteLabel={getPlan(owner?.plan ?? 'free').whiteLabel} />);
  });

  // --- account ----------------------------------------------------------------------

  function renderAccount(c: C, notice?: string, error?: string, status: 200 | 400 | 502 = 200) {
    const user = c.get('user')!;
    return c.html(
      <AccountPage ctx={pageContext(c)} usage={getUsage(db, user)} billingEnabled={billing.enabled} notice={notice} error={error} />,
      status,
    );
  }

  app.get('/app/account', (c) => {
    const t = c.get('t');
    const notices: Record<string, string> = {
      success: t.account.upgraded,
      pending: t.account.checkoutPending,
    };
    const notice = notices[c.req.query('checkout') ?? ''] ?? (c.req.query('password') === 'changed' ? t.account.passwordChanged : undefined);
    return renderAccount(c, notice);
  });

  app.post('/app/account/password', async (c) => {
    const t = c.get('t');
    const user = c.get('user')!;
    const body = await c.req.parseBody();
    const password = field(body, 'password');
    if (!(await verifyPassword(field(body, 'current'), user.password_hash))) {
      return renderAccount(c, undefined, t.auth.errors.invalidCredentials, 400);
    }
    if (password.length < MIN_PASSWORD) return renderAccount(c, undefined, t.auth.errors.weakPassword, 400);
    await setPassword(db, user.id, password);
    deleteUserSessions(db, user.id);
    startSession(c, user.id);
    return c.redirect('/app/account?password=changed', 303);
  });

  app.post('/app/account/delete', async (c) => {
    const t = c.get('t');
    const user = c.get('user')!;
    if (field(await c.req.parseBody(), 'confirm') !== 'DELETE') return renderAccount(c, undefined, t.app.errors.generic, 400);
    try {
      await billing.cancelImmediately(user);
    } catch (err) {
      console.error('[billing] cancel on delete failed:', err);
      return renderAccount(c, undefined, t.app.errors.generic, 502);
    }
    db.prepare('DELETE FROM users WHERE id = ?').run(user.id);
    deleteCookie(c, SESSION_COOKIE, { path: '/' });
    return c.redirect('/', 303);
  });

  // --- billing ---------------------------------------------------------------------------

  app.get('/billing/checkout', async (c) => {
    const user = c.get('user');
    const plan = c.req.query('plan') ?? '';
    if (!user) return c.redirect(`/signup?next=${encodeURIComponent(`/billing/checkout?plan=${plan}`)}`);
    if (!isPaidPlan(plan) || !billing.isPlanAvailable(plan)) return c.redirect('/pricing');
    // Plan changes for existing subscribers go through the Stripe customer portal.
    const url = isPaidPlan(user.plan) ? await billing.portalUrl(user) : await billing.checkoutUrl(user, plan);
    return c.redirect(url, 303);
  });

  app.post('/billing/portal', requireUser, async (c) => {
    if (!billing.enabled) return c.redirect('/app/account');
    return c.redirect(await billing.portalUrl(c.get('user')!), 303);
  });

  app.get('/billing/success', requireUser, async (c) => {
    const sessionId = c.req.query('session_id');
    let synced = false;
    if (sessionId) {
      try {
        synced = await billing.syncCheckoutSession(c.get('user')!, sessionId);
      } catch (err) {
        console.error('[billing] checkout sync failed (the webhook will catch up):', err);
      }
    }
    return c.redirect(`/app/account?checkout=${synced ? 'success' : 'pending'}`);
  });

  // --- admin ---------------------------------------------------------------------------

  app.get('/admin', requireUser, (c) => {
    if (!isAdmin(c.get('user'))) return c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404);
    return c.html(<AdminPage ctx={pageContext(c)} stats={adminStats(db)} />);
  });

  // --- errors ----------------------------------------------------------------------------

  app.notFound((c) => c.html(<ErrorPage ctx={pageContext(c)} status={404} />, 404));

  app.onError((err, c) => {
    // Deliberate HTTP errors from middleware (e.g. the CSRF check's 403) keep their status.
    if (err instanceof HTTPException) return err.getResponse();
    console.error(`[http] ${c.req.method} ${c.req.path}:`, err);
    if (!c.get('t')) {
      c.set('locale', 'en');
      c.set('t', dict('en'));
      c.set('user', null);
    }
    return c.html(<ErrorPage ctx={pageContext(c)} status={500} />, 500);
  });

  return app;
}

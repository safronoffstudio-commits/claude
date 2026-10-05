import Stripe from 'stripe';
import type { ReelsGenerator } from '../src/ai/generator.js';
import { createMockGenerator } from '../src/ai/mock.js';
import { createApp } from '../src/app.js';
import { createBilling } from '../src/billing.js';
import { loadConfig, type Config } from '../src/config.js';
import { openDb, type Db } from '../src/db.js';
import type { EmailMessage, Mailer } from '../src/email.js';
import { JobQueue } from '../src/jobs.js';

export const APP_URL = 'http://localhost:3000';
export const WEBHOOK_SECRET = 'whsec_test_secret';

export interface TestApp {
  app: ReturnType<typeof createApp>;
  db: Db;
  jobs: JobQueue;
  config: Config;
  emails: EmailMessage[];
  stripe: Stripe;
}

const quietLog = { info: () => {}, error: () => {} };

export function createTestApp(options: { generator?: ReelsGenerator; env?: Record<string, string>; stripe?: Stripe } = {}): TestApp {
  const config = loadConfig({
    NODE_ENV: 'test',
    APP_URL,
    AI_MOCK: '1',
    ADMIN_EMAILS: 'admin@example.com',
    ...options.env,
  });
  const db = openDb(':memory:');
  const jobs = new JobQueue(db, options.generator ?? createMockGenerator(), { concurrency: 2, retries: 0, log: quietLog });
  const emails: EmailMessage[] = [];
  const mailer: Mailer = {
    async send(message) {
      emails.push(message);
    },
  };
  const stripe = options.stripe ?? new Stripe('sk_test_dummy');
  const billing = createBilling(db, config, config.stripe.secretKey ? stripe : undefined);
  const app = createApp({ config, db, jobs, billing, mailer, publicDir: './public', assetVersion: 'test' });
  return { app, db, jobs, config, emails, stripe };
}

/** A tiny browser: keeps cookies and sends a same-origin Origin header on form posts. */
export class Client {
  private cookies = new Map<string, string>();

  constructor(private readonly app: TestApp['app']) {}

  async request(path: string, init: RequestInit & { form?: Record<string, string> } = {}): Promise<Response> {
    const headers = new Headers(init.headers);
    if (this.cookies.size) headers.set('cookie', [...this.cookies].map(([k, v]) => `${k}=${v}`).join('; '));
    let body = init.body;
    if (init.form) {
      body = new URLSearchParams(init.form).toString();
      headers.set('content-type', 'application/x-www-form-urlencoded');
      if (!headers.has('origin')) headers.set('origin', APP_URL);
    }
    const res = await this.app.request(`${APP_URL}${path}`, { ...init, headers, body, redirect: 'manual' });
    for (const cookie of res.headers.getSetCookie()) {
      const [pair = '', ...attrs] = cookie.split(';');
      const index = pair.indexOf('=');
      const name = pair.slice(0, index).trim();
      const value = pair.slice(index + 1).trim();
      const expired = attrs.some((a) => /max-age=0/i.test(a.trim())) || value === '';
      if (expired) this.cookies.delete(name);
      else this.cookies.set(name, value);
    }
    return res;
  }

  get(path: string, init: RequestInit = {}) {
    return this.request(path, init);
  }

  post(path: string, form: Record<string, string> = {}, init: RequestInit = {}) {
    return this.request(path, { ...init, method: 'POST', form });
  }

  hasCookie(name: string): boolean {
    return this.cookies.has(name);
  }

  async signup(email: string, password = 'correct horse battery') {
    const res = await this.post('/signup', { email, password });
    if (res.status !== 303) throw new Error(`signup failed with ${res.status}: ${await res.text()}`);
    return res;
  }
}

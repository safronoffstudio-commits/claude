import { serve } from '@hono/node-server';
import { createClaudeGenerator } from './ai/claude.js';
import { createMockGenerator } from './ai/mock.js';
import { createApp } from './app.js';
import { purgeExpired } from './auth.js';
import { createBilling } from './billing.js';
import { loadConfig } from './config.js';
import { openDb } from './db.js';
import { createMailer } from './email.js';
import { JobQueue } from './jobs.js';

const config = loadConfig();
const db = openDb(config.databasePath);

const generator = config.ai.mock ? createMockGenerator({ delayMs: 2500 }) : createClaudeGenerator(config.ai);
const jobs = new JobQueue(db, generator, { concurrency: config.ai.concurrency });
const billing = createBilling(db, config);
const app = createApp({ config, db, jobs, billing, mailer: createMailer(config.email) });

const resumed = jobs.resumePending();
purgeExpired(db);
setInterval(() => purgeExpired(db), 6 * 3600 * 1000).unref();

const server = serve({ fetch: app.fetch, port: config.port }, (info) => {
  console.info(`Hookcut listening on http://localhost:${info.port} (${config.env})`);
  console.info(`  AI: ${config.ai.mock ? 'demo generator (set ANTHROPIC_API_KEY for real scripts)' : `${config.ai.model}, effort ${config.ai.effort}`}`);
  console.info(`  Billing: ${billing.enabled ? 'Stripe' : 'disabled (set STRIPE_SECRET_KEY)'}`);
  if (resumed) console.info(`  Resumed ${resumed} pending generation(s)`);
});

function shutdown(signal: string): void {
  console.info(`${signal} received, shutting down`);
  // Unfinished generations stay `pending` in the database and resume on the next start.
  server.close(() => {
    db.close();
    process.exit(0);
  });
  setTimeout(() => process.exit(0), 10_000).unref();
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));

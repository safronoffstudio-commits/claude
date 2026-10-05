import { now, type Db } from './db.js';
import { getPlan, isPaidPlan } from './plans.js';
import type { AdminStats } from './views/admin.js';

/** USD per million tokens. Estimates for the admin dashboard — keep in sync with Anthropic pricing. */
const MODEL_PRICES: [prefix: string, prices: { input: number; output: number; cacheRead: number; cacheWrite: number }][] = [
  ['claude-opus-5-5', { input: 4, output: 20, cacheRead: 0.2, cacheWrite: 5 }],
  ['claude-sonnet-5-5', { input: 2, output: 10, cacheRead: 0.2, cacheWrite: 2.5 }],
  ['claude-fable-5-1', { input: 10, output: 50, cacheRead: 0.25, cacheWrite: 12.5 }],
  ['claude-fable-5', { input: 10, output: 50, cacheRead: 1, cacheWrite: 12.5 }],
  ['claude-opus', { input: 5, output: 25, cacheRead: 0.5, cacheWrite: 6.25 }],
  ['claude-sonnet', { input: 3, output: 15, cacheRead: 0.3, cacheWrite: 3.75 }],
  ['claude-haiku', { input: 1, output: 5, cacheRead: 0.1, cacheWrite: 1.25 }],
];

export function estimateCost(model: string, tokens: { input: number; output: number; cacheRead: number; cacheWrite: number }): number {
  if (model === 'mock') return 0;
  const prices = MODEL_PRICES.find(([prefix]) => model.startsWith(prefix))?.[1] ?? MODEL_PRICES[0]![1];
  return (
    (tokens.input * prices.input + tokens.output * prices.output + tokens.cacheRead * prices.cacheRead + tokens.cacheWrite * prices.cacheWrite) /
    1_000_000
  );
}

export function adminStats(db: Db): AdminStats {
  const t = now();
  const day = 24 * 3600;
  const count = (sql: string, ...params: number[]) => Number((db.prepare(sql).get(...params) as { n: number }).n);

  const paying = (
    db
      .prepare(
        `SELECT plan, COUNT(*) AS n FROM users
         WHERE plan != 'free' AND subscription_status IN ('active', 'trialing', 'past_due') GROUP BY plan`,
      )
      .all() as { plan: string; n: number }[]
  )
    .filter((row) => isPaidPlan(row.plan))
    .map((row) => ({ plan: row.plan, count: Number(row.n), price: getPlan(row.plan).price }));

  const usage = db
    .prepare(
      `SELECT model, SUM(input_tokens) AS input, SUM(output_tokens) AS output,
              SUM(cache_read_tokens) AS cacheRead, SUM(cache_write_tokens) AS cacheWrite
       FROM generations WHERE created_at >= ? AND model IS NOT NULL GROUP BY model`,
    )
    .all(t - 30 * day) as { model: string; input: number; output: number; cacheRead: number; cacheWrite: number }[];

  return {
    users: count('SELECT COUNT(*) AS n FROM users'),
    signups7d: count('SELECT COUNT(*) AS n FROM users WHERE created_at >= ?', t - 7 * day),
    paying,
    mrr: paying.reduce((sum, p) => sum + p.count * p.price, 0),
    generations24h: count("SELECT COUNT(*) AS n FROM generations WHERE status != 'failed' AND created_at >= ?", t - day),
    generations30d: count("SELECT COUNT(*) AS n FROM generations WHERE status != 'failed' AND created_at >= ?", t - 30 * day),
    failed30d: count("SELECT COUNT(*) AS n FROM generations WHERE status = 'failed' AND created_at >= ?", t - 30 * day),
    aiCost30d: usage.reduce(
      (sum, row) =>
        sum +
        estimateCost(row.model, {
          input: Number(row.input ?? 0),
          output: Number(row.output ?? 0),
          cacheRead: Number(row.cacheRead ?? 0),
          cacheWrite: Number(row.cacheWrite ?? 0),
        }),
      0,
    ),
    recentUsers: db.prepare('SELECT email, plan, created_at FROM users ORDER BY created_at DESC, id DESC LIMIT 10').all() as {
      email: string;
      plan: string;
      created_at: number;
    }[],
  };
}

import { now, type Db, type UserRow } from './db.js';
import { getPlan, isPaidPlan, type Plan } from './plans.js';

export interface Usage {
  plan: Plan;
  used: number;
  limit: number;
  remaining: number;
  windowStart: number;
}

/**
 * Paid plans count from the start of the current Stripe billing period; everyone else
 * (and a paid user whose period data is stale) counts from the start of the calendar month.
 */
export function usageWindowStart(user: UserRow, at = now()): number {
  if (isPaidPlan(user.plan) && user.current_period_start && user.current_period_end && user.current_period_end > at) {
    return user.current_period_start;
  }
  const d = new Date(at * 1000);
  return Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), 1) / 1000;
}

export function getUsage(db: Db, user: UserRow, at = now()): Usage {
  const plan = getPlan(user.plan);
  const windowStart = usageWindowStart(user, at);
  // Failed generations are never counted.
  const row = db
    .prepare("SELECT COALESCE(SUM(credits), 0) AS n FROM generations WHERE user_id = ? AND status != 'failed' AND created_at >= ?")
    .get(user.id, windowStart) as { n: number };
  const used = Number(row.n);
  return { plan, used, limit: plan.generationsPerMonth, remaining: Math.max(0, plan.generationsPerMonth - used), windowStart };
}

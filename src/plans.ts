export type PlanId = 'free' | 'creator' | 'pro' | 'agency';
export type PaidPlanId = Exclude<PlanId, 'free'>;

export interface Plan {
  id: PlanId;
  name: string;
  /** Monthly price in USD. */
  price: number;
  /** Generations (one transcript → N scripts) per billing period. */
  generationsPerMonth: number;
  /** Max Reels scripts per generation. */
  maxReels: number;
  /** Max normalized transcript length in characters (~1,000 chars ≈ 1 minute of speech). */
  maxChars: number;
  /** Shared pages show no Hookcut branding. */
  whiteLabel: boolean;
}

export const PLANS: Record<PlanId, Plan> = {
  free: { id: 'free', name: 'Free', price: 0, generationsPerMonth: 3, maxReels: 3, maxChars: 40_000, whiteLabel: false },
  creator: { id: 'creator', name: 'Creator', price: 29, generationsPerMonth: 40, maxReels: 5, maxChars: 150_000, whiteLabel: false },
  pro: { id: 'pro', name: 'Pro', price: 79, generationsPerMonth: 150, maxReels: 5, maxChars: 300_000, whiteLabel: false },
  agency: { id: 'agency', name: 'Agency', price: 199, generationsPerMonth: 400, maxReels: 5, maxChars: 300_000, whiteLabel: true },
};

export const PAID_PLANS: PaidPlanId[] = ['creator', 'pro', 'agency'];

/** One generation covers up to ~1 hour of speech; longer recordings use one per started hour. */
export const CHARS_PER_CREDIT = 60_000;

export function creditsFor(chars: number): number {
  return Math.max(1, Math.ceil(chars / CHARS_PER_CREDIT));
}

export function isPaidPlan(id: string): id is PaidPlanId {
  return (PAID_PLANS as string[]).includes(id);
}

export function getPlan(id: string): Plan {
  return PLANS[id as PlanId] ?? PLANS.free;
}

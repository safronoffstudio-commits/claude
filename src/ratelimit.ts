/** Fixed-window, in-memory rate limiter. Good enough for a single-process deployment. */
export class RateLimiter {
  private readonly hits = new Map<string, { count: number; resetAt: number }>();

  constructor(
    private readonly limit: number,
    private readonly windowMs: number,
  ) {}

  /** Records a hit; returns false when the key is over its limit. */
  take(key: string, at = Date.now()): boolean {
    if (this.hits.size > 10_000) this.sweep(at);
    const entry = this.hits.get(key);
    if (!entry || entry.resetAt <= at) {
      this.hits.set(key, { count: 1, resetAt: at + this.windowMs });
      return true;
    }
    entry.count++;
    return entry.count <= this.limit;
  }

  reset(key: string): void {
    this.hits.delete(key);
  }

  private sweep(at: number): void {
    for (const [key, entry] of this.hits) if (entry.resetAt <= at) this.hits.delete(key);
  }
}

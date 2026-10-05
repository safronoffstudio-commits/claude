import { GenerationError, type GenerationErrorCode, type ReelsGenerator } from './ai/generator.js';
import type { Db } from './db.js';
import { completeGeneration, failGeneration, getGeneration, parseOptions } from './generations.js';

const RETRYABLE: GenerationErrorCode[] = ['rate_limited', 'overloaded', 'network', 'invalid_output'];

export interface JobQueueOptions {
  concurrency: number;
  /** Extra attempts for transient failures, on top of the SDK's own retries. */
  retries?: number;
  retryDelayMs?: number;
  log?: Pick<Console, 'error' | 'info'>;
}

/**
 * In-process queue for generations. Rows are the source of truth: anything still
 * `pending` when the process restarts is picked up again by resumePending().
 */
export class JobQueue {
  private readonly queue: string[] = [];
  private readonly queued = new Set<string>();
  private running = 0;
  private waiters: (() => void)[] = [];

  constructor(
    private readonly db: Db,
    private readonly generator: ReelsGenerator,
    private readonly options: JobQueueOptions,
  ) {}

  enqueue(id: string): void {
    if (this.queued.has(id)) return;
    this.queued.add(id);
    this.queue.push(id);
    this.pump();
  }

  resumePending(): number {
    const rows = this.db.prepare("SELECT id FROM generations WHERE status = 'pending' ORDER BY created_at").all() as { id: string }[];
    rows.forEach((row) => this.enqueue(row.id));
    return rows.length;
  }

  /** Resolves when nothing is queued or running. */
  idle(): Promise<void> {
    if (this.running === 0 && this.queue.length === 0) return Promise.resolve();
    return new Promise((resolve) => this.waiters.push(resolve));
  }

  private pump(): void {
    while (this.running < this.options.concurrency && this.queue.length > 0) {
      const id = this.queue.shift()!;
      this.running++;
      void this.run(id).finally(() => {
        this.running--;
        this.queued.delete(id);
        this.pump();
        if (this.running === 0 && this.queue.length === 0) {
          const waiters = this.waiters;
          this.waiters = [];
          waiters.forEach((resolve) => resolve());
        }
      });
    }
  }

  private async run(id: string): Promise<void> {
    const row = getGeneration(this.db, id);
    if (!row || row.status !== 'pending') return;
    const options = parseOptions(row);
    const log = this.options.log ?? console;
    const retries = this.options.retries ?? 1;

    for (let attempt = 0; ; attempt++) {
      try {
        const result = await this.generator.generate({
          options,
          transcript: row.transcript,
          hasTimecodes: options.hasTimecodes,
          durationSec: options.durationSec,
        });
        completeGeneration(this.db, id, result);
        log.info(`[jobs] ${id} done (${result.model}, in=${result.usage.input} out=${result.usage.output} cache=${result.usage.cacheRead})`);
        return;
      } catch (err) {
        const code: GenerationErrorCode = err instanceof GenerationError ? err.code : 'unknown';
        if (attempt < retries && RETRYABLE.includes(code)) {
          log.error(`[jobs] ${id} attempt ${attempt + 1} failed (${code}), retrying:`, err instanceof Error ? err.message : err);
          await new Promise((resolve) => setTimeout(resolve, this.options.retryDelayMs ?? 15_000));
          continue;
        }
        log.error(`[jobs] ${id} failed (${code}):`, err);
        failGeneration(this.db, id, code);
        return;
      }
    }
  }
}

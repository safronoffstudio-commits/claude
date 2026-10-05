import type { BlockKind, Reel } from './ai/schema.js';

/**
 * Deterministic checks from section 6 of the framework ("Проверка перед сдачей") — the
 * ones that can be verified from the script's structure rather than taken on the model's word.
 */
export type CheckId = 'duration' | 'hookTiming' | 'hookWords' | 'bodyBeats' | 'singleCta' | 'sourced' | 'order';

export interface CheckResult {
  id: CheckId;
  ok: boolean;
  value?: number;
}

const ORDER: Record<BlockKind, number> = { hook: 0, promise: 1, body: 2, payoff: 3, cta: 4 };

export function countWords(text: string): number {
  return text.split(/\s+/).filter((w) => /[\p{L}\p{N}]/u.test(w)).length;
}

export function reelDuration(reel: Reel): number {
  return reel.blocks.reduce((max, b) => Math.max(max, b.end_sec), 0);
}

export function lintReel(reel: Reel): CheckResult[] {
  const { blocks } = reel;
  const duration = reelDuration(reel);
  const hook = blocks.find((b) => b.kind === 'hook');
  const hookWords = hook ? countWords(hook.on_screen_text) : 0;
  const bodyBeats = blocks.filter((b) => b.kind === 'body').length;
  const ctas = blocks.filter((b) => b.kind === 'cta').length;
  const sourced = blocks
    .filter((b) => b.kind === 'body' || b.kind === 'payoff')
    .every((b) => Boolean(b.source_timecode?.trim()));
  const ordered =
    blocks[0]?.kind === 'hook' &&
    blocks.at(-1)?.kind === 'cta' &&
    blocks.every((b, i) => i === 0 || ORDER[b.kind] >= ORDER[blocks[i - 1]!.kind]);

  return [
    { id: 'duration', ok: duration >= 40 && duration <= 60, value: duration },
    { id: 'hookTiming', ok: Boolean(hook && hook.start_sec === 0 && hook.end_sec <= 3) },
    { id: 'hookWords', ok: hookWords >= 4 && hookWords <= 7, value: hookWords },
    { id: 'bodyBeats', ok: bodyBeats >= 2 && bodyBeats <= 3, value: bodyBeats },
    { id: 'singleCta', ok: ctas === 1 },
    { id: 'sourced', ok: sourced },
    { id: 'order', ok: ordered },
  ];
}

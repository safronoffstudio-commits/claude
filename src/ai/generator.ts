import type { GenerationRequest } from './prompt.js';
import { ReelsOutputSchema, type ReelsOutput } from './schema.js';

export interface TokenUsage {
  input: number;
  output: number;
  cacheRead: number;
  cacheWrite: number;
}

export interface GenerationResult {
  output: ReelsOutput;
  model: string;
  usage: TokenUsage;
}

export interface ReelsGenerator {
  readonly name: string;
  generate(req: GenerationRequest): Promise<GenerationResult>;
}

/** Codes are stored on the generation row and shown to the user as localized messages. */
export type GenerationErrorCode =
  | 'refused'
  | 'truncated'
  | 'invalid_output'
  | 'rate_limited'
  | 'overloaded'
  | 'network'
  | 'config'
  | 'bad_request'
  | 'interrupted'
  | 'unknown';

export class GenerationError extends Error {
  constructor(
    readonly code: GenerationErrorCode,
    message: string,
    options?: { cause?: unknown },
  ) {
    super(message, options);
    this.name = 'GenerationError';
  }
}

/** Validates raw model JSON and normalizes the parts the UI relies on. */
export function parseReelsOutput(text: string, requestedCount: number): ReelsOutput {
  let json: unknown;
  try {
    json = JSON.parse(text);
  } catch (err) {
    throw new GenerationError('invalid_output', 'Model output is not valid JSON', { cause: err });
  }
  const parsed = ReelsOutputSchema.safeParse(json);
  if (!parsed.success) {
    throw new GenerationError('invalid_output', `Model output does not match the schema: ${parsed.error.message}`);
  }
  const output = parsed.data;
  output.reels = output.reels.slice(0, requestedCount).map((reel) => ({
    ...reel,
    blocks: reel.blocks
      .map((b) => ({ ...b, start_sec: Math.max(0, Math.round(b.start_sec)), end_sec: Math.max(0, Math.round(b.end_sec)) }))
      .sort((a, b) => a.start_sec - b.start_sec),
    hashtags: reel.hashtags
      .map((tag) => tag.trim().replace(/\s+/g, ''))
      .filter(Boolean)
      .map((tag) => (tag.startsWith('#') ? tag : `#${tag}`)),
  }));
  if (output.reels.length === 0) {
    throw new GenerationError('invalid_output', 'Model returned no reels');
  }
  return output;
}

import { z } from 'zod';

export const BLOCK_KINDS = ['hook', 'promise', 'body', 'payoff', 'cta'] as const;
export const HOOK_TYPES = ['expectation_gap', 'number_with_stakes', 'open_loop'] as const;
export const MOMENT_TYPES = ['fact', 'insight', 'conflict', 'number', 'emotion'] as const;

export type BlockKind = (typeof BLOCK_KINDS)[number];
export type HookType = (typeof HOOK_TYPES)[number];
export type MomentType = (typeof MOMENT_TYPES)[number];

// Property order is generation order: the model analyses the source before it writes.
export const MomentSchema = z.object({
  timecode: z.string().describe('Range in the source, e.g. "12:30–12:42", or the first words of the passage if there are no timecodes'),
  what_happens: z.string(),
  type: z.enum(MOMENT_TYPES),
  use_in_reels: z.boolean(),
  reason: z.string().describe('Why the moment does or does not work without context'),
});

export const BlockSchema = z.object({
  kind: z.enum(BLOCK_KINDS),
  start_sec: z.number().describe('Start on the Reel timeline, whole seconds'),
  end_sec: z.number().describe('End on the Reel timeline, whole seconds'),
  voice: z.string().describe('What is heard: the speaker’s own words (trimmed) or new voiceover'),
  on_screen_text: z.string().describe('Burned-in text, short'),
  visual: z.string().describe('Shot, framing, b-roll or zoom direction for the editor'),
  source_timecode: z
    .string()
    .nullable()
    .describe('Source range this beat is cut from, e.g. "12:30–12:42"; null for pure new voiceover'),
});

export const ReelSchema = z.object({
  title: z.string(),
  hook_type: z.enum(HOOK_TYPES),
  blocks: z.array(BlockSchema),
  alternative_hooks: z.array(z.string()).describe('Two more hook lines for A/B tests'),
  cover_text: z.string(),
  caption: z.string(),
  hashtags: z.array(z.string()),
  editing_notes: z.array(z.string()),
  loop_note: z.string(),
});

export const ReelsOutputSchema = z.object({
  source_summary: z.string().describe('One or two sentences: what the recording is about'),
  moments: z.array(MomentSchema),
  on_screen_quotes: z.array(z.string()).describe('2–3 verbatim phrases from the transcript that work as on-screen text'),
  reels: z.array(ReelSchema),
  notes: z.string().describe('Anything the user should know; empty string if nothing'),
});

export type Moment = z.infer<typeof MomentSchema>;
export type Block = z.infer<typeof BlockSchema>;
export type Reel = z.infer<typeof ReelSchema>;
export type ReelsOutput = z.infer<typeof ReelsOutputSchema>;

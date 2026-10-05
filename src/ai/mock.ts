import type { GenerationResult, ReelsGenerator } from './generator.js';
import type { GenerationRequest } from './prompt.js';
import type { Block, HookType, Moment, MomentType, Reel, ReelsOutput } from './schema.js';
import { formatTimestamp, parseTimestamp } from '../transcript.js';

/**
 * Offline generator for local development, demos and tests. It assembles a structurally
 * valid result from the transcript itself, so the whole product flow works without an
 * API key. Results are flagged in the UI as demo output.
 */
export function createMockGenerator(options: { delayMs?: number } = {}): ReelsGenerator {
  return {
    name: 'mock',
    async generate(req): Promise<GenerationResult> {
      if (options.delayMs) await new Promise((resolve) => setTimeout(resolve, options.delayMs));
      return {
        output: buildMockOutput(req),
        model: 'mock',
        usage: { input: Math.ceil(req.transcript.length / 4), output: 0, cacheRead: 0, cacheWrite: 0 },
      };
    },
  };
}

interface Piece {
  start: number | null;
  end: number | null;
  text: string;
}

const LINE_RE = /^\[((?:\d+:)?\d{1,2}:\d{2})\]\s*(.*)$/;

function splitPieces(transcript: string): Piece[] {
  const lines = transcript.split('\n').filter((l) => l.trim());
  const pieces: Piece[] = lines.map((line) => {
    const m = LINE_RE.exec(line);
    return m ? { start: parseTimestamp(m[1]!), end: null, text: m[2]! } : { start: null, end: null, text: line.trim() };
  });
  pieces.forEach((p, i) => {
    const next = pieces[i + 1];
    if (p.start !== null) p.end = next?.start ?? p.start + 10;
  });
  return pieces;
}

function score(text: string): number {
  let s = 0;
  if (/\d/.test(text)) s += 3;
  if (/[?!]/.test(text)) s += 1;
  if (text.length > 60 && text.length < 400) s += 1;
  return s;
}

function words(text: string, count: number): string {
  const list = text.replace(/[«»"“”]/g, '').split(/\s+/).filter(Boolean);
  return list.slice(0, count).join(' ') + (list.length > count ? '…' : '');
}

function range(piece: Piece): string {
  if (piece.start === null) return `"${words(piece.text, 5)}"`;
  return `${formatTimestamp(piece.start)}–${formatTimestamp(piece.end ?? piece.start + 10)}`;
}

const MOMENT_TYPES: MomentType[] = ['insight', 'number', 'conflict', 'fact', 'emotion'];
const HOOK_TYPES: HookType[] = ['expectation_gap', 'number_with_stakes', 'open_loop'];

const COPY = {
  ru: {
    summary: (n: number) => `Демо-разбор записи из ${n} фрагментов.`,
    promise: 'Покажу за минуту, что из этого следует.',
    cta: {
      subscribe: 'Подпишитесь, чтобы не пропустить продолжение.',
      comment: 'Напишите кодовое слово в комментариях.',
      link: 'Ссылка — в профиле.',
      dm: 'Напишите мне в директ.',
      save: 'Сохраните, чтобы не потерять.',
      custom: 'Сделайте это прямо сейчас.',
    },
    visual: ['Крупный план спикера, резкий зум', 'Средний план, текст по центру', 'Смена ракурса, B-roll', 'Чистый кадр спикера', 'Спикер в кадр, текст CTA снизу'],
    reason: 'Понятно без контекста и укладывается в 5–12 секунд.',
    weak: 'Требует контекста всего разговора.',
    notes: 'Демо-режим: сценарии собраны из фрагментов расшифровки без ИИ.',
    editing: ['Вырезать паузы и «э-э» встык', 'Субтитры по 2–4 слова, ключевое слово цветом', 'Звуковой акцент на стыке блоков'],
    loop: 'Последняя фраза подводит к хуку — ролик замыкается в луп.',
    title: (n: number) => `Ролик ${n}`,
    alt: ['Все делают это неправильно.', 'Одна фраза, которая всё меняет.'],
  },
  en: {
    summary: (n: number) => `Demo breakdown of a recording with ${n} fragments.`,
    promise: 'Here is what it means, in under a minute.',
    cta: {
      subscribe: 'Follow so you don’t miss part two.',
      comment: 'Comment the keyword below.',
      link: 'The link is in my bio.',
      dm: 'Send me a DM.',
      save: 'Save this so you don’t lose it.',
      custom: 'Do it right now.',
    },
    visual: ['Close-up of the speaker, punch-in zoom', 'Medium shot, text centered', 'Angle change, B-roll', 'Clean shot of the speaker', 'Speaker on camera, CTA text at the bottom'],
    reason: 'Works without context and fits in 5–12 seconds.',
    weak: 'Needs the context of the whole conversation.',
    notes: 'Demo mode: scripts were assembled from transcript fragments without AI.',
    editing: ['Cut pauses and filler words back-to-back', 'Subtitles 2–4 words per line, key word highlighted', 'Sound accent on every beat join'],
    loop: 'The last line leads back into the hook so the clip loops.',
    title: (n: number) => `Reel ${n}`,
    alt: ['Everyone gets this wrong.', 'One sentence that changes everything.'],
  },
};

export function buildMockOutput(req: GenerationRequest): ReelsOutput {
  const { options } = req;
  const ru = options.language === 'ru' || (options.language === 'auto' && /[а-яё]/i.test(req.transcript));
  const copy = ru ? COPY.ru : COPY.en;
  const pieces = splitPieces(req.transcript);
  const ranked = pieces
    .map((piece, index) => ({ piece, index, score: score(piece.text) }))
    .sort((a, b) => b.score - a.score || a.index - b.index);
  const picked = ranked.slice(0, Math.min(10, ranked.length)).sort((a, b) => a.index - b.index);

  const moments: Moment[] = picked.map(({ piece, score: s }, i) => ({
    timecode: range(piece),
    what_happens: words(piece.text, 14),
    type: /\d/.test(piece.text) ? 'number' : MOMENT_TYPES[i % MOMENT_TYPES.length]!,
    use_in_reels: s >= 2,
    reason: s >= 2 ? copy.reason : copy.weak,
  }));

  const cores = [...ranked].slice(0, Math.max(1, options.count));
  const reels: Reel[] = cores.slice(0, options.count).map(({ index }, i) => {
    const at = (offset: number) => pieces[Math.min(pieces.length - 1, index + offset)]!;
    const core = at(0);
    const beats: [Block['kind'], number, number, Piece | null, string][] = [
      ['hook', 0, 3, core, words(core.text, 6)],
      ['promise', 3, 7, null, copy.promise],
      ['body', 7, 18, at(1), at(1).text],
      ['body', 18, 29, at(2), at(2).text],
      ['body', 29, 40, at(3), at(3).text],
      ['payoff', 40, 52, core, core.text],
      ['cta', 52, 58, null, options.ctaDetail ? `${copy.cta[options.ctaGoal].replace(/\.$/, '')} — ${options.ctaDetail}` : copy.cta[options.ctaGoal]],
    ];
    const blocks: Block[] = beats.map(([kind, start, end, source, voice], j) => ({
      kind,
      start_sec: start,
      end_sec: end,
      voice: words(voice, 40),
      on_screen_text: words(voice, kind === 'hook' ? 5 : 4),
      visual: copy.visual[j % copy.visual.length]!,
      source_timecode: source ? range(source) : null,
    }));
    return {
      title: `${copy.title(i + 1)}: ${words(core.text, 6)}`,
      hook_type: HOOK_TYPES[i % HOOK_TYPES.length]!,
      blocks,
      alternative_hooks: copy.alt,
      cover_text: words(core.text, 4),
      caption: words(core.text, 30),
      hashtags: ru ? ['#reels', '#контент', '#нарезка'] : ['#reels', '#content', '#podcast'],
      editing_notes: copy.editing,
      loop_note: copy.loop,
    };
  });

  return {
    source_summary: copy.summary(pieces.length),
    moments,
    on_screen_quotes: picked.slice(0, 3).map(({ piece }) => words(piece.text, 8)),
    reels,
    notes: copy.notes,
  };
}

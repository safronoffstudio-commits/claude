/**
 * Turns whatever the user pasted or uploaded (SRT, WebVTT incl. YouTube auto-captions,
 * Whisper output, "0:00 text" transcripts, plain text) into a compact, uniformly
 * time-coded transcript the model can cite: one "[m:ss] text" line per ~6–12 seconds.
 */

export type TranscriptFormat = 'srt' | 'vtt' | 'timecoded' | 'plain';

export interface Segment {
  /** Start time in seconds, or null for plain text. */
  start: number | null;
  text: string;
}

export interface NormalizedTranscript {
  format: TranscriptFormat;
  hasTimecodes: boolean;
  segments: Segment[];
  /** The transcript as sent to the model. */
  text: string;
  /** Source duration (from timecodes) or an estimate from the word count. */
  durationSec: number;
  words: number;
}

const TS = String.raw`(?:\d+:)?\d{1,2}:\d{2}(?:[.,]\d{1,3})?`;
const CUE_RE = new RegExp(String.raw`^\s*\[?\s*(${TS})\s*-->\s*(${TS})\s*\]?(.*)$`);
const LEADING_TC_RE = new RegExp(String.raw`^\s*[\[(]?(${TS})[\])]?\s*(?:[-–—:|]\s*)?(.*)$`);
const VTT_SETTINGS_RE = /^(\s*[a-z-]+:\S+)*\s*$/i;
const NOISE_RE = /\[(?:music|applause|laughter|laughs|inaudible|музыка|аплодисменты|смех|неразборчиво)\]/gi;

const MIN_SEGMENT_SEC = 6;
const MAX_SEGMENT_SEC = 12;
const WORDS_PER_MINUTE = 140;

export function parseTimestamp(value: string): number {
  const [clock = '0', frac = ''] = value.trim().split(/[.,]/);
  const parts = clock.split(':').map(Number);
  let seconds = 0;
  for (const part of parts) seconds = seconds * 60 + part;
  return seconds + (frac ? Number(`0.${frac}`) : 0);
}

export function formatTimestamp(totalSeconds: number): string {
  const s = Math.max(0, Math.floor(totalSeconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = String(s % 60).padStart(2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${sec}` : `${m}:${sec}`;
}

function cleanText(text: string): string {
  return text
    .replace(/\{\\[^}]*\}/g, '') // SSA/ASS override tags inside SRT ({\an8})
    .replace(/<[^>]*>/g, '') // VTT voice/class/timestamp tags, stray HTML
    .replace(/&nbsp;/g, ' ')
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(NOISE_RE, '')
    .replace(/\s+/g, ' ')
    .trim();
}

interface Cue {
  start: number;
  end: number | null;
  lines: string[];
}

function parseCues(lines: string[]): Cue[] {
  const cues: Cue[] = [];
  let current: Cue | null = null;
  let inNote = false;
  for (const line of lines) {
    const match = CUE_RE.exec(line);
    if (match) {
      inNote = false;
      const trailing = match[3] ?? '';
      current = { start: parseTimestamp(match[1]!), end: parseTimestamp(match[2]!), lines: [] };
      // Whisper prints "[00:00.000 --> 00:05.000] text" on one line; VTT puts cue settings there.
      if (!VTT_SETTINGS_RE.test(trailing)) current.lines.push(trailing);
      cues.push(current);
      continue;
    }
    if (line.trim() === '') {
      current = null;
      inNote = false;
      continue;
    }
    if (/^(NOTE|STYLE|REGION)\b/.test(line)) {
      inNote = true;
      continue;
    }
    if (inNote || !current) continue; // WEBVTT header, SRT counters, metadata
    current.lines.push(line);
  }
  return cues;
}

/** Cues → segments, dropping the rolling duplicates of YouTube auto-captions. */
function cuesToSegments(cues: Cue[]): Segment[] {
  const out: Segment[] = [];
  const recent: string[] = [];
  for (const cue of cues) {
    const fresh: string[] = [];
    for (const raw of cue.lines) {
      const text = cleanText(raw);
      if (!text || recent.includes(text)) continue;
      fresh.push(text);
      recent.push(text);
      if (recent.length > 4) recent.shift();
    }
    if (fresh.length) out.push({ start: cue.start, text: fresh.join(' ') });
  }
  return out;
}

function parseTimecodedLines(lines: string[]): Segment[] | null {
  const segments: Segment[] = [];
  let pending: Segment | null = null;
  let timecoded = 0;
  let nonEmpty = 0;
  for (const line of lines) {
    if (!line.trim()) continue;
    nonEmpty++;
    const match = LEADING_TC_RE.exec(line);
    if (match) {
      timecoded++;
      pending = { start: parseTimestamp(match[1]!), text: cleanText(match[2] ?? '') };
      segments.push(pending);
    } else if (pending) {
      // YouTube's "Show transcript" copy puts the text on the line after the timecode.
      const text = cleanText(line);
      if (text) pending.text = pending.text ? `${pending.text} ${text}` : text;
    }
  }
  if (timecoded < 3 || timecoded / nonEmpty < 0.25) return null;
  return segments.filter((s) => s.text);
}

function mergeSegments(segments: Segment[]): Segment[] {
  const merged: Segment[] = [];
  let current: Segment | null = null;
  for (const seg of segments) {
    if (current && current.start !== null && seg.start !== null) {
      const length = seg.start - current.start;
      const sentenceEnded = /[.!?…]["»”)]?$/.test(current.text);
      if (length < MIN_SEGMENT_SEC || (length < MAX_SEGMENT_SEC && !sentenceEnded)) {
        current.text = `${current.text} ${seg.text}`;
        continue;
      }
    }
    current = { ...seg };
    merged.push(current);
  }
  return merged;
}

function countWords(text: string): number {
  return text.split(/\s+/).filter(Boolean).length;
}

export function normalizeTranscript(raw: string): NormalizedTranscript {
  const input = raw.replace(/^﻿/, '').replace(/\r\n?/g, '\n');
  const lines = input.split('\n');

  let format: TranscriptFormat;
  let segments: Segment[];
  let endSec: number | null = null;

  if (lines.some((l) => CUE_RE.test(l))) {
    format = /^\s*WEBVTT/.test(input) ? 'vtt' : 'srt';
    const cues = parseCues(lines);
    endSec = cues.reduce((max, c) => Math.max(max, c.end ?? c.start), 0);
    segments = mergeSegments(cuesToSegments(cues));
  } else {
    const timecoded = parseTimecodedLines(lines);
    if (timecoded) {
      format = 'timecoded';
      endSec = timecoded.reduce((max, s) => Math.max(max, s.start ?? 0), 0);
      segments = mergeSegments(timecoded);
    } else {
      format = 'plain';
      segments = input
        .split(/\n\s*\n/)
        .map((p) => cleanText(p))
        .filter(Boolean)
        .map((text) => ({ start: null, text }));
    }
  }

  const hasTimecodes = format !== 'plain';
  const text = segments
    .map((s) => (s.start !== null ? `[${formatTimestamp(s.start)}] ${s.text}` : s.text))
    .join(hasTimecodes ? '\n' : '\n\n');
  const words = segments.reduce((n, s) => n + countWords(s.text), 0);
  const lastStart = segments.at(-1)?.start ?? null;
  const durationSec = hasTimecodes
    ? Math.round(Math.max(endSec ?? 0, lastStart ?? 0))
    : Math.round((words / WORDS_PER_MINUTE) * 60);

  return { format, hasTimecodes, segments, text, durationSec, words };
}

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { describe, it } from 'node:test';
import { formatTimestamp, normalizeTranscript, parseTimestamp } from '../src/transcript.js';

describe('timestamps', () => {
  it('parses SRT, VTT and short forms', () => {
    assert.equal(parseTimestamp('01:02:03,500'), 3723.5);
    assert.equal(parseTimestamp('02:03.250'), 123.25);
    assert.equal(parseTimestamp('2:03'), 123);
  });

  it('formats with hours only when needed', () => {
    assert.equal(formatTimestamp(65), '1:05');
    assert.equal(formatTimestamp(3723), '1:02:03');
    assert.equal(formatTimestamp(-4), '0:00');
  });
});

describe('normalizeTranscript', () => {
  it('reads SRT, strips markup and merges cues into ~6–12s segments', () => {
    const srt = [
      '1',
      '00:00:01,000 --> 00:00:04,000',
      'Все делают одну ошибку.',
      '',
      '2',
      '00:00:04,500 --> 00:00:07,000',
      '<i>Они начинают</i> с логотипа.',
      '',
      '3',
      '00:00:08,000 --> 00:00:15,000',
      '{\\an8}А надо начинать с хука. [Музыка]',
    ].join('\n');
    const result = normalizeTranscript(srt);
    assert.equal(result.format, 'srt');
    assert.equal(result.hasTimecodes, true);
    assert.equal(result.durationSec, 15);
    assert.equal(result.text, '[0:01] Все делают одну ошибку. Они начинают с логотипа.\n[0:08] А надо начинать с хука.');
  });

  it('drops the rolling duplicates of YouTube auto-captions', () => {
    const vtt = [
      'WEBVTT',
      'Kind: captions',
      'Language: en',
      '',
      '00:00:00.000 --> 00:00:02.000 align:start position:0%',
      'hello<00:00:00.500><c> everyone</c>',
      '',
      '00:00:02.000 --> 00:00:02.010 align:start position:0%',
      'hello everyone',
      '',
      '00:00:02.010 --> 00:00:05.000 align:start position:0%',
      'hello everyone',
      'today<00:00:03.000><c> we talk</c>',
      '',
      'NOTE this is a comment',
      '',
      '00:00:05.000 --> 00:00:09.000',
      'today we talk',
      'about money.',
    ].join('\n');
    const result = normalizeTranscript(vtt);
    assert.equal(result.format, 'vtt');
    assert.equal(result.text, '[0:00] hello everyone today we talk about money.');
  });

  it('reads Whisper one-line cues', () => {
    const result = normalizeTranscript('[00:00.000 --> 00:05.000]  First.\n[00:05.000 --> 00:12.000]  Second.\n[00:12.000 --> 00:20.000]  Third.');
    assert.equal(result.text, '[0:00] First. Second.\n[0:12] Third.');
  });

  it('reads YouTube "Show transcript" copies with timecodes on their own lines', () => {
    const fixture = readFileSync(new URL('./fixtures/podcast-ru.txt', import.meta.url), 'utf8');
    const result = normalizeTranscript(fixture);
    assert.equal(result.format, 'timecoded');
    assert.ok(result.text.startsWith('[0:00] Привет, это подкаст'));
    assert.match(result.text, /\[1:04\] Вот это и есть главная ошибка/);
    assert.equal(result.durationSec, 218);
    assert.ok(result.segments.length > 10);
  });

  it('keeps plain text as paragraphs and estimates duration from words', () => {
    const words = Array.from({ length: 278 }, () => 'слово').join(' ');
    const result = normalizeTranscript(`${words}\n\nВторой абзац.`);
    assert.equal(result.format, 'plain');
    assert.equal(result.hasTimecodes, false);
    assert.equal(result.segments.length, 2);
    assert.equal(result.durationSec, 120);
  });

  it('does not mistake a few times of day in prose for a timecoded transcript', () => {
    const text = '10:30 — встреча с командой.\nПотом обед.\nПотом ещё одна встреча.\nИ ещё одна.\nВечером созвон.';
    assert.equal(normalizeTranscript(text).format, 'plain');
  });
});

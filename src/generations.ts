import type { GenerationOptions } from './ai/prompt.js';
import type { ReelsOutput } from './ai/schema.js';
import type { GenerationErrorCode, GenerationResult } from './ai/generator.js';
import { now, randomId, type Db, type GenerationRow } from './db.js';
import type { TranscriptFormat } from './transcript.js';

/** Options as stored with the generation: what the user chose plus transcript facts. */
export interface StoredOptions extends GenerationOptions {
  hasTimecodes: boolean;
  durationSec: number;
  format: TranscriptFormat;
}

export interface GenerationListItem {
  id: string;
  title: string;
  status: GenerationRow['status'];
  created_at: number;
  reels: number | null;
}

export function createGeneration(
  db: Db,
  userId: number,
  title: string,
  options: StoredOptions,
  transcript: string,
  credits: number,
): GenerationRow {
  return db
    .prepare(
      `INSERT INTO generations (id, user_id, title, status, options_json, transcript, credits, created_at)
       VALUES (?, ?, ?, 'pending', ?, ?, ?, ?) RETURNING *`,
    )
    .get(randomId(), userId, title, JSON.stringify(options), transcript, credits, now()) as unknown as GenerationRow;
}

export function getGeneration(db: Db, id: string): GenerationRow | undefined {
  return db.prepare('SELECT * FROM generations WHERE id = ?').get(id) as GenerationRow | undefined;
}

export function getUserGeneration(db: Db, userId: number, id: string): GenerationRow | undefined {
  return db.prepare('SELECT * FROM generations WHERE id = ? AND user_id = ?').get(id, userId) as GenerationRow | undefined;
}

export function getSharedGeneration(db: Db, shareId: string): GenerationRow | undefined {
  return db.prepare("SELECT * FROM generations WHERE share_id = ? AND status = 'done'").get(shareId) as GenerationRow | undefined;
}

export function listGenerations(db: Db, userId: number, limit = 100): GenerationListItem[] {
  return db
    .prepare(
      `SELECT id, title, status, created_at,
              CASE WHEN result_json IS NULL THEN NULL ELSE json_array_length(result_json, '$.reels') END AS reels
       FROM generations WHERE user_id = ? ORDER BY created_at DESC, rowid DESC LIMIT ?`,
    )
    .all(userId, limit) as unknown as GenerationListItem[];
}

export function deleteGeneration(db: Db, userId: number, id: string): boolean {
  return Number(db.prepare('DELETE FROM generations WHERE id = ? AND user_id = ?').run(id, userId).changes) > 0;
}

export function setSharing(db: Db, userId: number, id: string, enabled: boolean): void {
  db.prepare('UPDATE generations SET share_id = ? WHERE id = ? AND user_id = ?').run(enabled ? randomId(12) : null, id, userId);
}

export function requeueGeneration(db: Db, userId: number, id: string): boolean {
  const result = db
    .prepare(
      `UPDATE generations SET status = 'pending', error = NULL, completed_at = NULL, created_at = ?
       WHERE id = ? AND user_id = ? AND status = 'failed'`,
    )
    .run(now(), id, userId);
  return Number(result.changes) > 0;
}

export function completeGeneration(db: Db, id: string, result: GenerationResult): void {
  db.prepare(
    `UPDATE generations SET status = 'done', result_json = ?, model = ?, input_tokens = ?, output_tokens = ?,
       cache_read_tokens = ?, cache_write_tokens = ?, error = NULL, completed_at = ?
     WHERE id = ? AND status = 'pending'`,
  ).run(
    JSON.stringify(result.output),
    result.model,
    result.usage.input,
    result.usage.output,
    result.usage.cacheRead,
    result.usage.cacheWrite,
    now(),
    id,
  );
}

export function failGeneration(db: Db, id: string, code: GenerationErrorCode): void {
  db.prepare("UPDATE generations SET status = 'failed', error = ?, completed_at = ? WHERE id = ? AND status = 'pending'").run(code, now(), id);
}

export function parseResult(row: GenerationRow): ReelsOutput | null {
  return row.result_json ? (JSON.parse(row.result_json) as ReelsOutput) : null;
}

export function parseOptions(row: GenerationRow): StoredOptions {
  return JSON.parse(row.options_json) as StoredOptions;
}

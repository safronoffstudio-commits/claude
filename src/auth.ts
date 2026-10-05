import { createHash, randomBytes, scrypt, timingSafeEqual, type ScryptOptions } from 'node:crypto';
import { now, type Db, type UserRow } from './db.js';
import type { Locale } from './i18n.js';

export const SESSION_COOKIE = 'hc_session';
export const SESSION_TTL_SEC = 30 * 24 * 3600;
const RESET_TTL_SEC = 3600;

const SCRYPT = { N: 16384, r: 8, p: 1, keylen: 64, maxmem: 64 * 1024 * 1024 };

function scryptAsync(password: string, salt: Buffer, keylen: number, options: ScryptOptions): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    scrypt(password, salt, keylen, options, (err, key) => (err ? reject(err) : resolve(key)));
  });
}

export async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(16);
  const { N, r, p, keylen, maxmem } = SCRYPT;
  const hash = await scryptAsync(password.normalize('NFKC'), salt, keylen, { N, r, p, maxmem });
  return ['scrypt', N, r, p, salt.toString('base64'), hash.toString('base64')].join('$');
}

export async function verifyPassword(password: string, stored: string): Promise<boolean> {
  const [alg, n, r, p, salt, hash] = stored.split('$');
  if (alg !== 'scrypt' || !n || !r || !p || !salt || !hash) return false;
  const expected = Buffer.from(hash, 'base64');
  const actual = await scryptAsync(password.normalize('NFKC'), Buffer.from(salt, 'base64'), expected.length, {
    N: Number(n),
    r: Number(r),
    p: Number(p),
    maxmem: SCRYPT.maxmem,
  });
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}

/** Tokens are only ever stored hashed, so a database leak doesn't leak sessions or reset links. */
function tokenId(token: string): string {
  return createHash('sha256').update(token).digest('hex');
}

export function normalizeEmail(email: string): string {
  return email.trim().toLowerCase();
}

export function isValidEmail(email: string): boolean {
  return email.length <= 254 && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
}

export const MIN_PASSWORD = 8;

export class EmailTakenError extends Error {}

export async function createUser(db: Db, email: string, password: string, locale: Locale): Promise<UserRow> {
  const normalized = normalizeEmail(email);
  if (getUserByEmail(db, normalized)) throw new EmailTakenError();
  const hash = await hashPassword(password);
  try {
    return db
      .prepare('INSERT INTO users (email, password_hash, locale, created_at) VALUES (?, ?, ?, ?) RETURNING *')
      .get(normalized, hash, locale, now()) as unknown as UserRow;
  } catch (err) {
    // Two signups racing for the same address: the UNIQUE constraint decides.
    if (getUserByEmail(db, normalized)) throw new EmailTakenError();
    throw err;
  }
}

export function getUserByEmail(db: Db, email: string): UserRow | undefined {
  return db.prepare('SELECT * FROM users WHERE email = ?').get(normalizeEmail(email)) as UserRow | undefined;
}

export function getUserById(db: Db, id: number): UserRow | undefined {
  return db.prepare('SELECT * FROM users WHERE id = ?').get(id) as UserRow | undefined;
}

export async function setPassword(db: Db, userId: number, password: string): Promise<void> {
  db.prepare('UPDATE users SET password_hash = ? WHERE id = ?').run(await hashPassword(password), userId);
}

export function setLocale(db: Db, userId: number, locale: Locale): void {
  db.prepare('UPDATE users SET locale = ? WHERE id = ?').run(locale, userId);
}

export function createSession(db: Db, userId: number): string {
  const token = randomBytes(32).toString('base64url');
  const t = now();
  db.prepare('INSERT INTO sessions (id, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)').run(tokenId(token), userId, t, t + SESSION_TTL_SEC);
  return token;
}

export interface SessionLookup {
  user: UserRow;
  /** True when the session was extended and the cookie should be re-issued. */
  renewed: boolean;
}

export function getSessionUser(db: Db, token: string): SessionLookup | null {
  const id = tokenId(token);
  const t = now();
  const row = db.prepare('SELECT user_id, expires_at FROM sessions WHERE id = ? AND expires_at > ?').get(id, t) as
    | { user_id: number; expires_at: number }
    | undefined;
  if (!row) return null;
  const user = getUserById(db, row.user_id);
  if (!user) return null;
  // Sliding expiry: active users stay logged in.
  const renewed = row.expires_at - t < SESSION_TTL_SEC / 2;
  if (renewed) db.prepare('UPDATE sessions SET expires_at = ? WHERE id = ?').run(t + SESSION_TTL_SEC, id);
  return { user, renewed };
}

export function deleteSession(db: Db, token: string): void {
  db.prepare('DELETE FROM sessions WHERE id = ?').run(tokenId(token));
}

export function deleteUserSessions(db: Db, userId: number): void {
  db.prepare('DELETE FROM sessions WHERE user_id = ?').run(userId);
}

export function purgeExpired(db: Db): void {
  const t = now();
  db.prepare('DELETE FROM sessions WHERE expires_at <= ?').run(t);
  db.prepare('DELETE FROM password_resets WHERE expires_at <= ?').run(t);
}

export function createPasswordReset(db: Db, userId: number): string {
  const token = randomBytes(32).toString('base64url');
  db.prepare('INSERT INTO password_resets (id, user_id, expires_at) VALUES (?, ?, ?)').run(tokenId(token), userId, now() + RESET_TTL_SEC);
  return token;
}

export function findPasswordReset(db: Db, token: string): { userId: number } | null {
  const row = db
    .prepare('SELECT user_id FROM password_resets WHERE id = ? AND used_at IS NULL AND expires_at > ?')
    .get(tokenId(token), now()) as { user_id: number } | undefined;
  return row ? { userId: row.user_id } : null;
}

export function markPasswordResetUsed(db: Db, token: string): void {
  db.prepare('UPDATE password_resets SET used_at = ? WHERE id = ?').run(now(), tokenId(token));
}

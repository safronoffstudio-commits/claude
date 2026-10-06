// Где лежат ответы: Vercel Blob на Vercel, папка .data при локальном запуске.
//
// Каждое событие (open / yes / final) — отдельный неизменяемый файл:
//   answers/<visitorId>/<время в мс>-<событие>-<случайный хвост>.json
// Так ничего не перезаписывается, а админка собирает картину по списку файлов.

import { randomBytes } from 'node:crypto';
import { mkdir, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';

const PREFIX = 'answers/';
const PATHNAME_RE = /^answers\/([a-z0-9]{8,32})\/(\d{13})-(open|yes|final)\b/;

export function parsePathname(pathname) {
  const m = PATHNAME_RE.exec(pathname);
  return m ? { visitorId: m[1], at: Number(m[2]), event: m[3] } : null;
}

export function storageMode(env = process.env) {
  if (env.BLOB_READ_WRITE_TOKEN || env.BLOB_STORE_ID) return 'blob';
  if (env.VERCEL) return null; // на Vercel без подключённого Blob писать некуда
  return 'local';
}

export async function getStore(env = process.env) {
  const mode = storageMode(env);
  if (mode === 'blob') return blobStore(await import('@vercel/blob'));
  if (mode === 'local') return localStore(path.resolve(env.LOCAL_DATA_DIR || '.data'));
  return null;
}

export function blobStore(client) {
  // Хранилище бывает публичным или приватным; пробуем приватный режим и запоминаем тот, что сработал.
  let access = null;

  return {
    kind: 'blob',

    async add(visitorId, event, record) {
      const pathname = `${PREFIX}${visitorId}/${Date.now()}-${event}.json`;
      const body = JSON.stringify(record);
      let lastError;
      for (const mode of access ? [access] : ['private', 'public']) {
        try {
          await client.put(pathname, body, { access: mode, contentType: 'application/json', addRandomSuffix: true });
          access = mode;
          return;
        } catch (err) {
          lastError = err;
        }
      }
      throw lastError;
    },

    async list() {
      const items = [];
      let cursor;
      for (let page = 0; page < 10; page++) {
        const res = await client.list({ prefix: PREFIX, limit: 1000, cursor });
        for (const blob of res.blobs) items.push({ pathname: blob.pathname, url: blob.url });
        if (!res.hasMore || !res.cursor) break;
        cursor = res.cursor;
      }
      return items;
    },

    async read(item) {
      // По полному адресу режим доступа не важен: запрос всё равно идёт с токеном хранилища.
      const res = await client.get(item.url, { access: access || 'private' });
      if (!res || !res.stream) return null;
      return JSON.parse(await new Response(res.stream).text());
    },

    async remove(visitorId) {
      const res = await client.list({ prefix: `${PREFIX}${visitorId}/`, limit: 1000 });
      if (res.blobs.length) await client.del(res.blobs.map((blob) => blob.url));
    },
  };
}

export function localStore(root) {
  const dirOf = (visitorId) => path.join(root, PREFIX, visitorId);

  return {
    kind: 'local',

    async add(visitorId, event, record) {
      await mkdir(dirOf(visitorId), { recursive: true });
      const name = `${Date.now()}-${event}-${randomBytes(4).toString('hex')}.json`;
      await writeFile(path.join(dirOf(visitorId), name), JSON.stringify(record));
    },

    async list() {
      const base = path.join(root, PREFIX);
      const visitors = await readdir(base).catch(() => []);
      const items = [];
      for (const visitorId of visitors) {
        for (const file of await readdir(dirOf(visitorId)).catch(() => [])) {
          items.push({ pathname: `${PREFIX}${visitorId}/${file}`, file: path.join(dirOf(visitorId), file) });
        }
      }
      return items;
    },

    async read(item) {
      return JSON.parse(await readFile(item.file, 'utf8'));
    },

    async remove(visitorId) {
      await rm(dirOf(visitorId), { recursive: true, force: true });
    },
  };
}

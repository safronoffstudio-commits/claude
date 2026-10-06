// Копирует site/ в public/ и подставляет адрес сайта в превью для мессенджеров:
// Telegram и другие показывают картинку из og:image только по полному адресу.
import { cp, readFile, rm, writeFile } from 'node:fs/promises';

const host = process.env.VERCEL_PROJECT_PRODUCTION_URL || process.env.VERCEL_URL || '';
const siteUrl = host ? `https://${host}` : '';

await rm('public', { recursive: true, force: true });
await cp('site', 'public', { recursive: true });

const html = await readFile('public/index.html', 'utf8');
await writeFile('public/index.html', html.replaceAll('__SITE_URL__', siteUrl));

console.log(`public/ готова, адрес для превью: ${siteUrl || '(относительный)'}`);

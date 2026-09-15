import { ImageResponse } from '@vercel/og';
import { generateMeme } from '../lib/memes.js';
import { memeElement } from '../lib/render.js';
import { W, H } from '../lib/theme.js';

export const config = { runtime: 'edge' };

// Пути к шрифтам обязаны быть статическими строками — иначе сборщик
// Vercel не найдёт файлы и не положит их в бандл edge-функции.
const FONT_URLS = [
  new URL('../assets/fonts/tektur-display.ttf', import.meta.url),
  new URL('../assets/fonts/jbmono-regular.ttf', import.meta.url),
  new URL('../assets/fonts/jbmono-bold.ttf', import.meta.url),
];

let fontsPromise = null;
function loadFonts() {
  if (!fontsPromise) {
    fontsPromise = Promise.all(FONT_URLS.map((u) => fetch(u).then((r) => r.arrayBuffer()))).then(
      ([tektur, regular, bold]) => [
        { name: 'Tektur', data: tektur, weight: 500, style: 'normal' },
        { name: 'JetBrains Mono', data: regular, weight: 400, style: 'normal' },
        { name: 'JetBrains Mono', data: bold, weight: 700, style: 'normal' },
      ]
    );
  }
  return fontsPromise;
}

export default async function handler(req) {
  try {
    const { searchParams } = new URL(req.url);
    const seed = (searchParams.get('s') || 'demo').slice(0, 32);
    const name = (searchParams.get('n') || '').slice(0, 24) || null;
    const target = (searchParams.get('t') || '').slice(0, 64) || null;
    const brand = (searchParams.get('b') || '').slice(0, 32) || null;
    const force = (searchParams.get('f') || '').slice(0, 12) || null;

    const meme = generateMeme(seed, { name, target, force });
    const fonts = await loadFonts();

    return new ImageResponse(memeElement(meme, brand), {
      width: W,
      height: H,
      fonts,
      headers: {
        // Картинка детерминирована сидом, поэтому кэшируем навсегда.
        'cache-control': 'public, max-age=31536000, immutable',
      },
    });
  } catch (err) {
    return new Response('render error: ' + (err?.message || err), {
      status: 500,
      headers: { 'content-type': 'text/plain; charset=utf-8' },
    });
  }
}

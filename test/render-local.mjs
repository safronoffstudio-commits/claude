// Локальное превью: рендерит PNG всех шаблонов в test/out/.
// Использует satori 0.33.3 + resvg-wasm 2.4.1 — ровно те версии,
// что зашиты внутрь @vercel/og, который работает в проде.
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import satori from 'satori';
import { initWasm, Resvg } from '@resvg/resvg-wasm';
import { generateMeme } from '../lib/memes.js';
import { memeElement } from '../lib/render.js';
import { W, H } from '../lib/theme.js';

await initWasm(
  readFileSync(new URL('../node_modules/@resvg/resvg-wasm/index_bg.wasm', import.meta.url))
);

const f = (p) => readFileSync(new URL('../assets/fonts/' + p, import.meta.url));
const fonts = [
  { name: 'Tektur',         data: f('tektur-display.ttf'), weight: 500, style: 'normal' },
  { name: 'JetBrains Mono', data: f('jbmono-regular.ttf'), weight: 400, style: 'normal' },
  { name: 'JetBrains Mono', data: f('jbmono-bold.ttf'),    weight: 700, style: 'normal' },
];

mkdirSync(new URL('./out/', import.meta.url), { recursive: true });

const want = ['ladder', 'scale', 'versus', 'convert', 'top', 'cert'];
const found = {};
for (let i = 0; i < 4000 && Object.keys(found).length < want.length; i++) {
  const seed = 's' + i;
  const m = generateMeme(seed, { name: 'Санёк' });
  if (!found[m.template]) found[m.template] = { seed, m };
}

for (const [tpl, { seed, m }] of Object.entries(found)) {
  const svg = await satori(memeElement(m, '@distancer_bot'), { width: W, height: H, fonts });
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: W } }).render().asPng();
  writeFileSync(new URL(`./out/${tpl}.png`, import.meta.url), png);
  console.log(`${tpl.padEnd(9)} seed=${seed.padEnd(6)} ${(png.length / 1024).toFixed(0)}KB`);
}
console.log('\nготово:', Object.keys(found).join(', '));

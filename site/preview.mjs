// Локальный просмотр артефактной версии.
// Оборачивает index.art.html в тот же каркас, что добавляет паблишер,
// вшивает шрифты (Chromium в песочнице не доверяет CA прокси)
// и складывает PNG всех шаблонов в out/.
//   node preview.mjs
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';

const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36';
// URL шрифтов берём из самой вёрстки, чтобы превью не разъезжалось с страницей
const CSS = (readFileSync('index.art.html', 'utf8')
  .match(/href="(https:\/\/fonts\.googleapis\.com\/css2[^"]+)"/) || [])[1];
if (!CSS) throw new Error('в index.art.html не найдена ссылка на Google Fonts');

function inlinedFontCss() {
  let css = execFileSync('curl', ['-sS', '-m', '30', '-A', UA, CSS], { encoding: 'utf8' });
  const urls = [...new Set([...css.matchAll(/url\((https:\/\/fonts\.gstatic\.com[^)]+\.woff2)\)/g)].map((m) => m[1]))];
  for (const u of urls) {
    const buf = execFileSync('curl', ['-sS', '-m', '30', '-A', UA, u], { encoding: 'buffer', maxBuffer: 1 << 24 });
    css = css.split(u).join('data:font/woff2;base64,' + buf.toString('base64'));
  }
  return { css, n: urls.length };
}

const inner = readFileSync('index.art.html', 'utf8');
const { css, n } = inlinedFontCss();
console.log('шрифтов вшито:', n);

writeFileSync('preview.html', `<!doctype html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>
  :root{color-scheme:light;padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}
  body{margin:0;font:14px system-ui,sans-serif;background:#faf9f7}
  img{max-width:100%}[hidden]{display:none!important}
</style>
<style>${css}</style>
</head><body>
${inner.replace(/<link rel="preconnect"[\s\S]*?css2\?family=[^>]*>/, '')}
</body></html>`);

mkdirSync('out', { recursive: true });
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 390, height: 900 }, deviceScaleFactor: 2 });
const errs = [];
page.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
page.on('console', (m) => { if (m.type() === 'error') errs.push('console: ' + m.text()); });

await page.goto('file://' + process.cwd() + '/preview.html');
await page.waitForFunction(() => {
  const i = document.getElementById('meme');
  return i && i.src && i.src.startsWith('data:image');
}, { timeout: 20000 });
await page.waitForTimeout(800);
await page.screenshot({ path: 'out/page.png', fullPage: true });

const save = async (label) => {
  const src = await page.getAttribute('#meme', 'src');
  const ext = src.slice(11, src.indexOf(';'));
  writeFileSync(`out/${label}.${ext}`, Buffer.from(src.split(',')[1], 'base64'));
  return (await page.getAttribute('#meme', 'alt')).replace('Мем в стиле старой аватарки. Подпись: ', '');
};

for (let i = 1; i <= 4; i++) {
  await page.waitForTimeout(260);
  console.log('кадр ' + i + ':', await save('shot' + i));
  await page.click('#go');
}

const of = await page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth);
console.log('\nгориз. скролл:', of ? 'ЕСТЬ' : 'нет', '| ошибки:', errs.length ? errs.join('; ') : 'нет');
await browser.close();

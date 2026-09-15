import { writeFileSync } from 'node:fs';
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
const b = await chromium.launch();
const p = await b.newPage({ viewport: { width: 390, height: 900 }, deviceScaleFactor: 2 });
const errs = [];
p.on('pageerror', (e) => errs.push(e.message));
await p.goto('file://' + process.cwd() + '/preview.html');
await p.waitForFunction(() => {
  const i = document.getElementById('meme');
  return i && i.src && i.src.startsWith('data:image');
}, { timeout: 20000 });

await p.setInputFiles('#photo', process.argv[2]);
await p.waitForTimeout(1500);

const src = await p.getAttribute('#meme', 'src');
writeFileSync('out/photo.jpeg', Buffer.from(src.split(',')[1], 'base64'));
console.log('подпись:', (await p.getAttribute('#meme', 'alt')).replace('Мем в стиле старой аватарки. Подпись: ', ''));
console.log('кнопка стала:', await p.textContent('#photoBtn'));
console.log('«убрать фото» видна:', !(await p.getAttribute('#clear', 'hidden') !== null));

await p.click('#clear');
await p.waitForTimeout(600);
console.log('после сброса кнопка:', await p.textContent('#photoBtn'));
console.log('ошибки:', errs.length ? errs.join('; ') : 'нет');
await b.close();

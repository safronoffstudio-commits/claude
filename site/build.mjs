// Собирает страницу из частей.
//   index.art.html — для публикации артефактом (паблишер сам оборачивает в каркас)
//   index.html     — самостоятельная страница для любого статик-хостинга
import { readFileSync, writeFileSync } from 'node:fs';

const head = readFileSync('head.part.html', 'utf8').trim();
const body = readFileSync('body.part.html', 'utf8').trim();
const js = ['memes.part.js', 'script.part.js']
  .map((f) => readFileSync(f, 'utf8').trim())
  .join('\n\n');

const inner = head + '\n\n' + body + '\n\n<script>\n' + js + '\n</script>\n';
writeFileSync('index.art.html', inner);

const standalone = [
  '<!doctype html>',
  '<html lang="ru">',
  '<head>',
  '<meta charset="utf-8">',
  '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">',
  '<meta name="description" content="Генератор мемов про дагестанское гостеприимство, хинкал, борьбу и родню.">',
  '<style>',
  '  :root{color-scheme:light dark;padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}',
  '  img{max-width:100%}[hidden]{display:none!important}',
  '</style>',
  head,
  '</head>',
  '<body>',
  body,
  '<script>',
  js,
  '</script>',
  '</body>',
  '</html>',
  '',
].join('\n');
writeFileSync('index.html', standalone);

console.log('index.art.html:', inner.length, 'байт');
console.log('index.html    :', standalone.length, 'байт');

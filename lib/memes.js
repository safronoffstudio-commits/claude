import { makeRng } from './rng.js';
import {
  COSMIC, EARTH, REAL, HOPELESS, LADDER_PUNCH, LADDER_TITLE,
  GOALS, UNITS, STAMPS, FOOTER,
} from './data.js';

// Разряды пробелами вручную: в edge-рантайме Intl может быть без русской локали,
// а «57,143» вместо «57 143» смотрелось бы криво.
function group(n) {
  return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
}

// Формат без экспоненты: маленькие числа смешнее, когда видно все нули.
function fmt(x) {
  if (!isFinite(x)) return '∞';
  if (x === 0) return '0';
  const abs = Math.abs(x);
  if (abs >= 1000) return group(Math.round(x));
  if (abs >= 10) return x.toFixed(1).replace('.0', '');
  if (abs >= 1) return x.toFixed(2).replace(/0$/, '').replace(/\.$/, '');
  // < 1: показываем два значащих знака обычной десятичной записью
  const places = Math.min(18, Math.ceil(-Math.log10(abs)) + 1);
  return x.toFixed(places).replace(/0+$/, '');
}

function cleanTarget(raw) {
  if (!raw) return null;
  const t = String(raw).replace(/\s+/g, ' ').trim().replace(/^[\/@]\S*\s*/, '');
  if (!t) return null;
  return t.length > 42 ? t.slice(0, 41).trimEnd() + '…' : t;
}

// --- Шаблон 1: лестница достижений ---
function ladder(rng, ctx) {
  const reals = rng.sample(REAL, 3).sort((a, b) => b.meters - a.meters);
  const fail = ctx.custom || rng.pick(HOPELESS);
  return {
    kind: 'ladder',
    title: rng.pick(LADDER_TITLE),
    rows: [
      ...reals.map((r) => ({ label: r.label, value: r.display, ok: true })),
      { label: fail.label, value: fail.display, ok: false },
    ],
    punch: rng.pick(LADDER_PUNCH),
    stamp: rng.pick(STAMPS),
  };
}

// --- Шаблон 2: шкала прогресса ---
function scale(rng, ctx) {
  const g = ctx.custom
    ? { goal: ctx.custom.label.replace(/^Ты → /, '').toUpperCase(), done: 'подумал об этом', left: 'всё' }
    : rng.pick(GOALS);
  const pct = rng.int(0, 6);
  return {
    kind: 'scale',
    title: 'ДИСТАНЦИЯ ДО ЦЕЛИ',
    subtitle: g.goal,
    bar: { pct },
    rows: [
      { label: 'Пройдено', value: g.done, ok: true },
      { label: 'Осталось', value: g.left, ok: false },
    ],
    punch: pct === 0 ? 'Старт не зафиксирован.' : 'Темп стабильный.',
    stamp: rng.pick(STAMPS),
  };
}

// --- Шаблон 3: что дальше? ---
function versus(rng, ctx) {
  const a = rng.pick(COSMIC);
  const b = ctx.custom || rng.pick(HOPELESS.filter((h) => h.meters === Infinity));
  return {
    kind: 'versus',
    title: 'ЧТО ДАЛЬШЕ?',
    left: { label: a.label, value: a.display },
    right: { label: b.label, value: b.display },
    punch: 'Правильный ответ — справа.',
    stamp: rng.pick(STAMPS),
  };
}

// --- Шаблон 4: абсурдная конвертация ---
function convert(rng, ctx) {
  const pool = HOPELESS.filter((h) => h.meters > 0 && isFinite(h.meters));
  const item = (ctx.custom && isFinite(ctx.custom.meters) && ctx.custom.meters > 0)
    ? ctx.custom
    : rng.pick(pool);
  const units = rng.sample(UNITS, 3);
  return {
    kind: 'convert',
    title: 'ПЕРЕВОД ЕДИНИЦ',
    subtitle: item.label,
    big: item.display,
    rows: units.map((u) => ({
      label: u.name,
      value: fmt(item.meters * u.perMeter),
      ok: true,
    })),
    punch: 'И ты всё равно не встал.',
    stamp: rng.pick(STAMPS),
  };
}

// --- Шаблон 5: топ-5 ---
function top(rng, ctx) {
  const picked = ctx.custom
    ? [ctx.custom, ...rng.sample(HOPELESS, 4)]
    : rng.sample(HOPELESS, 5);
  return {
    kind: 'top',
    title: 'ТОП НЕПРЕОДОЛИМЫХ',
    subtitle: 'пять дистанций этой недели',
    rows: picked.map((p, i) => ({ label: p.label, value: p.display, ok: false, rank: i + 1 })),
    punch: 'Список обновляется ежедневно.',
    stamp: rng.pick(STAMPS),
  };
}

// --- Шаблон 6: официальная справка ---
function cert(rng, ctx) {
  const feat = ctx.custom || rng.pick(HOPELESS.filter((h) => isFinite(h.meters) && h.meters > 0));
  const minutes = rng.int(12, 240);
  const kmh = (feat.meters / 1000) / (minutes / 60);
  return {
    kind: 'cert',
    title: 'СПРАВКА',
    subtitle: 'о преодолении дистанции',
    name: ctx.name || 'Аноним',
    rows: [
      { label: 'Дистанция', value: feat.label, ok: true },
      { label: 'Длина', value: feat.display, ok: true },
      { label: 'Время в пути', value: minutes + ' мин', ok: true },
      { label: 'Средняя скорость', value: fmt(kmh) + ' км/ч', ok: false },
    ],
    punch: 'Документ силы не имеет.',
    stamp: 'ЗАЧТЕНО',
  };
}

const TEMPLATES = [ladder, scale, versus, convert, top, cert];
const NAMES = ['ladder', 'scale', 'versus', 'convert', 'top', 'cert'];
const BY_NAME = { ladder, scale, versus, convert, top, cert };

export function generateMeme(seed, opts = {}) {
  const rng = makeRng(seed);
  const target = cleanTarget(opts.target);
  const ctx = {
    name: opts.name ? String(opts.name).slice(0, 24) : null,
    custom: target
      ? { label: 'Ты → ' + target, display: '∞', meters: Infinity }
      : null,
  };

  // Если юзер прислал свой текст, шаблоны без «бесконечности» смотрятся хуже
  let pool = TEMPLATES;
  if (ctx.custom) pool = [ladder, versus, top, scale];
  // Справка работает только когда знаем имя
  if (!ctx.name) pool = pool.filter((t) => t !== cert);

  // force позволяет командам вроде /top и /spravka попросить конкретный шаблон
  const forced = opts.force && BY_NAME[opts.force] ? BY_NAME[opts.force] : null;
  const tpl = forced || rng.pick(pool);
  const meme = tpl(rng, ctx);
  meme.footer = FOOTER;
  meme.template = NAMES[TEMPLATES.indexOf(tpl)];
  meme.seed = String(seed);
  return meme;
}

// Текстовая версия — подпись под картинкой и запасной вариант,
// если Telegram не смог забрать изображение.
export function memeToText(m) {
  const L = [];
  L.push('*' + m.title + '*');
  if (m.subtitle) L.push('_' + m.subtitle + '_');
  L.push('');
  if (m.kind === 'versus') {
    L.push(m.left.label + ' — ' + m.left.value);
    L.push(m.right.label + ' — ' + m.right.value);
  } else if (m.kind === 'convert') {
    L.push(m.big);
    m.rows.forEach((r) => L.push('= ' + r.value + ' ' + r.label));
  } else if (m.kind === 'cert') {
    L.push('Выдана: ' + m.name);
    m.rows.forEach((r) => L.push(r.label + ': ' + r.value));
  } else if (m.kind === 'scale') {
    const filled = Math.max(1, Math.round(m.bar.pct / 5));
    L.push('[' + '#'.repeat(filled) + '.'.repeat(20 - filled) + '] ' + m.bar.pct + '%');
    m.rows.forEach((r) => L.push(r.label + ': ' + r.value));
  } else {
    m.rows.forEach((r) => L.push((r.rank ? r.rank + '. ' : '') + r.label + ' — ' + r.value));
  }
  L.push('');
  L.push(m.punch);
  return L.join('\n');
}

export { fmt, cleanTarget };

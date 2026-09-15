import React from 'react';
import { C, W, H, DISPLAY, MONO } from './theme.js';

const el = React.createElement;

const box = (style, ...kids) =>
  el('div', { style: { display: 'flex', ...style } }, ...kids.filter(Boolean));

const txt = (style, content) =>
  el('div', { style: { display: 'flex', ...style } }, content);

const dot = (ok) =>
  box({
    width: 14, height: 14, borderRadius: 14, marginRight: 22,
    backgroundColor: ok ? C.ok : C.bad,
  });

function stamp(m) {
  if (!m.stamp) return null;
  const good = m.stamp === 'ЗАЧТЕНО';
  const col = good ? C.ok : C.bad;
  return box(
    {
      transform: 'rotate(-9deg)',
      border: '4px solid ' + col,
      borderRadius: 10,
      paddingTop: 7, paddingBottom: 9, paddingLeft: 18, paddingRight: 18,
      marginLeft: 24,
    },
    txt({ fontFamily: DISPLAY, fontSize: 28, color: col, letterSpacing: 2 }, m.stamp)
  );
}

function header(m) {
  return box(
    { flexDirection: 'column', width: '100%' },
    box(
      { width: '100%', alignItems: 'flex-start', justifyContent: 'space-between' },
      box(
        { flexDirection: 'column', flex: 1, paddingRight: 16 },
        txt({ fontFamily: DISPLAY, fontSize: 54, lineHeight: 1.06, letterSpacing: 1 }, m.title),
        m.subtitle
          ? txt({ fontSize: 26, color: C.dim, marginTop: 12, lineHeight: 1.25 }, m.subtitle)
          : null
      ),
      stamp(m)
    ),
    box({ width: '100%', height: 2, backgroundColor: C.line, marginTop: 26 })
  );
}

function row(r, i, total, compact) {
  return box(
    {
      key: 'r' + i,
      width: '100%', alignItems: 'center',
      paddingTop: compact ? 17 : 26,
      paddingBottom: compact ? 17 : 26,
      borderBottom: i < total - 1 ? '1px solid ' + C.line : 'none',
    },
    r.rank
      ? txt({ fontSize: 28, fontWeight: 700, color: C.dim, width: 46 }, String(r.rank))
      : dot(r.ok),
    txt({ fontSize: compact ? 27 : 30, flex: 1, lineHeight: 1.2, paddingRight: 18 }, r.label),
    txt(
      { fontSize: compact ? 28 : 32, fontWeight: 700, color: r.ok ? C.ok : C.bad },
      r.value
    )
  );
}

const rowsBlock = (m, compact) =>
  box(
    { flexDirection: 'column', flex: 1, width: '100%', justifyContent: 'center' },
    ...m.rows.map((r, i) => row(r, i, m.rows.length, compact))
  );

function scaleBlock(m) {
  const pct = m.bar.pct;
  return box(
    { flexDirection: 'column', flex: 1, width: '100%', justifyContent: 'center' },
    txt({ fontSize: 96, fontWeight: 700, color: C.bad, lineHeight: 1 }, pct + '%'),
    box(
      {
        width: '100%', height: 44, marginTop: 20,
        backgroundColor: '#161C2A', borderRadius: 10,
        border: '1px solid ' + C.line, padding: 6,
      },
      box({
        width: Math.max(pct, 1) + '%', height: '100%',
        backgroundColor: C.bad, borderRadius: 6,
      })
    ),
    box(
      { flexDirection: 'column', width: '100%', marginTop: 26 },
      ...m.rows.map((r, i) => row(r, i, m.rows.length, true))
    )
  );
}

const panel = (p, color) =>
  box(
    {
      flex: 1, flexDirection: 'column', height: 300,
      justifyContent: 'center', alignItems: 'center',
      backgroundColor: C.panel, borderRadius: 18,
      border: '1px solid ' + C.line,
      paddingLeft: 26, paddingRight: 26,
    },
    txt(
      { fontSize: 25, color: C.dim, textAlign: 'center', lineHeight: 1.35, marginBottom: 20 },
      p.label
    ),
    txt({ fontSize: 44, fontWeight: 700, color, textAlign: 'center', lineHeight: 1.1 }, p.value)
  );

const versusBlock = (m) =>
  box(
    { flex: 1, width: '100%', alignItems: 'center' },
    panel(m.left, C.ok),
    txt({ fontFamily: DISPLAY, fontSize: 40, color: C.dim, paddingLeft: 26, paddingRight: 26 }, 'VS'),
    panel(m.right, C.bad)
  );

const convertBlock = (m) =>
  box(
    { flexDirection: 'column', flex: 1, width: '100%', justifyContent: 'center' },
    txt({ fontSize: 86, fontWeight: 700, color: C.accent, lineHeight: 1 }, m.big),
    ...m.rows.map((r, i) =>
      box(
        {
          width: '100%', alignItems: 'center',
          paddingTop: 15, paddingBottom: 15,
          borderTop: '1px solid ' + C.line,
          marginTop: i === 0 ? 28 : 0,
        },
        txt({ fontSize: 32, color: C.dim, marginRight: 16 }, '='),
        txt({ fontSize: 32, fontWeight: 700, marginRight: 14 }, r.value),
        txt({ fontSize: 27, color: C.dim, flex: 1, lineHeight: 1.2 }, r.label)
      )
    )
  );

const certBlock = (m) =>
  box(
    { flexDirection: 'column', flex: 1, width: '100%', justifyContent: 'center' },
    txt({ fontSize: 23, color: C.dim }, 'Выдана'),
    txt({ fontFamily: DISPLAY, fontSize: 62, color: C.accent, lineHeight: 1.1, marginBottom: 22 }, m.name),
    ...m.rows.map((r, i) => row(r, i, m.rows.length, true))
  );

function body(m) {
  switch (m.kind) {
    case 'scale':   return scaleBlock(m);
    case 'versus':  return versusBlock(m);
    case 'convert': return convertBlock(m);
    case 'cert':    return certBlock(m);
    case 'top':     return rowsBlock(m, true);
    default:        return rowsBlock(m, false);
  }
}

const footer = (m, brand) =>
  box(
    { width: '100%', alignItems: 'flex-end', justifyContent: 'space-between', marginTop: 22 },
    txt(
      { fontFamily: DISPLAY, fontSize: 34, color: C.accent, lineHeight: 1.2, flex: 1, paddingRight: 24 },
      m.punch
    ),
    txt({ fontSize: 18, color: C.dim, paddingBottom: 4 }, brand || m.footer)
  );

export function memeElement(m, brand) {
  return box(
    {
      width: W, height: H, flexDirection: 'column',
      backgroundColor: C.bg,
      backgroundImage:
        'radial-gradient(900px 460px at 82% -12%, rgba(92,240,138,0.11), transparent 62%),' +
        'radial-gradient(760px 420px at -5% 112%, rgba(255,92,92,0.10), transparent 62%)',
      paddingTop: 50, paddingBottom: 44, paddingLeft: 58, paddingRight: 58,
      fontFamily: MONO,
      color: C.text,
    },
    header(m),
    body(m),
    footer(m, brand)
  );
}

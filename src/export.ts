import type { Reel, ReelsOutput } from './ai/schema.js';
import type { Dict } from './i18n.js';
import { reelDuration } from './lints.js';
import { formatTimestamp } from './transcript.js';

function cell(text: string): string {
  return text.replace(/\|/g, '\\|').replace(/\s*\n\s*/g, ' ');
}

/** One Reel in the plain-text layout of the framework's script template (section 5). */
export function reelToText(reel: Reel, index: number, t: Dict): string {
  const lines: string[] = [`${t.gen.reel(index + 1)}: ${reel.title}`, `${t.gen.cover}: ${reel.cover_text}`, ''];
  for (const block of reel.blocks) {
    const label = t.gen.blocks[block.kind].toUpperCase().padEnd(14);
    const time = `${formatTimestamp(block.start_sec)}–${formatTimestamp(block.end_sec)}`;
    const source = block.source_timecode ? `   (${t.gen.cols.source}: ${block.source_timecode})` : '';
    lines.push(`${label} ${time}${source}`);
    lines.push(`${t.gen.cols.voice}: ${block.voice}`);
    if (block.on_screen_text) lines.push(`${t.gen.cols.onScreen}: ${block.on_screen_text}`);
    if (block.visual) lines.push(`${t.gen.cols.visual}: ${block.visual}`);
    lines.push('');
  }
  if (reel.alternative_hooks.length) {
    lines.push(`${t.gen.altHooks}:`, ...reel.alternative_hooks.map((h) => `- ${h}`), '');
  }
  if (reel.editing_notes.length) {
    lines.push(`${t.gen.editing}:`, ...reel.editing_notes.map((n) => `- ${n}`), '');
  }
  if (reel.loop_note) lines.push(`${t.gen.loop}: ${reel.loop_note}`, '');
  lines.push(`${t.gen.caption}:`, reel.caption, '', reel.hashtags.join(' '));
  return lines.join('\n').trim();
}

export function generationToMarkdown(title: string, output: ReelsOutput, t: Dict): string {
  const md: string[] = [`# ${title}`, ''];
  if (output.source_summary) md.push(`> ${output.source_summary}`, '');
  if (output.notes) md.push(`**${t.gen.notes}:** ${output.notes}`, '');

  if (output.moments.length) {
    const c = t.gen.momentsCols;
    md.push(`## ${t.gen.momentsTitle}`, '', `| ${c.time} | ${c.what} | ${c.type} | ${c.fit} | ${c.why} |`, '|---|---|---|---|---|');
    for (const m of output.moments) {
      md.push(
        `| ${cell(m.timecode)} | ${cell(m.what_happens)} | ${t.gen.momentTypes[m.type]} | ${m.use_in_reels ? t.gen.yes : t.gen.no} | ${cell(m.reason)} |`,
      );
    }
    md.push('');
  }

  if (output.on_screen_quotes.length) {
    md.push(`## ${t.gen.quotesTitle}`, '', ...output.on_screen_quotes.map((q) => `- «${q}»`), '');
  }

  output.reels.forEach((reel, i) => {
    md.push(`## ${t.gen.reel(i + 1)}: ${reel.title}`, '');
    md.push(`${t.gen.hookTypes[reel.hook_type]} · ${t.gen.seconds(reelDuration(reel))} · ${t.gen.cover}: **${reel.cover_text}**`, '');
    md.push('```', reelToText(reel, i, t), '```', '');
  });
  return md.join('\n');
}

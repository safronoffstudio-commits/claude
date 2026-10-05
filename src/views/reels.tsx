import type { FC } from 'hono/jsx';
import type { Reel, ReelsOutput } from '../ai/schema.js';
import { reelToText } from '../export.js';
import type { Dict } from '../i18n.js';
import { lintReel, reelDuration, type CheckResult } from '../lints.js';
import { formatTimestamp } from '../transcript.js';

function checkLabel(t: Dict, check: CheckResult): string {
  switch (check.id) {
    case 'duration':
      return t.gen.lint.duration(check.value ?? 0);
    case 'hookWords':
      return t.gen.lint.hookWords(check.value ?? 0);
    case 'bodyBeats':
      return t.gen.lint.bodyBeats(check.value ?? 0);
    default:
      return t.gen.lint[check.id];
  }
}

export const Checklist: FC<{ t: Dict; reel: Reel }> = ({ t, reel }) => {
  const checks = lintReel(reel);
  const passed = checks.filter((c) => c.ok).length;
  return (
    <details class="checks">
      <summary>
        {t.gen.checks}{' '}
        <span class={`badge ${passed === checks.length ? 'badge--ok' : 'badge--warn'}`}>
          {passed}/{checks.length}
        </span>
      </summary>
      <ul>
        {checks.map((check) => (
          <li class={check.ok ? 'check check--ok' : 'check check--fail'}>
            <span aria-hidden="true">{check.ok ? '✓' : '!'}</span> {checkLabel(t, check)}
          </li>
        ))}
      </ul>
    </details>
  );
};

export const ReelCard: FC<{ t: Dict; reel: Reel; index: number; compact?: boolean }> = ({ t, reel, index, compact }) => {
  const textId = `reel-text-${index + 1}`;
  return (
    <article class="reel card" id={`reel-${index + 1}`}>
      <header class="reel__head">
        <div>
          <p class="eyebrow">{t.gen.reel(index + 1)}</p>
          <h3 class="reel__title">{reel.title}</h3>
        </div>
        <div class="reel__badges">
          <span class="badge">{t.gen.seconds(reelDuration(reel))}</span>
          <span class="badge badge--accent">{t.gen.hookTypes[reel.hook_type]}</span>
        </div>
      </header>

      <p class="reel__cover">
        <span class="muted">{t.gen.cover}:</span> <strong>{reel.cover_text}</strong>
      </p>

      <ol class="timeline">
        {reel.blocks.map((block) => (
          <li class={`beat beat--${block.kind}`}>
            <div class="beat__meta">
              <span class="beat__kind">{t.gen.blocks[block.kind]}</span>
              <span class="beat__time">
                {formatTimestamp(block.start_sec)}–{formatTimestamp(block.end_sec)}
              </span>
            </div>
            <div class="beat__body">
              <p class="beat__voice">{block.voice}</p>
              {block.on_screen_text && (
                <p class="beat__screen">
                  <span class="muted">{t.gen.cols.onScreen}:</span> {block.on_screen_text}
                </p>
              )}
              {!compact && block.visual && (
                <p class="beat__visual">
                  <span class="muted">{t.gen.cols.visual}:</span> {block.visual}
                </p>
              )}
              {block.source_timecode && (
                <p class="beat__source">
                  <span class="muted">{t.gen.cols.source}:</span> <code>{block.source_timecode}</code>
                </p>
              )}
            </div>
          </li>
        ))}
      </ol>

      {!compact && (
        <>
          <div class="reel__extras">
            {reel.alternative_hooks.length > 0 && (
              <section>
                <h4>{t.gen.altHooks}</h4>
                <ul>
                  {reel.alternative_hooks.map((hook) => (
                    <li>{hook}</li>
                  ))}
                </ul>
              </section>
            )}
            {reel.editing_notes.length > 0 && (
              <section>
                <h4>{t.gen.editing}</h4>
                <ul>
                  {reel.editing_notes.map((note) => (
                    <li>{note}</li>
                  ))}
                </ul>
              </section>
            )}
            <section>
              <h4>{t.gen.caption}</h4>
              <p class="prewrap">{reel.caption}</p>
              <p class="hashtags">{reel.hashtags.join(' ')}</p>
            </section>
            {reel.loop_note && (
              <section>
                <h4>{t.gen.loop}</h4>
                <p>{reel.loop_note}</p>
              </section>
            )}
          </div>
          <Checklist t={t} reel={reel} />
          <div class="reel__actions">
            <button type="button" class="btn btn--secondary btn--sm" data-copy={`#${textId}`} data-copied-label={t.gen.copied}>
              {t.gen.copy}
            </button>
          </div>
          <pre id={textId} hidden>
            {reelToText(reel, index, t)}
          </pre>
        </>
      )}
    </article>
  );
};

export const ResultView: FC<{ t: Dict; output: ReelsOutput }> = ({ t, output }) => (
  <div class="result">
    {(output.source_summary || output.notes) && (
      <section class="card result__summary">
        {output.source_summary && (
          <p>
            <strong>{t.gen.summary}:</strong> {output.source_summary}
          </p>
        )}
        {output.notes && (
          <p class="muted">
            <strong>{t.gen.notes}:</strong> {output.notes}
          </p>
        )}
      </section>
    )}

    <div class="reels">
      {output.reels.map((reel, i) => (
        <ReelCard t={t} reel={reel} index={i} />
      ))}
    </div>

    {output.moments.length > 0 && (
      <details class="card result__moments">
        <summary>
          <h2 class="h3">{t.gen.momentsTitle}</h2>
        </summary>
        <div class="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t.gen.momentsCols.time}</th>
                <th>{t.gen.momentsCols.what}</th>
                <th>{t.gen.momentsCols.type}</th>
                <th>{t.gen.momentsCols.fit}</th>
                <th>{t.gen.momentsCols.why}</th>
              </tr>
            </thead>
            <tbody>
              {output.moments.map((m) => (
                <tr>
                  <td>
                    <code>{m.timecode}</code>
                  </td>
                  <td>{m.what_happens}</td>
                  <td>{t.gen.momentTypes[m.type]}</td>
                  <td>{m.use_in_reels ? '✓' : '—'}</td>
                  <td class="muted">{m.reason}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </details>
    )}

    {output.on_screen_quotes.length > 0 && (
      <section class="card">
        <h2 class="h3">{t.gen.quotesTitle}</h2>
        <ul class="quotes">
          {output.on_screen_quotes.map((q) => (
            <li>«{q}»</li>
          ))}
        </ul>
      </section>
    )}
  </div>
);

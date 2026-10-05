import type { FC } from 'hono/jsx';
import type { GenerationErrorCode } from '../ai/generator.js';
import type { ReelsOutput } from '../ai/schema.js';
import type { GenerationRow } from '../db.js';
import type { StoredOptions } from '../generations.js';
import { formatDate } from '../i18n.js';
import { Alert, Layout, type PageContext } from './layout.js';
import { ResultView } from './reels.js';

export interface GenerationPageProps {
  ctx: PageContext;
  row: GenerationRow;
  options: StoredOptions;
  output: ReelsOutput | null;
  canRetry: boolean;
}

export const GenerationPage: FC<GenerationPageProps> = ({ ctx, row, options, output, canRetry }) => {
  const { t, locale } = ctx;
  const shareUrl = row.share_id ? `${ctx.appUrl}/s/${row.share_id}` : null;
  const errorCode = (row.error ?? 'unknown') as GenerationErrorCode;
  const pending = row.status === 'pending';

  return (
    <Layout
      ctx={ctx}
      title={row.title}
      noindex
      head={
        pending ? (
          <noscript>
            <meta http-equiv="refresh" content="5" />
          </noscript>
        ) : undefined
      }
    >
      <div class="container">
        <div class="gen-head">
          <a href="/app" class="back-link">
            ← {t.gen.back}
          </a>
          <h1 class="h2">{row.title}</h1>
          <p class="muted small">
            {formatDate(locale, row.created_at)} · {t.app.reelsCount(options.count)} · {t.app.languages[options.language]} ·{' '}
            {t.app.platforms[options.platform]}
          </p>

          {row.status === 'done' && (
            <div class="gen-actions">
              <a class="btn btn--secondary btn--sm" href={`/app/g/${row.id}/export.md`}>
                {t.gen.exportMd}
              </a>
              <form method="post" action={`/app/g/${row.id}/share`} class="inline-form">
                <input type="hidden" name="enabled" value={shareUrl ? '0' : '1'} />
                <button type="submit" class="btn btn--secondary btn--sm">
                  {shareUrl ? t.gen.unshare : t.gen.share}
                </button>
              </form>
              <a class="btn btn--secondary btn--sm" href="/app">
                {t.gen.newOne}
              </a>
              <form method="post" action={`/app/g/${row.id}/delete`} class="inline-form" data-confirm={t.gen.deleteConfirm}>
                <button type="submit" class="btn btn--ghost-danger btn--sm">
                  {t.gen.delete}
                </button>
              </form>
            </div>
          )}

          {shareUrl && (
            <div class="share-box">
              <input type="text" readonly value={shareUrl} aria-label={t.gen.share} data-select-all />
              <button type="button" class="btn btn--secondary btn--sm" data-copy-value={shareUrl} data-copied-label={t.gen.copied}>
                {t.gen.copyLink}
              </button>
              <small class="muted">{t.gen.shareHint}</small>
            </div>
          )}
        </div>

        {pending && (
          <div class="card pending" data-poll={`/app/g/${row.id}/status`}>
            <div class="spinner" aria-hidden="true"></div>
            <h2 class="h3">{t.gen.pendingTitle}</h2>
            <ol class="pending__steps" data-steps>
              {t.gen.pendingSteps.map((step, i) => (
                <li class={i === 0 ? 'is-active' : ''}>{step}</li>
              ))}
            </ol>
            <p class="muted small">{t.gen.pendingText}</p>
          </div>
        )}

        {row.status === 'failed' && (
          <div class="card failed">
            <h2 class="h3">{t.gen.failedTitle}</h2>
            <p>{t.gen.errorCodes[errorCode] ?? t.gen.errorCodes.unknown}</p>
            <p class="muted small">{t.gen.failedText}</p>
            <div class="gen-actions">
              {canRetry ? (
                <form method="post" action={`/app/g/${row.id}/retry`} class="inline-form">
                  <button type="submit" class="btn btn--primary">
                    {t.gen.retry}
                  </button>
                </form>
              ) : (
                <a href="/pricing" class="btn btn--primary">
                  {t.app.upgrade}
                </a>
              )}
              <form method="post" action={`/app/g/${row.id}/delete`} class="inline-form" data-confirm={t.gen.deleteConfirm}>
                <button type="submit" class="btn btn--ghost-danger">
                  {t.gen.delete}
                </button>
              </form>
            </div>
          </div>
        )}

        {output && (
          <>
            {row.model === 'mock' && <Alert kind="warning">{t.gen.demo}</Alert>}
            <ResultView t={t} output={output} />
          </>
        )}
      </div>
    </Layout>
  );
};

export const SharedPage: FC<{ ctx: PageContext; row: GenerationRow; output: ReelsOutput; whiteLabel: boolean }> = ({
  ctx,
  row,
  output,
  whiteLabel,
}) => {
  const { t } = ctx;
  return (
    <Layout ctx={ctx} title={row.title} description={output.source_summary} noindex bare={whiteLabel}>
      <div class="container">
        {!whiteLabel && (
          <div class="made-with card">
            <span>{t.gen.madeWith}</span>
            <a href="/signup" class="btn btn--primary btn--sm">
              {t.gen.tryFree}
            </a>
          </div>
        )}
        <div class="gen-head">
          <h1 class="h2">{row.title}</h1>
        </div>
        <ResultView t={t} output={output} />
      </div>
    </Layout>
  );
};

import type { FC } from 'hono/jsx';
import type { GenerationListItem } from '../generations.js';
import { CTA_GOALS, formatDate, formatLimitChars, OUTPUT_LANGUAGES, PLATFORMS, TONES } from '../i18n.js';
import { CHARS_PER_CREDIT, PLANS, type Plan } from '../plans.js';
import type { Usage } from '../usage.js';
import { Alert, Layout, type PageContext } from './layout.js';

export interface DashboardProps {
  ctx: PageContext;
  usage: Usage;
  generations: GenerationListItem[];
  error?: string;
  /** Previously submitted values, re-filled after a validation error. */
  form?: Record<string, string>;
}

/** The cheapest plan that allows more Reels per generation than the current one. */
function upgradeFor(plan: Plan): Plan | undefined {
  return Object.values(PLANS).find((p) => p.price > plan.price && p.maxReels > plan.maxReels);
}

const UsageBar: FC<{ ctx: PageContext; usage: Usage }> = ({ ctx, usage }) => {
  const pct = Math.min(100, Math.round((usage.used / Math.max(1, usage.limit)) * 100));
  return (
    <div class="usage card">
      <div class="usage__row">
        <span>
          <strong>{ctx.t.pricing.planNames[usage.plan.id]}</strong> · {ctx.t.app.usage(usage.used, usage.limit)}
        </span>
        {usage.plan.id !== 'agency' && (
          <a href="/pricing" class="btn btn--secondary btn--sm">
            {ctx.t.app.upgrade}
          </a>
        )}
      </div>
      <div class="meter" role="progressbar" aria-valuemin={0} aria-valuemax={usage.limit} aria-valuenow={usage.used}>
        <span style={`width:${pct}%`} class={pct >= 100 ? 'meter--full' : ''}></span>
      </div>
    </div>
  );
};

export const DashboardPage: FC<DashboardProps> = ({ ctx, usage, generations, error, form = {} }) => {
  const { t, locale } = ctx;
  const plan = usage.plan;
  const upgrade = upgradeFor(plan);
  const selectedCount = Number(form.count ?? Math.min(3, plan.maxReels));
  const select = (name: string, values: readonly string[], labels: Record<string, string>, fallback: string) => (
    <select name={name} id={`f-${name}`} data-remember>
      {values.map((value) => (
        <option value={value} selected={(form[name] ?? fallback) === value}>
          {labels[value]}
        </option>
      ))}
    </select>
  );

  return (
    <Layout ctx={ctx} title={t.nav.dashboard} noindex>
      <div class="container app-grid">
        <section class="app-main">
          <UsageBar ctx={ctx} usage={usage} />
          {error && <Alert kind="error">{error}</Alert>}

          <form method="post" action="/app/generations" enctype="multipart/form-data" class="card form generate-form" id="generate-form">
            <h1 class="h2">{t.app.newTitle}</h1>

            <label class="field">
              <span>{t.app.titleLabel}</span>
              <input type="text" name="title" maxlength={120} placeholder={t.app.titlePlaceholder} value={form.title ?? ''} />
            </label>

            <div class="field">
              <div class="field__label-row">
                <label for="f-transcript">{t.app.transcriptLabel}</label>
                <label class="btn btn--secondary btn--sm file-button">
                  {t.app.upload}
                  <input type="file" accept=".srt,.vtt,.txt,.sbv,text/plain,text/vtt" data-transcript-file hidden />
                </label>
              </div>
              <textarea
                id="f-transcript"
                name="transcript"
                rows={14}
                required
                placeholder={t.app.transcriptPlaceholder}
                data-transcript
                data-limit={plan.maxChars}
                data-limit-label={formatLimitChars(locale, plan.maxChars)}
                data-chars-per-credit={CHARS_PER_CREDIT}
                data-locale={locale}
              >
                {form.transcript ?? ''}
              </textarea>
              <small class="muted" data-transcript-stats data-template={t.app.charCount('{chars}', '{mins}', '{credits}', '{limit}')}></small>
            </div>

            <div class="form-row">
              <label class="field">
                <span>{t.app.countLabel}</span>
                <select name="count" data-remember>
                  {[1, 2, 3, 4, 5].map((n) => (
                    <option value={String(n)} selected={n === selectedCount} disabled={n > plan.maxReels}>
                      {n > plan.maxReels && upgrade ? `${n} — ${t.app.planLocked(upgrade.name)}` : String(n)}
                    </option>
                  ))}
                </select>
              </label>
              <label class="field">
                <span>{t.app.languageLabel}</span>
                {select('language', OUTPUT_LANGUAGES, t.app.languages, 'auto')}
              </label>
              <label class="field">
                <span>{t.app.platformLabel}</span>
                {select('platform', PLATFORMS, t.app.platforms, 'reels')}
              </label>
            </div>

            <div class="form-row">
              <label class="field">
                <span>{t.app.toneLabel}</span>
                {select('tone', TONES, t.app.tones, 'expert')}
              </label>
              <label class="field">
                <span>{t.app.ctaLabel}</span>
                {select('ctaGoal', CTA_GOALS, t.app.ctaGoals, 'subscribe')}
              </label>
              <label class="field">
                <span>{t.app.ctaDetailLabel}</span>
                <input type="text" name="ctaDetail" maxlength={200} placeholder={t.app.ctaDetailPlaceholder} value={form.ctaDetail ?? ''} data-remember />
              </label>
            </div>

            <label class="field">
              <span>{t.app.contextLabel}</span>
              <textarea name="context" rows={3} maxlength={1500} placeholder={t.app.contextPlaceholder} data-remember>
                {form.context ?? ''}
              </textarea>
            </label>

            <button type="submit" class="btn btn--primary btn--lg" disabled={usage.remaining === 0}>
              {t.app.submit}
            </button>
          </form>
        </section>

        <aside class="app-side">
          <h2 class="h3">{t.app.historyTitle}</h2>
          {generations.length === 0 ? (
            <p class="muted">{t.app.historyEmpty}</p>
          ) : (
            <ul class="history">
              {generations.map((g) => (
                <li>
                  <a href={`/app/g/${g.id}`} class="history__item">
                    <span class="history__title">{g.title}</span>
                    <span class="history__meta">
                      <span class={`status status--${g.status}`}>{t.app.status[g.status]}</span>
                      {g.reels ? <span>{t.app.reelsCount(g.reels)}</span> : null}
                      <span>{formatDate(locale, g.created_at)}</span>
                    </span>
                  </a>
                </li>
              ))}
            </ul>
          )}
        </aside>
      </div>
    </Layout>
  );
};

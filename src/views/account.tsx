import type { FC } from 'hono/jsx';
import { MIN_PASSWORD } from '../auth.js';
import { formatDate } from '../i18n.js';
import type { Usage } from '../usage.js';
import { Alert, Layout, type PageContext } from './layout.js';

export interface AccountPageProps {
  ctx: PageContext;
  usage: Usage;
  billingEnabled: boolean;
  notice?: string;
  error?: string;
}

export const AccountPage: FC<AccountPageProps> = ({ ctx, usage, billingEnabled, notice, error }) => {
  const { t, locale } = ctx;
  const user = ctx.user!;
  const periodEnd = user.current_period_end ? formatDate(locale, user.current_period_end) : null;
  const isPaid = usage.plan.id !== 'free';

  return (
    <Layout ctx={ctx} title={t.account.title} noindex>
      <div class="container container--narrow">
        <h1 class="h2">{t.account.title}</h1>
        {notice && <Alert kind="success">{notice}</Alert>}
        {error && <Alert kind="error">{error}</Alert>}

        <section class="card stack">
          <div class="kv">
            <span class="muted">{t.auth.email}</span>
            <strong>{user.email}</strong>
          </div>
          <div class="kv">
            <span class="muted">{t.account.plan}</span>
            <span>
              <strong>{t.pricing.planNames[usage.plan.id]}</strong>
              {isPaid && <span class="muted"> · ${usage.plan.price}{t.pricing.perMonth}</span>}
              {isPaid && periodEnd && (
                <span class="muted small block">{user.cancel_at_period_end ? t.account.cancels(periodEnd) : t.account.renews(periodEnd)}</span>
              )}
            </span>
          </div>
          {user.subscription_status === 'past_due' && <Alert kind="warning">{t.account.pastDue}</Alert>}
          <div class="kv">
            <span class="muted">{t.account.usage}</span>
            <span>{t.app.usage(usage.used, usage.limit)}</span>
          </div>
          <div class="gen-actions">
            <a href="/pricing" class="btn btn--primary btn--sm">
              {t.account.upgrade}
            </a>
            {billingEnabled && user.stripe_customer_id && (
              <form method="post" action="/billing/portal" class="inline-form">
                <button type="submit" class="btn btn--secondary btn--sm">
                  {t.account.manage}
                </button>
              </form>
            )}
          </div>
        </section>

        <section class="card stack">
          <h2 class="h3">{t.account.passwordTitle}</h2>
          <form method="post" action="/app/account/password" class="form">
            <label class="field">
              <span>{t.account.currentPassword}</span>
              <input type="password" name="current" required autocomplete="current-password" />
            </label>
            <label class="field">
              <span>{t.auth.newPassword}</span>
              <input type="password" name="password" required minlength={MIN_PASSWORD} autocomplete="new-password" />
            </label>
            <button type="submit" class="btn btn--secondary">
              {t.account.changePassword}
            </button>
          </form>
        </section>

        <section class="card stack danger-zone">
          <h2 class="h3">{t.account.danger}</h2>
          <p class="muted">{t.account.deleteText}</p>
          <form method="post" action="/app/account/delete" class="form">
            <label class="field">
              <span>{t.account.deleteConfirm}</span>
              <input type="text" name="confirm" required pattern="DELETE" autocomplete="off" />
            </label>
            <button type="submit" class="btn btn--danger">
              {t.account.deleteButton}
            </button>
          </form>
        </section>
      </div>
    </Layout>
  );
};

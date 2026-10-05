import type { FC, PropsWithChildren } from 'hono/jsx';
import { MIN_PASSWORD } from '../auth.js';
import { Alert, Layout, type PageContext } from './layout.js';

interface AuthProps {
  ctx: PageContext;
  error?: string;
  notice?: string;
  email?: string;
  next?: string;
}

const AuthCard: FC<PropsWithChildren<{ ctx: PageContext; title: string; subtitle?: string }>> = ({ ctx, title, subtitle, children }) => (
  <Layout ctx={ctx} title={title} noindex>
    <section class="section">
      <div class="auth card">
        <h1 class="h2">{title}</h1>
        {subtitle && <p class="muted">{subtitle}</p>}
        {children}
      </div>
    </section>
  </Layout>
);

const NextInput: FC<{ next?: string }> = ({ next }) => (next ? <input type="hidden" name="next" value={next} /> : null);

function withNext(path: string, next?: string): string {
  return next ? `${path}?next=${encodeURIComponent(next)}` : path;
}

export const LoginPage: FC<AuthProps> = ({ ctx, error, notice, email, next }) => {
  const { t } = ctx;
  return (
    <AuthCard ctx={ctx} title={t.auth.loginTitle}>
      {error && <Alert kind="error">{error}</Alert>}
      {notice && <Alert kind="success">{notice}</Alert>}
      <form method="post" action="/login" class="form">
        <NextInput next={next} />
        <label class="field">
          <span>{t.auth.email}</span>
          <input type="email" name="email" value={email ?? ''} required autocomplete="email" autofocus />
        </label>
        <label class="field">
          <span>{t.auth.password}</span>
          <input type="password" name="password" required autocomplete="current-password" />
        </label>
        <button type="submit" class="btn btn--primary btn--block">
          {t.auth.loginSubmit}
        </button>
      </form>
      <p class="auth__links">
        <a href="/forgot">{t.auth.forgotLink}</a>
        <span>
          {t.auth.noAccount} <a href={withNext('/signup', next)}>{t.nav.signup}</a>
        </span>
      </p>
    </AuthCard>
  );
};

export const SignupPage: FC<AuthProps> = ({ ctx, error, email, next }) => {
  const { t } = ctx;
  return (
    <AuthCard ctx={ctx} title={t.auth.signupTitle} subtitle={t.auth.signupSubtitle}>
      {error && <Alert kind="error">{error}</Alert>}
      <form method="post" action="/signup" class="form">
        <NextInput next={next} />
        <label class="field">
          <span>{t.auth.email}</span>
          <input type="email" name="email" value={email ?? ''} required autocomplete="email" autofocus />
        </label>
        <label class="field">
          <span>{t.auth.password}</span>
          <input type="password" name="password" required minlength={MIN_PASSWORD} autocomplete="new-password" />
          <small class="muted">{t.auth.passwordHint}</small>
        </label>
        <button type="submit" class="btn btn--primary btn--block">
          {t.auth.signupSubmit}
        </button>
        <p class="muted small">
          {t.auth.legal} <a href="/terms">{t.footer.terms}</a> · <a href="/privacy">{t.footer.privacy}</a>
        </p>
      </form>
      <p class="auth__links">
        <span>
          {t.auth.haveAccount} <a href={withNext('/login', next)}>{t.nav.login}</a>
        </span>
      </p>
    </AuthCard>
  );
};

export const ForgotPage: FC<AuthProps & { sent?: boolean }> = ({ ctx, error, sent }) => {
  const { t } = ctx;
  return (
    <AuthCard ctx={ctx} title={t.auth.forgotTitle} subtitle={sent ? undefined : t.auth.forgotText}>
      {error && <Alert kind="error">{error}</Alert>}
      {sent ? (
        <Alert kind="success">{t.auth.forgotSent}</Alert>
      ) : (
        <form method="post" action="/forgot" class="form">
          <label class="field">
            <span>{t.auth.email}</span>
            <input type="email" name="email" required autocomplete="email" autofocus />
          </label>
          <button type="submit" class="btn btn--primary btn--block">
            {t.auth.forgotSubmit}
          </button>
        </form>
      )}
      <p class="auth__links">
        <a href="/login">{t.nav.login}</a>
      </p>
    </AuthCard>
  );
};

export const ResetPage: FC<AuthProps & { token: string; valid: boolean }> = ({ ctx, error, token, valid }) => {
  const { t } = ctx;
  return (
    <AuthCard ctx={ctx} title={t.auth.resetTitle}>
      {error && <Alert kind="error">{error}</Alert>}
      {valid ? (
        <form method="post" action={`/reset/${encodeURIComponent(token)}`} class="form">
          <label class="field">
            <span>{t.auth.newPassword}</span>
            <input type="password" name="password" required minlength={MIN_PASSWORD} autocomplete="new-password" autofocus />
            <small class="muted">{t.auth.passwordHint}</small>
          </label>
          <button type="submit" class="btn btn--primary btn--block">
            {t.auth.resetSubmit}
          </button>
        </form>
      ) : (
        <>
          <Alert kind="error">{t.auth.errors.resetInvalid}</Alert>
          <a href="/forgot" class="btn btn--secondary btn--block">
            {t.auth.forgotSubmit}
          </a>
        </>
      )}
    </AuthCard>
  );
};

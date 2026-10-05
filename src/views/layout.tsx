import type { Child, FC, PropsWithChildren } from 'hono/jsx';
import type { UserRow } from '../db.js';
import type { Dict, Locale } from '../i18n.js';

export interface PageContext {
  t: Dict;
  locale: Locale;
  user: UserRow | null;
  isAdmin: boolean;
  appUrl: string;
  /** Current path + query, used for the language switch and canonical URL. */
  path: string;
  assetVersion: string;
}

interface LayoutProps {
  ctx: PageContext;
  title?: string;
  description?: string;
  noindex?: boolean;
  /** Extra elements for <head>, e.g. a no-JS refresh. */
  head?: Child;
  /** No site header/footer (white-label share pages). */
  bare?: boolean;
}

export const Logo: FC = () => (
  <span class="logo">
    <svg viewBox="0 0 32 32" width="26" height="26" aria-hidden="true">
      <rect width="32" height="32" rx="8" fill="currentColor" />
      <path d="M12 9.5v13l11-6.5z" fill="var(--logo-fg)" />
    </svg>
    Hookcut
  </span>
);

const Header: FC<{ ctx: PageContext }> = ({ ctx }) => {
  const { t, user } = ctx;
  return (
    <header class="site-header">
      <div class="container site-header__inner">
        <a href={user ? '/app' : '/'} class="site-header__brand" aria-label="Hookcut">
          <Logo />
        </a>
        <nav class="site-nav" aria-label="Main">
          {user ? (
            <>
              <a href="/app">{t.nav.dashboard}</a>
              <a href="/pricing">{t.nav.pricing}</a>
              <a href="/app/account">{t.nav.account}</a>
              {ctx.isAdmin && <a href="/admin">{t.nav.admin}</a>}
              <form method="post" action="/logout" class="inline-form">
                <button type="submit" class="link-button">
                  {t.nav.logout}
                </button>
              </form>
            </>
          ) : (
            <>
              <a href="/#features" class="hide-sm">
                {t.nav.features}
              </a>
              <a href="/pricing" class="hide-sm">
                {t.nav.pricing}
              </a>
              <a href="/login">{t.nav.login}</a>
              <a href="/signup" class="btn btn--primary btn--sm">
                {t.nav.signup}
              </a>
            </>
          )}
        </nav>
      </div>
    </header>
  );
};

const Footer: FC<{ ctx: PageContext }> = ({ ctx }) => {
  const { t, locale } = ctx;
  const next = encodeURIComponent(ctx.path);
  return (
    <footer class="site-footer">
      <div class="container site-footer__inner">
        <div>
          <Logo />
          <p class="muted">{t.footer.tagline}</p>
        </div>
        <nav class="site-footer__links" aria-label="Footer">
          <a href="/pricing">{t.nav.pricing}</a>
          <a href="/terms">{t.footer.terms}</a>
          <a href="/privacy">{t.footer.privacy}</a>
          <span class="lang-switch" aria-label={t.footer.language}>
            <a href={`/lang/en?next=${next}`} aria-current={locale === 'en' ? 'true' : undefined}>
              EN
            </a>
            <a href={`/lang/ru?next=${next}`} aria-current={locale === 'ru' ? 'true' : undefined}>
              RU
            </a>
          </span>
        </nav>
      </div>
      <div class="container muted small">© {new Date().getFullYear()} Hookcut</div>
    </footer>
  );
};

export const Layout: FC<PropsWithChildren<LayoutProps>> = ({ ctx, title, description, noindex, head, bare, children }) => {
  const fullTitle = title ? `${title} · Hookcut` : ctx.t.meta.title;
  const desc = description ?? ctx.t.meta.description;
  const v = ctx.assetVersion;
  return (
    <html lang={ctx.locale}>
      <head>
        <meta charset="utf-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
        <title>{fullTitle}</title>
        <meta name="description" content={desc} />
        {noindex && <meta name="robots" content="noindex" />}
        <meta property="og:type" content="website" />
        <meta property="og:site_name" content="Hookcut" />
        <meta property="og:title" content={fullTitle} />
        <meta property="og:description" content={desc} />
        <meta property="og:url" content={`${ctx.appUrl}${ctx.path}`} />
        <meta name="twitter:card" content="summary" />
        <meta name="theme-color" content="#0f0f14" />
        <link rel="icon" href="/favicon.svg" type="image/svg+xml" />
        <link rel="stylesheet" href={`/styles.css?v=${v}`} />
        <script src={`/app.js?v=${v}`} defer></script>
        {head}
      </head>
      <body>
        {!bare && <Header ctx={ctx} />}
        <main id="main">{children}</main>
        {!bare && <Footer ctx={ctx} />}
      </body>
    </html>
  );
};

export const Alert: FC<PropsWithChildren<{ kind?: 'error' | 'success' | 'info' | 'warning' }>> = ({ kind = 'info', children }) => (
  <div class={`alert alert--${kind}`} role={kind === 'error' ? 'alert' : 'status'}>
    {children}
  </div>
);

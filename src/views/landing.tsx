import type { FC } from 'hono/jsx';
import { PLANS, type PlanId } from '../plans.js';
import { Layout, type PageContext } from './layout.js';
import { ReelCard } from './reels.js';
import { SAMPLE_REEL } from './sample.js';

const PLAN_ORDER: PlanId[] = ['free', 'creator', 'pro', 'agency'];
const POPULAR: PlanId = 'pro';

export interface PricingProps {
  ctx: PageContext;
  /** Paid plans that have a Stripe price configured. */
  availablePlans: PlanId[];
}

export const Pricing: FC<PricingProps> = ({ ctx, availablePlans }) => {
  const { t, user } = ctx;
  return (
    <div class="pricing">
      {PLAN_ORDER.map((id) => {
        const plan = PLANS[id];
        const current = user?.plan === id;
        const available = id === 'free' || availablePlans.includes(id);
        let action;
        if (current) {
          action = (
            <span class="btn btn--secondary btn--block is-disabled" aria-disabled="true">
              {t.pricing.current}
            </span>
          );
        } else if (id === 'free') {
          action = (
            <a class="btn btn--secondary btn--block" href={user ? '/app' : '/signup'}>
              {t.pricing.startFree}
            </a>
          );
        } else if (!available) {
          action = (
            <span class="btn btn--secondary btn--block is-disabled" aria-disabled="true" title={t.pricing.unavailable}>
              {t.pricing.choose(plan.name)}
            </span>
          );
        } else {
          action = (
            <a class={`btn btn--block ${id === POPULAR ? 'btn--primary' : 'btn--secondary'}`} href={`/billing/checkout?plan=${id}`}>
              {t.pricing.choose(plan.name)}
            </a>
          );
        }
        return (
          <div class={`plan card ${id === POPULAR ? 'plan--popular' : ''}`}>
            {id === POPULAR && <span class="plan__flag">{t.pricing.popular}</span>}
            <h3 class="plan__name">{t.pricing.planNames[id]}</h3>
            <p class="plan__tagline muted">{t.pricing.planTaglines[id]}</p>
            <p class="plan__price">
              <span class="plan__amount">${plan.price}</span>
              <span class="muted">{t.pricing.perMonth}</span>
            </p>
            <ul class="plan__features">
              <li>{t.pricing.generations(plan.generationsPerMonth)}</li>
              <li>{t.pricing.reels(plan.maxReels)}</li>
              <li>{t.pricing.length(plan.maxChars)}</li>
              <li>{t.pricing.exportShare}</li>
              {plan.whiteLabel && <li>{t.pricing.whiteLabel}</li>}
            </ul>
            {action}
          </div>
        );
      })}
      <p class="pricing__footnote muted small">{t.pricing.footnote}</p>
    </div>
  );
};

export const LandingPage: FC<PricingProps> = ({ ctx, availablePlans }) => {
  const { t } = ctx;
  return (
    <Layout ctx={ctx}>
      <section class="hero">
        <div class="container hero__inner">
          <div class="hero__copy">
            <p class="eyebrow">{t.landing.eyebrow}</p>
            <h1 class="hero__title">{t.landing.title}</h1>
            <p class="hero__subtitle">{t.landing.subtitle}</p>
            <div class="hero__cta">
              <a href="/signup" class="btn btn--primary btn--lg">
                {t.landing.cta}
              </a>
              <p class="muted small">{t.landing.ctaNote}</p>
            </div>
          </div>
          <div class="hero__sample">
            <p class="hero__sample-label">{t.landing.sampleLabel}</p>
            <ReelCard t={t} reel={SAMPLE_REEL[ctx.locale]} index={0} compact />
          </div>
        </div>
      </section>

      <section class="section" id="how">
        <div class="container">
          <h2 class="section__title">{t.landing.howTitle}</h2>
          <ol class="steps">
            {t.landing.how.map((step, i) => (
              <li class="step card">
                <span class="step__num">{i + 1}</span>
                <h3>{step.title}</h3>
                <p class="muted">{step.text}</p>
              </li>
            ))}
          </ol>
        </div>
      </section>

      <section class="section section--alt" id="features">
        <div class="container">
          <h2 class="section__title">{t.landing.featuresTitle}</h2>
          <div class="grid grid--3">
            {t.landing.features.map((f) => (
              <div class="feature">
                <h3>{f.title}</h3>
                <p class="muted">{f.text}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      <section class="section" id="audience">
        <div class="container">
          <h2 class="section__title">{t.landing.audienceTitle}</h2>
          <div class="grid grid--4">
            {t.landing.audience.map((a) => (
              <div class="card audience">
                <h3>{a.title}</h3>
                <p class="muted">{a.text}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      <section class="section section--alt" id="pricing">
        <div class="container">
          <h2 class="section__title">{t.pricing.title}</h2>
          <p class="section__subtitle muted">{t.pricing.subtitle}</p>
          <Pricing ctx={ctx} availablePlans={availablePlans} />
        </div>
      </section>

      <section class="section" id="faq">
        <div class="container container--narrow">
          <h2 class="section__title">{t.landing.faqTitle}</h2>
          <div class="faq">
            {t.landing.faq.map((item) => (
              <details class="faq__item">
                <summary>{item.q}</summary>
                <p>{item.a}</p>
              </details>
            ))}
          </div>
        </div>
      </section>

      <section class="section final-cta">
        <div class="container container--narrow center">
          <h2 class="section__title">{t.landing.finalTitle}</h2>
          <p class="section__subtitle muted">{t.landing.finalText}</p>
          <a href="/signup" class="btn btn--primary btn--lg">
            {t.landing.cta}
          </a>
        </div>
      </section>
    </Layout>
  );
};

export const PricingPage: FC<PricingProps & { notice?: string }> = ({ ctx, availablePlans, notice }) => (
  <Layout ctx={ctx} title={ctx.t.pricing.title}>
    <section class="section">
      <div class="container">
        <h1 class="section__title">{ctx.t.pricing.title}</h1>
        <p class="section__subtitle muted">{ctx.t.pricing.subtitle}</p>
        {notice && <p class="center muted">{notice}</p>}
        <Pricing ctx={ctx} availablePlans={availablePlans} />
      </div>
    </section>
  </Layout>
);

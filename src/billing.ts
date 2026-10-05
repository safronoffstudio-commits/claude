import Stripe from 'stripe';
import type { Config } from './config.js';
import { getUserById } from './auth.js';
import { now, type Db, type UserRow } from './db.js';
import { PAID_PLANS, type PaidPlanId, type PlanId } from './plans.js';

// Statuses that keep paid access. past_due is a grace period while Stripe retries the card.
const ACCESS_STATUSES = new Set(['active', 'trialing', 'past_due']);

const SUBSCRIPTION_EVENTS = new Set([
  'customer.subscription.created',
  'customer.subscription.updated',
  'customer.subscription.deleted',
  'customer.subscription.paused',
  'customer.subscription.resumed',
]);

export class BillingDisabledError extends Error {}
export class WebhookSignatureError extends Error {}

export interface Billing {
  readonly enabled: boolean;
  isPlanAvailable(plan: PaidPlanId): boolean;
  checkoutUrl(user: UserRow, plan: PaidPlanId): Promise<string>;
  portalUrl(user: UserRow): Promise<string>;
  /** Applies the subscription from a finished Checkout session; called from the success redirect. */
  syncCheckoutSession(user: UserRow, sessionId: string): Promise<boolean>;
  /** Verifies the signature and applies the event. Throws on a bad signature. */
  handleWebhook(payload: string, signature: string): Promise<'processed' | 'duplicate' | 'ignored'>;
  cancelImmediately(user: UserRow): Promise<void>;
}

function id(value: string | { id: string } | null | undefined): string | null {
  if (!value) return null;
  return typeof value === 'string' ? value : value.id;
}

export function createBilling(db: Db, config: Config, stripeClient?: Stripe): Billing {
  const { secretKey, webhookSecret, prices } = config.stripe;
  const stripe = stripeClient ?? (secretKey ? new Stripe(secretKey) : null);
  const priceToPlan = new Map<string, PaidPlanId>();
  for (const plan of PAID_PLANS) {
    const price = prices[plan];
    if (price) priceToPlan.set(price, plan);
  }

  function client(): Stripe {
    if (!stripe) throw new BillingDisabledError('Stripe is not configured');
    return stripe;
  }

  async function ensureCustomer(user: UserRow): Promise<string> {
    if (user.stripe_customer_id) return user.stripe_customer_id;
    // Created before checkout so subscription events can always be matched to the user,
    // whatever order Stripe delivers them in.
    const customer = await client().customers.create({ email: user.email, metadata: { user_id: String(user.id) } });
    db.prepare('UPDATE users SET stripe_customer_id = ? WHERE id = ?').run(customer.id, user.id);
    return customer.id;
  }

  function applySubscription(sub: Stripe.Subscription): void {
    const customerId = id(sub.customer);
    let user = customerId
      ? (db.prepare('SELECT * FROM users WHERE stripe_customer_id = ?').get(customerId) as UserRow | undefined)
      : undefined;
    if (!user && sub.metadata?.user_id) {
      user = getUserById(db, Number(sub.metadata.user_id));
      if (user && customerId) db.prepare('UPDATE users SET stripe_customer_id = ? WHERE id = ?').run(customerId, user.id);
    }
    if (!user) {
      console.warn(`[billing] no user for subscription ${sub.id} (customer ${customerId})`);
      return;
    }

    const item = sub.items.data[0];
    const plan = item ? priceToPlan.get(item.price.id) : undefined;
    if (item && !plan) console.warn(`[billing] unknown price ${item.price.id} on subscription ${sub.id}`);
    const hasAccess = Boolean(plan) && ACCESS_STATUSES.has(sub.status);

    // An event about an old, inactive subscription must not downgrade the current one.
    if (user.stripe_subscription_id && user.stripe_subscription_id !== sub.id && !hasAccess) return;

    const nextPlan: PlanId = hasAccess ? plan! : 'free';
    db.prepare(
      `UPDATE users SET plan = ?, stripe_subscription_id = ?, subscription_status = ?, current_period_start = ?,
         current_period_end = ?, cancel_at_period_end = ? WHERE id = ?`,
    ).run(
      nextPlan,
      sub.id,
      sub.status,
      item?.current_period_start ?? null,
      item?.current_period_end ?? null,
      sub.cancel_at_period_end || sub.cancel_at ? 1 : 0,
      user.id,
    );
  }

  return {
    enabled: Boolean(stripe),

    isPlanAvailable(plan) {
      return Boolean(stripe && prices[plan]);
    },

    async checkoutUrl(user, plan) {
      const price = prices[plan];
      if (!price) throw new BillingDisabledError(`No Stripe price configured for ${plan}`);
      const customer = await ensureCustomer(user);
      const session = await client().checkout.sessions.create({
        mode: 'subscription',
        customer,
        client_reference_id: String(user.id),
        line_items: [{ price, quantity: 1 }],
        allow_promotion_codes: true,
        subscription_data: { metadata: { user_id: String(user.id) } },
        success_url: `${config.appUrl}/billing/success?session_id={CHECKOUT_SESSION_ID}`,
        cancel_url: `${config.appUrl}/pricing`,
      });
      if (!session.url) throw new Error('Stripe returned a Checkout session without a URL');
      return session.url;
    },

    async portalUrl(user) {
      const customer = await ensureCustomer(user);
      const session = await client().billingPortal.sessions.create({ customer, return_url: `${config.appUrl}/app/account` });
      return session.url;
    },

    async syncCheckoutSession(user, sessionId) {
      const session = await client().checkout.sessions.retrieve(sessionId);
      if (!user.stripe_customer_id || id(session.customer) !== user.stripe_customer_id) return false;
      const subscriptionId = id(session.subscription);
      if (session.status !== 'complete' || !subscriptionId) return false;
      applySubscription(await client().subscriptions.retrieve(subscriptionId));
      return true;
    },

    async handleWebhook(payload, signature) {
      if (!webhookSecret) throw new BillingDisabledError('STRIPE_WEBHOOK_SECRET is not set');
      let event: Stripe.Event;
      try {
        event = await client().webhooks.constructEventAsync(payload, signature, webhookSecret);
      } catch (err) {
        if (err instanceof Stripe.errors.StripeSignatureVerificationError) throw new WebhookSignatureError(err.message);
        throw err;
      }
      if (db.prepare('SELECT 1 FROM stripe_events WHERE id = ?').get(event.id)) return 'duplicate';

      let subscriptionId: string | null = null;
      if (SUBSCRIPTION_EVENTS.has(event.type)) {
        subscriptionId = (event.data.object as Stripe.Subscription).id;
      } else if (event.type === 'checkout.session.completed') {
        const session = event.data.object as Stripe.Checkout.Session;
        if (session.mode === 'subscription') subscriptionId = id(session.subscription);
      }
      if (subscriptionId) {
        // Events can arrive out of order; the current object from the API is the truth.
        applySubscription(await client().subscriptions.retrieve(subscriptionId));
      }
      // Recorded only after processing, so a failure is retried by Stripe.
      db.prepare('INSERT OR IGNORE INTO stripe_events (id, type, received_at) VALUES (?, ?, ?)').run(event.id, event.type, now());
      return subscriptionId ? 'processed' : 'ignored';
    },

    async cancelImmediately(user) {
      if (!stripe || !user.stripe_subscription_id) return;
      if (!user.subscription_status || !ACCESS_STATUSES.has(user.subscription_status)) return;
      await stripe.subscriptions.cancel(user.stripe_subscription_id);
    },
  };
}

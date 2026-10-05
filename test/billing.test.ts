import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import Stripe from 'stripe';
import { getUserByEmail } from '../src/auth.js';
import { Client, createTestApp, WEBHOOK_SECRET } from './helpers.js';

const STRIPE_ENV = {
  STRIPE_SECRET_KEY: 'sk_test_dummy',
  STRIPE_WEBHOOK_SECRET: WEBHOOK_SECRET,
  STRIPE_PRICE_CREATOR: 'price_creator',
  STRIPE_PRICE_PRO: 'price_pro',
  STRIPE_PRICE_AGENCY: 'price_agency',
};

function subscription(overrides: Record<string, unknown> = {}, price = 'price_pro'): Stripe.Subscription {
  return {
    id: 'sub_1',
    object: 'subscription',
    customer: 'cus_1',
    status: 'active',
    cancel_at_period_end: false,
    cancel_at: null,
    metadata: {},
    items: { object: 'list', data: [{ id: 'si_1', price: { id: price }, current_period_start: 1_700_000_000, current_period_end: 1_702_592_000 }] },
    ...overrides,
  } as unknown as Stripe.Subscription;
}

/** A Stripe client whose network-facing methods are replaced with in-memory fakes. */
function fakeStripe(state: { subscriptions: Map<string, Stripe.Subscription>; calls: string[] }): Stripe {
  const stripe = new Stripe('sk_test_dummy');
  Object.assign(stripe.customers, {
    create: async () => {
      state.calls.push('customers.create');
      return { id: 'cus_1' };
    },
  });
  Object.assign(stripe.checkout.sessions, {
    create: async (params: Stripe.Checkout.SessionCreateParams) => {
      state.calls.push(`checkout.create:${params.line_items?.[0]?.price}:${params.customer}`);
      return { url: 'https://checkout.stripe.com/c/pay/cs_test_1' };
    },
    retrieve: async () => ({ id: 'cs_test_1', customer: 'cus_1', subscription: 'sub_1', status: 'complete' }),
  });
  Object.assign(stripe.billingPortal.sessions, {
    create: async () => {
      state.calls.push('portal.create');
      return { url: 'https://billing.stripe.com/p/session/test_1' };
    },
  });
  Object.assign(stripe.subscriptions, {
    retrieve: async (id: string) => {
      const sub = state.subscriptions.get(id);
      if (!sub) throw new Error(`no such subscription ${id}`);
      return sub;
    },
    cancel: async (id: string) => {
      state.calls.push(`subscriptions.cancel:${id}`);
      return { id, status: 'canceled' };
    },
  });
  return stripe;
}

function setup() {
  const state = { subscriptions: new Map<string, Stripe.Subscription>(), calls: [] as string[] };
  const stripe = fakeStripe(state);
  const test = createTestApp({ env: STRIPE_ENV, stripe });
  return { ...test, state };
}

async function sendWebhook(test: ReturnType<typeof setup>, event: { id: string; type: string; object: unknown }, secret = WEBHOOK_SECRET) {
  const payload = JSON.stringify({ id: event.id, object: 'event', type: event.type, data: { object: event.object } });
  const signature = test.stripe.webhooks.generateTestHeaderString({ payload, secret });
  return test.app.request('http://localhost:3000/stripe/webhook', {
    method: 'POST',
    headers: { 'content-type': 'application/json', 'stripe-signature': signature },
    body: payload,
  });
}

describe('billing', () => {
  it('sends a free user to Checkout with a pre-created customer', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');

    const res = await client.get('/billing/checkout?plan=pro');
    assert.equal(res.status, 303);
    assert.equal(res.headers.get('location'), 'https://checkout.stripe.com/c/pay/cs_test_1');
    assert.deepEqual(test.state.calls, ['customers.create', 'checkout.create:price_pro:cus_1']);
    assert.equal(getUserByEmail(test.db, 'buyer@example.com')!.stripe_customer_id, 'cus_1');
  });

  it('asks anonymous visitors to sign up first and keeps the chosen plan', async () => {
    const test = setup();
    const res = await new Client(test.app).get('/billing/checkout?plan=agency');
    assert.equal(res.status, 302);
    assert.equal(res.headers.get('location'), '/signup?next=%2Fbilling%2Fcheckout%3Fplan%3Dagency');
  });

  it('applies the subscription on the success redirect, before any webhook arrives', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=pro');
    test.state.subscriptions.set('sub_1', subscription());

    const res = await client.get('/billing/success?session_id=cs_test_1');
    assert.equal(res.headers.get('location'), '/app/account?checkout=success');
    const user = getUserByEmail(test.db, 'buyer@example.com')!;
    assert.equal(user.plan, 'pro');
    assert.equal(user.current_period_end, 1_702_592_000);
  });

  it('syncs plan changes, cancellations and deletions from webhooks', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=creator');

    test.state.subscriptions.set('sub_1', subscription({}, 'price_creator'));
    let res = await sendWebhook(test, { id: 'evt_1', type: 'customer.subscription.created', object: subscription({}, 'price_creator') });
    assert.equal(res.status, 200);
    assert.equal(getUserByEmail(test.db, 'buyer@example.com')!.plan, 'creator');

    // The event payload is stale; the handler trusts the subscription fetched from the API.
    test.state.subscriptions.set('sub_1', subscription({ cancel_at_period_end: true }, 'price_agency'));
    await sendWebhook(test, { id: 'evt_2', type: 'customer.subscription.updated', object: subscription({}, 'price_creator') });
    let user = getUserByEmail(test.db, 'buyer@example.com')!;
    assert.equal(user.plan, 'agency');
    assert.equal(user.cancel_at_period_end, 1);

    test.state.subscriptions.set('sub_1', subscription({ status: 'canceled' }, 'price_agency'));
    await sendWebhook(test, { id: 'evt_3', type: 'customer.subscription.deleted', object: subscription({ status: 'canceled' }) });
    user = getUserByEmail(test.db, 'buyer@example.com')!;
    assert.equal(user.plan, 'free');
    assert.equal(user.subscription_status, 'canceled');
  });

  it('keeps access while past_due and ignores events about an older subscription', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=pro');

    test.state.subscriptions.set('sub_2', subscription({ id: 'sub_2', status: 'past_due' }));
    await sendWebhook(test, { id: 'evt_1', type: 'customer.subscription.updated', object: { id: 'sub_2' } });
    assert.equal(getUserByEmail(test.db, 'buyer@example.com')!.plan, 'pro');

    test.state.subscriptions.set('sub_old', subscription({ id: 'sub_old', status: 'canceled' }));
    await sendWebhook(test, { id: 'evt_2', type: 'customer.subscription.deleted', object: { id: 'sub_old' } });
    const user = getUserByEmail(test.db, 'buyer@example.com')!;
    assert.equal(user.plan, 'pro');
    assert.equal(user.stripe_subscription_id, 'sub_2');
  });

  it('rejects bad signatures and ignores duplicate deliveries', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=pro');
    test.state.subscriptions.set('sub_1', subscription());

    const forged = await sendWebhook(test, { id: 'evt_1', type: 'customer.subscription.created', object: { id: 'sub_1' } }, 'whsec_wrong');
    assert.equal(forged.status, 400);
    assert.equal(getUserByEmail(test.db, 'buyer@example.com')!.plan, 'free');

    const first = await sendWebhook(test, { id: 'evt_1', type: 'customer.subscription.created', object: { id: 'sub_1' } });
    assert.deepEqual(await first.json(), { received: true, result: 'processed' });
    const again = await sendWebhook(test, { id: 'evt_1', type: 'customer.subscription.created', object: { id: 'sub_1' } });
    assert.deepEqual(await again.json(), { received: true, result: 'duplicate' });

    const missing = await test.app.request('http://localhost:3000/stripe/webhook', { method: 'POST', body: '{}' });
    assert.equal(missing.status, 400);
  });

  it('sends subscribers to the customer portal to change plans', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=pro');
    test.state.subscriptions.set('sub_1', subscription());
    await client.get('/billing/success?session_id=cs_test_1');

    const res = await client.get('/billing/checkout?plan=agency');
    assert.equal(res.headers.get('location'), 'https://billing.stripe.com/p/session/test_1');
    const portal = await client.post('/billing/portal');
    assert.equal(portal.headers.get('location'), 'https://billing.stripe.com/p/session/test_1');
  });

  it('cancels the subscription when the account is deleted', async () => {
    const test = setup();
    const client = new Client(test.app);
    await client.signup('buyer@example.com');
    await client.get('/billing/checkout?plan=pro');
    test.state.subscriptions.set('sub_1', subscription());
    await client.get('/billing/success?session_id=cs_test_1');

    const res = await client.post('/app/account/delete', { confirm: 'DELETE' });
    assert.equal(res.status, 303);
    assert.ok(test.state.calls.includes('subscriptions.cancel:sub_1'));
    assert.equal(getUserByEmail(test.db, 'buyer@example.com'), undefined);
  });
});

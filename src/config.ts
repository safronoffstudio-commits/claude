export type Effort = 'low' | 'medium' | 'high' | 'xhigh' | 'max';

export interface Config {
  env: 'development' | 'production' | 'test';
  port: number;
  appUrl: string;
  /** True when APP_URL is https — enables Secure cookies. */
  secureCookies: boolean;
  databasePath: string;
  trustProxy: boolean;
  adminEmails: string[];
  ai: {
    apiKey: string | undefined;
    model: string;
    effort: Effort;
    /** Use the offline demo generator instead of Claude. */
    mock: boolean;
    concurrency: number;
  };
  stripe: {
    secretKey: string | undefined;
    webhookSecret: string | undefined;
    prices: { creator?: string; pro?: string; agency?: string };
  };
  email: {
    resendApiKey: string | undefined;
    from: string;
  };
}

const EFFORTS: Effort[] = ['low', 'medium', 'high', 'xhigh', 'max'];

function str(env: NodeJS.ProcessEnv, key: string): string | undefined {
  const value = env[key]?.trim();
  return value ? value : undefined;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  const nodeEnv = env.NODE_ENV === 'production' ? 'production' : env.NODE_ENV === 'test' ? 'test' : 'development';
  const port = Number(env.PORT ?? 3000);
  const appUrl = (str(env, 'APP_URL') ?? `http://localhost:${port}`).replace(/\/+$/, '');
  const apiKey = str(env, 'ANTHROPIC_API_KEY');
  const effort = (str(env, 'AI_EFFORT') ?? 'high') as Effort;
  if (!EFFORTS.includes(effort)) {
    throw new Error(`AI_EFFORT must be one of ${EFFORTS.join(', ')}`);
  }
  // Without a key the app still runs end-to-end in development, using the demo generator.
  const mock = env.AI_MOCK === '1' || (!apiKey && nodeEnv !== 'production');

  const config: Config = {
    env: nodeEnv,
    port,
    appUrl,
    secureCookies: appUrl.startsWith('https://'),
    databasePath: str(env, 'DATABASE_PATH') ?? './data/hookcut.db',
    trustProxy: env.TRUST_PROXY === '1',
    adminEmails: (str(env, 'ADMIN_EMAILS') ?? '')
      .split(',')
      .map((e) => e.trim().toLowerCase())
      .filter(Boolean),
    ai: {
      apiKey,
      model: str(env, 'AI_MODEL') ?? 'claude-opus-5-5',
      effort,
      mock,
      concurrency: Math.max(1, Number(env.AI_CONCURRENCY ?? 4)),
    },
    stripe: {
      secretKey: str(env, 'STRIPE_SECRET_KEY'),
      webhookSecret: str(env, 'STRIPE_WEBHOOK_SECRET'),
      prices: {
        creator: str(env, 'STRIPE_PRICE_CREATOR'),
        pro: str(env, 'STRIPE_PRICE_PRO'),
        agency: str(env, 'STRIPE_PRICE_AGENCY'),
      },
    },
    email: {
      resendApiKey: str(env, 'RESEND_API_KEY'),
      from: str(env, 'EMAIL_FROM') ?? 'Hookcut <hello@hookcut.app>',
    },
  };

  if (config.env === 'production') {
    if (!str(env, 'APP_URL')) throw new Error('APP_URL is required in production');
    if (!config.ai.apiKey && !config.ai.mock) {
      throw new Error('ANTHROPIC_API_KEY is required in production (or set AI_MOCK=1 for a demo deployment)');
    }
  }
  return config;
}

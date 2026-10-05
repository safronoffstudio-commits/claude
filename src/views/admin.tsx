import type { FC } from 'hono/jsx';
import { Layout, type PageContext } from './layout.js';

export interface AdminStats {
  users: number;
  signups7d: number;
  paying: { plan: string; count: number; price: number }[];
  mrr: number;
  generations24h: number;
  generations30d: number;
  failed30d: number;
  aiCost30d: number;
  recentUsers: { email: string; plan: string; created_at: number }[];
}

const usd = (n: number) => `$${n.toLocaleString('en-US', { maximumFractionDigits: 2 })}`;
const date = (unix: number) => new Date(unix * 1000).toISOString().slice(0, 16).replace('T', ' ');

/** Internal dashboard — English only. */
export const AdminPage: FC<{ ctx: PageContext; stats: AdminStats }> = ({ ctx, stats }) => {
  const margin = stats.mrr > 0 ? Math.round((1 - stats.aiCost30d / stats.mrr) * 100) : null;
  const tiles: [string, string][] = [
    ['MRR', usd(stats.mrr)],
    ['ARR run-rate', usd(stats.mrr * 12)],
    ['Paying customers', String(stats.paying.reduce((n, p) => n + p.count, 0))],
    ['Users', String(stats.users)],
    ['Signups, 7 days', String(stats.signups7d)],
    ['Generations, 24 h', String(stats.generations24h)],
    ['Generations, 30 days', String(stats.generations30d)],
    ['Failed, 30 days', String(stats.failed30d)],
    ['AI cost, 30 days (est.)', usd(stats.aiCost30d)],
    ['Gross margin vs MRR', margin === null ? '—' : `${margin}%`],
  ];
  return (
    <Layout ctx={ctx} title="Admin" noindex>
      <div class="container">
        <h1 class="h2">Admin</h1>
        <div class="stats">
          {tiles.map(([label, value]) => (
            <div class="stat card">
              <span class="stat__label muted small">{label}</span>
              <span class="stat__value">{value}</span>
            </div>
          ))}
        </div>

        <div class="grid grid--2">
          <section class="card">
            <h2 class="h3">Subscriptions by plan</h2>
            <table>
              <thead>
                <tr>
                  <th>Plan</th>
                  <th>Customers</th>
                  <th>MRR</th>
                </tr>
              </thead>
              <tbody>
                {stats.paying.length === 0 && (
                  <tr>
                    <td colspan={3} class="muted">
                      No paying customers yet
                    </td>
                  </tr>
                )}
                {stats.paying.map((p) => (
                  <tr>
                    <td>{p.plan}</td>
                    <td>{p.count}</td>
                    <td>{usd(p.count * p.price)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </section>
          <section class="card">
            <h2 class="h3">Latest signups</h2>
            <table>
              <thead>
                <tr>
                  <th>Email</th>
                  <th>Plan</th>
                  <th>Created (UTC)</th>
                </tr>
              </thead>
              <tbody>
                {stats.recentUsers.map((u) => (
                  <tr>
                    <td>{u.email}</td>
                    <td>{u.plan}</td>
                    <td>{date(u.created_at)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </section>
        </div>
      </div>
    </Layout>
  );
};

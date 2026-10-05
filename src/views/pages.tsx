import type { FC } from 'hono/jsx';
import { Layout, type PageContext } from './layout.js';

export const ErrorPage: FC<{ ctx: PageContext; status: 404 | 500 }> = ({ ctx, status }) => {
  const { t } = ctx;
  const title = status === 404 ? t.errors.notFoundTitle : t.errors.serverTitle;
  return (
    <Layout ctx={ctx} title={title} noindex>
      <section class="section">
        <div class="container container--narrow center">
          <p class="error-code">{status}</p>
          <h1 class="h2">{title}</h1>
          <p class="muted">{status === 404 ? t.errors.notFoundText : t.errors.serverText}</p>
          <a href="/" class="btn btn--primary">
            {t.errors.home}
          </a>
        </div>
      </section>
    </Layout>
  );
};

// Starting points written for a typical SaaS — have them reviewed for your company and jurisdiction.
const LEGAL = {
  en: {
    terms: {
      title: 'Terms of Service',
      body: [
        'Hookcut provides software that turns transcripts you upload into short-video scripts. By creating an account you agree to these terms.',
        'You are responsible for the content you upload and must have the right to use it. Do not upload content that is illegal or infringes on the rights of others.',
        'Scripts are generated automatically and may contain mistakes. Review them before publishing.',
        'Paid plans are billed monthly in advance through Stripe and renew automatically until cancelled. You can cancel at any time from your account; access continues until the end of the paid period. Payments are non-refundable except where required by law.',
        'We may suspend accounts that abuse the service, attempt to circumvent limits or violate these terms.',
        'The service is provided “as is” without warranties. To the extent permitted by law, our liability is limited to the amount you paid in the last 3 months.',
        'We may update these terms; material changes will be announced by email or in the product.',
      ],
    },
    privacy: {
      title: 'Privacy Policy',
      body: [
        'We collect your email address, a hash of your password, the transcripts you upload, the scripts we generate and basic billing information from Stripe. We do not see or store your card details.',
        'Transcripts are sent to our AI provider (Anthropic) solely to generate your scripts. Payments are processed by Stripe; transactional email is sent through our email provider.',
        'Your transcripts and scripts are visible only to you, unless you turn on a share link for a specific result.',
        'We use a single essential cookie to keep you signed in and one to remember your language. No advertising trackers.',
        'You can delete your account at any time from the account page; this permanently deletes your transcripts and scripts.',
        'Questions about your data: contact us at the address in the footer of our emails.',
      ],
    },
  },
  ru: {
    terms: {
      title: 'Условия использования',
      body: [
        'Hookcut — сервис, который превращает загруженные вами расшифровки в сценарии коротких видео. Создавая аккаунт, вы принимаете эти условия.',
        'Вы отвечаете за загружаемый контент и должны иметь право его использовать. Не загружайте незаконный контент и материалы, нарушающие чужие права.',
        'Сценарии создаются автоматически и могут содержать ошибки. Проверяйте их перед публикацией.',
        'Платные тарифы оплачиваются ежемесячно авансом через Stripe и продлеваются автоматически до отмены. Отменить подписку можно в любой момент в аккаунте; доступ сохраняется до конца оплаченного периода. Платежи не возвращаются, кроме случаев, предусмотренных законом.',
        'Мы можем приостановить аккаунт при злоупотреблении сервисом, попытках обойти лимиты или нарушении условий.',
        'Сервис предоставляется «как есть». В пределах, допустимых законом, наша ответственность ограничена суммой, оплаченной вами за последние 3 месяца.',
        'Мы можем обновлять условия; о существенных изменениях сообщим по email или в продукте.',
      ],
    },
    privacy: {
      title: 'Политика конфиденциальности',
      body: [
        'Мы храним ваш email, хеш пароля, загруженные расшифровки, созданные сценарии и базовые данные об оплате из Stripe. Данные карты мы не видим и не храним.',
        'Расшифровки передаются нашему провайдеру ИИ (Anthropic) только для создания ваших сценариев. Платежи обрабатывает Stripe; служебные письма отправляет наш почтовый провайдер.',
        'Расшифровки и сценарии видны только вам — пока вы сами не включите ссылку на конкретный результат.',
        'Мы используем один необходимый cookie для входа в аккаунт и один — для выбранного языка. Рекламных трекеров нет.',
        'Аккаунт можно удалить в любой момент на странице аккаунта — вместе со всеми расшифровками и сценариями.',
        'Вопросы о данных: напишите нам на адрес, указанный в наших письмах.',
      ],
    },
  },
};

export const LegalPage: FC<{ ctx: PageContext; doc: 'terms' | 'privacy' }> = ({ ctx, doc }) => {
  const content = LEGAL[ctx.locale][doc];
  return (
    <Layout ctx={ctx} title={content.title}>
      <section class="section">
        <div class="container container--narrow prose">
          <h1 class="h2">{content.title}</h1>
          {content.body.map((paragraph) => (
            <p>{paragraph}</p>
          ))}
        </div>
      </section>
    </Layout>
  );
};

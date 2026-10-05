export type Locale = 'en' | 'ru';
export const LOCALES: Locale[] = ['en', 'ru'];

export const OUTPUT_LANGUAGES = ['auto', 'ru', 'en', 'es', 'pt', 'de', 'fr', 'it', 'uk', 'pl', 'tr'] as const;
export const PLATFORMS = ['reels', 'tiktok', 'shorts'] as const;
export const TONES = ['expert', 'energetic', 'provocative', 'storytelling'] as const;
export const CTA_GOALS = ['subscribe', 'comment', 'link', 'dm', 'save', 'custom'] as const;

export type OutputLanguage = (typeof OUTPUT_LANGUAGES)[number];
export type Platform = (typeof PLATFORMS)[number];
export type Tone = (typeof TONES)[number];
export type CtaGoal = (typeof CTA_GOALS)[number];

const minutes = (chars: number) => Math.round(chars / 1000);
const hoursOrMinutes = (en: boolean, chars: number) => {
  const m = minutes(chars);
  if (m < 90) return en ? `~${m} min` : `~${m} мин`;
  const h = Math.round((m / 60) * 2) / 2;
  return en ? `~${h} h` : `~${String(h).replace('.', ',')} ч`;
};

const en = {
  meta: {
    title: 'Hookcut — turn long videos into ready-to-shoot Reels scripts',
    description:
      'Paste a podcast, webinar or interview transcript. Get 40–60s Reels, TikTok and Shorts scripts with hooks, timing, on-screen text and source timecodes for your editor.',
  },
  nav: {
    features: 'Features',
    pricing: 'Pricing',
    faq: 'FAQ',
    login: 'Log in',
    signup: 'Start free',
    dashboard: 'My scripts',
    account: 'Account',
    logout: 'Log out',
    admin: 'Admin',
  },
  footer: {
    tagline: 'Long videos in, short videos out.',
    terms: 'Terms',
    privacy: 'Privacy',
    language: 'Language',
  },
  landing: {
    eyebrow: 'For experts, podcasters and content agencies',
    title: 'One long video → five ready-to-shoot Reels',
    subtitle:
      'Paste the transcript of a podcast, live stream or interview. Hookcut finds the moments that work without context and writes 40–60 second scripts — hook, promise, body, payoff and CTA — with source timecodes for your editor.',
    cta: 'Get 3 scripts free',
    ctaNote: 'No card required · .srt, .vtt or plain text',
    sampleLabel: 'Sample output',
    howTitle: 'How it works',
    how: [
      { title: 'Paste a transcript', text: 'From YouTube, Zoom, Riverside, Descript or Whisper. .srt, .vtt or plain text with timecodes.' },
      { title: 'Pick the format', text: 'How many Reels, which platform, tone, language and what the viewer should do at the end.' },
      { title: 'Get edit-ready scripts', text: 'Second-by-second plan with voiceover, on-screen text and editing notes. Copy, export or send a link.' },
    ],
    featuresTitle: 'Not “AI wrote some text” — a script your editor can cut',
    features: [
      { title: 'Source breakdown', text: 'A table of the strongest moments — fact, insight, conflict, number or emotion — and why each one works or doesn’t.' },
      { title: 'Hooks that stop the scroll', text: 'Three proven hook archetypes plus two alternative hooks per Reel for A/B tests.' },
      { title: 'Hard timing', text: 'Hook in 0–3s, promise by 7s, 2–3 body beats, payoff and exactly one CTA. Every Reel lands in 40–60 seconds.' },
      { title: 'Timecodes for the editor', text: 'Every beat points to a range in the source. Nobody has to rewatch an hour of footage.' },
      { title: 'Automatic QA', text: 'Runtime, hook length, single CTA and body structure are checked automatically on every script.' },
      { title: 'Caption and hashtags', text: 'Post copy and hashtags for Reels, TikTok or Shorts in the language you need.' },
    ],
    audienceTitle: 'Built for',
    audience: [
      { title: 'Experts & creators', text: 'Turn every live stream into a week of content.' },
      { title: 'Podcasts', text: 'Clips from every episode without hours of rewatching.' },
      { title: 'Content agencies', text: 'Client-ready scripts in minutes, shared by link. White-label on Agency.' },
      { title: 'Video editors', text: 'A brief with timecodes instead of “cut something interesting”.' },
    ],
    faqTitle: 'Questions',
    faq: [
      {
        q: 'Where do I get a transcript?',
        a: 'On YouTube open “…more” → “Show transcript” and copy it with timecodes. Zoom, Riverside, Descript, Premiere and CapCut all export .srt/.vtt. Plain text without timecodes works too — scripts will quote the first words of each fragment instead.',
      },
      {
        q: 'Which languages are supported?',
        a: 'Transcripts in any language. Scripts are written in the language of the source or in one of 10 languages you choose.',
      },
      {
        q: 'How is this different from asking a chatbot?',
        a: 'Hookcut follows an editing method: hard beat timing, hook archetypes, source timecodes, automatic checks and export. No prompts to write, no wall of text to clean up.',
      },
      {
        q: 'What counts as one generation?',
        a: 'One transcript of up to ~1 hour → up to 5 scripts at once. Longer recordings count as one generation per started hour. Failed generations are never counted.',
      },
      { q: 'Can I cancel anytime?', a: 'Yes, in one click from your account. You keep access until the end of the paid period.' },
      {
        q: 'Who can see my transcripts?',
        a: 'Only you — until you turn on a share link for a specific result. Deleting your account deletes everything.',
      },
    ],
    finalTitle: 'Your next week of Reels is already recorded',
    finalText: 'It’s sitting in your last podcast. Get it out in two minutes.',
  },
  pricing: {
    title: 'Simple pricing',
    subtitle: 'Start free. Upgrade when Hookcut pays for itself — usually after the first client.',
    perMonth: '/mo',
    popular: 'Most popular',
    current: 'Current plan',
    choose: (name: string) => `Choose ${name}`,
    startFree: 'Start free',
    unavailable: 'Payments are not configured yet',
    footnote: '1 generation = one video of up to ~1 hour, turned into up to 5 scripts. Cancel anytime.',
    generations: (n: number) => `${n} generations per month`,
    reels: (n: number) => `Up to ${n} scripts per video`,
    length: (chars: number) => `Videos up to ${hoursOrMinutes(true, chars)}`,
    exportShare: 'Markdown export & share links',
    whiteLabel: 'White-label share pages',
    planNames: { free: 'Free', creator: 'Creator', pro: 'Pro', agency: 'Agency' },
    planTaglines: {
      free: 'Try it on your last video',
      creator: 'For creators publishing every day',
      pro: 'For podcasts and teams',
      agency: 'For agencies with many clients',
    },
  },
  auth: {
    email: 'Email',
    password: 'Password',
    passwordHint: 'At least 8 characters',
    newPassword: 'New password',
    loginTitle: 'Welcome back',
    loginSubmit: 'Log in',
    signupTitle: 'Create your account',
    signupSubtitle: '3 free generations. No card required.',
    signupSubmit: 'Create account',
    noAccount: 'No account yet?',
    haveAccount: 'Already have an account?',
    forgotLink: 'Forgot password?',
    forgotTitle: 'Reset your password',
    forgotText: 'Enter your email and we’ll send you a reset link.',
    forgotSubmit: 'Send reset link',
    forgotSent: 'If an account exists for that email, a reset link is on its way. It is valid for 1 hour.',
    resetTitle: 'Choose a new password',
    resetSubmit: 'Save password',
    legal: 'By signing up you agree to the Terms and Privacy Policy.',
    errors: {
      invalidCredentials: 'Wrong email or password.',
      emailTaken: 'An account with this email already exists.',
      invalidEmail: 'Enter a valid email address.',
      weakPassword: 'Password must be at least 8 characters.',
      resetInvalid: 'This reset link is invalid or has expired.',
      tooMany: 'Too many attempts. Please wait a few minutes and try again.',
    },
    resetEmailSubject: 'Reset your Hookcut password',
    resetEmailBody: (url: string) =>
      `Someone (hopefully you) asked to reset your Hookcut password.\n\nOpen this link to choose a new one (valid for 1 hour):\n${url}\n\nIf it wasn't you, just ignore this email.`,
  },
  app: {
    newTitle: 'New scripts',
    usage: (used: number, limit: number) => `${used} of ${limit} generations used this period`,
    upgrade: 'Upgrade',
    titleLabel: 'Project name',
    titlePlaceholder: 'e.g. Podcast #42 with Anna',
    transcriptLabel: 'Transcript',
    transcriptPlaceholder: '[0:00] Paste the transcript here — with timecodes if you have them…\n\nOr upload an .srt / .vtt / .txt file.',
    upload: 'Upload .srt / .vtt / .txt',
    charCount: (chars: string, mins: string, credits: string, limit: string) =>
      `${chars} characters · ~${mins} min of video · generations: ${credits} · plan limit ${limit}`,
    countLabel: 'Number of Reels',
    languageLabel: 'Script language',
    platformLabel: 'Platform',
    toneLabel: 'Tone',
    ctaLabel: 'Call to action',
    ctaDetailLabel: 'CTA details',
    ctaDetailPlaceholder: 'e.g. keyword GUIDE · link to the course · free consultation',
    contextLabel: 'About the speaker and audience',
    contextPlaceholder: 'Optional. Who is speaking, who the audience is, what you sell — helps sharpen hooks and the CTA.',
    submit: 'Write scripts',
    planLocked: (plan: string) => `More on ${plan}`,
    historyTitle: 'History',
    historyEmpty: 'Nothing here yet. Your first scripts are one transcript away.',
    reelsCount: (n: number) => `${n} ${n === 1 ? 'script' : 'scripts'}`,
    status: { pending: 'Writing…', done: 'Ready', failed: 'Failed' },
    errors: {
      quota: 'You’ve used all generations for this period. Upgrade to keep going.',
      tooShort: 'The transcript is too short — paste at least a few minutes of speech.',
      tooLong: (limit: string) => `The transcript is longer than your plan allows (${limit}). Upgrade or trim it.`,
      tooFast: 'Slow down a little — try again in a minute.',
      notEnough: (needed: number, left: number) =>
        `This recording counts as ${needed} generations (one per started hour), and you have ${left} left. Upgrade or split the transcript.`,
      generic: 'Something went wrong. Please try again.',
    },
    languages: {
      auto: 'Same as transcript',
      ru: 'Russian',
      en: 'English',
      es: 'Spanish',
      pt: 'Portuguese',
      de: 'German',
      fr: 'French',
      it: 'Italian',
      uk: 'Ukrainian',
      pl: 'Polish',
      tr: 'Turkish',
    },
    platforms: { reels: 'Instagram Reels', tiktok: 'TikTok', shorts: 'YouTube Shorts' },
    tones: { expert: 'Expert', energetic: 'Energetic', provocative: 'Provocative', storytelling: 'Storytelling' },
    ctaGoals: {
      subscribe: 'Follow the account',
      comment: 'Comment a keyword',
      link: 'Link in bio',
      dm: 'Send a DM',
      save: 'Save & share',
      custom: 'Custom',
    },
  },
  gen: {
    back: 'All scripts',
    pendingTitle: 'Writing your scripts…',
    pendingText: 'Usually takes 1–3 minutes. You can close this page — the result will be saved.',
    pendingSteps: ['Reading the transcript', 'Finding moments that work without context', 'Writing hooks', 'Timing the beats', 'Checking every script'],
    failedTitle: 'Generation failed',
    failedText: 'This attempt was not counted against your plan.',
    errorCodes: {
      refused: 'The model declined to process this transcript. Try removing the sensitive parts.',
      truncated: 'The result came out too long. Try fewer Reels per generation.',
      invalid_output: 'The model returned an unexpected result. Please try again.',
      rate_limited: 'The AI service is busy right now. Please try again in a minute.',
      overloaded: 'The AI service is overloaded. Please try again in a few minutes.',
      network: 'Couldn’t reach the AI service. Please try again.',
      config: 'The service is temporarily unavailable. We’re on it.',
      bad_request: 'This transcript couldn’t be processed. Try a shorter one.',
      interrupted: 'The generation was interrupted. Please try again.',
      unknown: 'Something went wrong. Please try again.',
    },
    retry: 'Try again',
    demo: 'Demo mode: this result was produced without AI. Set ANTHROPIC_API_KEY to get real scripts.',
    summary: 'Source',
    notes: 'Notes',
    momentsTitle: 'Source breakdown',
    momentsCols: { time: 'Timecode', what: 'What happens', type: 'Type', fit: 'Use?', why: 'Why' },
    quotesTitle: 'Lines for on-screen text',
    reel: (n: number) => `Reel ${n}`,
    seconds: (n: number) => `${n}s`,
    hookTypes: { expectation_gap: 'Expectation gap', number_with_stakes: 'Number with stakes', open_loop: 'Open loop' },
    momentTypes: { fact: 'Fact', insight: 'Insight', conflict: 'Conflict', number: 'Number', emotion: 'Emotion' },
    blocks: { hook: 'Hook', promise: 'Promise', body: 'Body', payoff: 'Payoff', cta: 'CTA' },
    cols: { time: 'Time', block: 'Beat', voice: 'Voice', onScreen: 'On screen', visual: 'Visual', source: 'Source' },
    altHooks: 'Alternative hooks',
    cover: 'Cover text',
    caption: 'Caption',
    hashtags: 'Hashtags',
    editing: 'Editing notes',
    loop: 'Loop',
    checks: 'Checklist',
    copy: 'Copy script',
    copyLink: 'Copy link',
    copied: 'Copied',
    yes: 'Yes',
    no: 'No',
    exportMd: 'Export .md',
    share: 'Share link',
    unshare: 'Turn off link',
    shareHint: 'Anyone with the link can view these scripts.',
    delete: 'Delete',
    deleteConfirm: 'Delete these scripts? This cannot be undone.',
    newOne: 'New scripts',
    madeWith: 'Made with Hookcut — turn any long video into Reels scripts.',
    tryFree: 'Try free',
    lint: {
      duration: (s: number) => `Runtime ${s}s (target 40–60s)`,
      hookTiming: 'Hook fits in the first 3 seconds',
      hookWords: (n: number) => `Hook on-screen text: ${n} words (target 4–7)`,
      bodyBeats: (n: number) => `${n} body beats (target 2–3)`,
      singleCta: 'Exactly one CTA',
      sourced: 'Body and payoff point to the source',
      order: 'Beats in order: hook → promise → body → payoff → CTA',
    },
  },
  account: {
    title: 'Account',
    plan: 'Plan',
    renews: (date: string) => `Renews on ${date}`,
    cancels: (date: string) => `Cancels on ${date}`,
    pastDue: 'Payment failed — update your card to keep your plan.',
    usage: 'Usage',
    manage: 'Manage billing',
    upgrade: 'Upgrade plan',
    upgraded: 'Payment received — your plan is active. Thank you!',
    checkoutPending: 'Payment received. Your plan will update in a few seconds.',
    danger: 'Danger zone',
    deleteText: 'Delete your account, all transcripts and scripts. Active subscriptions are cancelled.',
    deleteButton: 'Delete account',
    deleteConfirm: 'Type DELETE to confirm',
    passwordTitle: 'Password',
    passwordChanged: 'Password updated.',
    currentPassword: 'Current password',
    changePassword: 'Change password',
  },
  errors: {
    notFoundTitle: 'Page not found',
    notFoundText: 'The page you’re looking for doesn’t exist or was removed.',
    serverTitle: 'Something went wrong',
    serverText: 'We’ve been notified. Please try again in a minute.',
    home: 'Go home',
  },
};

export type Dict = typeof en;

const ru: Dict = {
  meta: {
    title: 'Hookcut — сценарии Reels из длинных видео',
    description:
      'Вставьте расшифровку подкаста, вебинара или интервью — получите сценарии Reels, TikTok и Shorts на 40–60 секунд: хуки, тайминг, текст на экран и тайм-коды исходника для монтажёра.',
  },
  nav: {
    features: 'Возможности',
    pricing: 'Тарифы',
    faq: 'Вопросы',
    login: 'Войти',
    signup: 'Начать бесплатно',
    dashboard: 'Мои сценарии',
    account: 'Аккаунт',
    logout: 'Выйти',
    admin: 'Админка',
  },
  footer: {
    tagline: 'Длинные видео на входе, короткие — на выходе.',
    terms: 'Условия',
    privacy: 'Конфиденциальность',
    language: 'Язык',
  },
  landing: {
    eyebrow: 'Для экспертов, подкастов и SMM-агентств',
    title: 'Одно длинное видео → пять готовых Reels',
    subtitle:
      'Вставьте расшифровку подкаста, эфира или интервью. Hookcut найдёт моменты, которые работают без контекста, и распишет сценарии на 40–60 секунд — хук, обещание, тело, payoff и CTA — с тайм-кодами исходника для монтажёра.',
    cta: 'Получить 3 сценария бесплатно',
    ctaNote: 'Без карты · .srt, .vtt или просто текст',
    sampleLabel: 'Пример результата',
    howTitle: 'Как это работает',
    how: [
      { title: 'Вставьте расшифровку', text: 'Из YouTube, Zoom, Riverside, Descript или Whisper. Подойдёт .srt, .vtt или текст с тайм-кодами.' },
      { title: 'Выберите формат', text: 'Сколько роликов, площадка, тон, язык и какое действие нужно от зрителя в конце.' },
      { title: 'Получите сценарии', text: 'Посекундный план с голосом, текстом на экран и пометками для монтажа. Копируйте, экспортируйте или отправьте ссылку.' },
    ],
    featuresTitle: 'Не «нейросеть написала текст», а сценарий, по которому можно монтировать',
    features: [
      { title: 'Разбор исходника', text: 'Таблица сильных моментов — факт, инсайт, конфликт, цифра или эмоция — и почему момент годится или нет.' },
      { title: 'Хуки, которые останавливают ленту', text: 'Три проверенных архетипа хука и ещё две альтернативы на каждый ролик — для A/B-тестов.' },
      { title: 'Жёсткий тайминг', text: 'Хук 0–3 с, обещание до 7 с, 2–3 блока тела, payoff и ровно один CTA. Каждый ролик — 40–60 секунд.' },
      { title: 'Тайм-коды для монтажёра', text: 'Каждый блок привязан к отрезку исходника. Никому не нужно пересматривать час видео.' },
      { title: 'Автопроверка', text: 'Хронометраж, длина хука, единственный CTA и структура тела проверяются автоматически.' },
      { title: 'Подпись и хэштеги', text: 'Текст поста и хэштеги под Reels, TikTok или Shorts на нужном языке.' },
    ],
    audienceTitle: 'Кому подходит',
    audience: [
      { title: 'Эксперты и блогеры', text: 'Каждый эфир превращается в неделю контента.' },
      { title: 'Подкасты', text: 'Нарезка из каждого выпуска без часов пересмотра.' },
      { title: 'SMM-агентства', text: 'Сценарии для клиентов за минуты, отправка ссылкой. White-label на тарифе Agency.' },
      { title: 'Монтажёры', text: 'ТЗ с тайм-кодами вместо «нарежь что-нибудь интересное».' },
    ],
    faqTitle: 'Вопросы',
    faq: [
      {
        q: 'Где взять расшифровку?',
        a: 'На YouTube: «…ещё» → «Показать текст видео» — скопируйте вместе с тайм-кодами. Zoom, Riverside, Descript, Premiere и CapCut выгружают .srt/.vtt. Подойдёт и текст без тайм-кодов — тогда в сценарии будут первые слова каждого фрагмента.',
      },
      {
        q: 'На каких языках работает?',
        a: 'Расшифровка — на любом языке. Сценарии — на языке исходника или на одном из 10 языков на выбор.',
      },
      {
        q: 'Чем это отличается от обычного чат-бота?',
        a: 'Hookcut работает по монтажной методике: жёсткий тайминг блоков, архетипы хуков, тайм-коды исходника, автопроверка и экспорт. Не нужно писать промпты и вычищать простыню текста.',
      },
      {
        q: 'Что считается одной генерацией?',
        a: 'Одна расшифровка до ~1 часа → до 5 сценариев за раз. Более длинные записи списываются по генерации за каждый начатый час. Неудачные генерации не списываются.',
      },
      { q: 'Можно отменить подписку?', a: 'Да, в один клик в аккаунте. Доступ сохраняется до конца оплаченного периода.' },
      {
        q: 'Кто видит мои расшифровки?',
        a: 'Только вы — пока вы сами не включите ссылку на конкретный результат. Удаление аккаунта удаляет всё.',
      },
    ],
    finalTitle: 'Reels на следующую неделю уже записаны',
    finalText: 'Они лежат в вашем последнем подкасте. Достаньте их за две минуты.',
  },
  pricing: {
    title: 'Простые тарифы',
    subtitle: 'Начните бесплатно. Переходите на платный, когда Hookcut окупится — обычно после первого клиента.',
    perMonth: '/мес',
    popular: 'Популярный',
    current: 'Текущий тариф',
    choose: (name: string) => `Выбрать ${name}`,
    startFree: 'Начать бесплатно',
    unavailable: 'Оплата ещё не подключена',
    footnote: '1 генерация = одно видео до ~1 часа → до 5 сценариев. Отмена в любой момент.',
    generations: (n: number) => `${n} генераций в месяц`,
    reels: (n: number) => `До ${n} сценариев из одного видео`,
    length: (chars: number) => `Видео до ${hoursOrMinutes(false, chars)}`,
    exportShare: 'Экспорт в Markdown и ссылки',
    whiteLabel: 'Ссылки без бренда Hookcut',
    planNames: { free: 'Free', creator: 'Creator', pro: 'Pro', agency: 'Agency' },
    planTaglines: {
      free: 'Попробуйте на последнем видео',
      creator: 'Для авторов, которые публикуются каждый день',
      pro: 'Для подкастов и команд',
      agency: 'Для агентств с десятками клиентов',
    },
  },
  auth: {
    email: 'Email',
    password: 'Пароль',
    passwordHint: 'Минимум 8 символов',
    newPassword: 'Новый пароль',
    loginTitle: 'С возвращением',
    loginSubmit: 'Войти',
    signupTitle: 'Создайте аккаунт',
    signupSubtitle: '3 генерации бесплатно. Без карты.',
    signupSubmit: 'Создать аккаунт',
    noAccount: 'Ещё нет аккаунта?',
    haveAccount: 'Уже есть аккаунт?',
    forgotLink: 'Забыли пароль?',
    forgotTitle: 'Восстановление пароля',
    forgotText: 'Укажите email — пришлём ссылку для сброса пароля.',
    forgotSubmit: 'Отправить ссылку',
    forgotSent: 'Если аккаунт с таким email существует, ссылка уже в пути. Она действует 1 час.',
    resetTitle: 'Новый пароль',
    resetSubmit: 'Сохранить пароль',
    legal: 'Регистрируясь, вы принимаете Условия и Политику конфиденциальности.',
    errors: {
      invalidCredentials: 'Неверный email или пароль.',
      emailTaken: 'Аккаунт с таким email уже существует.',
      invalidEmail: 'Введите корректный email.',
      weakPassword: 'Пароль должен быть не короче 8 символов.',
      resetInvalid: 'Ссылка недействительна или устарела.',
      tooMany: 'Слишком много попыток. Подождите несколько минут.',
    },
    resetEmailSubject: 'Сброс пароля Hookcut',
    resetEmailBody: (url: string) =>
      `Кто-то (надеемся, вы) запросил сброс пароля в Hookcut.\n\nОткройте ссылку, чтобы задать новый пароль (действует 1 час):\n${url}\n\nЕсли это были не вы — просто проигнорируйте письмо.`,
  },
  app: {
    newTitle: 'Новые сценарии',
    usage: (used: number, limit: number) => `Использовано ${used} из ${limit} генераций в этом периоде`,
    upgrade: 'Увеличить лимит',
    titleLabel: 'Название проекта',
    titlePlaceholder: 'Например, Подкаст #42 с Анной',
    transcriptLabel: 'Расшифровка',
    transcriptPlaceholder: '[0:00] Вставьте расшифровку — с тайм-кодами, если они есть…\n\nИли загрузите файл .srt / .vtt / .txt.',
    upload: 'Загрузить .srt / .vtt / .txt',
    charCount: (chars: string, mins: string, credits: string, limit: string) =>
      `${chars} символов · ~${mins} мин видео · генераций: ${credits} · лимит тарифа ${limit}`,
    countLabel: 'Сколько роликов',
    languageLabel: 'Язык сценариев',
    platformLabel: 'Площадка',
    toneLabel: 'Тон',
    ctaLabel: 'Призыв к действию',
    ctaDetailLabel: 'Детали CTA',
    ctaDetailPlaceholder: 'Например: кодовое слово ГАЙД · ссылка на курс · бесплатная консультация',
    contextLabel: 'О спикере и аудитории',
    contextPlaceholder: 'Необязательно. Кто говорит, кто аудитория, что вы продаёте — так хуки и CTA будут точнее.',
    submit: 'Написать сценарии',
    planLocked: (plan: string) => `Больше на ${plan}`,
    historyTitle: 'История',
    historyEmpty: 'Пока пусто. До первых сценариев — одна расшифровка.',
    reelsCount: (n: number) => `${n} ${plural(n, 'сценарий', 'сценария', 'сценариев')}`,
    status: { pending: 'Пишем…', done: 'Готово', failed: 'Ошибка' },
    errors: {
      quota: 'Генерации на этот период закончились. Перейдите на тариф выше, чтобы продолжить.',
      tooShort: 'Расшифровка слишком короткая — вставьте хотя бы несколько минут речи.',
      tooLong: (limit: string) => `Расшифровка длиннее, чем позволяет тариф (${limit}). Повысьте тариф или сократите текст.`,
      tooFast: 'Слишком часто — попробуйте через минуту.',
      notEnough: (needed: number, left: number) =>
        `Эта запись списывает ${needed} ${plural(needed, 'генерацию', 'генерации', 'генераций')} (по одной за каждый начатый час), а осталось ${left}. Повысьте тариф или разделите расшифровку.`,
      generic: 'Что-то пошло не так. Попробуйте ещё раз.',
    },
    languages: {
      auto: 'Как в расшифровке',
      ru: 'Русский',
      en: 'Английский',
      es: 'Испанский',
      pt: 'Португальский',
      de: 'Немецкий',
      fr: 'Французский',
      it: 'Итальянский',
      uk: 'Украинский',
      pl: 'Польский',
      tr: 'Турецкий',
    },
    platforms: { reels: 'Instagram Reels', tiktok: 'TikTok', shorts: 'YouTube Shorts' },
    tones: { expert: 'Экспертный', energetic: 'Энергичный', provocative: 'Дерзкий', storytelling: 'Сторителлинг' },
    ctaGoals: {
      subscribe: 'Подписаться',
      comment: 'Комментарий с кодовым словом',
      link: 'Ссылка в профиле',
      dm: 'Написать в директ',
      save: 'Сохранить и переслать',
      custom: 'Свой вариант',
    },
  },
  gen: {
    back: 'Все сценарии',
    pendingTitle: 'Пишем сценарии…',
    pendingText: 'Обычно это 1–3 минуты. Страницу можно закрыть — результат сохранится.',
    pendingSteps: ['Читаем расшифровку', 'Ищем моменты, понятные без контекста', 'Пишем хуки', 'Расставляем тайминг', 'Проверяем каждый сценарий'],
    failedTitle: 'Не получилось',
    failedText: 'Эта попытка не списана с лимита.',
    errorCodes: {
      refused: 'Модель отказалась обрабатывать эту расшифровку. Попробуйте убрать чувствительные фрагменты.',
      truncated: 'Результат получился слишком длинным. Попробуйте меньше роликов за раз.',
      invalid_output: 'Модель вернула неожиданный результат. Попробуйте ещё раз.',
      rate_limited: 'Сервис ИИ сейчас занят. Попробуйте через минуту.',
      overloaded: 'Сервис ИИ перегружен. Попробуйте через несколько минут.',
      network: 'Не удалось связаться с сервисом ИИ. Попробуйте ещё раз.',
      config: 'Сервис временно недоступен. Мы уже чиним.',
      bad_request: 'Не удалось обработать эту расшифровку. Попробуйте покороче.',
      interrupted: 'Генерация прервалась. Попробуйте ещё раз.',
      unknown: 'Что-то пошло не так. Попробуйте ещё раз.',
    },
    retry: 'Попробовать снова',
    demo: 'Демо-режим: результат собран без ИИ. Укажите ANTHROPIC_API_KEY, чтобы получать настоящие сценарии.',
    summary: 'Исходник',
    notes: 'Заметки',
    momentsTitle: 'Разбор исходника',
    momentsCols: { time: 'Тайм-код', what: 'Что происходит', type: 'Тип', fit: 'Брать?', why: 'Почему' },
    quotesTitle: 'Фразы для текста на экране',
    reel: (n: number) => `Ролик ${n}`,
    seconds: (n: number) => `${n} с`,
    hookTypes: { expectation_gap: 'Разрыв ожидания', number_with_stakes: 'Цифра с ценой', open_loop: 'Незакрытая петля' },
    momentTypes: { fact: 'Факт', insight: 'Инсайт', conflict: 'Конфликт', number: 'Цифра', emotion: 'Эмоция' },
    blocks: { hook: 'Хук', promise: 'Обещание', body: 'Тело', payoff: 'Payoff', cta: 'CTA' },
    cols: { time: 'Время', block: 'Блок', voice: 'Голос', onScreen: 'На экране', visual: 'Кадр', source: 'Исходник' },
    altHooks: 'Альтернативные хуки',
    cover: 'Текст обложки',
    caption: 'Подпись',
    hashtags: 'Хэштеги',
    editing: 'Монтаж',
    loop: 'Луп',
    checks: 'Чек-лист',
    copy: 'Скопировать сценарий',
    copyLink: 'Скопировать ссылку',
    copied: 'Скопировано',
    yes: 'Да',
    no: 'Нет',
    exportMd: 'Экспорт .md',
    share: 'Ссылка для просмотра',
    unshare: 'Выключить ссылку',
    shareHint: 'Любой, у кого есть ссылка, может посмотреть эти сценарии.',
    delete: 'Удалить',
    deleteConfirm: 'Удалить эти сценарии? Это нельзя отменить.',
    newOne: 'Новые сценарии',
    madeWith: 'Сделано в Hookcut — сценарии Reels из любого длинного видео.',
    tryFree: 'Попробовать бесплатно',
    lint: {
      duration: (s: number) => `Хронометраж ${s} с (нужно 40–60 с)`,
      hookTiming: 'Хук укладывается в первые 3 секунды',
      hookWords: (n: number) => `Текст хука на экране: ${n} ${plural(n, 'слово', 'слова', 'слов')} (нужно 4–7)`,
      bodyBeats: (n: number) => `Блоков в теле: ${n} (нужно 2–3)`,
      singleCta: 'Ровно один CTA',
      sourced: 'Тело и payoff привязаны к исходнику',
      order: 'Порядок: хук → обещание → тело → payoff → CTA',
    },
  },
  account: {
    title: 'Аккаунт',
    plan: 'Тариф',
    renews: (date: string) => `Продлится ${date}`,
    cancels: (date: string) => `Отключится ${date}`,
    pastDue: 'Платёж не прошёл — обновите карту, чтобы сохранить тариф.',
    usage: 'Использование',
    manage: 'Управление оплатой',
    upgrade: 'Сменить тариф',
    upgraded: 'Оплата прошла — тариф активен. Спасибо!',
    checkoutPending: 'Оплата получена. Тариф обновится через несколько секунд.',
    danger: 'Опасная зона',
    deleteText: 'Удалить аккаунт, все расшифровки и сценарии. Активная подписка будет отменена.',
    deleteButton: 'Удалить аккаунт',
    deleteConfirm: 'Введите DELETE для подтверждения',
    passwordTitle: 'Пароль',
    passwordChanged: 'Пароль обновлён.',
    currentPassword: 'Текущий пароль',
    changePassword: 'Сменить пароль',
  },
  errors: {
    notFoundTitle: 'Страница не найдена',
    notFoundText: 'Такой страницы нет или она была удалена.',
    serverTitle: 'Что-то пошло не так',
    serverText: 'Мы уже знаем о проблеме. Попробуйте через минуту.',
    home: 'На главную',
  },
};

function plural(n: number, one: string, few: string, many: string): string {
  const mod10 = n % 10;
  const mod100 = n % 100;
  if (mod10 === 1 && mod100 !== 11) return one;
  if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return few;
  return many;
}

const DICTS: Record<Locale, Dict> = { en, ru };

export function dict(locale: Locale): Dict {
  return DICTS[locale];
}

export function isLocale(value: unknown): value is Locale {
  return value === 'en' || value === 'ru';
}

const RU_FAMILY = ['ru', 'uk', 'be', 'kk', 'ky', 'uz', 'tg', 'hy', 'az', 'ka'];

/** Picks the UI locale from an Accept-Language header. */
export function localeFromHeader(header: string | undefined): Locale {
  if (!header) return 'en';
  const ranked = header
    .split(',')
    .map((part) => {
      const [tag = '', ...params] = part.trim().split(';');
      const q = params.find((p) => p.trim().startsWith('q='));
      return { lang: tag.toLowerCase().split('-')[0] ?? '', q: q ? Number(q.trim().slice(2)) : 1 };
    })
    .filter((x) => x.lang && !Number.isNaN(x.q))
    .sort((a, b) => b.q - a.q);
  for (const { lang } of ranked) {
    if (lang === 'en') return 'en';
    if (RU_FAMILY.includes(lang)) return 'ru';
  }
  return 'en';
}

export function formatNumber(locale: Locale, n: number): string {
  return new Intl.NumberFormat(locale === 'ru' ? 'ru-RU' : 'en-US').format(n);
}

export function formatDate(locale: Locale, unixSeconds: number): string {
  return new Intl.DateTimeFormat(locale === 'ru' ? 'ru-RU' : 'en-US', { day: 'numeric', month: 'long', year: 'numeric' }).format(
    new Date(unixSeconds * 1000),
  );
}

export function formatLimitChars(locale: Locale, chars: number): string {
  return hoursOrMinutes(locale === 'en', chars);
}

(() => {
  'use strict';

  const $ = (selector) => document.querySelector(selector);
  const KEY = 'svidanie:admin';

  const ERRORS = {
    wrong_password: 'Неверный пароль',
    admin_not_configured: 'На сервере не задан пароль.\nVercel → проект → Settings → Environment Variables → добавь ADMIN_PASSWORD, потом Deployments → ⋯ → Redeploy.',
    storage_not_configured: 'Не подключено хранилище для ответов.\nVercel → проект → Storage → Create → Blob → Connect, потом Deployments → ⋯ → Redeploy.',
    storage_failed: 'Хранилище ответило ошибкой',
    network: 'Нет связи с сервером, попробуй ещё раз',
  };

  const loginBox = $('#login');
  const loginForm = $('#login-form');
  const passwordInput = $('#password');
  const loginMessage = $('#login-message');
  const appBox = $('#app');
  const appMessage = $('#app-message');
  const list = $('#list');
  const stats = $('#stats');
  const refreshBtn = $('#refresh');
  const shareName = $('#share-name');
  const shareLink = $('#share-link');
  const shareCopy = $('#share-copy');
  const testLink = $('#test-link');

  let password = remember();
  let loading = false;
  let lastLoad = 0;

  // ---------- Сервер ----------

  async function api(payload) {
    try {
      const res = await fetch('/api/admin', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ password, ...payload }),
        cache: 'no-store',
      });
      return await res.json();
    } catch (_) {
      return { ok: false, error: 'network' };
    }
  }

  function describe(data) {
    const text = ERRORS[data.error] || 'Что-то пошло не так';
    return data.message ? `${text}: ${data.message}` : text;
  }

  async function refresh() {
    if (!password || loading) return;
    loading = true;
    refreshBtn.disabled = true;
    const data = await api({ action: 'list' });
    loading = false;
    refreshBtn.disabled = false;
    lastLoad = Date.now();

    if (data.ok) {
      appMessage.textContent = '';
      render(data.visitors);
    } else if (data.error === 'wrong_password' || data.error === 'admin_not_configured') {
      logout(describe(data));
    } else {
      appMessage.textContent = describe(data);
    }
  }

  // ---------- Вход ----------

  loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const submit = loginForm.querySelector('button');
    password = passwordInput.value;
    loginMessage.textContent = '';
    submit.disabled = true;
    const data = await api({ action: 'list' });
    submit.disabled = false;

    if (data.ok || data.error === 'storage_not_configured' || data.error === 'storage_failed') {
      store(password);
      passwordInput.value = '';
      showApp();
      if (data.ok) render(data.visitors);
      else appMessage.textContent = describe(data);
      lastLoad = Date.now();
      return;
    }
    password = null;
    loginMessage.textContent = describe(data);
  });

  $('#logout').addEventListener('click', () => logout());
  refreshBtn.addEventListener('click', () => refresh());

  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible' && password && Date.now() - lastLoad > 10000) refresh();
  });

  function showApp() {
    loginBox.hidden = true;
    appBox.hidden = false;
    updateShare();
  }

  function logout(message = '') {
    password = null;
    forget();
    appBox.hidden = true;
    loginBox.hidden = false;
    loginMessage.textContent = message;
    passwordInput.focus();
  }

  // ---------- Ответы ----------

  function render(visitors) {
    const real = visitors.filter((v) => !v.test);
    stats.replaceChildren(
      stat(real.length, 'открыли'),
      stat(real.filter((v) => v.yesAt).length, 'сказали «да»'),
      stat(real.filter((v) => v.answer).length, 'выбрали дату'),
    );

    if (!visitors.length) {
      list.replaceChildren(el('div', 'panel empty', [
        el('div', 'empty-emoji', '🙈'),
        el('p', null, 'Пока пусто. Отправь ссылку — ответы появятся здесь.'),
      ]));
      return;
    }
    list.replaceChildren(...visitors.map(card));
  }

  function stat(value, label) {
    return el('div', 'stat', [el('b', null, String(value)), el('span', null, label)]);
  }

  function card(v) {
    const status = v.answer
      ? '💌 Свидание назначено'
      : v.yesAt
        ? '💘 Ответ «Да», дату пока выбирают'
        : '👀 Открыто, ответа пока нет';

    const head = el('div', 'item-head', [el('span', 'status', status)]);
    if (v.name) head.append(el('span', 'badge', v.name));
    if (v.test) head.append(el('span', 'badge badge-test', 'ТЕСТ'));
    const node = el('article', v.answer ? 'item is-final' : 'item', [head]);

    if (v.answer) {
      const { date, time, activities, wish } = v.answer;
      const facts = el('div', 'facts', [
        fact('🗓', `${capitalize(formatDay(date))} · ${time === 'any' ? 'время на твой выбор' : time}`),
        fact('✨', activities.length ? activities.join(', ') : '—'),
      ]);
      if (wish) facts.append(fact('💬', wish));
      node.append(facts);
    }

    const meta = [];
    if (v.openedAt) meta.push(`👀 Открыто ${formatStamp(v.openedAt)}${v.opens > 1 ? ` (всего ${v.opens} ${plural(v.opens, ['раз', 'раза', 'раз'])})` : ''}`);
    if (v.yesAt) meta.push(`💘 «Да» ${formatStamp(v.yesAt)}`);
    if (v.finalAt) meta.push(`💌 Ответ ${formatStamp(v.finalAt)}${v.finals > 1 ? ` (меняли ${v.finals - 1} ${plural(v.finals - 1, ['раз', 'раза', 'раз'])})` : ''}`);
    if (v.yesAt || v.answer) meta.push(`🙈 Попыток нажать «Нет»: ${v.attempts}${v.viaNo ? ' — кнопка сдалась 😂' : ''}`);

    const remove = el('button', 'btn btn-danger', 'Удалить');
    remove.type = 'button';
    remove.addEventListener('click', () => removeVisitor(v));
    node.append(el('p', 'meta', meta.join('\n')), el('div', 'item-actions', [remove]));
    return node;
  }

  function fact(icon, text) {
    return el('div', 'fact', [el('span', 'fact-ico', icon), el('span', null, text)]);
  }

  async function removeVisitor(v) {
    const what = v.name ? `ответ «${v.name}»` : 'этот ответ';
    if (!window.confirm(`Удалить ${what}? Вернуть не получится.`)) return;
    const data = await api({ action: 'delete', id: v.id });
    if (!data.ok) {
      appMessage.textContent = describe(data);
      return;
    }
    refresh();
  }

  // ---------- Ссылка для отправки ----------

  function updateShare() {
    const name = shareName.value.trim();
    shareLink.value = `${location.origin}/${name ? `?name=${name}` : ''}`;
    testLink.href = `${location.origin}/?test${name ? `&name=${encodeURIComponent(name)}` : ''}`;
  }

  shareName.addEventListener('input', updateShare);

  shareCopy.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(shareLink.value);
    } catch (_) {
      shareLink.select();
      document.execCommand('copy');
    }
    shareCopy.textContent = 'Готово ✓';
    setTimeout(() => { shareCopy.textContent = 'Скопировать'; }, 1500);
  });

  // ---------- Утилиты ----------

  function el(tag, className, children) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (typeof children === 'string') node.textContent = children;
    else if (Array.isArray(children)) node.append(...children);
    return node;
  }

  function formatDay(iso) {
    const [y, m, d] = iso.split('-').map(Number);
    const date = new Date(y, m - 1, d);
    const options = { weekday: 'long', day: 'numeric', month: 'long' };
    if (y !== new Date().getFullYear()) options.year = 'numeric';
    return date.toLocaleDateString('ru-RU', options).replace(/\s*г\.$/, '');
  }

  function formatStamp(ms) {
    const date = new Date(ms);
    const time = date.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
    if (date.toDateString() === new Date().toDateString()) return `сегодня в ${time}`;
    return `${date.toLocaleDateString('ru-RU', { day: 'numeric', month: 'short' })} в ${time}`;
  }

  function plural(n, [one, few, many]) {
    const mod10 = n % 10;
    const mod100 = n % 100;
    if (mod10 === 1 && mod100 !== 11) return one;
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return few;
    return many;
  }

  function capitalize(text) {
    return text ? text[0].toUpperCase() + text.slice(1) : text;
  }

  function remember() {
    try { return localStorage.getItem(KEY); } catch (_) { return null; }
  }

  function store(value) {
    try { localStorage.setItem(KEY, value); } catch (_) { /* приватный режим */ }
  }

  function forget() {
    try { localStorage.removeItem(KEY); } catch (_) { /* приватный режим */ }
  }

  // ---------- Старт ----------

  if (password) {
    showApp();
    refresh();
  } else {
    loginBox.hidden = false;
    passwordInput.focus();
  }
})();

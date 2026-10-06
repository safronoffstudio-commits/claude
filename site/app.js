(() => {
  'use strict';

  const $ = (selector) => document.querySelector(selector);

  // ?name=Аня — обращение по имени, ?test — тестовый режим (сообщения помечаются «ТЕСТ»).
  const params = new URLSearchParams(location.search);
  const TEST = params.has('test');
  const NAME = formatName(params.get('name') || params.get('to') || '');
  const STORE_KEY = 'svidanie:v1';

  // Анимированные эмодзи Google Noto; пока картинка грузится, виден обычный эмодзи.
  const EMOJI = {
    plead: ['1f97a', '🥺'],
    flushed: ['1f633', '😳'],
    sad: ['1f622', '😢'],
    sob: ['1f62d', '😭'],
    huff: ['1f624', '😤'],
    love: ['1f970', '🥰'],
    starry: ['1f929', '🤩'],
    think: ['1f914', '🤔'],
    hearts: ['1f60d', '😍'],
  };
  const MASCOT_FOR = { ask: 'plead', yay: 'love', date: 'starry', plans: 'think', done: 'hearts' };
  const emojiUrl = (code) => `https://fonts.gstatic.com/s/e/notoemoji/latest/${code}/512.webp`;

  const NO_LABELS = [
    'Нет', 'Точно нет?', 'Подумай ещё 🥺', 'Ну пожа-а-алуйста', 'Не-а 😏', 'Мимо!',
    'Даже не пытайся', 'Кнопка сломалась', 'Ты серьёзно?', 'Жми «Да» 👉', 'Ну хватит 😭', 'Я всё вижу 👀',
  ];
  const HINTS = [
    [1, 'Ой, кнопка убежала 🙃'],
    [2, 'Кажется, кнопка стесняется'],
    [3, 'Кнопка «Да» всё больше… намёк? 😏'],
    [5, 'У кнопки «Нет» лапки 🐾'],
    [7, 'Это уже спорт 😂'],
    [9, 'Я всё записываю: попыток — {n} 📝'],
    [12, 'Сдавайся, это бесполезно 💖'],
  ];
  const SURRENDER_AT = 15;

  const TIMES = ['12:00', '14:00', '16:00', '17:00', '18:00', '19:00', '20:00', '21:00'];
  const ACTIVITIES = [
    ['☕', 'Кофе'], ['🍝', 'Ресторан'], ['🍣', 'Суши'], ['🍕', 'Пицца'],
    ['🎬', 'Кино'], ['🌙', 'Прогулка'], ['🎳', 'Боулинг'], ['🎤', 'Караоке'],
    ['🖼️', 'Выставка'], ['🎡', 'Парк'], ['🍰', 'Десерты'], ['🎁', 'Сюрприз'],
  ];
  const MONTHS = ['Январь', 'Февраль', 'Март', 'Апрель', 'Май', 'Июнь', 'Июль', 'Август', 'Сентябрь', 'Октябрь', 'Ноябрь', 'Декабрь'];

  const state = {
    screen: 'ask',
    attempts: 0,
    surrendered: false,
    viaNo: false,
    date: null,
    timeChoice: null,
    time: null,
    activities: [],
    wish: '',
  };

  const card = $('#card');
  const choices = $('#choices');
  const yesBtn = $('#btn-yes');
  const noBtn = $('#btn-no');
  const hint = $('#ask-hint');

  // ---------- Уведомления ----------

  async function notify(event, data = {}, retries = 0) {
    const payload = { event, attempts: state.attempts, ...data };
    if (NAME) payload.name = NAME;
    if (TEST) payload.test = true;
    const body = JSON.stringify(payload);

    for (let i = 0; ; i++) {
      try {
        const res = await fetch('/api/notify', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body,
          keepalive: true,
        });
        if (res.ok) return true;
        if (res.status >= 400 && res.status < 500 && res.status !== 429) return false;
      } catch (_) {
        // сеть моргнула — попробуем ещё раз
      }
      if (i >= retries) return false;
      await sleep(700 * 2 ** i);
    }
  }

  const store = {
    get() {
      try { return JSON.parse(localStorage.getItem(STORE_KEY)) || null; } catch (_) { return null; }
    },
    set(value) {
      if (TEST) return;
      try { localStorage.setItem(STORE_KEY, JSON.stringify(value)); } catch (_) { /* приватный режим */ }
    },
  };

  // ---------- Маскот ----------

  const mascot = $('#mascot');
  const mascotFloat = $('#mascot-float');
  const mascotChar = $('#mascot-char');
  let mascotKey = null;
  let mascotTimer = 0;

  function setMascot(key) {
    if (key === mascotKey) return;
    mascotKey = key;
    const [code, char] = EMOJI[key];
    mascotChar.textContent = char;
    mascot.classList.remove('pop');
    void mascot.offsetWidth;
    mascot.classList.add('pop');

    const img = new Image();
    img.className = 'mascot-img';
    img.alt = '';
    img.draggable = false;
    clearTimeout(mascotTimer);
    // Если новая картинка не успела загрузиться, лучше показать обычный эмодзи, чем старую эмоцию.
    mascotTimer = setTimeout(() => {
      if (mascotKey === key) mascot.classList.remove('has-img');
    }, 250);
    img.onload = () => {
      if (mascotKey !== key) return;
      clearTimeout(mascotTimer);
      mascotFloat.querySelectorAll('.mascot-img').forEach((el) => el.remove());
      mascotFloat.append(img);
      mascot.classList.add('has-img');
    };
    img.onerror = () => {
      if (mascotKey === key) mascot.classList.remove('has-img');
    };
    img.src = emojiUrl(code);
  }

  function preload(...keys) {
    keys.forEach((key) => { new Image().src = emojiUrl(EMOJI[key][0]); });
  }

  // ---------- Экраны ----------

  const progress = $('#progress');
  const progressLabel = $('#progress-label');
  const progressBar = $('#progress-bar');

  function show(name, { focus = true } = {}) {
    state.screen = name;
    document.querySelectorAll('.screen').forEach((el) => {
      el.classList.toggle('is-active', el.id === `screen-${name}`);
    });
    card.dataset.screen = name;

    const step = { date: 1, plans: 2 }[name];
    progress.hidden = !step;
    if (step) {
      progressLabel.textContent = `Шаг ${step} из 2`;
      progressBar.style.width = `${step * 50}%`;
    }

    if (name === 'date') renderCalendar();
    setMascot(MASCOT_FOR[name]);
    if (name === 'yay') preload('starry');
    if (name === 'date') preload('think');
    if (name === 'plans') preload('hearts');

    if (focus) {
      window.scrollTo({ top: 0, behavior: 'smooth' });
      const heading = document.querySelector(`#screen-${name} .title`);
      if (heading) {
        heading.tabIndex = -1;
        heading.focus({ preventScroll: true });
      }
    }
  }

  // ---------- Вопрос: «Да» и убегающее «Нет» ----------

  let noFlying = false;
  let noPos = null;
  let lastFlee = 0;
  let placeholder = null;

  function reactionFor(n) {
    if (n === 0) return 'plead';
    if (n <= 2) return 'flushed';
    if (n <= 4) return 'sad';
    if (n <= 8) return 'sob';
    return 'huff';
  }

  function detachNo() {
    const r = noBtn.getBoundingClientRect();
    placeholder = document.createElement('span');
    placeholder.className = 'no-placeholder';
    placeholder.style.width = `${r.width}px`;
    placeholder.style.height = `${r.height}px`;
    noBtn.replaceWith(placeholder);
    // Переносим в <body>: у карточки есть backdrop-filter, и position: fixed внутри неё считался бы от карточки.
    document.body.append(noBtn);
    noBtn.classList.add('is-flying');
    noBtn.style.left = `${r.left}px`;
    noBtn.style.top = `${r.top}px`;
    noFlying = true;
    void noBtn.offsetWidth;
    // Место «Нет» плавно схлопывается, и «Да» выезжает в центр.
    placeholder.style.width = '0px';
    choices.classList.add('is-solo');
  }

  function fleeFrom(px, py) {
    if (state.surrendered || state.screen !== 'ask') return;
    const now = performance.now();
    if (now - lastFlee < 160) return;
    lastFlee = now;

    if (!noFlying) detachNo();
    state.attempts += 1;
    if (state.attempts === 1) preload('flushed', 'sad', 'sob', 'huff');
    if (state.attempts >= SURRENDER_AT) {
      surrender();
      return;
    }

    noBtn.textContent = NO_LABELS[1 + ((state.attempts - 1) % (NO_LABELS.length - 1))];
    updateHint();
    growYes();
    setMascot(reactionFor(state.attempts));
    placeNo(px, py);
  }

  // Где «Да» окажется после анимации: по центру ряда и уже увеличенной.
  function yesZone() {
    const row = choices.getBoundingClientRect();
    const r = yesBtn.getBoundingClientRect();
    const grow = Number(yesBtn.style.getPropertyValue('--grow')) || 1;
    const cx = row.left + row.width / 2;
    const cy = r.top + r.height / 2;
    const hw = (yesBtn.offsetWidth * grow * 1.08) / 2 + 14;
    const hh = (yesBtn.offsetHeight * grow * 1.08) / 2 + 14;
    return { left: cx - hw, right: cx + hw, top: cy - hh, bottom: cy + hh };
  }

  function placeNo(px, py) {
    const vw = document.documentElement.clientWidth;
    const vh = window.innerHeight;
    const w = noBtn.offsetWidth;
    const h = noBtn.offsetHeight;
    const pad = 12;
    const maxX = Math.max(pad, vw - w - pad);
    const maxY = Math.max(pad, vh - h - pad);
    const yes = yesZone();

    let best = { x: pad, y: pad };
    let bestDist = -1;
    for (let i = 0; i < 60; i++) {
      const x = pad + Math.random() * (maxX - pad);
      const y = pad + Math.random() * (maxY - pad);
      const hitsYes = x < yes.right && x + w > yes.left && y < yes.bottom && y + h > yes.top;
      if (hitsYes) continue;
      const dist = Math.hypot(x + w / 2 - px, y + h / 2 - py);
      // Достаточно далеко, чтобы не достать, но не всегда в самый дальний угол.
      if (dist > 170 && dist < 520) {
        best = { x, y };
        break;
      }
      if (dist > bestDist) {
        bestDist = dist;
        best = { x, y };
      }
    }

    moveNo(best.x, best.y);
    noBtn.style.setProperty('--tilt', `${Math.round(Math.random() * 16 - 8)}deg`);
  }

  function moveNo(x, y) {
    noPos = { x: Math.round(x), y: Math.round(y) };
    noBtn.style.left = `${noPos.x}px`;
    noBtn.style.top = `${noPos.y}px`;
  }

  function updateHint() {
    let text = null;
    for (const [n, t] of HINTS) if (state.attempts >= n) text = t;
    if (text) hint.textContent = text.replace('{n}', state.attempts);
  }

  function growYes() {
    const max = window.innerWidth < 480 ? 1.55 : 1.9;
    yesBtn.style.setProperty('--grow', Math.min(1 + state.attempts * 0.09, max).toFixed(3));
  }

  // Кнопка «Нет» сдаётся: исчезает там, где была, и появляется рядом с «Да» уже как «Ладно, да».
  function surrender() {
    state.surrendered = true;
    hint.textContent = 'Кнопка «Нет» сдалась первой 🙈';
    setMascot('plead');
    yesBtn.style.setProperty('--grow', '1');
    noBtn.classList.add('is-gone');
    setTimeout(() => {
      if (state.screen !== 'ask' || !noBtn.isConnected) return;
      noBtn.textContent = 'Ладно, да 💖';
      noBtn.classList.remove('is-flying', 'is-gone');
      noBtn.classList.add('is-surrendered');
      noBtn.style.removeProperty('left');
      noBtn.style.removeProperty('top');
      noBtn.style.removeProperty('--tilt');
      placeholder.replaceWith(noBtn);
      placeholder = null;
      noFlying = false;
      choices.classList.remove('is-solo');
    }, 320);
  }

  function sayYes(viaNo) {
    if (state.screen !== 'ask') return;
    state.viaNo = viaNo;
    noBtn.classList.add('is-gone');
    setTimeout(() => noBtn.remove(), 400);
    show('yay');
    celebrate();
    store.set({ stage: 'yes', attempts: state.attempts, viaNo });
    notify('yes', { viaNo }, 2);
  }

  noBtn.addEventListener('pointerdown', (e) => {
    if (state.surrendered) return;
    e.preventDefault();
    fleeFrom(e.clientX, e.clientY);
  });
  // Без этого на телефонах после касания всё равно прилетел бы click.
  noBtn.addEventListener('touchstart', (e) => {
    if (!state.surrendered) e.preventDefault();
  }, { passive: false });
  noBtn.addEventListener('mouseenter', (e) => fleeFrom(e.clientX, e.clientY));
  noBtn.addEventListener('click', (e) => {
    e.preventDefault();
    if (state.surrendered) {
      sayYes(true);
      return;
    }
    const r = noBtn.getBoundingClientRect();
    fleeFrom(r.left + r.width / 2, r.top + r.height / 2);
  });
  noBtn.addEventListener('contextmenu', (e) => e.preventDefault());

  // Мышь: убегаем заранее, когда курсор только подбирается к кнопке.
  document.addEventListener('pointermove', (e) => {
    if (e.pointerType !== 'mouse' || state.screen !== 'ask' || state.surrendered) return;
    const r = noBtn.getBoundingClientRect();
    const dx = Math.max(r.left - e.clientX, 0, e.clientX - r.right);
    const dy = Math.max(r.top - e.clientY, 0, e.clientY - r.bottom);
    if (Math.hypot(dx, dy) < 36) fleeFrom(e.clientX, e.clientY);
  }, { passive: true });

  window.addEventListener('resize', () => {
    if (!noFlying || !noPos || state.surrendered || state.screen !== 'ask') return;
    const vw = document.documentElement.clientWidth;
    const vh = window.innerHeight;
    moveNo(
      Math.min(Math.max(12, noPos.x), vw - noBtn.offsetWidth - 12),
      Math.min(Math.max(12, noPos.y), vh - noBtn.offsetHeight - 12),
    );
  });

  yesBtn.addEventListener('click', () => sayYes(false));
  yesBtn.addEventListener('pointerenter', (e) => {
    if (e.pointerType === 'mouse' && state.screen === 'ask') setMascot('love');
  });
  yesBtn.addEventListener('pointerleave', (e) => {
    if (e.pointerType === 'mouse' && state.screen === 'ask') setMascot(state.surrendered ? 'plead' : reactionFor(state.attempts));
  });

  // ---------- Конфетти ----------

  let heartShape = null;
  function confettiShapes() {
    if (!heartShape && window.confetti && typeof window.confetti.shapeFromPath === 'function') {
      try {
        heartShape = window.confetti.shapeFromPath({
          path: 'M167 72c19,-38 37,-56 75,-56 42,0 76,33 76,75 0,76 -76,151 -151,227 -76,-76 -151,-151 -151,-227 0,-42 33,-75 75,-75 38,0 57,18 76,56z',
        });
      } catch (_) {
        heartShape = null;
      }
    }
    return heartShape ? [heartShape, heartShape, 'circle'] : ['circle'];
  }

  function burst(options) {
    if (typeof window.confetti !== 'function') return;
    window.confetti({
      colors: ['#ff4d8d', '#ff85b3', '#ffc2d9', '#ffffff', '#c9a7ff', '#ffd166'],
      shapes: confettiShapes(),
      disableForReducedMotion: true,
      zIndex: 100,
      ...options,
    });
  }

  function celebrate() {
    burst({ particleCount: 130, spread: 85, startVelocity: 50, scalar: 1.15, origin: { y: 0.62 } });
    setTimeout(() => {
      burst({ particleCount: 60, angle: 60, spread: 60, origin: { x: 0, y: 0.75 } });
      burst({ particleCount: 60, angle: 120, spread: 60, origin: { x: 1, y: 0.75 } });
    }, 260);
  }

  function celebrateSoft() {
    burst({ particleCount: 90, spread: 140, startVelocity: 30, gravity: 0.7, scalar: 1.2, ticks: 260, origin: { y: 0.25 } });
  }

  // ---------- Дата и время ----------

  const today = startOfDay(new Date());
  const lastDay = new Date(today.getFullYear(), today.getMonth(), today.getDate() + 180);
  let view = new Date(today.getFullYear(), today.getMonth(), 1);

  const calMonth = $('#cal-month');
  const calGrid = $('#cal-grid');
  const calPrev = $('#cal-prev');
  const calNext = $('#cal-next');
  const timeChips = $('#time-chips');
  const timeCustom = $('#time-custom');
  const datePicked = $('#date-picked');
  const dateNext = $('#date-next');

  function renderCalendar() {
    calMonth.textContent = `${MONTHS[view.getMonth()]} ${view.getFullYear()}`;
    calPrev.disabled = view <= new Date(today.getFullYear(), today.getMonth(), 1);
    calNext.disabled = new Date(view.getFullYear(), view.getMonth() + 1, 1) > lastDay;

    const frag = document.createDocumentFragment();
    const offset = (view.getDay() + 6) % 7; // неделя с понедельника
    for (let i = 0; i < offset; i++) frag.append(document.createElement('span'));

    const daysInMonth = new Date(view.getFullYear(), view.getMonth() + 1, 0).getDate();
    const todayIso = toIso(today);
    for (let d = 1; d <= daysInMonth; d++) {
      const date = new Date(view.getFullYear(), view.getMonth(), d);
      const value = toIso(date);
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'day';
      btn.textContent = String(d);
      btn.dataset.date = value;
      btn.disabled = date < today || date > lastDay;
      btn.classList.toggle('is-today', value === todayIso);
      btn.classList.toggle('is-weekend', date.getDay() === 0 || date.getDay() === 6);
      btn.classList.toggle('is-selected', value === state.date);
      btn.setAttribute('aria-pressed', String(value === state.date));
      btn.setAttribute('aria-label', longDate(date));
      frag.append(btn);
    }
    calGrid.replaceChildren(frag);
  }

  calPrev.addEventListener('click', () => {
    view = new Date(view.getFullYear(), view.getMonth() - 1, 1);
    renderCalendar();
  });
  calNext.addEventListener('click', () => {
    view = new Date(view.getFullYear(), view.getMonth() + 1, 1);
    renderCalendar();
  });
  calGrid.addEventListener('click', (e) => {
    const btn = e.target.closest('.day');
    if (!btn || btn.disabled) return;
    state.date = btn.dataset.date;
    renderCalendar();
    updatePicked();
  });

  function renderTimeChips() {
    const options = [
      ...TIMES.map((t) => [t, t]),
      ['any', 'Не важно 🤷'],
      ['custom', 'Другое время'],
    ];
    timeChips.replaceChildren(...options.map(([value, label]) => {
      const chip = document.createElement('button');
      chip.type = 'button';
      chip.className = value.includes(':') ? 'chip' : 'chip chip-wide';
      chip.dataset.value = value;
      chip.textContent = label;
      chip.setAttribute('aria-pressed', 'false');
      return chip;
    }));
  }

  timeChips.addEventListener('click', (e) => {
    const chip = e.target.closest('.chip');
    if (!chip) return;
    state.timeChoice = chip.dataset.value;
    timeChips.querySelectorAll('.chip').forEach((el) => el.setAttribute('aria-pressed', String(el === chip)));
    const custom = state.timeChoice === 'custom';
    timeCustom.hidden = !custom;
    if (custom) {
      if (!timeCustom.value) timeCustom.value = '19:30';
      state.time = timeCustom.value;
      timeCustom.focus();
    } else {
      state.time = state.timeChoice;
    }
    updatePicked();
  });

  timeCustom.addEventListener('input', () => {
    if (state.timeChoice !== 'custom') return;
    state.time = timeCustom.value || null;
    updatePicked();
  });

  function updatePicked() {
    const parts = [];
    if (state.date) parts.push(longDate(fromIso(state.date)));
    if (state.time) parts.push(state.time === 'any' ? 'время обсудим' : `в ${state.time}`);
    datePicked.textContent = parts.length ? `Выбрано: ${parts.join(', ')}` : '';
    dateNext.disabled = !(state.date && state.time);
  }

  $('#btn-to-date').addEventListener('click', () => show('date'));
  dateNext.addEventListener('click', () => {
    if (state.date && state.time) show('plans');
  });

  // ---------- Планы ----------

  const activitiesBox = $('#activities');
  const wishInput = $('#wish');
  const plansSend = $('#plans-send');

  function renderActivities() {
    activitiesBox.replaceChildren(...ACTIVITIES.map(([emoji, label]) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'activity';
      btn.dataset.value = `${emoji} ${label}`;
      btn.setAttribute('aria-pressed', 'false');
      const icon = document.createElement('span');
      icon.className = 'activity-emoji';
      icon.textContent = emoji;
      const text = document.createElement('span');
      text.className = 'activity-label';
      text.textContent = label;
      btn.append(icon, text);
      return btn;
    }));
  }

  activitiesBox.addEventListener('click', (e) => {
    const btn = e.target.closest('.activity');
    if (!btn) return;
    const on = btn.getAttribute('aria-pressed') !== 'true';
    btn.setAttribute('aria-pressed', String(on));
    state.activities = [...activitiesBox.querySelectorAll('.activity[aria-pressed="true"]')].map((el) => el.dataset.value);
    plansSend.disabled = state.activities.length === 0;
  });

  $('#plans-back').addEventListener('click', () => show('date'));

  plansSend.addEventListener('click', async () => {
    if (!state.date || !state.time || state.activities.length === 0) return;
    state.wish = wishInput.value.trim();
    const answer = {
      date: state.date,
      time: state.time,
      activities: state.activities.slice(),
      wish: state.wish,
    };
    renderDone(answer);
    show('done');
    celebrateSoft();
    store.set({ stage: 'done', attempts: state.attempts, viaNo: state.viaNo, ...answer });

    setStatus('Отправляю ответ… 🕊️');
    const ok = await notify('final', { ...answer, viaNo: state.viaNo }, 3);
    if (ok) setStatus('Ответ уже у меня ✨');
    else setStatus('Не получилось отправить автоматически 😅 Сделай скрин этого экрана и пришли мне', true);
  });

  // ---------- Финал ----------

  let finalAnswer = null;

  function renderDone(answer) {
    finalAnswer = answer;
    const when = longDate(fromIso(answer.date));
    $('#t-when').textContent = `${capitalize(when)} · ${answer.time === 'any' ? 'время обсудим' : answer.time}`;
    $('#t-plans').textContent = answer.activities.join(', ');
    $('#t-wish').textContent = answer.wish || '';
    $('#t-wish-row').hidden = !answer.wish;
    $('#gcal').href = googleCalendarUrl(answer);
  }

  function setStatus(text, isError = false) {
    const el = $('#send-status');
    el.textContent = text;
    el.classList.toggle('is-error', isError);
  }

  $('#ics').addEventListener('click', () => {
    if (finalAnswer) downloadIcs(finalAnswer);
  });

  $('#restart').addEventListener('click', () => {
    store.set({ stage: 'yes', attempts: state.attempts, viaNo: state.viaNo });
    show('date');
    updatePicked();
  });

  function eventTimes(answer) {
    const [y, m, d] = answer.date.split('-').map(Number);
    if (answer.time === 'any') {
      return { allDay: true, start: compactDate(new Date(y, m - 1, d)), end: compactDate(new Date(y, m - 1, d + 1)) };
    }
    const [hh, mm] = answer.time.split(':').map(Number);
    return {
      allDay: false,
      start: compactDateTime(new Date(y, m - 1, d, hh, mm)),
      end: compactDateTime(new Date(y, m - 1, d, hh + 2, mm)),
    };
  }

  function describe(answer) {
    return [`Планы: ${answer.activities.join(', ')}`, answer.wish && `Пожелание: ${answer.wish}`].filter(Boolean).join('\n');
  }

  function googleCalendarUrl(answer) {
    const t = eventTimes(answer);
    const q = new URLSearchParams({
      action: 'TEMPLATE',
      text: 'Свидание 💖',
      dates: `${t.start}/${t.end}`,
      details: describe(answer),
    });
    return `https://calendar.google.com/calendar/render?${q}`;
  }

  function downloadIcs(answer) {
    const t = eventTimes(answer);
    const esc = (s) => s.replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/([,;])/g, '\\$1');
    const now = new Date();
    const stamp = `${now.getUTCFullYear()}${pad(now.getUTCMonth() + 1)}${pad(now.getUTCDate())}T${pad(now.getUTCHours())}${pad(now.getUTCMinutes())}${pad(now.getUTCSeconds())}Z`;
    const lines = [
      'BEGIN:VCALENDAR',
      'VERSION:2.0',
      'PRODID:-//svidanie//RU',
      'CALSCALE:GREGORIAN',
      'METHOD:PUBLISH',
      'BEGIN:VEVENT',
      `UID:${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}@svidanie`,
      `DTSTAMP:${stamp}`,
      t.allDay ? `DTSTART;VALUE=DATE:${t.start}` : `DTSTART:${t.start}`,
      t.allDay ? `DTEND;VALUE=DATE:${t.end}` : `DTEND:${t.end}`,
      `SUMMARY:${esc('Свидание 💖')}`,
      `DESCRIPTION:${esc(describe(answer))}`,
      'END:VEVENT',
      'END:VCALENDAR',
    ];
    const blob = new Blob([`${lines.map(foldIcsLine).join('\r\n')}\r\n`], { type: 'text/calendar;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'svidanie.ics';
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  }

  // RFC 5545: строки длиннее 75 байт переносятся, продолжение начинается с пробела.
  function foldIcsLine(line) {
    const encoder = new TextEncoder();
    if (encoder.encode(line).length <= 75) return line;
    const parts = [];
    let current = '';
    let size = 0;
    let limit = 75;
    for (const ch of line) {
      const n = encoder.encode(ch).length;
      if (size + n > limit) {
        parts.push(current);
        current = '';
        size = 0;
        limit = 74;
      }
      current += ch;
      size += n;
    }
    parts.push(current);
    return parts.join('\r\n ');
  }

  // ---------- Сердечки на фоне ----------

  function spawnHearts() {
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    const box = $('#hearts');
    const colors = ['#ff6fa3', '#ff8fb8', '#ffb3cf', '#ffffff', '#e3a1ff', '#ff4d8d'];
    const count = window.innerWidth < 600 ? 14 : 22;
    const svg = '<svg viewBox="0 0 32 29"><path d="M23.6 0c-3.4 0-6.3 2.7-7.6 5.6C14.7 2.7 11.8 0 8.4 0 3.8 0 0 3.8 0 8.4c0 9.4 9.5 11.9 16 21.2 6.1-9.3 16-12.1 16-21.2C32 3.8 28.2 0 23.6 0z"/></svg>';
    for (let i = 0; i < count; i++) {
      const el = document.createElement('span');
      el.className = 'heart';
      el.style.cssText = [
        `--x:${(Math.random() * 100).toFixed(1)}%`,
        `--size:${Math.round(12 + Math.random() * 26)}px`,
        `--dur:${(10 + Math.random() * 12).toFixed(1)}s`,
        `--delay:${(-Math.random() * 20).toFixed(1)}s`,
        `--rot:${Math.round(Math.random() * 40 - 20)}deg`,
        `--alpha:${(0.35 + Math.random() * 0.45).toFixed(2)}`,
        `--color:${colors[i % colors.length]}`,
        `--drift:${Math.round(8 + Math.random() * 18)}px`,
        `--sway:${(2.5 + Math.random() * 2.5).toFixed(1)}s`,
      ].join(';');
      el.innerHTML = svg;
      box.append(el);
    }
  }

  // ---------- Утилиты ----------

  function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function pad(n) {
    return String(n).padStart(2, '0');
  }

  function startOfDay(date) {
    return new Date(date.getFullYear(), date.getMonth(), date.getDate());
  }

  function toIso(date) {
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
  }

  function fromIso(value) {
    const [y, m, d] = value.split('-').map(Number);
    return new Date(y, m - 1, d);
  }

  function compactDate(date) {
    return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}`;
  }

  function compactDateTime(date) {
    return `${compactDate(date)}T${pad(date.getHours())}${pad(date.getMinutes())}00`;
  }

  function longDate(date) {
    const options = { weekday: 'long', day: 'numeric', month: 'long' };
    if (date.getFullYear() !== today.getFullYear()) options.year = 'numeric';
    return date.toLocaleDateString('ru-RU', options).replace(/\s*г\.$/, '');
  }

  function capitalize(text) {
    return text ? text[0].toUpperCase() + text.slice(1) : text;
  }

  function formatName(raw) {
    const name = Array.from(raw.replace(/\s+/g, ' ').trim()).slice(0, 40).join('');
    return capitalize(name);
  }

  // ---------- Старт ----------

  renderTimeChips();
  renderActivities();
  spawnHearts();

  if (NAME) {
    $('#ask-kicker').textContent = `${NAME}, у меня есть вопрос…`;
    document.title = `${NAME}, тебе приглашение 💌`;
  }

  const saved = TEST ? null : store.get();
  if (saved && saved.stage === 'done' && saved.date && saved.time && Array.isArray(saved.activities)) {
    state.attempts = saved.attempts || 0;
    state.viaNo = Boolean(saved.viaNo);
    noBtn.remove();
    renderDone({ date: saved.date, time: saved.time, activities: saved.activities, wish: saved.wish || '' });
    show('done', { focus: false });
    setStatus('Ответ уже у меня 💌');
  } else if (saved && saved.stage === 'yes') {
    state.attempts = saved.attempts || 0;
    state.viaNo = Boolean(saved.viaNo);
    noBtn.remove();
    show('date', { focus: false });
  } else {
    show('ask', { focus: false });
    preload('love');
    let opened = false;
    try {
      opened = sessionStorage.getItem('svidanie:opened') === '1';
      sessionStorage.setItem('svidanie:opened', '1');
    } catch (_) { /* приватный режим */ }
    if (!opened) notify('open');
  }
})();

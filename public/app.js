// Progressive enhancement only: every page works without this script.
(() => {
  'use strict';

  const flash = (button, label) => {
    const original = button.textContent;
    button.textContent = label || '✓';
    setTimeout(() => {
      button.textContent = original;
    }, 1500);
  };

  const copyText = async (text) => {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      document.execCommand('copy');
      area.remove();
    }
  };

  document.addEventListener('click', async (event) => {
    const button = event.target.closest('[data-copy], [data-copy-value]');
    if (!button) return;
    const source = button.dataset.copy ? document.querySelector(button.dataset.copy) : null;
    const text = button.dataset.copyValue ?? source?.textContent ?? '';
    await copyText(text.trim());
    flash(button, button.dataset.copiedLabel);
  });

  document.addEventListener('submit', (event) => {
    const form = event.target;
    if (form.dataset.confirm && !window.confirm(form.dataset.confirm)) event.preventDefault();
  });

  document.querySelectorAll('[data-select-all]').forEach((input) => {
    input.addEventListener('focus', () => input.select());
  });

  // --- new generation form -------------------------------------------------------
  const form = document.getElementById('generate-form');
  const transcript = document.querySelector('[data-transcript]');
  const stats = document.querySelector('[data-transcript-stats]');

  if (transcript && stats) {
    const numbers = new Intl.NumberFormat(transcript.dataset.locale === 'ru' ? 'ru-RU' : 'en-US');
    const limit = Number(transcript.dataset.limit);
    const update = () => {
      const chars = transcript.value.length;
      stats.textContent = stats.dataset.template
        .replace('{chars}', numbers.format(chars))
        .replace('{mins}', String(Math.round(chars / 1000)))
        .replace('{credits}', String(Math.max(1, Math.ceil(chars / Number(transcript.dataset.charsPerCredit)))))
        .replace('{limit}', transcript.dataset.limitLabel);
      stats.classList.toggle('is-over', chars > limit);
    };
    transcript.addEventListener('input', update);
    update();

    const file = document.querySelector('[data-transcript-file]');
    file?.addEventListener('change', async () => {
      const picked = file.files?.[0];
      if (!picked) return;
      transcript.value = await picked.text();
      update();
      const title = form?.querySelector('[name="title"]');
      if (title && !title.value) title.value = picked.name.replace(/\.[^.]+$/, '');
    });
  }

  if (form) {
    const KEY = 'hookcut:form';
    let saved = {};
    try {
      saved = JSON.parse(localStorage.getItem(KEY) || '{}');
    } catch {}
    // Restore last-used settings on a fresh form only (not after a validation error).
    if (transcript && !transcript.value) {
      form.querySelectorAll('[data-remember]').forEach((el) => {
        const value = saved[el.name];
        if (value == null) return;
        if (el.tagName === 'SELECT') {
          const option = [...el.options].find((o) => o.value === value && !o.disabled);
          if (option) el.value = value;
        } else if (!el.value) {
          el.value = value;
        }
      });
    }
    form.addEventListener('submit', () => {
      const data = {};
      form.querySelectorAll('[data-remember]').forEach((el) => {
        data[el.name] = el.value;
      });
      try {
        localStorage.setItem(KEY, JSON.stringify(data));
      } catch {}
      const submit = form.querySelector('[type="submit"]');
      if (submit) {
        submit.disabled = true;
        submit.textContent += '…';
      }
    });
  }

  // --- pending generation ---------------------------------------------------------
  const poll = document.querySelector('[data-poll]');
  if (poll) {
    const steps = [...poll.querySelectorAll('[data-steps] li')];
    let step = 0;
    const advance = setInterval(() => {
      if (step >= steps.length - 1) return clearInterval(advance);
      steps[step].classList.replace('is-active', 'is-done');
      step += 1;
      steps[step].classList.add('is-active');
    }, 9000);

    const check = async () => {
      try {
        const response = await fetch(poll.dataset.poll, { headers: { accept: 'application/json' }, cache: 'no-store' });
        if (response.ok) {
          const { status } = await response.json();
          if (status !== 'pending') {
            window.location.reload();
            return;
          }
        }
      } catch {}
      setTimeout(check, 2500);
    };
    setTimeout(check, 2500);
  }
})();

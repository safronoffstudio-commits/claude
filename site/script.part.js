(function () {
  var W = 1080, H = 1080, B = 78, PAD = B + 54, CW = W - 2 * PAD;
  var CV = { ground:'#1E1A2E', madder:'#A8322A', saffron:'#E0A32E',
             wool:'#EFE6D4', indigo:'#2E4A6B', dim:'#8F87A6' };
  var FD = function (w, s) { return w + ' ' + s + 'px Oswald, "Arial Narrow", sans-serif'; };
  var FB = function (w, s) { return w + ' ' + s + 'px "PT Sans", system-ui, sans-serif'; };

  var cv = document.createElement('canvas');
  cv.width = W; cv.height = H;
  var x = cv.getContext('2d');

  // --- вспомогательное ---
  function wrap(text, maxW) {
    var words = String(text).split(' '), lines = [], cur = '';
    for (var i = 0; i < words.length; i++) {
      var t = cur ? cur + ' ' + words[i] : words[i];
      if (x.measureText(t).width <= maxW || !cur) cur = t;
      else { lines.push(cur); cur = words[i]; }
    }
    if (cur) lines.push(cur);
    return lines;
  }
  function fit(text, maxW, start, min, weight) {
    var s = start;
    for (; s > min; s -= 2) { x.font = FD(weight, s); if (x.measureText(text).width <= maxW) break; }
    x.font = FD(weight, s);
    return s;
  }
  function tracked(text, px, py, sp) {
    var cx = px;
    for (var i = 0; i < text.length; i++) {
      x.fillText(text[i], cx, py);
      cx += x.measureText(text[i]).width + sp;
    }
  }

  // --- ковровая рамка: ступенчатые ромбы с крючками ---
  function motif(cx, cy, r) {
    function dia(rr, fill) {
      x.beginPath();
      x.moveTo(cx, cy - rr); x.lineTo(cx + rr, cy);
      x.lineTo(cx, cy + rr); x.lineTo(cx - rr, cy);
      x.closePath(); x.fillStyle = fill; x.fill();
    }
    dia(r, CV.wool); dia(r * 0.62, CV.indigo); dia(r * 0.26, CV.saffron);
    var h = r * 0.3;
    x.fillStyle = CV.wool;
    x.fillRect(cx - h / 2, cy - r - h / 2, h, h);
    x.fillRect(cx - h / 2, cy + r - h / 2, h, h);
    x.fillRect(cx - r - h / 2, cy - h / 2, h, h);
    x.fillRect(cx + r - h / 2, cy - h / 2, h, h);
  }
  function border() {
    x.fillStyle = CV.madder; x.fillRect(0, 0, W, H);
    x.fillStyle = CV.ground; x.fillRect(B, B, W - 2 * B, H - 2 * B);
    var r = B * 0.3, n = 12, sx = W / n, i;
    for (i = 0; i < n; i++) { var c = sx * (i + 0.5); motif(c, B / 2, r); motif(c, H - B / 2, r); }
    var m = Math.round((H - 2 * B) / sx), sy = (H - 2 * B) / m;
    for (i = 0; i < m; i++) { var cc = B + sy * (i + 0.5); motif(B / 2, cc, r); motif(W - B / 2, cc, r); }
    x.strokeStyle = CV.saffron; x.lineWidth = 3;
    x.strokeRect(B + 13, B + 13, W - 2 * B - 26, H - 2 * B - 26);
  }

  // --- шапка и подвал ---
  function header(m) {
    var y = PAD + 14;
    x.textBaseline = 'alphabetic';
    x.fillStyle = CV.saffron; x.font = FD(600, 24);
    tracked(m.k === 'cert' ? 'ОФИЦИАЛЬНЫЙ ДОКУМЕНТ' : m.c.toUpperCase(), PAD, y, 3.5);
    y += 46;
    var s = fit(m.t, CW, 76, 42, 700);
    x.fillStyle = CV.wool; x.font = FD(700, s);
    x.fillText(m.t, PAD, y + s * 0.76);
    y += s * 0.95 + 18;
    x.fillStyle = CV.madder; x.fillRect(PAD, y, 110, 5);
    return y + 46;
  }
  function footer(m) {
    x.font = FD(700, 42);
    var lines = wrap(m.p, CW);
    if (lines.length > 2) { x.font = FD(700, 34); lines = wrap(m.p, CW); }
    var lh = 50, y = H - PAD - 28 - (lines.length - 1) * lh;
    x.fillStyle = CV.saffron;
    for (var i = 0; i < lines.length; i++) x.fillText(lines[i], PAD, y + i * lh);
    x.font = FB(400, 19); x.fillStyle = CV.dim;
    var wm = 'вацок-генератор';
    x.fillText(wm, W - PAD - x.measureText(wm).width, H - PAD + 16);
  }

  // --- тело: правила / уровни / диалог ---
  function rowsBody(m, y0, y1, kind) {
    // Две реплики диалога не должны расползаться на всё поле:
    // ограничиваем шаг и центрируем блок по вертикали.
    var n = m.r.length, span = y1 - y0;
    var gap = Math.min(span / n, 112);
    var top = y0 + (span - gap * n) / 2;
    for (var i = 0; i < n; i++) {
      var cy = top + gap * i + gap / 2, tx = PAD, ind = 0;
      if (kind === 'rules') {
        x.font = FD(600, 34); x.fillStyle = CV.saffron;
        x.fillText(String(i + 1), PAD, cy + 12);
        tx = PAD + 62;
      } else if (kind === 'levels') {
        for (var k = 0; k <= i; k++) {
          x.fillStyle = k === i ? CV.saffron : CV.indigo;
          x.fillRect(PAD + k * 23, cy - 8, 16, 16);
        }
        tx = PAD + 4 * 23 + 24;
      } else if (kind === 'dialog') {
        ind = i % 2 ? 46 : 0;
      }
      x.font = FB(400, 38);
      x.fillStyle = (kind === 'dialog' && i % 2) ? CV.saffron : CV.wool;
      var lines = wrap(m.r[i], CW - (tx - PAD) - ind), lh = 46;
      var ly = cy - ((lines.length - 1) * lh) / 2 + 13;
      for (var j = 0; j < lines.length; j++) x.fillText(lines[j], tx + ind, ly + j * lh);
      if (kind !== 'dialog' && i < n - 1) {
        x.fillStyle = 'rgba(239,230,212,.12)';
        x.fillRect(PAD, top + gap * (i + 1), CW, 1);
      }
    }
  }

  // --- тело: справка ---
  function certBody(m, y0, y1, name) {
    var nm0 = (name || 'Вацок').toUpperCase();
    var s0 = fit(nm0, CW, 70, 34, 700);
    x.font = FB(400, 38);
    var deed = wrap(m.d, CW);
    // меряем блок целиком, чтобы поставить его по центру поля
    var total = 54 + (s0 * 0.95 + 44) + 46 + deed.length * 46 + 30 + 60;
    var y = y0 + Math.max(0, (y1 - y0 - total) / 2);
    x.font = FB(400, 24); x.fillStyle = CV.dim; x.fillText('Выдана', PAD, y);
    y += 54;
    x.font = FD(700, s0); x.fillStyle = CV.saffron;
    x.fillText(nm0, PAD, y + s0 * 0.74);
    y += s0 * 0.95 + 44;
    x.font = FB(400, 24); x.fillStyle = CV.dim; x.fillText('Основание', PAD, y);
    y += 46;
    x.font = FB(400, 38); x.fillStyle = CV.wool;
    for (var i = 0; i < deed.length; i++) x.fillText(deed[i], PAD, y + i * 46);
    y += deed.length * 46 + 30;
    x.font = FD(600, 30);
    var tw = Math.min(x.measureText(m.s).width + 44, CW);
    x.strokeStyle = CV.madder; x.lineWidth = 4;
    x.strokeRect(PAD + 2, y, tw, 60);
    x.fillStyle = CV.madder; x.fillText(m.s, PAD + 24, y + 40);
  }

  function render(m, name) {
    border();
    var y0 = header(m), y1 = H - PAD - 150;
    if (m.k === 'cert') certBody(m, y0, y1, name);
    else rowsBody(m, y0, y1, m.k);
    footer(m);
    return cv.toDataURL('image/png');
  }

  function altOf(m, name) {
    var body = m.k === 'cert'
      ? ['Выдана ' + (name || 'Вацок'), m.d, m.s]
      : m.r;
    return m.t + '. ' + body.join('. ') + '. ' + m.p;
  }

  // --- интерфейс ---
  var img = document.getElementById('meme');
  var cap = document.getElementById('cap');
  var nameInput = document.getElementById('name');
  var chipsBox = document.getElementById('chips');
  var filter = 'Все', queue = [], current = null;

  try {
    var saved = localStorage.getItem('vacok-name');
    if (saved) nameInput.value = saved;
  } catch (e) {}

  function next() {
    if (!queue.length) {
      queue = MEMES.filter(function (m) { return filter === 'Все' || m.c === filter; });
      for (var i = queue.length - 1; i > 0; i--) {
        var j = Math.floor(Math.random() * (i + 1));
        var t = queue[i]; queue[i] = queue[j]; queue[j] = t;
      }
      if (queue.length > 1 && current && queue[queue.length - 1] === current) {
        queue.unshift(queue.pop());
      }
    }
    return queue.pop();
  }

  function draw(m) {
    current = m;
    var name = nameInput.value.trim();
    img.classList.remove('in');
    img.src = render(m, name);
    img.alt = altOf(m, name);
    cap.textContent = m.k === 'cert'
      ? 'Справка готова. Зажми картинку, чтобы сохранить.'
      : 'Зажми картинку и выбери «Сохранить изображение».';
    requestAnimationFrame(function () { img.classList.add('in'); });
  }

  function buildChips() {
    var cats = ['Все'];
    MEMES.forEach(function (m) { if (cats.indexOf(m.c) < 0) cats.push(m.c); });
    cats.forEach(function (c) {
      var li = document.createElement('li');
      var b = document.createElement('button');
      b.type = 'button'; b.className = 'chip'; b.textContent = c;
      b.setAttribute('aria-pressed', String(c === filter));
      b.addEventListener('click', function () {
        filter = c; queue = [];
        chipsBox.querySelectorAll('.chip').forEach(function (o) {
          o.setAttribute('aria-pressed', String(o.textContent === c));
        });
        draw(next());
      });
      li.appendChild(b); chipsBox.appendChild(li);
    });
  }

  document.getElementById('go').addEventListener('click', function () { draw(next()); });
  nameInput.addEventListener('input', function () {
    try { localStorage.setItem('vacok-name', nameInput.value); } catch (e) {}
    if (current && current.k === 'cert') draw(current);
  });

  (function boot() {
    var fonts = document.fonts
      ? Promise.all([
          document.fonts.load('700 76px Oswald'),
          document.fonts.load('600 34px Oswald'),
          document.fonts.load('400 38px "PT Sans"'),
        ]).then(function () { return document.fonts.ready; })
      : Promise.resolve();
    fonts.catch(function () {}).then(function () {
      buildChips();
      draw(next());
    });
  })();
})();

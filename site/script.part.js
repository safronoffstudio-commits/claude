(function () {
  var W = 1080, H = 1080, F = 84;
  var IX = F, IY = F, IW = W - 2 * F, IH = H - 2 * F;
  var C = { band:'#7A2420', gold:'#D4A24C', cream:'#F0E2C6', deep:'#2A1410' };

  var cv = document.createElement('canvas');
  cv.width = W; cv.height = H;
  var x = cv.getContext('2d');

  // Один и тот же текст — одни и те же блики, чтобы картинка не «дёргалась»
  function hash(s) {
    var h = 1779033703 ^ s.length;
    for (var i = 0; i < s.length; i++) {
      h = Math.imul(h ^ s.charCodeAt(i), 3432918353);
      h = (h << 13) | (h >>> 19);
    }
    return (h ^ (h >>> 16)) >>> 0;
  }
  function mulberry(a) {
    return function () {
      a |= 0; a = (a + 0x6d2b79f5) | 0;
      var t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }

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

  // --- зерно: та самая пересжатая фактура ---
  var grainPat = null;
  function grain() {
    if (!grainPat) {
      var gc = document.createElement('canvas');
      gc.width = gc.height = 140;
      var g = gc.getContext('2d'), d = g.createImageData(140, 140);
      for (var i = 0; i < d.data.length; i += 4) {
        var v = 90 + Math.random() * 76;
        d.data[i] = d.data[i + 1] = d.data[i + 2] = v; d.data[i + 3] = 255;
      }
      g.putImageData(d, 0, 0);
      grainPat = x.createPattern(gc, 'repeat');
    }
    x.save();
    x.globalCompositeOperation = 'overlay';
    x.globalAlpha = 0.07;
    x.fillStyle = grainPat;
    x.fillRect(0, 0, W, H);
    x.restore();
  }

  // --- ковровая рамка ---
  function motif(cx, cy, r) {
    function dia(rr, fill) {
      x.beginPath();
      x.moveTo(cx, cy - rr); x.lineTo(cx + rr, cy);
      x.lineTo(cx, cy + rr); x.lineTo(cx - rr, cy);
      x.closePath(); x.fillStyle = fill; x.fill();
    }
    dia(r, C.cream); dia(r * 0.6, C.deep); dia(r * 0.26, C.gold);
    var h = r * 0.3;
    x.fillStyle = C.cream;
    x.fillRect(cx - h / 2, cy - r - h / 2, h, h);
    x.fillRect(cx - h / 2, cy + r - h / 2, h, h);
    x.fillRect(cx - r - h / 2, cy - h / 2, h, h);
    x.fillRect(cx + r - h / 2, cy - h / 2, h, h);
  }
  function frame() {
    x.fillStyle = C.band;
    x.fillRect(0, 0, W, F); x.fillRect(0, H - F, W, F);
    x.fillRect(0, F, F, H - 2 * F); x.fillRect(W - F, F, F, H - 2 * F);
    var r = F * 0.3, n = 14, sx = W / n, i;
    for (i = 0; i < n; i++) { var c = sx * (i + 0.5); motif(c, F / 2, r); motif(c, H - F / 2, r); }
    var m = Math.round(IH / sx), sy = IH / m;
    for (i = 0; i < m; i++) { var cc = F + sy * (i + 0.5); motif(F / 2, cc, r); motif(W - F / 2, cc, r); }
    x.strokeStyle = C.gold; x.lineWidth = 3;
    x.strokeRect(5.5, 5.5, W - 11, H - 11);
    x.strokeRect(F - 5.5, F - 5.5, IW + 11, IH + 11);
  }

  // --- фон, когда своего фото нет ---
  function soft(cx, cy, r, col) {
    var g = x.createRadialGradient(cx, cy, 0, cx, cy, r);
    g.addColorStop(0, col); g.addColorStop(1, 'rgba(0,0,0,0)');
    x.fillStyle = g; x.fillRect(IX, IY, IW, IH);
  }
  function ridge(baseY, h, col, peaks, phase) {
    x.beginPath();
    x.moveTo(IX, IY + IH); x.lineTo(IX, baseY);
    for (var i = 0; i <= peaks; i++) {
      var px = IX + IW * (i / peaks);
      var hh = (i % 2 === 0) ? h * 0.3 : h * (0.7 + 0.3 * Math.abs(Math.sin(i + phase)));
      x.lineTo(px, baseY - hh);
    }
    x.lineTo(IX + IW, IY + IH); x.closePath();
    x.fillStyle = col; x.fill();
  }
  function moon() {
    var mx = IX + IW * 0.74, my = IY + IH * 0.19, r = IW * 0.055;
    var g = x.createRadialGradient(mx, my, 0, mx, my, r * 3.6);
    g.addColorStop(0, 'rgba(255,240,205,.42)'); g.addColorStop(1, 'rgba(255,240,205,0)');
    x.fillStyle = g; x.beginPath(); x.arc(mx, my, r * 3.6, 0, 6.3); x.fill();
    x.fillStyle = 'rgba(255,246,222,.92)';
    x.beginPath(); x.arc(mx, my, r, 0, 6.3); x.fill();
  }

  function backdrop() {
    var g = x.createLinearGradient(0, IY, 0, IY + IH);
    g.addColorStop(0, '#26395C'); g.addColorStop(0.55, '#141D30'); g.addColorStop(1, '#090C15');
    x.fillStyle = g; x.fillRect(IX, IY, IW, IH);
    soft(IX + IW * 0.3, IY + IH * 0.28, IW * 0.52, 'rgba(96,146,186,.22)');
    soft(IX + IW * 0.8, IY + IH * 0.16, IW * 0.34, 'rgba(212,162,76,.16)');
    moon();
    ridge(IY + IH * 0.72, IH * 0.34, '#101927', 5, 1.2);
    ridge(IY + IH * 0.84, IH * 0.26, '#080C14', 7, 2.4);
  }

  function drawPhoto(img) {
    x.save();
    x.beginPath(); x.rect(IX, IY, IW, IH); x.clip();
    try { x.filter = 'saturate(1.25) contrast(1.06) brightness(.94)'; } catch (e) {}
    var s = Math.max(IW / img.width, IH / img.height);
    var w = img.width * s, h = img.height * s;
    x.drawImage(img, IX + (IW - w) / 2, IY + (IH - h) / 2, w, h);
    try { x.filter = 'none'; } catch (e) {}
    x.restore();
  }

  function grade() {
    x.save();
    x.beginPath(); x.rect(IX, IY, IW, IH); x.clip();
    x.fillStyle = 'rgba(20,34,58,.20)'; x.fillRect(IX, IY, IW, IH);
    var cx = IX + IW / 2, cy = IY + IH / 2;
    var g = x.createRadialGradient(cx, cy, IW * 0.20, cx, cy, IW * 0.78);
    g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,.62)');
    x.fillStyle = g; x.fillRect(IX, IY, IW, IH);
    x.restore();
  }

  function sparkles(rnd, hasPhoto) {
    x.save();
    x.beginPath(); x.rect(IX, IY, IW, IH); x.clip();
    if (!hasPhoto) {
      for (var i = 0; i < 46; i++) {
        var sx2 = IX + rnd() * IW, sy2 = IY + rnd() * IH * 0.58;
        var rr = 0.8 + rnd() * 1.8, a = 0.25 + rnd() * 0.5;
        x.fillStyle = 'rgba(255,248,230,' + a.toFixed(2) + ')';
        x.beginPath(); x.arc(sx2, sy2, rr, 0, 6.3); x.fill();
      }
    }
    var n = hasPhoto ? 4 : 3;
    for (var j = 0; j < n; j++) {
      var gx = IX + rnd() * IW, gy = IY + rnd() * IH * 0.5, gr = 5 + rnd() * 9;
      var g = x.createRadialGradient(gx, gy, 0, gx, gy, gr);
      g.addColorStop(0, 'rgba(255,248,230,.85)'); g.addColorStop(1, 'rgba(255,248,230,0)');
      x.fillStyle = g;
      x.beginPath(); x.arc(gx, gy, gr, 0, 6.3); x.fill();
    }
    x.restore();
  }

  function caption(text, rnd) {
    var lob = rnd() < 0.28;
    var fam = lob ? 'Lobster, cursive' : '"Marck Script", cursive';
    var size = lob ? 62 : 80, maxW = IW * 0.84, lines;
    for (;;) {
      x.font = size + 'px ' + fam;
      lines = wrap(text, maxW);
      if (lines.length <= 3 || size <= 38) break;
      size -= 4;
    }
    var lh = size * 1.16, total = lines.length * lh;
    var startY = IY + IH - 58 - total + lh * 0.8;

    x.save();
    x.beginPath(); x.rect(IX, IY, IW, IH); x.clip();
    var top = IY + IH - total - 160;
    var sg = x.createLinearGradient(0, top, 0, IY + IH);
    sg.addColorStop(0, 'rgba(0,0,0,0)'); sg.addColorStop(1, 'rgba(0,0,0,.80)');
    x.fillStyle = sg; x.fillRect(IX, top, IW, IY + IH - top);

    x.textAlign = 'center';
    var cx = IX + IW / 2, i;
    x.fillStyle = '#FFF9EC';
    x.shadowColor = 'rgba(0,0,0,.92)'; x.shadowBlur = 20; x.shadowOffsetY = 3;
    for (i = 0; i < lines.length; i++) x.fillText(lines[i], cx, startY + i * lh);
    x.shadowColor = 'rgba(255,226,170,.6)'; x.shadowBlur = 26; x.shadowOffsetY = 0;
    for (i = 0; i < lines.length; i++) x.fillText(lines[i], cx, startY + i * lh);
    x.restore();
  }

  var photo = null;
  function render(text) {
    x.setTransform(1, 0, 0, 1, 0, 0);
    var rnd = mulberry(hash(text));
    if (photo) drawPhoto(photo); else backdrop();
    grade();
    sparkles(rnd, !!photo);
    caption(text, rnd);
    frame();
    grain();
    // JPEG неслучайно: лёгкая пересжатость — часть жанра
    return cv.toDataURL('image/jpeg', 0.85);
  }

  // --- интерфейс ---
  var img = document.getElementById('meme');
  var cap = document.getElementById('cap');
  var own = document.getElementById('own');
  var chipsBox = document.getElementById('chips');
  var fileIn = document.getElementById('photo');
  var photoBtn = document.getElementById('photoBtn');
  var clearBtn = document.getElementById('clear');
  var filter = 'Все', queue = [], current = null;

  function next() {
    if (!queue.length) {
      queue = MEMES.filter(function (m) { return filter === 'Все' || m.c === filter; });
      for (var i = queue.length - 1; i > 0; i--) {
        var j = Math.floor(Math.random() * (i + 1));
        var t = queue[i]; queue[i] = queue[j]; queue[j] = t;
      }
      if (queue.length > 1 && current && queue[queue.length - 1].t === current.t) queue.unshift(queue.pop());
    }
    return queue.pop();
  }

  function draw(m) {
    current = m;
    var text = (own.value || '').trim() || m.t;
    ensureFonts(text).then(function () { paint(m, text); });
  }

  function paint(m, text) {
    if (current !== m) return;
    img.classList.remove('in');
    img.src = render(text);
    img.alt = 'Мем в стиле старой аватарки. Подпись: ' + text;
    cap.textContent = photo
      ? 'Зажми картинку и выбери «Сохранить изображение».'
      : 'Поставь своё фото — будет совсем как надо.';
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

  fileIn.addEventListener('change', function (e) {
    var f = e.target.files && e.target.files[0];
    if (!f) return;
    var r = new FileReader();
    r.onload = function () {
      var im = new Image();
      im.onload = function () {
        photo = im;
        photoBtn.textContent = 'Другое фото';
        clearBtn.hidden = false;
        draw(current || next());
      };
      im.onerror = function () { cap.textContent = 'Не получилось открыть это фото. Попробуй другое.'; };
      im.src = r.result;
    };
    r.onerror = function () { cap.textContent = 'Не получилось прочитать файл. Попробуй другой.'; };
    r.readAsDataURL(f);
  });

  clearBtn.addEventListener('click', function () {
    photo = null; fileIn.value = '';
    photoBtn.textContent = 'Своё фото';
    clearBtn.hidden = true;
    draw(current || next());
  });

  document.getElementById('go').addEventListener('click', function () {
    own.value = '';
    draw(next());
  });
  own.addEventListener('input', function () { if (current) draw(current); });

  // Без текста-образца грузится только латинский сабсет, и кириллица
  // уходит в засечный фолбэк. Поэтому всегда передаём саму подпись.
  var CYR = 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя...';
  function ensureFonts(text) {
    if (!document.fonts) return Promise.resolve();
    return Promise.all([
      document.fonts.load('80px "Marck Script"', text),
      document.fonts.load('62px Lobster', text),
    ]).catch(function () {});
  }

  (function boot() {
    ensureFonts(CYR).then(function () {
      return document.fonts ? document.fonts.ready.catch(function () {}) : null;
    }).catch(function () {}).then(function () {
      buildChips();
      draw(next());
    });
  })();
})();

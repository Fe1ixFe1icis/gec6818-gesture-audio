/* =========================================================================
   GESTURE AUDIO · 交互脚本
   无第三方依赖。所有操作都做元素存在性判断，页面局部改动不会让脚本报错。
   ========================================================================= */
(function () {
  'use strict';

  var root = document.documentElement;
  var reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------- 1. 主题切换 ---------- */
  var themeToggle = document.getElementById('themeToggle');
  var themeColor = document.getElementById('themeColor');
  function applyTheme(theme, persist) {
    root.setAttribute('data-theme', theme);
    if (themeColor) themeColor.setAttribute('content', theme === 'dark' ? '#12100E' : '#F4F1EA');
    if (themeToggle) {
      themeToggle.setAttribute('aria-pressed', theme === 'dark' ? 'true' : 'false');
      themeToggle.setAttribute('aria-label', theme === 'dark' ? '切换到浅色模式' : '切换到深色模式');
    }
    if (persist) {
      try { localStorage.setItem('ga-theme', theme); } catch (e) { /* 忽略 */ }
    }
  }
  if (themeToggle) {
    applyTheme(root.getAttribute('data-theme') || 'light', false);
    themeToggle.addEventListener('click', function () {
      applyTheme(root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark', true);
    });
    try {
      var savedTheme = localStorage.getItem('ga-theme');
      if (!savedTheme && window.matchMedia) {
        window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function (e) {
          applyTheme(e.matches ? 'dark' : 'light', false);
        });
      }
    } catch (e) { /* 忽略 */ }
  }

  /* ---------- 2. 移动端菜单 ---------- */
  var menuToggle = document.getElementById('menuToggle');
  var nav = document.getElementById('siteNav');
  function closeMenu(restoreFocus) {
    if (!nav || !menuToggle) return;
    var wasOpen = nav.classList.contains('open');
    nav.classList.remove('open');
    menuToggle.setAttribute('aria-expanded', 'false');
    menuToggle.setAttribute('aria-label', '打开导航菜单');
    if (wasOpen && restoreFocus) menuToggle.focus();
  }
  if (menuToggle && nav) {
    menuToggle.addEventListener('click', function (event) {
      event.stopPropagation();
      var open = !nav.classList.contains('open');
      nav.classList.toggle('open', open);
      menuToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      menuToggle.setAttribute('aria-label', open ? '关闭导航菜单' : '打开导航菜单');
      if (open) {
        var firstLink = nav.querySelector('a');
        if (firstLink) firstLink.focus();
      }
    });
    nav.addEventListener('click', function (event) {
      var link = event.target.closest('a');
      if (!link) return;
      closeMenu(false);
      var hash = link.getAttribute('href') || '';
      if (hash.charAt(0) === '#') {
        var target = document.getElementById(hash.slice(1));
        if (target) {
          target.setAttribute('tabindex', '-1');
          target.focus({ preventScroll: true });
        }
      }
    });
    document.addEventListener('click', function (event) {
      if (!nav.classList.contains('open')) return;
      if (!nav.contains(event.target) && !menuToggle.contains(event.target)) closeMenu(false);
    });
    document.addEventListener('keydown', function (event) {
      if (event.key === 'Escape' && nav.classList.contains('open')) closeMenu(true);
    });
  }

  /* ---------- 3. 滚动进度 ---------- */
  var progressBar = document.getElementById('progressBar');
  var progressTicking = false;
  function updateProgress() {
    if (!progressBar) return;
    var docHeight = document.documentElement.scrollHeight - window.innerHeight;
    var ratio = docHeight > 0 ? window.scrollY / docHeight : 0;
    progressBar.style.width = Math.max(0, Math.min(1, ratio)) * 100 + '%';
  }
  window.addEventListener('scroll', function () {
    if (progressTicking) return;
    progressTicking = true;
    window.requestAnimationFrame(function () {
      updateProgress();
      progressTicking = false;
    });
  }, { passive: true });
  window.addEventListener('resize', updateProgress);
  updateProgress();

  /* ---------- 4. 入场动画 ---------- */
  var revealItems = Array.prototype.slice.call(document.querySelectorAll('.reveal'));
  if (reduceMotion || !('IntersectionObserver' in window)) {
    revealItems.forEach(function (el) { el.classList.add('in'); });
  } else {
    var revealObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('in');
        revealObserver.unobserve(entry.target);
      });
    }, { rootMargin: '0px 0px -8% 0px', threshold: 0.08 });
    revealItems.forEach(function (el) { revealObserver.observe(el); });
  }

  /* ---------- 5. 导航高亮 ---------- */
  var navLinks = Array.prototype.slice.call(document.querySelectorAll('.nav a[href^="#"]'));
  if (navLinks.length && 'IntersectionObserver' in window) {
    var linkForId = {};
    navLinks.forEach(function (link) {
      var id = link.getAttribute('href').slice(1);
      var section = id && document.getElementById(id);
      if (section) linkForId[id] = link;
    });
    var setCurrent = function (id) {
      navLinks.forEach(function (link) {
        if (link === linkForId[id]) link.setAttribute('aria-current', 'true');
        else link.removeAttribute('aria-current');
      });
    };
    var navObserver = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) setCurrent(entry.target.id);
      });
    }, { rootMargin: '-42% 0px -52% 0px', threshold: 0 });
    Object.keys(linkForId).forEach(function (id) { navObserver.observe(document.getElementById(id)); });
    window.addEventListener('scroll', function () {
      if (window.scrollY < 120) setCurrent(null);
    }, { passive: true });
  }

  /* ---------- 6. Tab 通用逻辑（声音链 / 快速开始） ---------- */
  function initTablist(tablistSelector, options) {
    var list = document.querySelector(tablistSelector);
    if (!list) return;
    var tabs = Array.prototype.slice.call(list.querySelectorAll('[role="tab"]'));
    if (!tabs.length) return;
    var panels = tabs.map(function (tab) {
      return document.getElementById(tab.getAttribute('aria-controls'));
    });
    function select(index, focus) {
      if (index < 0 || index >= tabs.length) return;
      tabs.forEach(function (tab, i) {
        var active = i === index;
        tab.setAttribute('aria-selected', active ? 'true' : 'false');
        tab.setAttribute('tabindex', active ? '0' : '-1');
        if (panels[i]) panels[i].hidden = !active;
      });
      if (focus) tabs[index].focus();
      if (options && typeof options.onSelect === 'function') options.onSelect(index);
    }
    tabs.forEach(function (tab, i) {
      tab.addEventListener('click', function () { select(i, false); });
      tab.addEventListener('keydown', function (event) {
        var key = event.key;
        if (key === 'ArrowRight' || key === 'ArrowDown') {
          event.preventDefault();
          select((i + 1) % tabs.length, true);
        } else if (key === 'ArrowLeft' || key === 'ArrowUp') {
          event.preventDefault();
          select((i - 1 + tabs.length) % tabs.length, true);
        } else if (key === 'Home') {
          event.preventDefault();
          select(0, true);
        } else if (key === 'End') {
          event.preventDefault();
          select(tabs.length - 1, true);
        }
      });
    });
    var initial = tabs.findIndex(function (tab) { return tab.getAttribute('aria-selected') === 'true'; });
    select(initial < 0 ? 0 : initial, false);
  }

  initTablist('#chain [role="tablist"]');
  initTablist('.tabs[role="tablist"]');

  /* ---------- 7. 复制按钮 ---------- */
  function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text);
    }
    return new Promise(function (resolve, reject) {
      var area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      var ok = false;
      try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
      document.body.removeChild(area);
      if (ok) resolve();
      else reject(new Error('copy failed'));
    });
  }
  var copyStatus = document.getElementById('copyStatus');
  Array.prototype.forEach.call(document.querySelectorAll('.copy-btn[data-copy]'), function (btn) {
    btn.addEventListener('click', function () {
      var target = document.querySelector(btn.getAttribute('data-copy'));
      if (!target) return;
      var text = target.textContent.replace(/\s+$/, '');
      var label = btn.querySelector('span');
      var originalLabel = label ? label.textContent : '';
      var originalAria = btn.getAttribute('aria-label') || '复制';
      copyText(text).then(function () {
        btn.classList.add('copied');
        btn.setAttribute('aria-label', '已复制');
        if (label) label.textContent = '已复制';
        if (copyStatus) copyStatus.textContent = '已复制到剪贴板';
        window.setTimeout(function () {
          btn.classList.remove('copied');
          btn.setAttribute('aria-label', originalAria);
          if (label) label.textContent = originalLabel || '复制';
          if (copyStatus) copyStatus.textContent = '';
        }, 1600);
      }).catch(function () {
        btn.setAttribute('aria-label', '复制失败');
        if (label) label.textContent = '请手动复制';
        if (copyStatus) copyStatus.textContent = '复制失败，请手动选择文本';
        window.setTimeout(function () {
          btn.setAttribute('aria-label', originalAria);
          if (label) label.textContent = originalLabel || '复制';
          if (copyStatus) copyStatus.textContent = '';
        }, 1800);
      });
    });
  });

  /* ---------- 8. 手势模拟器 ---------- */
  var pad = document.getElementById('gesturePad');
  if (pad) {
    var $ = function (id) { return document.getElementById(id); };
    var valVol = $('valVol');
    var valSpeed = $('valSpeed');
    var valTrack = $('valTrack');
    var valEcho = $('valEcho');
    var stateDot = $('stateDot');
    var stateHint = $('stateHint');
    var barVol = $('barVol');
    var barSpeed = $('barSpeed');
    var trackDots = $('trackDots');
    var eventLog = $('gestureLog');
    var announce = $('gestureAnnounce');
    var trailPath = $('gestureTrailPath');
    var reticle = $('gestureReticle');

    var state = { vol: 72, speed: 1.2, track: 1, tracks: 8, echo: true, playing: true };
    var pads2 = function (n) { return (n < 10 ? '0' : '') + n; };
    var actionMeta = {
      up: { code: 'GESTURE_UP', label: '上扫 · 音量 +5' },
      down: { code: 'GESTURE_DOWN', label: '下扫 · 音量 −5' },
      left: { code: 'GESTURE_LEFT', label: '左扫 · 上一首' },
      right: { code: 'GESTURE_RIGHT', label: '右扫 · 下一首' },
      push: { code: 'GESTURE_PUSH', label: '前推 · 播放 / 暂停' },
      pull: { code: 'GESTURE_PULL', label: '后拉 · 速度 +0.2×' },
      hover: { code: 'GESTURE_HOVER', label: '挥手 · 回声开关' }
    };

    if (trackDots) {
      for (var t = 1; t <= state.tracks; t += 1) {
        var seg = document.createElement('i');
        seg.setAttribute('aria-hidden', 'true');
        trackDots.appendChild(seg);
      }
    }

    function render() {
      if (valVol) valVol.innerHTML = state.vol + '<small>%</small>';
      if (barVol) barVol.style.width = state.vol + '%';
      var speedPct = (state.speed - 0.5) / 1.5 * 100;
      if (barSpeed) barSpeed.style.width = Math.max(0, Math.min(100, speedPct)) + '%';
      if (valSpeed) valSpeed.innerHTML = state.speed.toFixed(1) + '<small>×</small>';
      if (valTrack) valTrack.innerHTML = pads2(state.track) + '<small>/' + pads2(state.tracks) + '</small>';
      if (trackDots) {
        Array.prototype.forEach.call(trackDots.children, function (dot, i) {
          dot.classList.toggle('on', i === state.track - 1);
        });
      }
      if (valEcho) {
        valEcho.textContent = state.echo ? '开' : '关';
        valEcho.classList.toggle('on', state.echo);
      }
      if (stateDot) stateDot.classList.toggle('paused', !state.playing);
      if (stateHint) stateHint.textContent = state.playing ? '播放中' : '暂停';
    }

    function addLog(code, detail) {
      if (!eventLog) return;
      var empty = eventLog.querySelector('.event-empty');
      if (empty) empty.remove();
      var now = new Date();
      var time = now.toLocaleTimeString('zh-CN', { hour12: false });
      var li = document.createElement('li');
      var timeEl = document.createElement('time');
      timeEl.textContent = time;
      var codeEl = document.createElement('span');
      codeEl.className = 'code';
      codeEl.textContent = code;
      var detailEl = document.createElement('span');
      detailEl.textContent = detail;
      li.appendChild(timeEl);
      li.appendChild(codeEl);
      li.appendChild(detailEl);
      eventLog.insertBefore(li, eventLog.firstChild);
      while (eventLog.children.length > 5) eventLog.removeChild(eventLog.lastChild);
    }

    function flashGhost(name) {
      var ghost = pad.querySelector('.pad-ghost[data-ghost="' + name + '"]');
      if (!ghost) return;
      ghost.classList.add('on');
      window.setTimeout(function () { ghost.classList.remove('on'); }, 420);
    }

    function commit(action) {
      var detail = '';
      var human = '';
      switch (action) {
        case 'up':
          state.vol = Math.min(100, state.vol + 5);
          detail = 'VOL ' + state.vol;
          human = '音量 ' + state.vol + '%';
          flashGhost('up');
          break;
        case 'down':
          state.vol = Math.max(0, state.vol - 5);
          detail = 'VOL ' + state.vol;
          human = '音量 ' + state.vol + '%';
          flashGhost('down');
          break;
        case 'right':
          state.track = state.track >= state.tracks ? 1 : state.track + 1;
          detail = 'TRACK ' + pads2(state.track) + '/' + pads2(state.tracks);
          human = '下一首，曲目 ' + state.track;
          flashGhost('right');
          break;
        case 'left':
          state.track = state.track <= 1 ? state.tracks : state.track - 1;
          detail = 'TRACK ' + pads2(state.track) + '/' + pads2(state.tracks);
          human = '上一首，曲目 ' + state.track;
          flashGhost('left');
          break;
        case 'push':
          state.playing = !state.playing;
          detail = state.playing ? 'PLAYING' : 'PAUSED';
          human = state.playing ? '播放' : '暂停';
          break;
        case 'pull':
          state.speed = Math.round((state.speed + 0.2) * 10) / 10;
          if (state.speed > 2.0001) state.speed = 0.5;
          detail = 'SPEED ' + state.speed.toFixed(1);
          human = '速度 ' + state.speed.toFixed(1) + ' 倍';
          break;
        case 'hover':
          state.echo = !state.echo;
          detail = 'ECHO ' + (state.echo ? 'ON' : 'OFF');
          human = '回声' + (state.echo ? '开' : '关');
          break;
        default:
          return;
      }
      render();
      var meta = actionMeta[action];
      if (meta) addLog(meta.code, detail);
      if (announce) announce.textContent = (meta ? meta.label + '。' : '') + human;
    }

    render();

    Array.prototype.forEach.call(document.querySelectorAll('.gesture-btn[data-action]'), function (btn) {
      btn.addEventListener('click', function () { commit(btn.getAttribute('data-action')); });
    });

    /* 指针轨迹 */
    var dragging = false;
    var lastPoint = null;
    var trail = [];
    var trailTimer = null;

    function localPoint(event) {
      var rect = pad.getBoundingClientRect();
      return {
        x: event.clientX - rect.left,
        y: event.clientY - rect.top,
        w: rect.width,
        h: rect.height
      };
    }
    function pushTrail(point) {
      trail.push([point.x / point.w * 100, point.y / point.h * 100]);
      if (trail.length > 56) trail.shift();
      if (trailPath) {
        var d = 'M' + trail.map(function (p) {
          return p[0].toFixed(2) + ' ' + p[1].toFixed(2);
        }).join(' L ');
        trailPath.setAttribute('d', d);
        trailPath.style.opacity = '1';
      }
    }
    function clearTrailSoon() {
      window.clearTimeout(trailTimer);
      trailTimer = window.setTimeout(function () {
        trail = [];
        if (trailPath) {
          trailPath.style.opacity = '0';
          window.setTimeout(function () { trailPath.setAttribute('d', ''); }, 420);
        }
      }, 520);
    }

    pad.addEventListener('pointermove', function (event) {
      var point = localPoint(event);
      if (reticle) {
        reticle.style.left = point.x + 'px';
        reticle.style.top = point.y + 'px';
      }
      if (!dragging) return;
      pushTrail(point);
      if (!lastPoint) { lastPoint = point; return; }
      var dx = point.x - lastPoint.x;
      var dy = point.y - lastPoint.y;
      if (Math.sqrt(dx * dx + dy * dy) < 30) return;
      if (Math.abs(dx) > Math.abs(dy)) commit(dx > 0 ? 'right' : 'left');
      else commit(dy > 0 ? 'down' : 'up');
      lastPoint = point;
    });

    pad.addEventListener('pointerdown', function (event) {
      if (event.button !== undefined && event.button !== 0) return;
      dragging = true;
      lastPoint = localPoint(event);
      trail = [];
      pushTrail(lastPoint);
      pad.classList.add('is-dragging');
      if (pad.setPointerCapture && event.pointerId !== undefined) {
        try { pad.setPointerCapture(event.pointerId); } catch (e) { /* 忽略 */ }
      }
      event.preventDefault();
    });

    function endDrag() {
      if (!dragging) return;
      dragging = false;
      lastPoint = null;
      pad.classList.remove('is-dragging');
      clearTrailSoon();
    }
    pad.addEventListener('pointerup', endDrag);
    pad.addEventListener('pointercancel', endDrag);
    pad.addEventListener('pointerleave', function (event) {
      if (event.pointerType === 'mouse' && !dragging) {
        if (reticle) reticle.style.opacity = '0';
      }
    });
    pad.addEventListener('pointerenter', function () {
      if (reticle) reticle.style.opacity = '';
    });

    pad.addEventListener('keydown', function (event) {
      var map = {
        ArrowUp: 'up',
        ArrowDown: 'down',
        ArrowLeft: 'left',
        ArrowRight: 'right'
      };
      var action = map[event.key];
      if (!action) return;
      event.preventDefault();
      commit(action);
    });
  }

  /* ---------- 9. 复制协议示例的换行保持 ---------- */
  /* pre 内的空白在 textContent 中保留，这里无需额外处理。 */
})();

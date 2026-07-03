(function () {
  var LS = 'rly_theme';
  var LS_STYLE = 'rly_btnstyle';
  var BUTTON_STYLES = [
    { id: 'classic', name: 'Classic' },
    { id: 'soft',    name: 'Soft' },
    { id: 'glass',   name: 'Glass' },
    { id: 'outline', name: 'Outline' },
    { id: 'tactile', name: 'Tactile' },
    { id: 'pill',    name: 'Pill' }
  ];
  var currentHex = '';
  var selectedHex = null;
  var currentStyle = 'classic';
  var selectedStyle = null;
  var isDarkMode = false;
  var schemeData = null;

  function fetchJson(url, cb) {
    var x = new XMLHttpRequest();
    x.open('GET', url, true);
    x.onload = function () {
      try { cb(x.status === 200 ? JSON.parse(x.responseText) : null); }
      catch (e) { cb(null); }
    };
    x.onerror = function () { cb(null); };
    x.send();
  }

  function applyLive(h) {
    var s = document.documentElement.style;
    s.setProperty('--clr-bg',      h[0]);
    s.setProperty('--clr-primary', h[1]);
    s.setProperty('--clr-accent',  h[2]);
    s.setProperty('--clr-active',  h[3]);
    s.setProperty('--clr-text',       h[4] || h[1]);
    s.setProperty('--clr-banner',     h[5] || h[2]);
    s.setProperty('--clr-banner-txt', h[6] || '#ffffff');
    s.setProperty('--clr-btn1-txt',   h[7] || '#ffffff');
    s.setProperty('--clr-btn2-txt',   h[8] || '#ffffff');
  }

  function applyStyleLive(styleId) {
    if (styleId && styleId !== 'classic') {
      document.documentElement.setAttribute('data-btnstyle', styleId);
    } else {
      document.documentElement.removeAttribute('data-btnstyle');
    }
  }

  function updateApplyEnabled() {
    document.getElementById('applyBtn').disabled = !(selectedHex || selectedStyle);
  }

  function makeStyleCard(style) {
    var card = document.createElement('div');
    card.className = 'style-card' +
      (style.id === (selectedStyle || currentStyle) ? ' selected' : '');
    card.setAttribute('data-style', style.id);

    var name = document.createElement('p');
    name.className = 'style-card-name';
    name.textContent = style.name;
    card.appendChild(name);

    var row = document.createElement('div');
    row.className = 'style-card-buttons';
    [{ label: 'OFF', on: false }, { label: 'ON', on: true }].forEach(function (sample) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'pv-btn bs-' + style.id + (sample.on ? ' on' : '');
      btn.textContent = sample.label;
      row.appendChild(btn);
    });
    card.appendChild(row);

    card.addEventListener('click', function () {
      document.querySelectorAll('.style-card.selected').forEach(function (el) {
        el.classList.remove('selected');
      });
      card.classList.add('selected');
      selectedStyle = style.id;
      applyStyleLive(style.id);
      updateApplyEnabled();
    });

    return card;
  }

  function renderStyleGrid() {
    var grid = document.getElementById('styleGrid');
    grid.innerHTML = '';
    BUTTON_STYLES.forEach(function (style) {
      grid.appendChild(makeStyleCard(style));
    });
    document.getElementById('styleSection').removeAttribute('data-loading');
  }

  function hexesMatch(h, storedStr) {
    if (!storedStr) return false;
    var parts = storedStr.split(',');
    if (parts.length !== h.length) return false;
    for (var i = 0; i < h.length; i++) {
      if (h[i].toLowerCase() !== parts[i].toLowerCase()) return false;
    }
    return true;
  }

  function makeCard(scheme) {
    var h = scheme.h;
    var card = document.createElement('div');
    card.className = 'scheme-card' + (hexesMatch(h, currentHex) ? ' selected' : '');
    card.style.background = h[0];
    card.style.borderColor = h[1];

    var banner = document.createElement('div');
    banner.className = 'scheme-card-banner';
    banner.style.background = h[5] || h[2];
    banner.style.color = h[6] || '#ffffff';
    banner.textContent = scheme.t.replace(' · Light', '').replace(' · Dark', '');
    card.appendChild(banner);

    var text = document.createElement('p');
    text.className = 'scheme-card-text';
    text.style.color = h[4] || h[1];
    text.textContent = 'Sample body text for this colour scheme.';
    card.appendChild(text);

    var inp = document.createElement('input');
    inp.type = 'text';
    inp.className = 'scheme-card-input';
    inp.placeholder = 'Input field…';
    inp.readOnly = true;
    inp.style.borderColor = h[1];
    inp.style.color = h[4] || h[1];
    card.appendChild(inp);

    var row = document.createElement('div');
    row.className = 'scheme-card-buttons';

    ['OFF', 'ON'].forEach(function (label, i) {
      var btn = document.createElement('div');
      btn.className = 'scheme-btn';
      btn.style.background = h[i + 2];
      btn.style.borderColor = h[i + 2];
      btn.style.color = h[7 + i] || '#ffffff';
      btn.textContent = label;
      row.appendChild(btn);
    });

    card.appendChild(row);

    card.addEventListener('click', function () {
      document.querySelectorAll('.scheme-card.selected').forEach(function (el) {
        el.classList.remove('selected');
      });
      card.classList.add('selected');
      selectedHex = h;
      applyLive(h);
      document.getElementById('applyBtn').disabled = false;
    });

    return card;
  }

  function isDarkScheme(name) {
    return name.indexOf(' · Dark') !== -1;
  }

  function renderGrid() {
    var container = document.getElementById('themeGrid');
    container.innerHTML = '';

    if (!schemeData) {
      container.textContent = 'Failed to load colour schemes.';
      return;
    }

    Object.keys(schemeData).forEach(function (group) {
      var filtered = schemeData[group].filter(function (s) {
        if (!s.h || s.h.length < 4) return false;
        return isDarkScheme(s.t) === isDarkMode;
      });

      if (filtered.length === 0) return;

      var heading = document.createElement('div');
      heading.className = 'theme-group-title';
      heading.textContent = group;
      container.appendChild(heading);

      var grid = document.createElement('div');
      grid.className = 'theme-grid';
      filtered.forEach(function (scheme) {
        grid.appendChild(makeCard(scheme));
      });
      container.appendChild(grid);
    });
  }

  function render(schemes, currentTheme) {
    if (currentTheme && currentTheme.s) {
      currentStyle = currentTheme.s;
      // Device state is the source of truth; refresh this browser's cache
      // in case the theme was changed from another client.
      applyStyleLive(currentStyle);
      try { localStorage.setItem(LS_STYLE, currentStyle); } catch (e) {}
    }
    renderStyleGrid();
    if (currentTheme && currentTheme.h) {
      currentHex = currentTheme.h;
      applyLive(currentHex.split(','));
      try { localStorage.setItem(LS, JSON.stringify(currentHex.split(','))); } catch (e) {}
      if (schemes) {
        Object.keys(schemes).forEach(function (group) {
          schemes[group].forEach(function (s) {
            if (s.h && hexesMatch(s.h, currentHex) && isDarkScheme(s.t)) {
              isDarkMode = true;
            }
          });
        });
      }
    }
    schemeData = schemes;
    var container = document.getElementById('themeGrid');
    container.removeAttribute('data-loading');
    var modeBtn = document.getElementById('modeBtn');
    if (modeBtn) modeBtn.textContent = isDarkMode ? 'Switch to Light Themes' : 'Switch to Dark Themes';
    renderGrid();
  }

  function showStatus(msg, ok) {
    var el = document.getElementById('themeStatus');
    el.textContent = msg;
    el.style.display = 'block';
    el.style.color = ok ? 'var(--clr-accent)' : 'var(--clr-active)';
  }

  document.getElementById('applyBtn').addEventListener('click', function () {
    if (!selectedHex && !selectedStyle) return;
    var btn = document.getElementById('applyBtn');
    btn.disabled = true;

    var hexStr = selectedHex ? selectedHex.join(',') : currentHex;
    var styleStr = selectedStyle || currentStyle;
    if (!hexStr) {
      showStatus('No colour scheme selected.', false);
      btn.disabled = false;
      return;
    }

    var body = 'h=' + encodeURIComponent(hexStr) +
               '&s=' + encodeURIComponent(styleStr);

    var x = new XMLHttpRequest();
    x.open('POST', '/api/theme', true);
    x.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    x.onload = function () {
      if (x.status === 200) {
        try {
          localStorage.setItem(LS, JSON.stringify(hexStr.split(',')));
          localStorage.setItem(LS_STYLE, styleStr);
        } catch (e) {}
        currentHex = hexStr;
        currentStyle = styleStr;
        showStatus('Theme saved.', true);
      } else {
        showStatus('Save failed (' + x.status + ').', false);
        btn.disabled = false;
      }
    };
    x.onerror = function () {
      showStatus('Save failed.', false);
      btn.disabled = false;
    };
    x.send(body);
  });

  document.getElementById('modeBtn').addEventListener('click', function () {
    isDarkMode = !isDarkMode;
    this.textContent = isDarkMode ? 'Switch to Light Themes' : 'Switch to Dark Themes';
    selectedHex = null;
    updateApplyEnabled();
    renderGrid();
  });

  document.getElementById('backBtn').addEventListener('click', function () {
    history.back();
  });

  var pending = 2;
  var themeData = null;

  function tryRender() {
    if (--pending === 0) render(schemeData, themeData);
  }

  fetchJson('/color-schemes.json', function (d) { schemeData = d; tryRender(); });
  fetchJson('/api/theme',          function (d) { themeData = d;  tryRender(); });
}());

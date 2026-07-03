(function () {
  var LS = 'rly_theme';
  var LS_STYLE = 'rly_btnstyle';
  function apply(h) {
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
  function applyStyle(styleId) {
    if (styleId && styleId !== 'classic') {
      document.documentElement.setAttribute('data-btnstyle', styleId);
    } else {
      document.documentElement.removeAttribute('data-btnstyle');
    }
  }
  var cachedColors = null;
  var cachedStyle = null;
  try {
    var c = localStorage.getItem(LS);
    if (c) { cachedColors = JSON.parse(c); apply(cachedColors); }
    cachedStyle = localStorage.getItem(LS_STYLE);
    if (cachedStyle) applyStyle(cachedStyle);
    if (cachedColors && cachedStyle) return;
  } catch (e) {}
  var x = new XMLHttpRequest();
  x.open('GET', '/api/theme', true);
  x.onload = function () {
    if (x.status !== 200) return;
    try {
      var d = JSON.parse(x.responseText);
      if (d.h && !cachedColors) {
        var colors = d.h.split(',');
        if (colors.length >= 4) {
          apply(colors);
          try { localStorage.setItem(LS, JSON.stringify(colors)); } catch (e) {}
        }
      }
      if (!cachedStyle) {
        var styleId = d.s || 'classic';
        applyStyle(styleId);
        try { localStorage.setItem(LS_STYLE, styleId); } catch (e) {}
      }
    } catch (e) {}
  };
  x.send();
}());

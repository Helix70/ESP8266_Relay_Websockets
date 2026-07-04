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

// Shared across every page (this file loads first in <head> on all of them):
// detects a boot-session change (relay board rebooted, e.g. after a network
// config save) via the bootSessionId the firmware includes in WS/JSON
// payloads, and forces a full reload so stale in-page JS/state doesn't keep
// running against a restarted device.
var bootSessionStorageKey = 'relayBootSessionId:' + window.location.hostname;

function forceRootRefreshAfterBootChange() {
  var cacheBuster = Date.now();
  window.location.replace('/?refresh=' + cacheBuster);
}

function clearRefreshQueryParam() {
  if (!window.history || typeof window.history.replaceState !== 'function') {
    return;
  }

  if (window.location.search.indexOf('refresh=') === -1) {
    return;
  }

  window.history.replaceState(null, '', window.location.pathname + window.location.hash);
}

function trackBootSessionAndRedirectIfChanged(payload) {
  if (!payload || !payload.bootSessionId) {
    return false;
  }

  var incomingBootSessionId = String(payload.bootSessionId);
  var previousBootSessionId = '';

  try {
    previousBootSessionId = window.localStorage.getItem(bootSessionStorageKey) || '';
  } catch (e) {
    previousBootSessionId = '';
  }

  try {
    window.localStorage.setItem(bootSessionStorageKey, incomingBootSessionId);
  } catch (e) {
    // Ignore storage failures; the page still works without boot tracking.
  }

  if (previousBootSessionId && previousBootSessionId !== incomingBootSessionId) {
    forceRootRefreshAfterBootChange();
    return true;
  }

  return false;
}

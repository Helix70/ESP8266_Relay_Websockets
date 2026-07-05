var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var relayLabels = {};
var maxRelays = 0;
var pageContentReady = false;
var DEBUG_LOGS = false;
var renderedRelayCount = 0;
var relayButtonUi = {};
var relayButtonStateCache = {};
// bootSessionStorageKey, forceRootRefreshAfterBootChange, clearRefreshQueryParam,
// and trackBootSessionAndRedirectIfChanged are inlined in this page's own
// <head> (see index.html) rather than loaded from a shared file.

var INTERLOCK_GROUP_COLORS = [
  '#2e8b57', '#1f6feb', '#d97706', '#7c3aed', '#0f766e', '#c2410c', '#b45309', '#4338ca'
];

function debugLog() {
  if (DEBUG_LOGS && window.console && typeof console.log === 'function') {
    console.log.apply(console, arguments);
  }
}

window.addEventListener('load', onLoad);

function initWebSocket() {
  debugLog('Trying to open a WebSocket connection...');
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage; // <-- add this line
}

function onOpen(event) {
  debugLog('Connection opened');
  websocket.send('home');
}

function onClose(event) {
  debugLog('Connection closed');
  setTimeout(initWebSocket, 2000);
}

function ensureRelayUi(relayId) {
  if (relayButtonUi[relayId]) {
    return relayButtonUi[relayId];
  }

  var button = getRelayButtonElement(relayId);
  if (!button) {
    return null;
  }

  relayButtonUi[relayId] = {
    button: button,
    onSpan: button.querySelector('span.on'),
    offSpan: button.querySelector('span.off')
  };

  return relayButtonUi[relayId];
}

function normalizeIncomingButtonState(relayId, buttonState) {
  var prev = relayButtonStateCache[relayId] || {};
  var ui = ensureRelayUi(relayId);

  var normalized = {
    on: !!buttonState.on,
    last: !!buttonState.last,
    d: !!buttonState.d,
    onLabel: '',
    offLabel: '',
    m: 0,
    g: 0,
    p: 1
  };

  if (typeof buttonState.onLabel === 'string' && buttonState.onLabel.length > 0) {
    normalized.onLabel = buttonState.onLabel;
  } else if (typeof prev.onLabel === 'string' && prev.onLabel.length > 0) {
    normalized.onLabel = prev.onLabel;
  } else if (ui && ui.onSpan) {
    normalized.onLabel = ui.onSpan.textContent || '';
  }

  if (typeof buttonState.offLabel === 'string' && buttonState.offLabel.length > 0) {
    normalized.offLabel = buttonState.offLabel;
  } else if (typeof prev.offLabel === 'string' && prev.offLabel.length > 0) {
    normalized.offLabel = prev.offLabel;
  } else if (ui && ui.offSpan) {
    normalized.offLabel = ui.offSpan.textContent || '';
  }

  if (typeof buttonState.m === 'number') {
    normalized.m = buttonState.m;
  } else if (typeof prev.m === 'number') {
    normalized.m = prev.m;
  }

  if (typeof buttonState.g === 'number') {
    normalized.g = buttonState.g;
  } else if (typeof prev.g === 'number') {
    normalized.g = prev.g;
  }

  if (typeof buttonState.p === 'number') {
    normalized.p = buttonState.p;
  } else if (typeof prev.p === 'number') {
    normalized.p = prev.p;
  }

  return normalized;
}

function setButtonState(relayId, buttonState) {
  var ui = ensureRelayUi(relayId);
  if (!ui) {
    return;
  }

  var button = ui.button;

  if (buttonState.on) button.classList.add('on');
  else button.classList.remove('on');

  if (buttonState.last) button.classList.add('last');
  else button.classList.remove('last');

  button.disabled = !!buttonState.d;

  if (ui.onSpan && typeof buttonState.onLabel === 'string' && buttonState.onLabel.length > 0) {
    ui.onSpan.textContent = buttonState.onLabel;
  }
  if (ui.offSpan && typeof buttonState.offLabel === 'string' && buttonState.offLabel.length > 0) {
    ui.offSpan.textContent = buttonState.offLabel;
  }

  relayLabels[relayId] = {
    on: ui.onSpan ? ui.onSpan.textContent : `Relay ${relayId} On`,
    off: ui.offSpan ? ui.offSpan.textContent : `Relay ${relayId} Off`
  };

  setButtonGroupDecoration('#' + button.id, buttonState.m, buttonState.g);
}

function didRelayStateChange(relayId, buttonState) {
  var prev = relayButtonStateCache[relayId];
  if (!prev) {
    relayButtonStateCache[relayId] = {
      on: !!buttonState.on,
      last: !!buttonState.last,
      d: !!buttonState.d,
      onLabel: buttonState.onLabel || '',
      offLabel: buttonState.offLabel || '',
      m: buttonState.m || 0,
      g: buttonState.g || 0,
      p: buttonState.p || 1
    };
    return true;
  }

  var changed = (
    prev.on !== !!buttonState.on ||
    prev.last !== !!buttonState.last ||
    prev.d !== !!buttonState.d ||
    prev.onLabel !== (buttonState.onLabel || '') ||
    prev.offLabel !== (buttonState.offLabel || '') ||
    prev.m !== (buttonState.m || 0) ||
    prev.g !== (buttonState.g || 0)
  );

  if (changed) {
    prev.on = !!buttonState.on;
    prev.last = !!buttonState.last;
    prev.d = !!buttonState.d;
    prev.onLabel = buttonState.onLabel || '';
    prev.offLabel = buttonState.offLabel || '';
    prev.m = buttonState.m || 0;
    prev.g = buttonState.g || 0;
    prev.p = buttonState.p || 1;
  }

  return changed;
}

function getInterlockGroupColor(group) {
  var index = (Math.max(1, group) - 1) % INTERLOCK_GROUP_COLORS.length;
  return INTERLOCK_GROUP_COLORS[index];
}

function setButtonGroupDecoration(selector, mode, group) {
  var button = document.querySelector(selector);
  if (!button) {
    return;
  }

  // Left edge marks group membership (any grouped mode); right edge marks a
  // pulse mode.
  if (group > 0) {
    button.classList.add('interlock-group');
    button.style.setProperty('--group-accent', getInterlockGroupColor(group));
  } else {
    button.classList.remove('interlock-group');
    button.style.removeProperty('--group-accent');
  }

  if (mode === 2) {
    button.classList.add('pulse-mode');
  } else {
    button.classList.remove('pulse-mode');
  }
}

function applyBoardName(boardName) {
  var normalized = (boardName || '').trim();
  if (!normalized) {
    normalized = 'Relay Board';
  }

  var titleElement = document.getElementById('pageTitle');
  if (titleElement) {
    titleElement.textContent = normalized;
  }

  document.title = normalized + ' - Main';
}

function getRelayButtonElement(relayId) {
  return document.getElementById(`button${relayId}`);
}

function getRelayButtonSelector(relayId) {
  var button = getRelayButtonElement(relayId);
  if (!button) {
    return null;
  }
  return `#${button.id}`;
}

function extractRelayNumber(buttonId) {
  var match = buttonId.match(/^button(\d+)$/);
  if (match) {
    return parseInt(match[1], 10);
  }

  return 0;
}

function renderRelayTable(count) {
  var grid = document.getElementById('relay-table');
  if (!grid) {
    return;
  }

  grid.className = 'relay-grid';
  grid.innerHTML = '';

  for (var relayId = 1; relayId <= count; relayId++) {
    var card = document.createElement('div');
    card.className = 'card';
    card.innerHTML = '<button id="button' + relayId + '" class="button"><span class="on">Relay ' + relayId + ' On</span><span class="off">Relay ' + relayId + ' Off</span></button>';
    grid.appendChild(card);
  }

  if (count % 2 !== 0) {
    grid.appendChild(document.createElement('div'));
  }

  var allOffCard = document.createElement('div');
  allOffCard.className = 'card';
  allOffCard.innerHTML = '<button id="alloff" class="button">All Relays Off</button>';
  grid.appendChild(allOffCard);

  var refreshCard = document.createElement('div');
  refreshCard.className = 'card';
  refreshCard.innerHTML = '<button id="home" class="button">Refresh</button>';
  grid.appendChild(refreshCard);

  renderedRelayCount = count;
  relayButtonUi = {};
  relayButtonStateCache = {};
  initButtons();
}

function applySetupState(complete) {
  var table = document.getElementById('relay-table');
  var msg   = document.getElementById('setup-incomplete');
  if (complete) {
    if (table) table.style.display = '';
    if (msg)   msg.style.display   = 'none';
  } else {
    if (table) table.style.display = 'none';
    if (msg) {
      msg.innerHTML = '<p>Board hardware is not configured.</p>'
                    + '<p><a href="/boards.html" class="button">Open Board Manager</a></p>';
      msg.style.display = '';
    }
  }
}

var bootWarningDismissed = false;

function applyBootWarning(message) {
  var banner = document.getElementById('boot-warning');
  if (!banner) return;
  if (!message || bootWarningDismissed) {
    banner.style.display = 'none';
    return;
  }
  banner.innerHTML = '';
  var text = document.createElement('span');
  text.textContent = message;
  banner.appendChild(text);
  var dismiss = document.createElement('button');
  dismiss.textContent = 'Dismiss';
  dismiss.className = 'boot-warning-dismiss';
  dismiss.addEventListener('click', function () {
    bootWarningDismissed = true;
    banner.style.display = 'none';
  });
  banner.appendChild(dismiss);
  banner.style.display = '';
}

function onMessage(event) {
  var jsonObj = JSON.parse(event.data);
  debugLog("Received message: ", jsonObj);

  if (trackBootSessionAndRedirectIfChanged(jsonObj)) {
    return;
  }

  var isPartial = !!jsonObj.partial;

  if (!pageContentReady) {
    document.getElementById('relay-table').removeAttribute('data-loading');
    pageContentReady = true;
  }

  if (jsonObj.boardName) {
    applyBoardName(jsonObj.boardName);
  }

  if (typeof jsonObj.setupComplete !== 'undefined') {
    applySetupState(jsonObj.setupComplete);
  }

  if (typeof jsonObj.bootWarning !== 'undefined') {
    applyBootWarning(jsonObj.bootWarning);
  }

  if (jsonObj.firmwareVersion) {
    var versionItem = document.getElementById('firmwareVersionItem');
    if (versionItem) {
      versionItem.textContent = 'Firmware ' + jsonObj.firmwareVersion;
    }
  }

  var incomingCount = jsonObj.n || (jsonObj.buttons ? jsonObj.buttons.length : 0);
  if (!isPartial && incomingCount > 0 && incomingCount !== renderedRelayCount) {
    maxRelays = incomingCount;
    renderRelayTable(incomingCount);
  }

  var buttons = jsonObj['buttons'];
  if (!buttons) {
    return;
  }

  if (!isPartial && buttons.length > 0 && renderedRelayCount !== buttons.length) {
    maxRelays = buttons.length;
    renderRelayTable(buttons.length);
  }

  for (var i = 0; i < buttons.length; i++) {
    var button = buttons[i];
    debugLog(button);
    if (!ensureRelayUi(button.id)) {
      continue;
    }

    var normalized = normalizeIncomingButtonState(button.id, button);

    if (!didRelayStateChange(button.id, normalized)) {
      continue;
    }

    setButtonState(button.id, normalized);
  }
}

function onLoad(event) {
  clearRefreshQueryParam();
  initWebSocket();
  initMenu();
}

function initMenu() {
  var menuButton = document.getElementById('menuButton');
  var menuDropdown = document.getElementById('menuDropdown');
  if (!menuButton || !menuDropdown) {
    return;
  }

  menuButton.addEventListener('click', function (event) {
    event.stopPropagation();
    var open = menuDropdown.classList.toggle('open');
    menuButton.setAttribute('aria-expanded', open ? 'true' : 'false');
  });

  document.addEventListener('click', function (event) {
    if (!menuDropdown.classList.contains('open')) {
      return;
    }
    if (!menuDropdown.contains(event.target) && event.target !== menuButton && !menuButton.contains(event.target)) {
      menuDropdown.classList.remove('open');
      menuButton.setAttribute('aria-expanded', 'false');
    }
  });

  var rebootMenuItem = document.getElementById('rebootMenuItem');
  if (rebootMenuItem) {
    rebootMenuItem.addEventListener('click', function () {
      menuDropdown.classList.remove('open');
      menuButton.setAttribute('aria-expanded', 'false');
      fetch('/api/reboot', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: ''
      }).catch(function () {
        // Device went offline — expected during reboot.
      });
    });
  }
}

function initButtons() {
  for (var relayId = 1; relayId <= maxRelays; relayId++) {
    var relayButton = getRelayButtonElement(relayId);
    if (relayButton) {
      ensureRelayUi(relayId);
      relayButton.addEventListener('click', toggle);
    }
  }
  var allOffButton = document.getElementById('alloff');
  var homeButton = document.getElementById('home');
  if (allOffButton) {
    allOffButton.addEventListener('click', toggle);
  }
  if (homeButton) {
    homeButton.addEventListener('click', toggle);
  }
}

function toggle(callee) {
  var button = callee.target.closest('.button');
  if (!button) {
    return;
  }

  if (button.id === 'alloff' || button.id === 'home') {
    debugLog('sending message: ', button.id);
    websocket.send(button.id);
    return;
  }

  var relayNum = extractRelayNumber(button.id);
  if (relayNum > 0) {
    debugLog('sending message: ', `button${relayNum}`);
    websocket.send(`button${relayNum}`);
  }
}


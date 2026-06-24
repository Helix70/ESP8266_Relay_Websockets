var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var relayLabels = {};
var maxRelays = 8;

window.addEventListener('load', onLoad);

function initWebSocket() {
  console.log('Trying to open a WebSocket connection...');
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage; // <-- add this line
}

function onOpen(event) {
  console.log('Connection opened');
}

function onClose(event) {
  console.log('Connection closed');
  setTimeout(initWebSocket, 2000);
}

function setButtonOn(selector, value) {
  console.log('selector:' + selector + ' value:' + value);
  if (value) document.querySelector(selector).classList.add('on');
  else document.querySelector(selector).classList.remove('on');
}

function setButtonLast(selector, value) {
  if (value) document.querySelector(selector).classList.add('last');
  else document.querySelector(selector).classList.remove('last');
}

function setButtonDisabled(selector, value) {
  if (value) document.querySelector(selector).disabled = true;
  else document.querySelector(selector).disabled = false;
}

function setButtonLabels(selector, onLabel, offLabel, relayId) {
  var button = document.querySelector(selector);
  if (!button) {
    return;
  }

  var onSpan = button.querySelector('span.on');
  var offSpan = button.querySelector('span.off');

  if (onSpan && typeof onLabel === 'string' && onLabel.length > 0) {
    onSpan.textContent = onLabel;
  }
  if (offSpan && typeof offLabel === 'string' && offLabel.length > 0) {
    offSpan.textContent = offLabel;
  }

  relayLabels[relayId] = {
    on: onSpan ? onSpan.textContent : `Relay ${relayId} On`,
    off: offSpan ? offSpan.textContent : `Relay ${relayId} Off`
  };
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
  return document.getElementById(`button${relayId}`) || document.getElementById(`relay${relayId}toggle`);
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

  match = buttonId.match(/^relay(\d+)toggle$/);
  if (match) {
    return parseInt(match[1], 10);
  }

  return 0;
}

function onMessage(event) {
  var jsonObj = JSON.parse(event.data);
  console.log("Received message: ", jsonObj);

  if (jsonObj.boardName) {
    applyBoardName(jsonObj.boardName);
  }

  var buttons = jsonObj['buttons'];
  for (var i = 0; i < buttons.length; i++) {
    var button = buttons[i];
    console.log(button);
    var btnName = getRelayButtonSelector(button.id);
    if (!btnName) {
      continue;
    }
    setButtonOn(btnName, button.on);
    setButtonLast(btnName, button.last);
    setButtonDisabled(btnName, button.disabled);
    setButtonLabels(btnName, button.onLabel, button.offLabel, button.id);
  }
}

function onLoad(event) {
  initWebSocket();
  initButtons();
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
}

function initButtons() {
  for (var relayId = 1; relayId <= maxRelays; relayId++) {
    var relayButton = getRelayButtonElement(relayId);
    if (relayButton) {
      relayButton.addEventListener('click', toggle);
    }
  }
  document.getElementById('alloff').addEventListener('click', toggle);
  document.getElementById('home').addEventListener('click', toggle);
}

function toggle(callee) {
  var button = callee.target.closest('.button');
  if (!button) {
    return;
  }

  if (button.id === 'alloff' || button.id === 'home') {
    console.log('sending message: ', button.id);
    websocket.send(button.id);
    return;
  }

  var relayNum = extractRelayNumber(button.id);
  if (relayNum > 0) {
    console.log('sending message: ', `button${relayNum}`);
    websocket.send(`button${relayNum}`);
  }
}


var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var relayButtons = [];
var allTemplates = [];

window.addEventListener('load', onLoad);

function applyBoardName(boardName) {
  var normalized = (boardName || '').trim();
  if (!normalized) {
    normalized = 'Relay Board';
  }

  var titleElement = document.getElementById('labelsPageTitle');
  if (titleElement) {
    titleElement.textContent = normalized;
  }

  document.title = normalized + ' - Edit Labels';
}

function onLoad() {
  document.getElementById('saveLabels').addEventListener('click', saveLabels);
  document.getElementById('backLabelsButton').addEventListener('click', function () {
    window.location.href = '/';
  });
  document.getElementById('loadTemplateButton').addEventListener('click', loadSelectedTemplate);
  document.getElementById('saveTemplateButton').addEventListener('click', saveAsTemplate);
  loadTemplateList();
  initWebSocket();
}

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

function onOpen() {
  console.log('Label editor websocket connected');
  websocket.send('home');
}

function onClose() {
  setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
  var jsonObj;
  try {
    jsonObj = JSON.parse(event.data);
  } catch (e) {
    return;
  }

  if (jsonObj.boardName) {
    applyBoardName(jsonObj.boardName);
  }

  relayButtons = jsonObj.buttons || [];
  renderRelayEditor();
  refreshTemplateDropdown();
}

function renderRelayEditor() {
  var grid = document.getElementById('relay-label-grid');
  grid.innerHTML = '';

  for (var i = 0; i < relayButtons.length; i++) {
    var relay = relayButtons[i];
    var relayId = relay.id;

    var card = document.createElement('div');
    card.className = 'relay-label-card';

    var title = document.createElement('h3');
    title.textContent = `Relay ${relayId}`;

    var onLabelTitle = document.createElement('label');
    onLabelTitle.className = 'relay-input-label';
    onLabelTitle.setAttribute('for', `onLabel${relayId}`);
    onLabelTitle.textContent = 'ON Label';

    var onInput = document.createElement('input');
    onInput.type = 'text';
    onInput.id = `onLabel${relayId}`;
    onInput.className = 'relay-label-input';
    onInput.maxLength = 32;
    onInput.value = relay.onLabel || '';

    var offLabelTitle = document.createElement('label');
    offLabelTitle.className = 'relay-input-label';
    offLabelTitle.setAttribute('for', `offLabel${relayId}`);
    offLabelTitle.textContent = 'OFF Label';

    var offInput = document.createElement('input');
    offInput.type = 'text';
    offInput.id = `offLabel${relayId}`;
    offInput.className = 'relay-label-input';
    offInput.maxLength = 32;
    offInput.value = relay.offLabel || '';

    card.appendChild(title);
    card.appendChild(onLabelTitle);
    card.appendChild(onInput);
    card.appendChild(offLabelTitle);
    card.appendChild(offInput);
    grid.appendChild(card);
  }
}

function loadTemplateList() {
  fetch('/api/templates')
    .then(function (r) { return r.json(); })
    .then(function (data) {
      allTemplates = (data.templates || []).slice().sort(function (a, b) {
        return a.title.localeCompare(b.title);
      });
      refreshTemplateDropdown();
    })
    .catch(function () {});
}

function refreshTemplateDropdown() {
  var count = relayButtons.length;
  var select = document.getElementById('templateSelect');
  var previousValue = select.value;
  while (select.options.length > 1) select.remove(1);
  if (count === 0) return;
  allTemplates
    .filter(function (t) { return t.relayCount === count; })
    .forEach(function (t) {
      var opt = document.createElement('option');
      opt.value = t.filename;
      opt.textContent = t.title;
      select.appendChild(opt);
    });
  select.value = previousValue;
}

function loadSelectedTemplate() {
  var select = document.getElementById('templateSelect');
  var filename = select.value;
  if (!filename) return;

  fetch('/templates/' + encodeURIComponent(filename))
    .then(function (r) {
      if (!r.ok) throw new Error('not found');
      return r.json();
    })
    .then(function (data) {
      var labels = data.labels || [];
      var count = Math.min(labels.length, relayButtons.length);
      for (var i = 0; i < count; i++) {
        var relayId = relayButtons[i].id;
        var onInput = document.getElementById('onLabel' + relayId);
        var offInput = document.getElementById('offLabel' + relayId);
        if (onInput) onInput.value = labels[i].on || '';
        if (offInput) offInput.value = labels[i].off || '';
      }
    })
    .catch(function (e) {
      alert('Failed to load template: ' + e.message);
    });
}

function saveAsTemplate() {
  if (!relayButtons.length) {
    alert('Relay labels are not ready yet. Try again in a moment.');
    return;
  }

  var title = document.getElementById('templateTitle').value.trim();
  if (!title) {
    alert('Please enter a template name.');
    return;
  }

  var form = new URLSearchParams();
  form.set('title', title);
  form.set('relayCount', String(relayButtons.length));
  for (var i = 0; i < relayButtons.length; i++) {
    var relayId = relayButtons[i].id;
    var onVal = (document.getElementById('onLabel' + relayId).value || '').trim();
    var offVal = (document.getElementById('offLabel' + relayId).value || '').trim();
    form.set('relay' + relayId + '_on', onVal);
    form.set('relay' + relayId + '_off', offVal);
  }

  var btn = document.getElementById('saveTemplateButton');
  btn.disabled = true;
  btn.textContent = 'Saving...';

  fetch('/api/templates', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: form.toString()
  })
    .then(function (r) {
      return r.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        return { ok: r.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'save failed');
      }
      document.getElementById('templateTitle').value = '';
      loadTemplateList();
      alert('Template "' + title + '" saved.');
    })
    .catch(function (e) {
      alert('Save failed: ' + e.message);
    })
    .finally(function () {
      btn.disabled = false;
      btn.textContent = 'Save Template';
    });
}

function saveLabels() {
  if (!relayButtons.length) {
    alert('Relay labels are not ready yet. Try again in a moment.');
    return;
  }

  var labels = [];
  for (var i = 0; i < relayButtons.length; i++) {
    var relayId = relayButtons[i].id;
    var onValue = document.getElementById(`onLabel${relayId}`).value.trim();
    var offValue = document.getElementById(`offLabel${relayId}`).value.trim();

    if (onValue.length === 0) {
      onValue = offValue;
    }

    labels.push({
      relay: relayId,
      on: onValue,
      off: offValue
    });
  }

  var form = new URLSearchParams();
  for (var i = 0; i < labels.length; i++) {
    form.set('relay' + labels[i].relay + '_on', labels[i].on);
    form.set('relay' + labels[i].relay + '_off', labels[i].off);
  }

  var saveButton = document.getElementById('saveLabels');
  var originalButtonText = saveButton.textContent;
  saveButton.disabled = true;
  saveButton.textContent = 'Saving...';

  fetch('/api/labels', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
    },
    body: form.toString()
  })
    .then(function (response) {
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        return { ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'save failed');
      }
      window.location.href = '/';
    })
    .catch(function (error) {
      alert('Save failed: ' + error.message);
    })
    .finally(function () {
      saveButton.disabled = false;
      saveButton.textContent = originalButtonText;
    });
}

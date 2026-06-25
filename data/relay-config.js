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

  document.title = normalized + ' - Relay Configuration';
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

// Map numeric mode (from websocket) to string used in selects
var MODES = ['onoff', 'interlocked', 'pulsed'];

function modeNumToStr(num) {
  return MODES[num] || 'onoff';
}

function updateModeUI(relayId) {
  var modeEl   = document.getElementById('mode' + relayId);
  var grpEl    = document.getElementById('groupSection' + relayId);
  var pulseEl  = document.getElementById('pulseSection' + relayId);
  if (!modeEl) return;
  var val = modeEl.value;
  if (grpEl)   grpEl.style.display   = (val === 'interlocked') ? 'block' : 'none';
  if (pulseEl) pulseEl.style.display  = (val === 'pulsed')      ? 'block' : 'none';
}

function renderRelayEditor() {
  var grid = document.getElementById('relay-label-grid');
  grid.innerHTML = '';

  var numGroups = Math.floor(relayButtons.length / 2);

  for (var i = 0; i < relayButtons.length; i++) {
    var relay   = relayButtons[i];
    var relayId = relay.id;
    var modeStr = modeNumToStr(relay.mode || 0);

    var card = document.createElement('div');
    card.className = 'relay-label-card';

    var title = document.createElement('h3');
    title.textContent = 'Relay ' + relayId;

    // GPIO info (read-only)
    var gpioInfo = document.createElement('p');
    gpioInfo.className = 'relay-gpio-info';
    var gpioNum = relay.gpio;
    gpioInfo.textContent = (gpioNum !== undefined && gpioNum >= 0)
      ? 'GPIO ' + gpioNum
      : 'Shift Register';

    // ON label
    var onLabelTitle = document.createElement('label');
    onLabelTitle.className = 'relay-input-label';
    onLabelTitle.setAttribute('for', 'onLabel' + relayId);
    onLabelTitle.textContent = 'ON Label';
    var onInput = document.createElement('input');
    onInput.type = 'text';
    onInput.id = 'onLabel' + relayId;
    onInput.className = 'relay-label-input';
    onInput.maxLength = 32;
    onInput.value = relay.onLabel || '';

    // OFF label
    var offLabelTitle = document.createElement('label');
    offLabelTitle.className = 'relay-input-label';
    offLabelTitle.setAttribute('for', 'offLabel' + relayId);
    offLabelTitle.textContent = 'OFF Label';
    var offInput = document.createElement('input');
    offInput.type = 'text';
    offInput.id = 'offLabel' + relayId;
    offInput.className = 'relay-label-input';
    offInput.maxLength = 32;
    offInput.value = relay.offLabel || '';

    // Mode select
    var modeLbl = document.createElement('label');
    modeLbl.className = 'relay-input-label';
    modeLbl.setAttribute('for', 'mode' + relayId);
    modeLbl.textContent = 'Button Mode';
    var modeSelect = document.createElement('select');
    modeSelect.id = 'mode' + relayId;
    modeSelect.className = 'relay-label-input';
    [['onoff', 'Latched (On/Off)'], ['interlocked', 'Interlocked'], ['pulsed', 'Pulsed']].forEach(function (pair) {
      var o = document.createElement('option');
      o.value = pair[0];
      o.textContent = pair[1];
      modeSelect.appendChild(o);
    });
    modeSelect.value = modeStr;
    modeSelect.addEventListener('change', (function (id) {
      return function () { updateModeUI(id); };
    })(relayId));

    // Interlocked group section
    var groupSection = document.createElement('div');
    groupSection.id = 'groupSection' + relayId;
    groupSection.style.display = (modeStr === 'interlocked') ? 'block' : 'none';
    var grpLbl = document.createElement('label');
    grpLbl.className = 'relay-input-label';
    grpLbl.setAttribute('for', 'group' + relayId);
    grpLbl.textContent = 'Group';
    var grpSelect = document.createElement('select');
    grpSelect.id = 'group' + relayId;
    grpSelect.className = 'relay-label-input';
    for (var g = 1; g <= numGroups; g++) {
      var go = document.createElement('option');
      go.value = String(g);
      go.textContent = 'Group ' + g;
      grpSelect.appendChild(go);
    }
    grpSelect.value = String(relay.group || 1);
    groupSection.appendChild(grpLbl);
    groupSection.appendChild(grpSelect);

    // Pulsed section
    var pulseSection = document.createElement('div');
    pulseSection.id = 'pulseSection' + relayId;
    pulseSection.style.display = (modeStr === 'pulsed') ? 'block' : 'none';
    var pulseLbl = document.createElement('label');
    pulseLbl.className = 'relay-input-label';
    pulseLbl.setAttribute('for', 'pulseTimeout' + relayId);
    pulseLbl.textContent = 'Duration (1–30 seconds)';
    var ptInput = document.createElement('input');
    ptInput.type = 'number';
    ptInput.id = 'pulseTimeout' + relayId;
    ptInput.className = 'relay-label-input';
    ptInput.min = '1';
    ptInput.max = '30';
    ptInput.value = String(relay.pulseTimeout || 1);
    pulseSection.appendChild(pulseLbl);
    pulseSection.appendChild(ptInput);

    card.appendChild(title);
    card.appendChild(gpioInfo);
    card.appendChild(onLabelTitle);
    card.appendChild(onInput);
    card.appendChild(offLabelTitle);
    card.appendChild(offInput);
    card.appendChild(modeLbl);
    card.appendChild(modeSelect);
    card.appendChild(groupSection);
    card.appendChild(pulseSection);
    grid.appendChild(card);
  }
}

// Collect relay config from the editor, optionally validating interlocked groups.
// Returns array of {relay, on, off, mode, group, pulseTimeout}.
// If validate=true, lone interlocked relays are converted to 'onoff' and the user is notified.
function collectRelayConfig(validate) {
  var configs     = [];
  var groupCounts = {};

  for (var i = 0; i < relayButtons.length; i++) {
    var relayId   = relayButtons[i].id;
    var onValue   = (document.getElementById('onLabel' + relayId).value || '').trim();
    var offValue  = (document.getElementById('offLabel' + relayId).value || '').trim();
    if (onValue.length === 0) onValue = offValue;

    var modeEl  = document.getElementById('mode' + relayId);
    var mode    = modeEl ? modeEl.value : 'onoff';
    var group   = 0;
    var pt      = 1;

    if (mode === 'interlocked') {
      group = parseInt((document.getElementById('group' + relayId).value || '1'), 10) || 1;
      groupCounts[group] = (groupCounts[group] || 0) + 1;
    } else if (mode === 'pulsed') {
      pt = parseInt((document.getElementById('pulseTimeout' + relayId).value || '1'), 10) || 1;
      if (pt < 1)  pt = 1;
      if (pt > 30) pt = 30;
    }

    configs.push({ relay: relayId, on: onValue, off: offValue, mode: mode, group: group, pulseTimeout: pt });
  }

  if (validate) {
    var converted = [];
    configs.forEach(function (cfg) {
      if (cfg.mode === 'interlocked' && (groupCounts[cfg.group] || 0) < 2) {
        cfg.mode  = 'onoff';
        cfg.group = 0;
        converted.push('Relay ' + cfg.relay);
        // Update the UI to match
        var modeEl = document.getElementById('mode' + cfg.relay);
        if (modeEl) {
          modeEl.value = 'onoff';
          updateModeUI(cfg.relay);
        }
      }
    });
    if (converted.length > 0) {
      alert('Converted to Latched (no group partner): ' + converted.join(', '));
    }
  }

  return configs;
}

// Apply a template's label array to the editor inputs
function applyTemplateLabels(labels) {
  var count = Math.min(labels.length, relayButtons.length);
  for (var i = 0; i < count; i++) {
    var relayId = relayButtons[i].id;
    var label   = labels[i] || {};

    var onEl  = document.getElementById('onLabel'  + relayId);
    var offEl = document.getElementById('offLabel' + relayId);
    if (onEl)  onEl.value  = label.on  || '';
    if (offEl) offEl.value = label.off || '';

    // Mode — templates may store it as string or number
    var modeRaw = label.mode;
    var modeStr = (typeof modeRaw === 'number') ? modeNumToStr(modeRaw) : (modeRaw || 'onoff');
    var modeEl  = document.getElementById('mode' + relayId);
    if (modeEl) {
      modeEl.value = modeStr;
      updateModeUI(relayId);
    }

    if (modeStr === 'interlocked') {
      var grpEl = document.getElementById('group' + relayId);
      if (grpEl) grpEl.value = String(label.group || 1);
    } else if (modeStr === 'pulsed') {
      var ptEl = document.getElementById('pulseTimeout' + relayId);
      if (ptEl) ptEl.value = String(label.pulseTimeout || 1);
    }
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
      document.querySelector('.labels-page').removeAttribute('data-loading');
    })
    .catch(function () {
      document.querySelector('.labels-page').removeAttribute('data-loading');
    });
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
  var select   = document.getElementById('templateSelect');
  var filename = select.value;
  if (!filename) return;

  fetch('/templates/' + encodeURIComponent(filename))
    .then(function (r) {
      if (!r.ok) throw new Error('not found');
      return r.json();
    })
    .then(function (data) {
      applyTemplateLabels(data.labels || []);
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

  var configs = collectRelayConfig(true);

  var form = new URLSearchParams();
  form.set('title', title);
  form.set('relayCount', String(relayButtons.length));
  configs.forEach(function (cfg) {
    form.set('relay' + cfg.relay + '_on',           cfg.on);
    form.set('relay' + cfg.relay + '_off',          cfg.off);
    form.set('relay' + cfg.relay + '_mode',         cfg.mode);
    form.set('relay' + cfg.relay + '_group',        String(cfg.group));
    form.set('relay' + cfg.relay + '_pulseTimeout', String(cfg.pulseTimeout));
  });

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

  var configs = collectRelayConfig(true);

  var form = new URLSearchParams();
  configs.forEach(function (cfg) {
    form.set('relay' + cfg.relay + '_on',           cfg.on);
    form.set('relay' + cfg.relay + '_off',          cfg.off);
    form.set('relay' + cfg.relay + '_mode',         cfg.mode);
    form.set('relay' + cfg.relay + '_group',        String(cfg.group));
    form.set('relay' + cfg.relay + '_pulseTimeout', String(cfg.pulseTimeout));
  });

  var saveButton = document.getElementById('saveLabels');
  var originalButtonText = saveButton.textContent;
  saveButton.disabled = true;
  saveButton.textContent = 'Saving...';

  fetch('/api/labels', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
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

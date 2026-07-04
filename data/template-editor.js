var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;
var relayButtons = [];
var allTemplates = [];
var selectedTemplateFilename = '';
var loadedTemplateFilename = '';
var loadedTemplateSnapshot = '';
var restartRedirectDelayTimer = null;
var restartRedirectPollTimer = null;
var websocketEverConnected = false;
var activeLabelInput = null;
var suppressTemplateSelectChange = false;
var invalidTemplateFilenames = {};
// bootSessionStorageKey, forceRootRefreshAfterBootChange, clearRefreshQueryParam,
// and trackBootSessionAndRedirectIfChanged live in theme-apply.js (loaded on
// every page before this script).

function normalizeTemplateFilenameRef(value) {
  var normalized = String(value || '').trim();
  if (!normalized) {
    return '';
  }
  if (normalized.indexOf('/templates/') === 0) {
    normalized = normalized.substring('/templates/'.length);
  }
  return normalized;
}

var INTERLOCK_GROUP_COLORS = [
  '#2e8b57', '#1f6feb', '#d97706', '#7c3aed', '#0f766e', '#c2410c', '#b45309', '#4338ca'
];

// Indexed by the numeric mode from the firmware (0=Latched, 1=Interlocked,
// 2=Pulsed, 3=Momentary [reserved, shown as Latched]).
var MODES = ['L', 'I', 'P', 'L'];

var MODE_HELP_CONTENT = [
  {
    name: 'Latched',
    badge: 'group optional',
    required: false,
    text: 'Switch on or off manually. If a group is assigned: switching a latched button on switches off '
      + 'and disables the other latched buttons in the group; switching it back off re-enables them.'
  },
  {
    name: 'Interlocked',
    badge: 'group required',
    required: true,
    text: 'Switch on or off manually. Switching an interlocked button on switches off all other buttons in '
      + 'the same group (they are not disabled — they can still be switched on again directly). '
      + 'Switching it off has no special effect on the group.'
  },
  {
    name: 'Pulsed',
    badge: 'group optional',
    required: false,
    text: 'Switch on manually; it switches itself off after its pulse timeout. If a group is assigned: '
      + 'switching a pulsed button on disables the other pulsed buttons in the group until the pulse '
      + 'expires, at which point they are enabled again.'
  }
];

function buildModeHelpContent() {
  var container = document.getElementById('modeHelpBody');
  if (!container || container.childElementCount) {
    return;
  }
  MODE_HELP_CONTENT.forEach(function (mode) {
    var entry = document.createElement('div');
    entry.className = 'mode-help-entry';

    var heading = document.createElement('div');
    heading.className = 'mode-help-heading';

    var name = document.createElement('span');
    name.className = 'mode-help-name';
    name.textContent = mode.name;
    heading.appendChild(name);

    var badge = document.createElement('span');
    badge.className = 'mode-help-badge' + (mode.required ? ' mode-help-badge-required' : '');
    badge.textContent = mode.badge;
    heading.appendChild(badge);

    entry.appendChild(heading);

    var text = document.createElement('p');
    text.textContent = mode.text;
    entry.appendChild(text);

    container.appendChild(entry);
  });
}

function openModeHelpModal() {
  buildModeHelpContent();
  var overlay = document.getElementById('modeHelpOverlay');
  if (overlay) overlay.style.display = 'flex';
}

function closeModeHelpModal() {
  var overlay = document.getElementById('modeHelpOverlay');
  if (overlay) overlay.style.display = 'none';
}

function initModeHelpModal() {
  var overlay = document.getElementById('modeHelpOverlay');
  var closeBtn = document.getElementById('modeHelpClose');
  if (closeBtn) closeBtn.addEventListener('click', closeModeHelpModal);
  if (overlay) {
    overlay.addEventListener('click', function (event) {
      if (event.target === overlay) closeModeHelpModal();
    });
  }
  document.addEventListener('keydown', function (event) {
    if (event.key === 'Escape') closeModeHelpModal();
  });
}

window.addEventListener('load', onLoad);

function onLoad() {
  clearRefreshQueryParam();
  document.getElementById('saveTemplateButton').addEventListener('click', saveAsTemplate);
  document.getElementById('templateSelect').addEventListener('change', function () {
    if (suppressTemplateSelectChange) {
      return;
    }

    var filename = getSelectedTemplateFilename();
    if (hasUnsavedTemplateChanges()) {
      var proceed = window.confirm('Discard unsaved template changes and load another template?');
      if (!proceed) {
        suppressTemplateSelectChange = true;
        document.getElementById('templateSelect').value = loadedTemplateFilename || '';
        suppressTemplateSelectChange = false;
        return;
      }
    }

    if (!filename) {
      loadedTemplateFilename = '';
      loadedTemplateSnapshot = '';
      updateTemplateEditorStatus();
      return;
    }
    loadTemplateByFilename(filename);
  });
  document.getElementById('backTemplateEditorButton').addEventListener('click', function () {
    window.location.href = '/relay-config.html';
  });

  Array.prototype.forEach.call(document.querySelectorAll('.label-symbol-button[data-symbol]'), function (button) {
    button.addEventListener('click', function () {
      insertSymbolIntoActiveLabel(button.getAttribute('data-symbol') || '');
    });
  });

  document.getElementById('relay-label-grid').addEventListener('focusin', function (event) {
    var target = event.target;
    if (target && target.tagName === 'INPUT' && target.type === 'text') {
      activeLabelInput = target;
    }
  });

  initModeHelpModal();
  loadTemplateList();
  initWebSocket();

  window.addEventListener('beforeunload', function (event) {
    if (!hasUnsavedTemplateChanges()) {
      return;
    }
    event.preventDefault();
    event.returnValue = '';
  });
}

function buildCurrentTemplateSnapshot() {
  if (!relayButtons.length) {
    return '';
  }

  var payload = [];
  for (var i = 0; i < relayButtons.length; i++) {
    var relayId = relayButtons[i].id;
    var onEl = document.getElementById('onLabel' + relayId);
    var offEl = document.getElementById('offLabel' + relayId);
    var modeEl = document.getElementById('mode' + relayId);
    var groupEl = document.getElementById('group' + relayId);
    var pulseEl = document.getElementById('pulse' + relayId);

    payload.push({
      relay: relayId,
      on: onEl ? String(onEl.value || '').trim() : '',
      off: offEl ? String(offEl.value || '').trim() : '',
      mode: modeEl ? String(modeEl.value || 'L') : 'L',
      group: groupEl ? (parseInt(groupEl.value, 10) || 0) : 0,
      pulse: pulseEl ? (parseInt(pulseEl.value, 10) || 0) : 0
    });
  }

  return JSON.stringify(payload);
}

function hasUnsavedTemplateChanges() {
  if (!loadedTemplateFilename || !loadedTemplateSnapshot) {
    return false;
  }
  return buildCurrentTemplateSnapshot() !== loadedTemplateSnapshot;
}

function applyBoardName(boardName) {
  var normalized = (boardName || '').trim();
  if (!normalized) {
    normalized = 'Relay Board';
  }

  var titleElement = document.getElementById('templateEditorTitle');
  if (titleElement) {
    titleElement.textContent = normalized;
  }

  document.title = normalized + ' - Template Editor';
}

function insertSymbolIntoActiveLabel(symbol) {
  if (!symbol) {
    return;
  }

  if (!activeLabelInput || !document.body.contains(activeLabelInput)) {
    alert('Click into an ON/OFF label field first.');
    return;
  }

  var input = activeLabelInput;
  var start = input.selectionStart;
  var end = input.selectionEnd;

  if (typeof start !== 'number' || typeof end !== 'number') {
    input.value = (input.value || '') + symbol;
    input.focus();
    return;
  }

  var value = input.value || '';
  input.value = value.substring(0, start) + symbol + value.substring(end);
  var cursor = start + symbol.length;
  input.focus();
  input.setSelectionRange(cursor, cursor);
}

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

function stopRestartRedirectWatcher() {
  if (restartRedirectDelayTimer) {
    clearTimeout(restartRedirectDelayTimer);
    restartRedirectDelayTimer = null;
  }
  if (restartRedirectPollTimer) {
    clearInterval(restartRedirectPollTimer);
    restartRedirectPollTimer = null;
  }
}

function startRestartRedirectWatcher() {
  if (window.location.pathname === '/') {
    return;
  }
  if (restartRedirectDelayTimer || restartRedirectPollTimer) {
    return;
  }

  restartRedirectDelayTimer = setTimeout(function () {
    restartRedirectDelayTimer = null;

    if (restartRedirectPollTimer) {
      return;
    }

    restartRedirectPollTimer = setInterval(function () {
      fetch('/', { cache: 'no-store' })
        .then(function (response) {
          if (response.ok) {
            stopRestartRedirectWatcher();
            window.location.href = '/';
          }
        })
        .catch(function () {
          // Keep polling until reachable.
        });
    }, 1200);
  }, 1500);
}

function onOpen() {
  websocketEverConnected = true;
  stopRestartRedirectWatcher();
  websocket.send('home');
}

function onClose() {
  if (websocketEverConnected) {
    startRestartRedirectWatcher();
  }
  setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
  var jsonObj;
  try {
    jsonObj = JSON.parse(event.data);
  } catch (e) {
    return;
  }

  if (trackBootSessionAndRedirectIfChanged(jsonObj)) {
    return;
  }

  if (jsonObj.partial) {
    return;
  }

  if (jsonObj.boardName) {
    applyBoardName(jsonObj.boardName);
  }

  if (jsonObj.hasOwnProperty('selectedRelayTemplate')) {
    selectedTemplateFilename = normalizeTemplateFilenameRef(jsonObj.selectedRelayTemplate || '');
  }

  var buttons = jsonObj.buttons || [];
  if (buttons.length > 0) {
    relayButtons = buttons;
    renderRelayEditor();
    refreshTemplateDropdown();

    if (!loadedTemplateFilename && selectedTemplateFilename && !invalidTemplateFilenames[selectedTemplateFilename]) {
      loadTemplateByFilename(selectedTemplateFilename);
    }
  }
}

function extractTemplateLabels(doc) {
  if (Array.isArray(doc)) {
    return doc;
  }
  if (doc && Array.isArray(doc.l)) {
    return doc.l;
  }
  return [];
}

function modeNumToStr(num) {
  return MODES[num] || 'L';
}

function getInterlockGroupColor(group) {
  var index = (Math.max(1, group) - 1) % INTERLOCK_GROUP_COLORS.length;
  return INTERLOCK_GROUP_COLORS[index];
}

function modeRequiresGroup(mode) {
  return mode === 'I';
}

function modeUsesPulse(mode) {
  return mode === 'P';
}

function updateGroupDecoration(relayId) {
  var card = document.getElementById('relayCard' + relayId);
  var groupEl = document.getElementById('group' + relayId);
  if (!card) {
    return;
  }

  var group = groupEl ? (parseInt(groupEl.value, 10) || 0) : 0;
  if (group > 0) {
    card.classList.add('interlock-group');
    card.style.setProperty('--group-accent', getInterlockGroupColor(group));
  } else {
    card.classList.remove('interlock-group');
    card.style.removeProperty('--group-accent');
  }
}

function updateModeUI(relayId) {
  var modeEl = document.getElementById('mode' + relayId);
  var grpEl = document.getElementById('groupSection' + relayId);
  var grpLbl = document.getElementById('groupLabel' + relayId);
  var pulseEl = document.getElementById('pulseSection' + relayId);
  if (!modeEl) return;
  var val = modeEl.value;
  // Every mode can carry a group: optional for Latched/Pulsed, required for
  // Interlocked.
  if (grpEl) grpEl.style.display = 'block';
  if (grpLbl) grpLbl.textContent = modeRequiresGroup(val) ? 'Group (required)' : 'Group (optional)';
  if (pulseEl) pulseEl.style.display = modeUsesPulse(val) ? 'block' : 'none';
  updateGroupDecoration(relayId);
}

function renderRelayEditor() {
  var grid = document.getElementById('relay-label-grid');
  if (!grid) {
    return;
  }

  grid.innerHTML = '';

  var numGroups = Math.floor(relayButtons.length / 2);

  for (var i = 0; i < relayButtons.length; i++) {
    var relay = relayButtons[i];
    var relayId = relay.id;
    var modeStr = modeNumToStr(relay.mode || 0);

    var card = document.createElement('div');
    card.className = 'relay-label-card';
    card.id = 'relayCard' + relayId;

    var title = document.createElement('h3');
    title.textContent = 'Relay ' + relayId;

    var gpioInfo = document.createElement('p');
    gpioInfo.className = 'relay-gpio-info';
    var gpioNum = relay.gpio;
    gpioInfo.textContent = (gpioNum !== undefined && gpioNum >= 0) ? ('GPIO ' + gpioNum) : 'Shift Register';

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

    var modeLblRow = document.createElement('div');
    modeLblRow.className = 'relay-input-label-row';

    var modeLbl = document.createElement('label');
    modeLbl.className = 'relay-input-label';
    modeLbl.setAttribute('for', 'mode' + relayId);
    modeLbl.textContent = 'Button Mode';
    modeLblRow.appendChild(modeLbl);

    var modeHelpBtn = document.createElement('button');
    modeHelpBtn.type = 'button';
    modeHelpBtn.className = 'help-icon-button';
    modeHelpBtn.textContent = '?';
    modeHelpBtn.setAttribute('aria-label', 'Explain button modes');
    modeHelpBtn.addEventListener('click', openModeHelpModal);
    modeLblRow.appendChild(modeHelpBtn);

    var modeSelect = document.createElement('select');
    modeSelect.id = 'mode' + relayId;
    modeSelect.className = 'relay-label-input';
    [['L', 'Latched (On/Off)'], ['I', 'Interlocked'], ['P', 'Pulsed']].forEach(function (pair) {
      var o = document.createElement('option');
      o.value = pair[0];
      o.textContent = pair[1];
      modeSelect.appendChild(o);
    });
    modeSelect.value = modeStr;
    modeSelect.addEventListener('change', (function (id) {
      return function () { updateModeUI(id); };
    })(relayId));

    var groupSection = document.createElement('div');
    groupSection.id = 'groupSection' + relayId;
    groupSection.style.display = 'block';

    var grpLbl = document.createElement('label');
    grpLbl.className = 'relay-input-label';
    grpLbl.id = 'groupLabel' + relayId;
    grpLbl.setAttribute('for', 'group' + relayId);
    grpLbl.textContent = modeRequiresGroup(modeStr) ? 'Group (required)' : 'Group (optional)';

    var grpSelect = document.createElement('select');
    grpSelect.id = 'group' + relayId;
    grpSelect.className = 'relay-label-input';
    var noneOpt = document.createElement('option');
    noneOpt.value = '0';
    noneOpt.textContent = 'None';
    grpSelect.appendChild(noneOpt);
    for (var g = 1; g <= numGroups; g++) {
      var go = document.createElement('option');
      go.value = String(g);
      go.textContent = 'Group ' + g;
      grpSelect.appendChild(go);
    }
    grpSelect.value = String(relay.g || 0);
    grpSelect.addEventListener('change', (function (id) {
      return function () { updateGroupDecoration(id); };
    })(relayId));

    groupSection.appendChild(grpLbl);
    groupSection.appendChild(grpSelect);

    var pulseSection = document.createElement('div');
    pulseSection.id = 'pulseSection' + relayId;
    pulseSection.style.display = modeUsesPulse(modeStr) ? 'block' : 'none';

    var pulseLbl = document.createElement('label');
    pulseLbl.className = 'relay-input-label';
    pulseLbl.setAttribute('for', 'pulse' + relayId);
    pulseLbl.textContent = 'Duration (1-30 seconds)';

    var ptInput = document.createElement('input');
    ptInput.type = 'number';
    ptInput.id = 'pulse' + relayId;
    ptInput.className = 'relay-label-input';
    ptInput.min = '1';
    ptInput.max = '30';
    ptInput.value = String(relay.p || 1);

    pulseSection.appendChild(pulseLbl);
    pulseSection.appendChild(ptInput);

    card.appendChild(title);
    card.appendChild(gpioInfo);
    card.appendChild(onLabelTitle);
    card.appendChild(onInput);
    card.appendChild(offLabelTitle);
    card.appendChild(offInput);
    card.appendChild(modeLblRow);
    card.appendChild(modeSelect);
    card.appendChild(groupSection);
    card.appendChild(pulseSection);
    grid.appendChild(card);

    updateGroupDecoration(relayId);
  }

  var page = document.querySelector('.labels-page');
  if (page) {
    page.removeAttribute('data-loading');
  }
}

function collectRelayConfig(validate) {
  var configs = [];
  var groupCounts = {};

  for (var i = 0; i < relayButtons.length; i++) {
    var relayId = relayButtons[i].id;
    var onValue = (document.getElementById('onLabel' + relayId).value || '').trim();
    var offValue = (document.getElementById('offLabel' + relayId).value || '').trim();
    if (onValue.length === 0) onValue = offValue;

    var modeEl = document.getElementById('mode' + relayId);
    var mode = modeEl ? modeEl.value : 'L';
    var groupEl = document.getElementById('group' + relayId);
    var group = groupEl ? (parseInt(groupEl.value, 10) || 0) : 0;
    var pt = 0;

    if (modeUsesPulse(mode)) {
      pt = parseInt((document.getElementById('pulse' + relayId).value || '1'), 10) || 1;
      if (pt < 1) pt = 1;
      if (pt > 30) pt = 30;
    }

    // Count members per (mode, group) so interlocked families can be validated
    // for having at least one partner in the group.
    if (modeRequiresGroup(mode) && group > 0) {
      var key = mode + ':' + group;
      groupCounts[key] = (groupCounts[key] || 0) + 1;
    }

    configs.push({ relay: relayId, o: onValue, f: offValue, m: mode, g: group, p: pt });
  }

  if (validate) {
    var converted = [];
    configs.forEach(function (cfg) {
      if (modeRequiresGroup(cfg.m) && (!cfg.g || (groupCounts[cfg.m + ':' + cfg.g] || 0) < 2)) {
        cfg.m = 'L';
        cfg.g = 0;
        cfg.p = 0;
        converted.push('Relay ' + cfg.relay);
        var modeEl = document.getElementById('mode' + cfg.relay);
        if (modeEl) {
          modeEl.value = 'L';
          updateModeUI(cfg.relay);
        }
      }
    });
    if (converted.length > 0) {
      alert('Converted to Latched (Interlocked needs a group with at least two members): ' + converted.join(', '));
    }
  }

  return configs;
}

function loadTemplateList() {
  fetch('/api/templates')
    .then(function (r) { return r.json(); })
    .then(function (data) {
      allTemplates = (data.templates || []).slice().sort(function (a, b) {
        return String(a.t || '').localeCompare(String(b.t || ''));
      });
      selectedTemplateFilename = normalizeTemplateFilenameRef(data.selectedTemplate || selectedTemplateFilename || '');
      refreshTemplateDropdown();
    });
}

function refreshTemplateDropdown() {
  var select = document.getElementById('templateSelect');
  var previousValue = select.value;
  var activeTemplate = normalizeTemplateFilenameRef(selectedTemplateFilename);
  while (select.options.length > 1) {
    select.remove(1);
  }

  var count = relayButtons.length;
  var visibleTemplates = allTemplates.filter(function (t) {
    return count > 0 ? (t.n === count) : true;
  });

  visibleTemplates.forEach(function (t) {
    var opt = document.createElement('option');
    opt.value = t.filename;
    opt.textContent = t.t + ((activeTemplate && t.filename === activeTemplate) ? ' (Active)' : '');
    select.appendChild(opt);
  });

  if (loadedTemplateFilename && visibleTemplates.some(function (t) { return t.filename === loadedTemplateFilename; })) {
    suppressTemplateSelectChange = true;
    select.value = loadedTemplateFilename;
    suppressTemplateSelectChange = false;
  } else if (selectedTemplateFilename && visibleTemplates.some(function (t) { return t.filename === selectedTemplateFilename; })) {
    suppressTemplateSelectChange = true;
    select.value = selectedTemplateFilename;
    suppressTemplateSelectChange = false;
  } else if (previousValue && visibleTemplates.some(function (t) { return t.filename === previousValue; })) {
    suppressTemplateSelectChange = true;
    select.value = previousValue;
    suppressTemplateSelectChange = false;
  } else {
    suppressTemplateSelectChange = true;
    select.value = '';
    suppressTemplateSelectChange = false;
  }

  updateTemplateEditorStatus();
}

function updateTemplateEditorStatus() {
  var status = document.getElementById('templateEditorStatus');
  if (!status) {
    return;
  }

  if (!loadedTemplateFilename) {
    status.textContent = 'Loaded template: none';
    return;
  }

  var meta = allTemplates.find(function (t) { return t.filename === loadedTemplateFilename; });
  var title = meta ? meta.t : loadedTemplateFilename;
  status.textContent = 'Loaded template: ' + title;
}

function getSelectedTemplateFilename() {
  var select = document.getElementById('templateSelect');
  return select && select.value ? select.value : '';
}

function applyTemplateLabels(labels) {
  var count = Math.min(labels.length, relayButtons.length);
  for (var i = 0; i < count; i++) {
    var relayId = relayButtons[i].id;
    var label = labels[i] || {};

    var onEl = document.getElementById('onLabel' + relayId);
    var offEl = document.getElementById('offLabel' + relayId);
    if (onEl) onEl.value = label.o || '';
    if (offEl) offEl.value = label.f || '';

    var modeRaw = label.m;
    var modeStr = (typeof modeRaw === 'number') ? modeNumToStr(modeRaw) : (modeRaw || 'L');
    if (modeStr === 'onoff') {
      modeStr = 'L';
    }

    if (modeStr === 'I') {
      var grpEl = document.getElementById('group' + relayId);
      if (grpEl) {
        grpEl.value = String(label.g || 1);
      }
    }

    var modeEl = document.getElementById('mode' + relayId);
    if (modeEl) {
      modeEl.value = modeStr;
      updateModeUI(relayId);
    }

    if (modeStr === 'P') {
      var ptEl = document.getElementById('pulse' + relayId);
      if (ptEl) ptEl.value = String(label.p || 1);
    }

    updateGroupDecoration(relayId);
  }
}

function loadTemplateByFilename(filename) {
  if (!filename) {
    return;
  }

  fetch('/templates/' + encodeURIComponent(filename), { cache: 'no-store' })
    .then(function (r) {
      if (!r.ok) {
        throw new Error('template not found (HTTP ' + r.status + ')');
      }
      return r.json();
    })
    .then(function (doc) {
      var labels = extractTemplateLabels(doc);
      if (!labels.length) {
        invalidTemplateFilenames[filename] = true;
        loadedTemplateFilename = '';
        loadedTemplateSnapshot = '';
        updateTemplateEditorStatus();
        refreshTemplateDropdown();
        throw new Error('template labels missing');
      }
      delete invalidTemplateFilenames[filename];
      applyTemplateLabels(labels);
      loadedTemplateFilename = filename;
      loadedTemplateSnapshot = buildCurrentTemplateSnapshot();
      refreshTemplateDropdown();
    })
    .catch(function (e) {
      alert('Load failed: ' + e.message + '. You can still edit current labels and save to overwrite/repair this template.');
    });
}

function getTemplateMetaByFilename(filename) {
  if (!filename) {
    return null;
  }
  return allTemplates.find(function (t) {
    return t.filename === filename;
  }) || null;
}

function buildDefaultNewTemplateName() {
  var relayCount = relayButtons.length;
  var base = relayCount > 0 ? ('Relay Template ' + relayCount + '-Relay') : 'Relay Template';

  var titles = {};
  allTemplates.forEach(function (t) {
    var title = String(t.t || '').trim().toLowerCase();
    if (title.length > 0) {
      titles[title] = true;
    }
  });

  if (!titles[base.toLowerCase()]) {
    return base;
  }

  for (var i = 2; i < 200; i++) {
    var candidate = base + ' ' + i;
    if (!titles[candidate.toLowerCase()]) {
      return candidate;
    }
  }

  return base;
}

function getDefaultTemplateNameForSave() {
  var loadedMeta = getTemplateMetaByFilename(loadedTemplateFilename);
  if (loadedMeta && loadedMeta.t) {
    return loadedMeta.t;
  }

  var selectedMeta = getTemplateMetaByFilename(getSelectedTemplateFilename());
  if (selectedMeta && selectedMeta.t) {
    return selectedMeta.t;
  }

  return buildDefaultNewTemplateName();
}

function saveAsTemplate() {
  if (!relayButtons.length) {
    alert('Relay labels are not ready yet. Try again in a moment.');
    return;
  }

  var defaultTitle = getDefaultTemplateNameForSave();
  var promptedTitle = window.prompt('Template name:', defaultTitle);
  if (promptedTitle === null) {
    return;
  }

  var title = promptedTitle.trim();
  if (!title) {
    alert('Please enter a template name.');
    return;
  }
  if (title.length > 40) {
    title = title.substring(0, 40).trim();
  }

  var configs = collectRelayConfig(true);
  var form = new URLSearchParams();
  form.set('title', title);
  form.set('n', String(relayButtons.length));
  configs.forEach(function (cfg) {
    form.set('relay' + cfg.relay + '_on', cfg.o);
    form.set('relay' + cfg.relay + '_off', cfg.f);
    form.set('relay' + cfg.relay + '_mode', cfg.m);
    form.set('relay' + cfg.relay + '_group', String(cfg.g));
    form.set('relay' + cfg.relay + '_pulse', String(cfg.p));
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
      loadedTemplateFilename = String(result.body.filename || loadedTemplateFilename || '');
      selectedTemplateFilename = loadedTemplateFilename;
      loadedTemplateSnapshot = buildCurrentTemplateSnapshot();
      loadTemplateList();
      alert('Template "' + title + '" saved.');
    })
    .catch(function (e) {
      alert('Save failed: ' + e.message);
    })
    .finally(function () {
      btn.disabled = false;
      btn.textContent = 'Save';
    });
}

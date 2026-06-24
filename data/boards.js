var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;
var activeBoardFile = '';

window.addEventListener('load', function () {
  document.getElementById('backButton').addEventListener('click', function () {
    window.location.href = '/';
  });
  initWebSocket();
  loadBoards();
});

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen  = function () { websocket.send('home'); };
  websocket.onclose = function () { setTimeout(initWebSocket, 2000); };
  websocket.onmessage = function (event) {
    var obj;
    try { obj = JSON.parse(event.data); } catch (e) { return; }
    if (obj.boardName) {
      document.getElementById('boardsPageTitle').textContent = obj.boardName;
      document.title = obj.boardName + ' - Board Hardware';
    }
    if (obj.boardHardwareFile) {
      activeBoardFile = obj.boardHardwareFile;
      updateActiveLabel();
    }
    if (obj.boardHardwareName) {
      document.getElementById('activeBoardInfo').textContent =
        'Active: ' + obj.boardHardwareName + ' (' + (obj.boardHardwareFile || '') + ')';
    }
  };
}

var boardData = [];

function loadBoards() {
  fetch('/api/boards')
    .then(function (r) { return r.json(); })
    .then(function (data) {
      activeBoardFile = data.activeBoardFile || activeBoardFile;
      boardData = data.boards || [];
      renderBoards();
    })
    .catch(function () {});
}

function updateActiveLabel() {
  boardData.forEach(function (b) {
    var card = document.getElementById('board-card-' + safeId(b.filename));
    if (!card) return;
    var badge = card.querySelector('.board-active-badge');
    if (b.filename === activeBoardFile || ('/' + b.filename) === activeBoardFile) {
      if (!badge) {
        badge = document.createElement('span');
        badge.className = 'board-active-badge';
        badge.textContent = 'Active';
        card.querySelector('h3').appendChild(badge);
      }
    } else if (badge) {
      badge.remove();
    }
  });
}

function safeId(str) {
  return (str || '').replace(/[^a-zA-Z0-9]/g, '-');
}

function renderBoards() {
  var list = document.getElementById('boardList');
  list.innerHTML = '';
  boardData.forEach(function (board) {
    list.appendChild(buildBoardCard(board));
  });
  updateActiveLabel();
}

function buildBoardCard(board) {
  var card = document.createElement('div');
  card.className = 'config-card board-config-card';
  card.id = 'board-card-' + safeId(board.filename);

  var h3 = document.createElement('h3');
  h3.textContent = board.name || board.filename;
  card.appendChild(h3);

  var meta = document.createElement('p');
  meta.className = 'config-note';
  meta.textContent = board.cpu + ' • ' + board.relayCount + ' relays • '
                   + (board.outputType === 'shiftregister' ? 'Shift Register' : 'Direct GPIO');
  card.appendChild(meta);

  var editBtn = document.createElement('button');
  editBtn.className = 'button button-inline board-edit-btn';
  editBtn.textContent = 'Edit';
  editBtn.addEventListener('click', function () {
    toggleEditor(board.filename, card);
  });
  card.appendChild(editBtn);

  var editorWrapper = document.createElement('div');
  editorWrapper.id = 'editor-' + safeId(board.filename);
  editorWrapper.style.display = 'none';
  card.appendChild(editorWrapper);

  return card;
}

function toggleEditor(filename, card) {
  var wrapper = document.getElementById('editor-' + safeId(filename));
  if (wrapper.style.display !== 'none') {
    wrapper.style.display = 'none';
    return;
  }

  // Fetch full config and build editor
  fetch('/' + filename)
    .then(function (r) { if (!r.ok) throw new Error('not found'); return r.json(); })
    .then(function (cfg) {
      wrapper.innerHTML = '';
      wrapper.appendChild(buildEditor(cfg));
      wrapper.style.display = 'block';
    })
    .catch(function (e) { alert('Failed to load board config: ' + e.message); });
}

function buildEditor(cfg) {
  var form = document.createElement('div');
  form.className = 'board-editor';

  function addField(label, id, value, type) {
    var lbl = document.createElement('label');
    lbl.className = 'config-input-label';
    lbl.setAttribute('for', id);
    lbl.textContent = label;
    form.appendChild(lbl);
    var inp = document.createElement('input');
    inp.type = type || 'text';
    inp.id = id;
    inp.className = 'config-input';
    inp.value = value !== undefined ? value : '';
    if (type === 'number') { inp.min = '0'; inp.max = '39'; }
    form.appendChild(inp);
    return inp;
  }

  addField('Board Name', 'ed_name', cfg.name);
  addField('LED Pin', 'ed_ledPin', cfg.ledPin, 'number');

  var outputType = cfg.outputType || 'gpio';

  if (outputType === 'gpio') {
    var hr = document.createElement('hr');
    hr.style.margin = '12px 0';
    form.appendChild(hr);

    var relayHdr = document.createElement('label');
    relayHdr.className = 'config-input-label';
    relayHdr.style.fontWeight = 'bold';
    relayHdr.textContent = 'GPIO Pins';
    form.appendChild(relayHdr);

    var relays = cfg.relays || [];
    relays.forEach(function (r) {
      addField('Relay ' + r.relay + ' GPIO Pin', 'ed_relay' + r.relay + '_pin', r.pin, 'number');
    });
  } else {
    var sr = cfg.shiftRegister || {};
    var hr = document.createElement('hr');
    hr.style.margin = '12px 0';
    form.appendChild(hr);

    var srHdr = document.createElement('label');
    srHdr.className = 'config-input-label';
    srHdr.style.fontWeight = 'bold';
    srHdr.textContent = 'Shift Register Pins';
    form.appendChild(srHdr);

    addField('Latch Pin', 'ed_sr_latchPin', sr.latchPin, 'number');
    addField('Clock Pin', 'ed_sr_clockPin', sr.clockPin, 'number');
    addField('Data Pin',  'ed_sr_dataPin',  sr.dataPin,  'number');
    addField('OE Pin (Output Enable, active LOW)', 'ed_sr_oePin', sr.oePin, 'number');
  }

  var saveBtn = document.createElement('button');
  saveBtn.className = 'button';
  saveBtn.style.marginTop = '14px';
  saveBtn.textContent = 'Save';
  saveBtn.addEventListener('click', function () {
    saveBoard(cfg, saveBtn);
  });
  form.appendChild(saveBtn);

  return form;
}

function saveBoard(cfg, btn) {
  var variant = (cfg.relayCount === 16) ? '16relay' : '8relay';
  var form    = new URLSearchParams();

  form.set('variant',    variant);
  form.set('outputType', cfg.outputType || 'gpio');
  form.set('name',    (document.getElementById('ed_name')   || {}).value || cfg.name);
  form.set('ledPin',  (document.getElementById('ed_ledPin') || {}).value || cfg.ledPin);

  if ((cfg.outputType || 'gpio') === 'gpio') {
    var relays = cfg.relays || [];
    relays.forEach(function (r) {
      var el = document.getElementById('ed_relay' + r.relay + '_pin');
      form.set('relay' + r.relay + '_pin', el ? el.value : r.pin);
    });
  } else {
    ['latchPin', 'clockPin', 'dataPin', 'oePin'].forEach(function (key) {
      var el = document.getElementById('ed_sr_' + key);
      form.set('sr_' + key, el ? el.value : '');
    });
  }

  btn.disabled    = true;
  btn.textContent = 'Saving…';

  fetch('/api/boards', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: form.toString()
  })
    .then(function (r) { return r.json(); })
    .then(function (res) {
      if (!res.ok) throw new Error(res.error || 'save failed');
      loadBoards();
    })
    .catch(function (e) { alert('Save failed: ' + e.message); })
    .finally(function () {
      btn.disabled    = false;
      btn.textContent = 'Save';
    });
}

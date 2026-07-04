var gateway = 'ws://' + window.location.hostname + '/ws';
var websocket;
var activeBoardFile = '';
var restartRedirectDelayTimer = null;
var restartRedirectPollTimer = null;
var websocketEverConnected = false;
// bootSessionStorageKey, forceRootRefreshAfterBootChange, clearRefreshQueryParam,
// and trackBootSessionAndRedirectIfChanged live in theme-apply.js (loaded on
// every page before this script).

var boardData = [];
var currentEditorFilename = '';

window.addEventListener('load', function () {
  clearRefreshQueryParam();
  document.getElementById('backButton').addEventListener('click', function () {
    window.location.href = '/';
  });

  document.getElementById('boardSelect').addEventListener('change', function () {
    updateBoardStatus();
  });

  document.getElementById('setActiveBoardButton').addEventListener('click', setActiveBoard);
  document.getElementById('editBoardButton').addEventListener('click', editSelectedBoard);
  document.getElementById('newBoardButton').addEventListener('click', createNewBoard);
  document.getElementById('downloadBoardButton').addEventListener('click', downloadSelectedBoard);
  document.getElementById('uploadBoardButton').addEventListener('click', uploadBoardFile);
  document.getElementById('renameBoardButton').addEventListener('click', renameSelectedBoard);
  document.getElementById('deleteBoardButton').addEventListener('click', deleteSelectedBoard);
  document.getElementById('boardUploadInput').addEventListener('change', uploadSelectedBoardFile);

  initWebSocket();
  loadBoards();
});

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen = function () {
    websocketEverConnected = true;
    stopRestartRedirectWatcher();
    websocket.send('home');
  };
  websocket.onclose = function () {
    if (websocketEverConnected) {
      startRestartRedirectWatcher();
    }
    setTimeout(initWebSocket, 2000);
  };
  websocket.onmessage = function (event) {
    var obj;
    try { obj = JSON.parse(event.data); } catch (e) { return; }
    if (trackBootSessionAndRedirectIfChanged(obj)) {
      return;
    }
    if (obj.boardName) {
      document.title = obj.boardName + ' - Board Manager';
    }
  };
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

function getSelectedBoardFilename() {
  var select = document.getElementById('boardSelect');
  return (select && select.value) ? select.value : '';
}

function getBoardByFilename(filename) {
  return boardData.find(function (b) { return b.filename === filename; }) || null;
}

function normalizeBoardFilenameRef(value) {
  var normalized = String(value || '').trim();
  if (!normalized) {
    return '';
  }
  if (normalized.charAt(0) === '/') {
    normalized = normalized.substring(1);
  }
  if (normalized.indexOf('boards/') !== 0) {
    return '';
  }
  return normalized;
}

function updateBoardStatus() {
  var status = document.getElementById('boardStatus');
  var filename = getSelectedBoardFilename();
  if (!filename) {
    status.textContent = 'Selected board: none';
    return;
  }

  var board = getBoardByFilename(filename);
  var name = board ? (board.name || filename) : filename;
  var isActive = (activeBoardFile === ('/' + filename)) || (activeBoardFile === filename);
  status.textContent = 'Selected board: ' + name + (isActive ? ' (Active)' : '');
}

function loadBoards(preferredFilename) {
  fetch('/api/boards')
    .then(function (r) { return r.json(); })
    .then(function (data) {
      activeBoardFile = data.activeBoardFile || activeBoardFile;
      boardData = (data.boards || []).slice().sort(function (a, b) {
        return String(a.name || a.filename || '').localeCompare(String(b.name || b.filename || ''));
      });

      var select = document.getElementById('boardSelect');
      select.innerHTML = '<option value="">-- Select a board --</option>';
      var activeBoardRef = normalizeBoardFilenameRef(activeBoardFile);
      boardData.forEach(function (board) {
        var opt = document.createElement('option');
        opt.value = board.filename;
        var isActive = activeBoardRef && board.filename === activeBoardRef;
        opt.textContent = (board.name || board.filename) + ' [' + (board.cpu || '') + ', ' + (board.relayCount || 0) + ' relays]' + (isActive ? ' (Active)' : '');
        select.appendChild(opt);
      });

      var desired = normalizeBoardFilenameRef(preferredFilename) ||
        normalizeBoardFilenameRef(currentEditorFilename) ||
        normalizeBoardFilenameRef(activeBoardFile) ||
        '';
      if (desired && boardData.some(function (b) { return b.filename === desired; })) {
        select.value = desired;
      }

      var activeBoard = boardData.find(function (board) {
        return normalizeBoardFilenameRef(activeBoardFile) && board.filename === normalizeBoardFilenameRef(activeBoardFile);
      }) || null;
      var activeInfo = document.getElementById('activeBoardInfo');
      if (activeInfo) {
        if (activeBoard) {
          activeInfo.textContent = 'Active: ' + (activeBoard.name || activeBoard.filename) + ' (' + (activeBoard.filename || '') + ')';
        } else if (activeBoardFile) {
          activeInfo.textContent = 'Active: ' + activeBoardFile;
        } else {
          activeInfo.textContent = 'Active: unknown';
        }
      }

      updateBoardStatus();

      var page = document.querySelector('.labels-page');
      if (page) {
        page.removeAttribute('data-loading');
      }
    })
    .catch(function () {
      var page = document.querySelector('.labels-page');
      if (page) {
        page.removeAttribute('data-loading');
      }
    });
}

function renderEditor(cfg, filename) {
  var host = document.getElementById('boardEditorHost');
  host.innerHTML = '';

  var card = document.createElement('div');
  card.className = 'config-card board-config-card';

  var heading = document.createElement('h3');
  heading.textContent = cfg.name || filename;
  card.appendChild(heading);

  var meta = document.createElement('p');
  meta.className = 'config-note';
  meta.textContent = (cfg.cpu || '') + ' • ' + (cfg.relayCount || 0) + ' relays • ' + ((cfg.outputType === 'shiftregister') ? 'Shift Register' : 'Direct GPIO');
  card.appendChild(meta);

  card.appendChild(buildEditor(cfg, filename));
  host.appendChild(card);
}

function buildEditor(cfg, filename) {
  var form = document.createElement('div');
  form.className = 'board-editor';
  var idPrefix = 'ed_' + String(filename || '').replace(/[^a-zA-Z0-9]/g, '-') + '_';

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
    inp.value = (value !== undefined && value !== null) ? value : '';
    if (type === 'number') {
      inp.min = '0';
      inp.max = '39';
    }
    form.appendChild(inp);
    return inp;
  }

  addField('Board Name', idPrefix + 'name', cfg.name || '');
  addField('LED Pin', idPrefix + 'ledPin', cfg.ledPin || 0, 'number');

  var outputType = cfg.outputType || 'gpio';
  var relayCount = parseInt(cfg.relayCount, 10) || 8;

  if (outputType === 'gpio') {
    var hr = document.createElement('hr');
    hr.style.margin = '12px 0';
    form.appendChild(hr);

    var relayHdr = document.createElement('label');
    relayHdr.className = 'config-input-label';
    relayHdr.style.fontWeight = 'bold';
    relayHdr.textContent = 'GPIO Pins';
    form.appendChild(relayHdr);

    var relays = Array.isArray(cfg.relays) ? cfg.relays : [];
    if (!relays.length) {
      for (var ri = 1; ri <= relayCount; ri++) {
        relays.push({ relay: ri, pin: 255 });
      }
    }

    relays.forEach(function (r) {
      addField('Relay ' + r.relay + ' GPIO Pin', idPrefix + 'relay' + r.relay + '_pin', r.pin, 'number');
    });
  } else {
    var sr = cfg.shiftRegister || {};
    var hr2 = document.createElement('hr');
    hr2.style.margin = '12px 0';
    form.appendChild(hr2);

    var srHdr = document.createElement('label');
    srHdr.className = 'config-input-label';
    srHdr.style.fontWeight = 'bold';
    srHdr.textContent = 'Shift Register Pins';
    form.appendChild(srHdr);

    addField('Latch Pin', idPrefix + 'sr_latchPin', sr.latchPin, 'number');
    addField('Clock Pin', idPrefix + 'sr_clockPin', sr.clockPin, 'number');
    addField('Data Pin', idPrefix + 'sr_dataPin', sr.dataPin, 'number');
    addField('OE Pin (Output Enable, active LOW)', idPrefix + 'sr_oePin', sr.oePin, 'number');
  }

  var saveBtn = document.createElement('button');
  saveBtn.className = 'button';
  saveBtn.style.marginTop = '14px';
  saveBtn.textContent = 'Save';
  saveBtn.addEventListener('click', function () {
    saveBoard(cfg, filename, saveBtn, idPrefix);
  });
  form.appendChild(saveBtn);

  var cancelBtn = document.createElement('button');
  cancelBtn.className = 'button';
  cancelBtn.style.marginTop = '14px';
  cancelBtn.textContent = 'Back';
  cancelBtn.addEventListener('click', function () {
    currentEditorFilename = '';
    document.getElementById('boardEditorHost').innerHTML = '';
  });
  form.appendChild(cancelBtn);

  return form;
}

function openEditor(filename) {
  if (!filename) {
    return;
  }

  fetch('/' + filename)
    .then(function (r) {
      if (!r.ok) {
        throw new Error('not found');
      }
      return r.json();
    })
    .then(function (cfg) {
      currentEditorFilename = filename;
      renderEditor(cfg, filename);
      var select = document.getElementById('boardSelect');
      select.value = filename;
      updateBoardStatus();
    })
    .catch(function (e) {
      alert('Failed to load board config: ' + e.message);
    });
}

function setActiveBoard() {
  var filename = getSelectedBoardFilename();
  if (!filename) {
    alert('Select a board first.');
    return;
  }

  var btn = document.getElementById('setActiveBoardButton');
  btn.disabled = true;
  btn.textContent = 'Saving...';

  var payload = new URLSearchParams();
  payload.set('action', 'setactive');
  payload.set('filename', filename);

  fetch('/api/boards', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: payload.toString()
  })
    .then(function (r) {
      return r.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) { return { ok: r.ok, body: json }; });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'set active failed');
      }
      activeBoardFile = String(result.body.activeBoardFile || filename);
      loadBoards(filename);
    })
    .catch(function (e) { alert('Set active board failed: ' + e.message); })
    .finally(function () {
      btn.disabled = false;
      btn.textContent = 'Set Active';
    });
}

function editSelectedBoard() {
  var filename = getSelectedBoardFilename();
  if (!filename) {
    alert('Select a board first.');
    return;
  }
  openEditor(filename);
}

function saveBoard(cfg, filename, btn, idPrefix) {
  var relayCount = parseInt(cfg.relayCount, 10) || 8;
  var form = new URLSearchParams();

  form.set('action', 'save');
  form.set('filename', filename);
  form.set('relayCount', String(relayCount));
  form.set('outputType', cfg.outputType || 'gpio');
  form.set('name', (document.getElementById(idPrefix + 'name') || {}).value || cfg.name || 'Board');
  form.set('ledPin', (document.getElementById(idPrefix + 'ledPin') || {}).value || cfg.ledPin || 2);

  if ((cfg.outputType || 'gpio') === 'gpio') {
    for (var i = 1; i <= relayCount; i++) {
      var el = document.getElementById(idPrefix + 'relay' + i + '_pin');
      form.set('relay' + i + '_pin', el ? el.value : '255');
    }
  } else {
    ['latchPin', 'clockPin', 'dataPin', 'oePin'].forEach(function (key) {
      var el = document.getElementById(idPrefix + 'sr_' + key);
      form.set('sr_' + key, el ? el.value : '');
    });
  }

  btn.disabled = true;
  btn.textContent = 'Saving...';

  fetch('/api/boards', {
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
      var savedFilename = String(result.body.filename || filename);
      currentEditorFilename = savedFilename;
      loadBoards(savedFilename);
      alert('Board saved.');
    })
    .catch(function (e) {
      alert('Save failed: ' + e.message);
    })
    .finally(function () {
      btn.disabled = false;
      btn.textContent = 'Save';
    });
}

function createNewBoard() {
  var title = window.prompt('New board name:', 'Custom Board');
  if (title === null) {
    return;
  }
  title = String(title || '').trim();
  if (!title) {
    alert('Please enter a board name.');
    return;
  }

  var relayChoice = window.prompt('Relay count (8 or 16):', '8');
  if (relayChoice === null) {
    return;
  }
  var relayCount = parseInt(relayChoice, 10);
  if (relayCount !== 8 && relayCount !== 16) {
    alert('Relay count must be 8 or 16.');
    return;
  }

  var outputType = (relayCount === 16) ? 'shiftregister' : 'gpio';
  var form = new URLSearchParams();
  form.set('action', 'save');
  form.set('title', title);
  form.set('relayCount', String(relayCount));
  form.set('outputType', outputType);
  form.set('name', title);
  form.set('ledPin', '2');

  if (outputType === 'gpio') {
    for (var i = 1; i <= relayCount; i++) {
      form.set('relay' + i + '_pin', '255');
    }
  } else {
    form.set('sr_latchPin', '12');
    form.set('sr_clockPin', '13');
    form.set('sr_dataPin', '14');
    form.set('sr_oePin', '5');
  }

  fetch('/api/boards', {
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
        throw new Error(result.body.error || 'create failed');
      }
      var createdFilename = String(result.body.filename || '');
      currentEditorFilename = createdFilename;
      loadBoards(createdFilename);
      if (createdFilename) {
        openEditor(createdFilename);
      }
    })
    .catch(function (e) {
      alert('Create failed: ' + e.message);
    });
}

function renameSelectedBoard() {
  var filename = getSelectedBoardFilename();
  if (!filename) {
    alert('Select a board first.');
    return;
  }

  var board = getBoardByFilename(filename);
  var defaultTitle = board ? (board.name || filename.replace(/\.json$/i, '')) : filename.replace(/\.json$/i, '');
  var newTitle = window.prompt('Rename board to:', defaultTitle);
  if (newTitle === null) {
    return;
  }
  newTitle = String(newTitle || '').trim();
  if (!newTitle) {
    alert('Please enter a board name.');
    return;
  }

  var payload = new URLSearchParams();
  payload.set('action', 'rename');
  payload.set('filename', filename);
  payload.set('title', newTitle);

  fetch('/api/boards', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: payload.toString()
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
        throw new Error(result.body.error || 'rename failed');
      }
      var newFilename = String(result.body.filename || filename);
      currentEditorFilename = newFilename;
      loadBoards(newFilename);
      if (currentEditorFilename) {
        openEditor(currentEditorFilename);
      }
    })
    .catch(function (e) {
      alert('Rename failed: ' + e.message);
    });
}

function deleteSelectedBoard() {
  var filename = getSelectedBoardFilename();
  if (!filename) {
    alert('Select a board first.');
    return;
  }

  var confirmDelete = window.confirm('Delete board "' + filename + '"?');
  if (!confirmDelete) {
    return;
  }

  var payload = new URLSearchParams();
  payload.set('action', 'delete');
  payload.set('filename', filename);

  fetch('/api/boards', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: payload.toString()
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
        throw new Error(result.body.error || 'delete failed');
      }
      currentEditorFilename = '';
      document.getElementById('boardEditorHost').innerHTML = '';
      loadBoards('');
    })
    .catch(function (e) {
      alert('Remove failed: ' + e.message);
    });
}

function uploadBoardFile() {
  var input = document.getElementById('boardUploadInput');
  if (!input) {
    return;
  }
  input.value = '';
  input.click();
}

function uploadSelectedBoardFile() {
  var input = document.getElementById('boardUploadInput');
  if (!input || !input.files || input.files.length === 0) {
    return;
  }

  var file = input.files[0];
  var reader = new FileReader();
  reader.onload = function () {
    var content = String(reader.result || '');
    var payload = new URLSearchParams();
    payload.set('action', 'upload');
    payload.set('filename', file.name);
    payload.set('content', content);

    fetch('/api/boards', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
      body: payload.toString()
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
          throw new Error(result.body.error || 'upload failed');
        }
        var newFilename = String(result.body.filename || '');
        currentEditorFilename = newFilename;
        loadBoards(newFilename);
        if (newFilename) {
          openEditor(newFilename);
        }
        alert('Board uploaded.');
      })
      .catch(function (e) {
        alert('Upload failed: ' + e.message);
      });
  };

  reader.onerror = function () {
    alert('Failed to read selected file.');
  };

  reader.readAsText(file);
}

function downloadSelectedBoard() {
  var filename = getSelectedBoardFilename();
  if (!filename) {
    alert('Select a board first.');
    return;
  }
  window.location.href = '/' + filename;
}

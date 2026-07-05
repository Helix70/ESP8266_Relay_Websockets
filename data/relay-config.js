var allTemplates = [];
var selectedTemplateFilename = '';
var relayCount = 0;
var templateSummaryCache = {};
var diagnosticsLoadTimer = null;

// bootSessionStorageKey, forceRootRefreshAfterBootChange, clearRefreshQueryParam,
// and trackBootSessionAndRedirectIfChanged are inlined in this page's own
// <head> (see relay-config.html) rather than loaded from a shared file.

window.addEventListener('load', onLoad);

function setRelayConfigPageReady() {
  var page = document.querySelector('.labels-page');
  if (page) {
    page.removeAttribute('data-loading');
  }
}

function onLoad() {
  clearRefreshQueryParam();
  // Show the page immediately, then hydrate async data as it arrives.
  setRelayConfigPageReady();

  document.getElementById('templateSelect').addEventListener('change', onTemplateSelectionChanged);
  document.getElementById('setActiveTemplateButton').addEventListener('click', setActiveTemplate);
  document.getElementById('openTemplateEditorButton').addEventListener('click', function () {
    window.location.href = '/template-editor.html';
  });
  document.getElementById('backLabelsButton').addEventListener('click', function () {
    window.location.href = '/';
  });
  document.getElementById('downloadTemplateButton').addEventListener('click', downloadSelectedTemplate);
  document.getElementById('deleteTemplateButton').addEventListener('click', deleteSelectedTemplate);
  document.getElementById('renameTemplateButton').addEventListener('click', renameSelectedTemplate);
  document.getElementById('uploadTemplateButton').addEventListener('click', uploadTemplateFile);
  document.getElementById('templateUploadInput').addEventListener('change', uploadSelectedTemplateFile);
  loadTemplateList();
  onTemplateSelectionChanged();
}

function scheduleTemplateDiagnosticsLoad() {
  if (diagnosticsLoadTimer) {
    clearTimeout(diagnosticsLoadTimer);
  }

  diagnosticsLoadTimer = setTimeout(function () {
    diagnosticsLoadTimer = null;
    loadTemplateDiagnostics();
  }, 250);
}

function loadTemplateDiagnostics() {
  var status = document.getElementById('templateDiagnosticsStatus');
  if (!status) {
    return;
  }

  fetch('/api/templates/diagnostics', { cache: 'no-store' })
    .then(function (r) { return r.json(); })
    .then(function (data) {
      var count = (typeof data.templateCount === 'number') ? data.templateCount : 0;
      var largest = (typeof data.largestTemplateBytes === 'number') ? data.largestTemplateBytes : 0;
      var freeBytes = (typeof data.fsFreeBytes === 'number') ? data.fsFreeBytes : null;
      var totalBytes = (typeof data.fsTotalBytes === 'number') ? data.fsTotalBytes : null;
      var lastReason = String(data.lastWriteErrorReason || '').trim();

      var parts = [
        'Templates: ' + count,
        'Largest: ' + largest + ' B'
      ];

      if (freeBytes !== null && totalBytes !== null) {
        parts.push('FS Free: ' + freeBytes + ' / ' + totalBytes + ' B');
      }

      if (lastReason) {
        parts.push('Last write error: ' + lastReason);
      }

      status.textContent = 'Template storage: ' + parts.join(' | ');
    })
    .catch(function () {
      status.textContent = 'Template storage: unavailable';
    });
}

function onTemplateSelectionChanged() {
  updateTemplateSummary();
}

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

function applyRelayConfigBootstrap(data) {
  if (data && data.boardName) {
    var normalized = String(data.boardName || '').trim();
    if (!normalized) {
      normalized = 'Relay Board';
    }
    document.title = normalized + ' - Template Manager';
  }

  if (data && typeof data.n === 'number' && data.n > 0) {
    relayCount = data.n;
  }

  templateSummaryCache = {};
  allTemplates = (data && data.templates ? data.templates : []).slice().sort(function (a, b) {
    return String(a.t || '').localeCompare(String(b.t || ''));
  });
  selectedTemplateFilename = normalizeTemplateFilenameRef((data && data.selectedTemplate) || selectedTemplateFilename || '');
  refreshTemplateDropdown();
}

function loadTemplateList() {
  fetch('/api/templates/bootstrap', { cache: 'no-store' })
    .then(function (r) { return r.json(); })
    .then(function (data) {
      if (trackBootSessionAndRedirectIfChanged(data)) {
        return;
      }
      applyRelayConfigBootstrap(data);
      scheduleTemplateDiagnosticsLoad();
      setRelayConfigPageReady();
    })
    .catch(function () {
      scheduleTemplateDiagnosticsLoad();
      setRelayConfigPageReady();
    });
}

function refreshTemplateDropdown() {
  var select = document.getElementById('templateSelect');
  var previousValue = select.value;
  var activeTemplate = normalizeTemplateFilenameRef(selectedTemplateFilename);
  while (select.options.length > 1) {
    select.remove(1);
  }

  var visibleTemplates = allTemplates.filter(function (t) {
    if (!relayCount) {
      return true;
    }
    return t.n === relayCount;
  });

  visibleTemplates.forEach(function (t) {
    var opt = document.createElement('option');
    opt.value = t.filename;
    opt.textContent = t.t + ((activeTemplate && t.filename === activeTemplate) ? ' (Active)' : '');
    select.appendChild(opt);
  });

  if (selectedTemplateFilename && visibleTemplates.some(function (t) { return t.filename === selectedTemplateFilename; })) {
    select.value = selectedTemplateFilename;
  } else if (previousValue && visibleTemplates.some(function (t) { return t.filename === previousValue; })) {
    select.value = previousValue;
  } else {
    select.value = '';
  }

  updateTemplateSelectionStatus();
  onTemplateSelectionChanged();
}

function updateTemplateSelectionStatus() {
  var status = document.getElementById('templateSelectionStatus');
  if (!status) {
    return;
  }

  if (!selectedTemplateFilename) {
    status.textContent = 'Selected template: none';
    return;
  }

  var selected = allTemplates.find(function (t) {
    return t.filename === selectedTemplateFilename;
  });

  var title = selected ? selected.t : selectedTemplateFilename;
  status.textContent = 'Selected template: ' + title;
}

function getSelectedTemplateFilename() {
  var select = document.getElementById('templateSelect');
  return select && select.value ? select.value : '';
}

function buildApiErrorMessage(body, fallback) {
  var message = (body && body.error) ? String(body.error) : String(fallback || 'request failed');
  if (body && body.reason) {
    message += ' (' + body.reason + ')';
  }

  if (body && typeof body.requiredBytes === 'number' && typeof body.fsFreeBytes === 'number') {
    message += ' [required: ' + body.requiredBytes + ', free: ' + body.fsFreeBytes + ']';
  }

  return message;
}

function summarizeMode(mode, group, pulse) {
  var groupText = group > 0 ? (' G' + group) : '';
  if (mode === 'I') {
    return 'interlocked' + groupText;
  }
  if (mode === 'P') {
    return 'pulsed' + (pulse > 0 ? (' ' + pulse + 's') : '') + groupText;
  }
  return 'latched' + groupText;
}

function getCompactLabelText(label) {
  var on = String(label.o || '').trim();
  var off = String(label.f || '').trim();

  if (!on && !off) {
    return '(blank)';
  }
  if (!on) {
    return off;
  }
  if (!off || on === off) {
    return on;
  }
  return on + ' / ' + off;
}

function normalizeSummaryMode(modeValue) {
  var mode = String(modeValue || 'L');
  if (mode !== 'I' && mode !== 'P') {
    mode = 'L';
  }
  return mode;
}

function renderTemplateSummaryDoc(doc, filename) {
  var container = document.getElementById('templateSummary');
  if (!container) {
    return;
  }

  container.innerHTML = '';

  var labels = Array.isArray(doc.l) ? doc.l : [];
  if (!labels.length) {
    var empty = document.createElement('p');
    empty.className = 'template-summary-error';
    empty.textContent = 'Template has no relay labels.';
    container.appendChild(empty);
    return;
  }

  var title = String(doc.t || filename);
  var header = document.createElement('p');
  header.className = 'template-summary-header';
  header.textContent = title;
  container.appendChild(header);

  var modeCounts = { L: 0, I: 0, P: 0 };
  labels.forEach(function (label) {
    modeCounts[normalizeSummaryMode(label.m)] += 1;
  });

  var meta = document.createElement('p');
  meta.className = 'template-summary-meta';
  meta.textContent =
    'Relays: ' + labels.length +
    ' | Latched: ' + modeCounts.L +
    ' | Interlocked: ' + modeCounts.I +
    ' | Pulsed: ' + modeCounts.P;
  container.appendChild(meta);

  var grid = document.createElement('div');
  grid.className = 'template-summary-grid';

  for (var i = 0; i < labels.length; i++) {
    var label = labels[i] || {};
    var mode = normalizeSummaryMode(label.m);
    var modeText = summarizeMode(mode, parseInt(label.g, 10) || 0, parseInt(label.p, 10) || 0);

    var card = document.createElement('div');
    card.className = 'relay-label-card template-summary-card';

    var titleEl = document.createElement('h3');
    titleEl.textContent = 'Relay ' + (i + 1);
    card.appendChild(titleEl);

    var onRow = document.createElement('p');
    onRow.className = 'template-summary-row';
    onRow.innerHTML = '<span class="template-summary-key">ON</span><span class="template-summary-value">' +
      escapeHtml(String(label.o || '').trim() || '(blank)') + '</span>';
    card.appendChild(onRow);

    var offRow = document.createElement('p');
    offRow.className = 'template-summary-row';
    offRow.innerHTML = '<span class="template-summary-key">OFF</span><span class="template-summary-value">' +
      escapeHtml(String(label.f || '').trim() || '(blank)') + '</span>';
    card.appendChild(offRow);

    var modeRow = document.createElement('p');
    modeRow.className = 'template-summary-row';
    modeRow.innerHTML = '<span class="template-summary-key">MODE</span><span class="template-summary-value">' +
      escapeHtml(modeText) + '</span>';
    card.appendChild(modeRow);

    grid.appendChild(card);
  }

  container.appendChild(grid);
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function updateTemplateSummary() {
  var container = document.getElementById('templateSummary');
  if (!container) {
    return;
  }

  var filename = getSelectedTemplateFilename();
  if (!filename) {
    container.innerHTML = '<p class="template-summary-empty">Select a template to view relay summary.</p>';
    return;
  }

  if (templateSummaryCache[filename]) {
    renderTemplateSummaryDoc(templateSummaryCache[filename], filename);
    return;
  }

  container.innerHTML = '<p class="template-summary-loading">Loading summary...</p>';

  fetch('/templates/' + encodeURIComponent(filename), { cache: 'no-store' })
    .then(function (response) {
      if (!response.ok) {
        throw new Error('template not found (HTTP ' + response.status + ')');
      }
      return response.json();
    })
    .then(function (doc) {
      templateSummaryCache[filename] = doc;
      if (getSelectedTemplateFilename() === filename) {
        renderTemplateSummaryDoc(doc, filename);
      }
    })
    .catch(function (error) {
      container.innerHTML = '<p class="template-summary-error">Summary unavailable: ' + error.message + '</p>';
    });
}

function setActiveTemplate() {
  var filename = getSelectedTemplateFilename();
  if (!filename) {
    alert('Select a template first.');
    return;
  }

  var btn = document.getElementById('setActiveTemplateButton');
  btn.disabled = true;
  btn.textContent = 'Saving...';

  var form = new URLSearchParams();
  form.set('action', 'setactive');
  form.set('filename', filename);

  fetch('/api/templates', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: form.toString()
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
      selectedTemplateFilename = normalizeTemplateFilenameRef(result.body.selectedTemplate || filename);
      refreshTemplateDropdown();
    })
    .catch(function (e) { alert('Set active template failed: ' + e.message); })
    .finally(function () {
      btn.disabled = false;
      btn.textContent = 'Set Active';
    });
}

function downloadSelectedTemplate() {
  var filename = getSelectedTemplateFilename();
  if (!filename) {
    alert('Please select a template to download.');
    return;
  }

  var link = document.createElement('a');
  link.href = '/templates/' + encodeURIComponent(filename);
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

function renameSelectedTemplate() {
  var filename = getSelectedTemplateFilename();
  if (!filename) {
    alert('Please select a template to rename.');
    return;
  }

  var current = allTemplates.find(function (t) { return t.filename === filename; });
  var defaultTitle = current && current.t ? current.t : filename.replace(/\.json$/i, '');
  var prompted = window.prompt('New template name:', defaultTitle);
  if (prompted === null) {
    return;
  }

  var title = prompted.trim();
  if (!title) {
    alert('Template name cannot be empty.');
    return;
  }

  var button = document.getElementById('renameTemplateButton');
  button.disabled = true;
  button.textContent = 'Renaming...';

  var form = new URLSearchParams();
  form.set('action', 'rename');
  form.set('filename', filename);
  form.set('title', title);

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
        throw new Error(buildApiErrorMessage(result.body, 'rename failed'));
      }
      selectedTemplateFilename = String(result.body.filename || filename);
      loadTemplateList();
    })
    .catch(function (e) {
      alert('Rename failed: ' + e.message);
    })
    .finally(function () {
      button.disabled = false;
      button.textContent = 'Rename';
    });
}

function deleteSelectedTemplate() {
  var filename = getSelectedTemplateFilename();
  if (!filename) {
    alert('Please select a template to remove.');
    return;
  }

  if (!window.confirm('Remove template "' + filename + '"?')) {
    return;
  }

  var button = document.getElementById('deleteTemplateButton');
  button.disabled = true;
  button.textContent = 'Removing...';

  var form = new URLSearchParams();
  form.set('action', 'delete');
  form.set('filename', filename);

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
        throw new Error(buildApiErrorMessage(result.body, 'remove failed'));
      }
      if (selectedTemplateFilename === filename) {
        selectedTemplateFilename = '';
      }
      loadTemplateList();
    })
    .catch(function (e) {
      alert('Remove failed: ' + e.message);
    })
    .finally(function () {
      button.disabled = false;
      button.textContent = 'Remove';
    });
}

function uploadTemplateFile() {
  var input = document.getElementById('templateUploadInput');
  if (!input) {
    return;
  }

  input.value = '';
  input.click();
}

function uploadSelectedTemplateFile() {
  var input = document.getElementById('templateUploadInput');
  if (!input || !input.files || input.files.length === 0) {
    return;
  }

  var file = input.files[0];
  var button = document.getElementById('uploadTemplateButton');
  button.disabled = true;
  button.textContent = 'Uploading...';

  var reader = new FileReader();
  reader.onload = function () {
    var content = String(reader.result || '');
    var doc;
    try {
      doc = JSON.parse(content);
    } catch (err) {
      button.disabled = false;
      button.textContent = 'Upload';
      alert('Upload failed: invalid template json');
      return;
    }

    var labels = [];
    if (Array.isArray(doc)) {
      labels = doc;
    } else if (doc && Array.isArray(doc.l)) {
      labels = doc.l;
    }

    if (!labels.length) {
      button.disabled = false;
      button.textContent = 'Upload';
      alert('Upload failed: labels missing');
      return;
    }

    var relayCount = parseInt(doc && doc.n, 10);
    if (!relayCount || relayCount < 1 || relayCount > 16) {
      relayCount = labels.length;
    }
    if (relayCount < 1 || relayCount > 16) {
      button.disabled = false;
      button.textContent = 'Upload';
      alert('Upload failed: invalid relay count');
      return;
    }

    var title = String((doc && doc.title) || '').trim();
    if (!title) {
      title = String(file.name || 'Imported Template').replace(/\.json$/i, '');
    }

    var uploadFilename = String(file.name || '').trim();
    if (uploadFilename && !/\.json$/i.test(uploadFilename)) {
      uploadFilename += '.json';
    }

    var payload = new URLSearchParams();
    payload.set('action', 'upload');
    payload.set('title', title);
    if (uploadFilename) {
      payload.set('filename', uploadFilename);
    }
    payload.set('relayCount', String(relayCount));

    for (var i = 0; i < relayCount; i++) {
      var relayIndex = i + 1;
      var label = labels[i] || {};
      var mode = String(label.m || 'L');
      if (mode !== 'I' && mode !== 'P') {
        mode = 'L';
      }

      var group = parseInt(label.g, 10);
      if (!group || group < 0) {
        group = 0;
      }

      var pulse = parseInt(label.p, 10);
      if (!pulse || pulse < 1 || pulse > 30) {
        pulse = 1;
      }

      var usesPulse = (mode === 'P');
      payload.set('relay' + relayIndex + '_on', String(label.o || ''));
      payload.set('relay' + relayIndex + '_off', String(label.f || ''));
      payload.set('relay' + relayIndex + '_mode', mode);
      // Group is optional for Latched/Pulsed and required for Interlocked
      // families, so preserve whatever group the template carried.
      payload.set('relay' + relayIndex + '_group', String(group));
      payload.set('relay' + relayIndex + '_pulse', String(usesPulse ? pulse : 0));
    }

    fetch('/api/templates', {
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
          throw new Error(buildApiErrorMessage(result.body, 'upload failed'));
        }
        input.value = '';
        loadTemplateList();
        alert('Template uploaded.');
      })
      .catch(function (e) {
        alert('Upload failed: ' + e.message);
      })
      .finally(function () {
        button.disabled = false;
        button.textContent = 'Upload';
      });
  };

  reader.onerror = function () {
    button.disabled = false;
    button.textContent = 'Upload';
    alert('Failed to read selected file.');
  };

  reader.readAsText(file);
}

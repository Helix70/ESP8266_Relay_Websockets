var currentBoardName = '';
var currentUseStaticIp = false;
var currentIpConfig = {
  ip: '',
  dns: '',
  gateway: '',
  subnet: ''
};
var lastReportedNetwork = {
  ip: '',
  dns: '',
  gateway: '',
  subnet: ''
};
var currentRelayModes = {
  doDelay: false
};
var connectStrongestOnStartup = false;
var DEBUG_LOGS = false;
var wifiRefreshTimer = null;
var restartRedirectDelayTimer = null;
var restartRedirectPollTimer = null;
var suspendConfigFormHydration = false;
// bootSessionStorageKey, forceRootRefreshAfterBootChange, clearRefreshQueryParam,
// and trackBootSessionAndRedirectIfChanged live in theme-apply.js (loaded on
// every page before this script).

var wifiSsidDisplay;
var wifiConnectedSsidDisplay;
var wifiStatusDisplay;
var wifiRssiDisplay;
var wifiRescanStatusDisplay;
var rescanWifiButton;

function applyWifiStatusFields(state) {
  if (wifiSsidDisplay) {
    var configuredSsid = String((state && (state.wifiConfiguredSsid || state.ssid)) || '').trim();
    wifiSsidDisplay.value = configuredSsid.length > 0 ? configuredSsid : '(not configured)';
  }

  if (wifiConnectedSsidDisplay) {
    var connectedSsid = String((state && state.wifiConnectedSsid) || '').trim();
    wifiConnectedSsidDisplay.value = connectedSsid.length > 0 ? connectedSsid : '-';
  }

  if (wifiStatusDisplay) {
    var connected = !!(state && state.wifiConnected);
    wifiStatusDisplay.value = connected ? 'Connected' : 'Disconnected';
  }

  if (wifiRssiDisplay) {
    var isConnected = !!(state && state.wifiConnected);
    var hasRssi = !!(state && typeof state.wifiRssi === 'number');
    wifiRssiDisplay.value = (isConnected && hasRssi) ? (String(state.wifiRssi) + ' dBm') : '-';
  }

  if (wifiRescanStatusDisplay) {
    wifiRescanStatusDisplay.value = String((state && state.wifiRescanStatus) || 'Idle');
  }

  if (rescanWifiButton) {
    var scanning = !!(state && state.wifiRescanInProgress);
    rescanWifiButton.disabled = scanning;
    rescanWifiButton.textContent = scanning ? 'Rescanning...' : 'Rescan & Refresh Strongest AP';
  }
}

function debugLog() {
  if (DEBUG_LOGS && window.console && typeof console.log === 'function') {
    console.log.apply(console, arguments);
  }
}

function setIpPlaceholders(ip, dns, gateway, subnet) {
  document.getElementById('ipInput').placeholder = ip || '';
  document.getElementById('dnsInput').placeholder = dns || '';
  document.getElementById('gatewayInput').placeholder = gateway || '';
  document.getElementById('subnetInput').placeholder = subnet || '';
}

function setConfigPageReady() {
  var configPage = document.querySelector('.config-page');
  if (configPage) { configPage.removeAttribute('data-loading'); }
}

function stopRestartRedirectWatcher() {
  if (restartRedirectDelayTimer) { clearTimeout(restartRedirectDelayTimer); restartRedirectDelayTimer = null; }
  if (restartRedirectPollTimer) { clearInterval(restartRedirectPollTimer); restartRedirectPollTimer = null; }
}

function startRestartRedirectWatcher() {
  if (window.location.pathname === '/') { return; }
  if (restartRedirectDelayTimer || restartRedirectPollTimer) { return; }

  restartRedirectDelayTimer = setTimeout(function () {
    restartRedirectDelayTimer = null;
    if (restartRedirectPollTimer) { return; }

    restartRedirectPollTimer = setInterval(function () {
      fetch('/', { cache: 'no-store' })
        .then(function (response) {
          if (response.ok) {
            stopRestartRedirectWatcher();
            window.location.href = '/';
          }
        })
        .catch(function () {});
    }, 1200);
  }, 1500);
}

function shouldPreserveBoardNameInput(incomingName) {
  var input = document.getElementById('boardNameInput');
  if (!input) { return false; }
  if (document.activeElement === input) { return true; }
  var localValue = String(input.value || '').trim();
  if (!localValue) { return false; }
  return localValue !== currentBoardName && localValue !== incomingName;
}

function applyBoardName(name) {
  var normalized = (name || '').trim();
  if (!normalized) { normalized = 'Relay Board'; }
  currentBoardName = normalized;
  if (!shouldPreserveBoardNameInput(normalized)) {
    document.getElementById('boardNameInput').value = normalized;
  }
  document.title = normalized + ' - Configuration';
}

function applyNetworkState(useDhcp, ip, dns, gateway, subnet) {
  if (typeof useDhcp === 'boolean') {
    currentUseStaticIp = !useDhcp;
    document.getElementById('useDhcpCheckbox').checked = useDhcp;
  }

  if (typeof ip === 'string' && ip.length > 0) { lastReportedNetwork.ip = ip; }
  if (typeof dns === 'string' && dns.length > 0) { lastReportedNetwork.dns = dns; }
  if (typeof gateway === 'string' && gateway.length > 0) { lastReportedNetwork.gateway = gateway; }
  if (typeof subnet === 'string' && subnet.length > 0) { lastReportedNetwork.subnet = subnet; }

  setIpPlaceholders(
    lastReportedNetwork.ip,
    lastReportedNetwork.dns,
    lastReportedNetwork.gateway,
    lastReportedNetwork.subnet
  );

  setIpInputValues(
    getFallbackValue('ipInput', lastReportedNetwork.ip),
    getFallbackValue('dnsInput', lastReportedNetwork.dns),
    getFallbackValue('gatewayInput', lastReportedNetwork.gateway),
    getFallbackValue('subnetInput', lastReportedNetwork.subnet)
  );

  updateIpFieldMode();
}

function fetchNetInfo() {
  fetch('/netinfo', { cache: 'no-store' })
    .then(function (response) {
      if (!response.ok) { throw new Error('netinfo fetch failed'); }
      return response.json();
    })
    .then(function (net) {
      if (trackBootSessionAndRedirectIfChanged(net)) { return; }

      if (!suspendConfigFormHydration) {
        var useDhcp = (typeof net.useDhcp === 'boolean') ? net.useDhcp : undefined;
        applyNetworkState(useDhcp, net.ipAddress || '', net.dns || '', net.gateway || '', net.subnet || '');
      }

      if (net.boardName && !suspendConfigFormHydration) {
        applyBoardName(net.boardName);
      }

      applyWifiStatusFields(net);

      if (!suspendConfigFormHydration) {
        if (net.hasOwnProperty('doDelay')) {
          currentRelayModes.doDelay = !!net.doDelay;
          document.getElementById('doDelayCheckbox').checked = currentRelayModes.doDelay;
        }
        if (net.hasOwnProperty('startupDelaySeconds')) {
          document.getElementById('delaySecondsInput').value = net.startupDelaySeconds;
          currentRelayModes.delaySeconds = net.startupDelaySeconds;
        }
        if (net.hasOwnProperty('connectStrongestOnStartup')) {
          connectStrongestOnStartup = !!net.connectStrongestOnStartup;
          document.getElementById('connectStrongestCheckbox').checked = connectStrongestOnStartup;
        }
        updateDelayFieldMode();
        updateIpFieldMode();
      }
    })
    .catch(function () {});
}

function getInput(id) {
  return document.getElementById(id);
}

function setIpInputValues(ip, dns, gateway, subnet) {
  getInput('ipInput').value = ip || '';
  getInput('dnsInput').value = dns || '';
  getInput('gatewayInput').value = gateway || '';
  getInput('subnetInput').value = subnet || '';
}

function getFallbackValue(inputId, liveValue) {
  var input = getInput(inputId);
  if (liveValue && liveValue.length > 0) { return liveValue; }
  if (input.value && input.value.trim().length > 0) { return input.value.trim(); }
  if (input.placeholder && input.placeholder.length > 0) { return input.placeholder; }
  return '';
}

function updateDelayFieldMode() {
  var enabled = document.getElementById('doDelayCheckbox').checked;
  document.getElementById('delaySecondsInput').disabled = !enabled;
}

function updateIpFieldMode() {
  var useDhcp = document.getElementById('useDhcpCheckbox').checked;
  document.getElementById('staticIpFields').style.display = 'block';
  document.getElementById('ipInput').readOnly = useDhcp;
  document.getElementById('dnsInput').readOnly = useDhcp;
  document.getElementById('gatewayInput').readOnly = useDhcp;
  document.getElementById('subnetInput').readOnly = useDhcp;
}

function onDhcpToggle() {
  var useDhcp = document.getElementById('useDhcpCheckbox').checked;

  if (useDhcp) {
    setIpInputValues(
      getFallbackValue('ipInput', lastReportedNetwork.ip),
      getFallbackValue('dnsInput', lastReportedNetwork.dns),
      getFallbackValue('gatewayInput', lastReportedNetwork.gateway),
      getFallbackValue('subnetInput', lastReportedNetwork.subnet)
    );
  } else {
    if (!getInput('ipInput').value.trim()) { getInput('ipInput').value = getFallbackValue('ipInput', lastReportedNetwork.ip); }
    if (!getInput('dnsInput').value.trim()) { getInput('dnsInput').value = getFallbackValue('dnsInput', lastReportedNetwork.dns); }
    if (!getInput('gatewayInput').value.trim()) { getInput('gatewayInput').value = getFallbackValue('gatewayInput', lastReportedNetwork.gateway); }
    if (!getInput('subnetInput').value.trim()) { getInput('subnetInput').value = getFallbackValue('subnetInput', lastReportedNetwork.subnet); }
  }

  updateIpFieldMode();
}

window.addEventListener('load', onLoad);

function onLoad() {
  clearRefreshQueryParam();

  wifiSsidDisplay = document.getElementById('wifiSsidDisplay');
  wifiConnectedSsidDisplay = document.getElementById('wifiConnectedSsidDisplay');
  wifiStatusDisplay = document.getElementById('wifiStatusDisplay');
  wifiRssiDisplay = document.getElementById('wifiRssiDisplay');
  wifiRescanStatusDisplay = document.getElementById('wifiRescanStatusDisplay');
  rescanWifiButton = document.getElementById('rescanWifiButton');

  document.getElementById('saveConfig').addEventListener('click', saveConfig);
  document.getElementById('rescanWifiButton').addEventListener('click', triggerWifiRescan);
  document.getElementById('clearWifiButton').addEventListener('click', clearWifiCredentials);
  document.getElementById('backButton').addEventListener('click', function () {
    window.location.href = '/';
  });
  document.getElementById('useDhcpCheckbox').addEventListener('change', onDhcpToggle);
  document.getElementById('doDelayCheckbox').addEventListener('change', function () {
    currentRelayModes.doDelay = this.checked;
    updateDelayFieldMode();
  });
  document.getElementById('connectStrongestCheckbox').addEventListener('change', function () {
    connectStrongestOnStartup = this.checked;
  });
  document.getElementById('delaySecondsInput').addEventListener('input', function () {
    currentRelayModes.delaySeconds = this.value;
  });

  ['boardNameInput', 'useDhcpCheckbox', 'ipInput', 'dnsInput', 'gatewayInput', 'subnetInput',
    'doDelayCheckbox', 'connectStrongestCheckbox', 'delaySecondsInput']
    .forEach(function (id) {
      var el = document.getElementById(id);
      if (!el) { return; }
      el.addEventListener('input', function () { suspendConfigFormHydration = true; });
      el.addEventListener('change', function () { suspendConfigFormHydration = true; });
    });

  fetchNetInfo();
  if (!wifiRefreshTimer) {
    wifiRefreshTimer = setInterval(fetchNetInfo, 3000);
  }
  updateDelayFieldMode();
  setConfigPageReady();
}

function clearWifiCredentials() {
  var confirmed = window.confirm('Clear stored Wi-Fi credentials and restart now?\n\nDefault: No');
  if (!confirmed) { return; }

  var clearButton = document.getElementById('clearWifiButton');
  var originalText = clearButton.textContent;
  clearButton.disabled = true;
  clearButton.textContent = 'Clearing...';

  fetch('/api/clearwifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: 'confirm=1'
  })
    .then(function (response) {
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        return { status: response.status, ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) { throw new Error(result.body.error || 'clear failed'); }
      alert('Wi-Fi credentials cleared. Device is restarting into provisioning mode.');
      setTimeout(function () { window.location.href = '/'; }, 800);
    })
    .catch(function (error) { alert('Clear Wi-Fi failed: ' + error.message); })
    .finally(function () {
      clearButton.disabled = false;
      clearButton.textContent = originalText;
    });
}

function triggerWifiRescan() {
  if (!rescanWifiButton || rescanWifiButton.disabled) { return; }

  var originalText = rescanWifiButton.textContent;
  rescanWifiButton.disabled = true;
  rescanWifiButton.textContent = 'Rescanning...';

  fetch('/api/wifi/rescan', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: 'confirm=1'
  })
    .then(function (response) {
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        return { status: response.status, ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) { throw new Error(result.body.error || 'rescan failed'); }
    })
    .catch(function (error) {
      alert('Wi-Fi rescan failed: ' + error.message);
      rescanWifiButton.disabled = false;
      rescanWifiButton.textContent = originalText;
    });
}

function saveConfig() {
  debugLog('saveConfig() called');

  var boardNameInput = document.getElementById('boardNameInput');
  var newName = boardNameInput.value.trim();

  if (newName.length === 0) {
    alert('Board name cannot be empty.');
    return;
  }

  var useDhcpChecked = document.getElementById('useDhcpCheckbox').checked;
  var ipValue = document.getElementById('ipInput').value.trim();
  var dnsValue = document.getElementById('dnsInput').value.trim();
  var gatewayValue = document.getElementById('gatewayInput').value.trim();
  var subnetValue = document.getElementById('subnetInput').value.trim();
  var doDelayChecked = document.getElementById('doDelayCheckbox').checked;
  var strongestChecked = document.getElementById('connectStrongestCheckbox').checked;
  var delaySecondsValue = document.getElementById('delaySecondsInput').value.trim();

  if (doDelayChecked) {
    if (delaySecondsValue.length === 0) {
      alert('Enter a startup delay in seconds.');
      return;
    }
    var parsedDelaySeconds = parseInt(delaySecondsValue, 10);
    if (isNaN(parsedDelaySeconds) || parsedDelaySeconds < 0 || parsedDelaySeconds > 300) {
      alert('Startup delay must be between 0 and 300 seconds.');
      return;
    }
    delaySecondsValue = String(parsedDelaySeconds);
  } else {
    delaySecondsValue = delaySecondsValue.length > 0 ? delaySecondsValue : '60';
  }

  currentRelayModes.doDelay = doDelayChecked;
  connectStrongestOnStartup = strongestChecked;
  suspendConfigFormHydration = false;
  applyBoardName(newName);

  var hasAnyStaticField = ipValue.length > 0 || dnsValue.length > 0 || gatewayValue.length > 0 || subnetValue.length > 0;
  var hasAllStaticFields = ipValue.length > 0 && dnsValue.length > 0 && gatewayValue.length > 0 && subnetValue.length > 0;

  var ipPayload;
  if (useDhcpChecked || !hasAnyStaticField) {
    ipPayload = { useDhcp: true };
  } else {
    if (!hasAllStaticFields) {
      alert('Provide all static IP fields, or clear them all to use DHCP.');
      return;
    }
    ipPayload = { useDhcp: false, ip: ipValue, dns: dnsValue, gateway: gatewayValue, subnet: subnetValue };
  }

  var saveButton = document.getElementById('saveConfig');
  saveButton.disabled = true;
  saveButton.textContent = 'Saving...';
  var isRebooting = false;

  var form = new URLSearchParams();
  form.set('name', newName);
  form.set('useDhcp', ipPayload.useDhcp ? '1' : '0');
  form.set('ip', ipPayload.ip || '');
  form.set('dns', ipPayload.dns || '');
  form.set('gateway', ipPayload.gateway || '');
  form.set('subnet', ipPayload.subnet || '');
  form.set('doDelay', doDelayChecked ? '1' : '0');
  form.set('connectStrongestOnStartup', strongestChecked ? '1' : '0');
  form.set('connectStrongest', strongestChecked ? '1' : '0');
  form.set('delaySeconds', delaySecondsValue);

  fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: form.toString()
  })
    .then(function (response) {
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        return { status: response.status, ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) { throw new Error(result.body.error || 'save failed'); }

      isRebooting = true;
      saveButton.textContent = 'Rebooting...';

      var appliedIp = (result.body.appliedIp || '').trim();
      var useDhcp = !!result.body.useDhcp;
      var ipChanged = !useDhcp && appliedIp.length > 0 && appliedIp !== window.location.hostname;

      if (ipChanged) {
        setTimeout(function () { window.location.href = 'http://' + appliedIp + '/'; }, 6000);
      } else {
        startRestartRedirectWatcher();
      }
    })
    .catch(function (error) {
      alert('Save failed: ' + error.message);
    })
    .finally(function () {
      if (!isRebooting) {
        saveButton.disabled = false;
        saveButton.textContent = 'Save';
      }
    });
}

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
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
var currentHardwareVariant = '8relay';
var DEBUG_LOGS = false;
var wifiRefreshTimer = null;
var restartRedirectDelayTimer = null;
var restartRedirectPollTimer = null;
var websocketEverConnected = false;
var suspendConfigFormHydration = false;

var wifiSsidDisplay;
var wifiConnectedSsidDisplay;
var wifiStatusDisplay;
var wifiRssiDisplay;
var wifiRescanStatusDisplay;
var rescanWifiButton;

function debugLog() {
  if (DEBUG_LOGS && window.console && typeof console.log === 'function') {
    console.log.apply(console, arguments);
  }
}

window.addEventListener('load', onLoad);

function setIpPlaceholders(ip, dns, gateway, subnet) {
  document.getElementById('ipInput').placeholder = ip || '';
  document.getElementById('dnsInput').placeholder = dns || '';
  document.getElementById('gatewayInput').placeholder = gateway || '';
  document.getElementById('subnetInput').placeholder = subnet || '';
}

function setConfigPageReady() {
  var configPage = document.querySelector('.config-page');
  if (configPage) {
    configPage.removeAttribute('data-loading');
  }
}

function onLoad() {
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
   'doDelayCheckbox', 'connectStrongestCheckbox', 'delaySecondsInput', 'hardwareVariantSelect']
    .forEach(function (id) {
      var el = document.getElementById(id);
      if (!el) {
        return;
      }
      el.addEventListener('input', function () {
        suspendConfigFormHydration = true;
      });
      el.addEventListener('change', function () {
        suspendConfigFormHydration = true;
      });
    });

  fetchNetInfo();
  initWebSocket();
  if (!wifiRefreshTimer) {
    wifiRefreshTimer = setInterval(requestRuntimeRefresh, 3000);
  }
  updateDelayFieldMode();
  setConfigPageReady();
}

function requestRuntimeRefresh() {
  if (websocket && websocket.readyState === WebSocket.OPEN) {
    websocket.send('home');
  } else {
    fetchNetInfo();
  }
}

function clearWifiCredentials() {
  var confirmed = window.confirm('Clear stored Wi-Fi credentials and restart now?\n\nDefault: No');
  if (!confirmed) {
    return;
  }

  var clearButton = document.getElementById('clearWifiButton');
  var originalText = clearButton.textContent;
  clearButton.disabled = true;
  clearButton.textContent = 'Clearing...';

  fetch('/api/clearwifi', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
    },
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
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'clear failed');
      }

      alert('Wi-Fi credentials cleared. Device is restarting into provisioning mode.');
      setTimeout(function () {
        window.location.href = '/';
      }, 800);
    })
    .catch(function (error) {
      alert('Clear Wi-Fi failed: ' + error.message);
    })
    .finally(function () {
      clearButton.disabled = false;
      clearButton.textContent = originalText;
    });
}

function triggerWifiRescan() {
  if (!rescanWifiButton || rescanWifiButton.disabled) {
    return;
  }

  var originalText = rescanWifiButton.textContent;
  rescanWifiButton.disabled = true;
  rescanWifiButton.textContent = 'Rescanning...';

  fetch('/api/wifi/rescan', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
    },
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
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'rescan failed');
      }
    })
    .catch(function (error) {
      alert('Wi-Fi rescan failed: ' + error.message);
      rescanWifiButton.disabled = false;
      rescanWifiButton.textContent = originalText;
    });
}

function shouldPreserveBoardNameInput(incomingName) {
  var input = document.getElementById('boardNameInput');
  if (!input) {
    return false;
  }

  if (document.activeElement === input) {
    return true;
  }

  var localValue = String(input.value || '').trim();
  if (!localValue) {
    return false;
  }

  // Keep unsaved local edits if they differ from both current and incoming values.
  return localValue !== currentBoardName && localValue !== incomingName;
}

function applyBoardName(name) {
  var normalized = (name || '').trim();
  if (!normalized) {
    normalized = 'Relay Board';
  }

  currentBoardName = normalized;
  if (!shouldPreserveBoardNameInput(normalized)) {
    document.getElementById('boardNameInput').value = normalized;
  }

  var header = document.getElementById('configPageTitle');
  if (header) {
    header.textContent = normalized;
  }
  document.title = normalized + ' - Configuration';
}

function applyNetworkState(useDhcp, ip, dns, gateway, subnet) {
  if (typeof useDhcp === 'boolean') {
    currentUseStaticIp = !useDhcp;
    document.getElementById('useDhcpCheckbox').checked = useDhcp;
  }

  if (typeof ip === 'string' && ip.length > 0) lastReportedNetwork.ip = ip;
  if (typeof dns === 'string' && dns.length > 0) lastReportedNetwork.dns = dns;
  if (typeof gateway === 'string' && gateway.length > 0) lastReportedNetwork.gateway = gateway;
  if (typeof subnet === 'string' && subnet.length > 0) lastReportedNetwork.subnet = subnet;

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
      if (!response.ok) {
        throw new Error('netinfo fetch failed');
      }
      return response.json();
    })
    .then(function (net) {
      var useDhcp = (typeof net.useDhcp === 'boolean') ? net.useDhcp : undefined;
      applyNetworkState(useDhcp, net.ipAddress || '', net.dns || '', net.gateway || '', net.subnet || '');
    })
    .catch(function () {
      // WebSocket updates remain the primary source; this is a fallback path.
    });
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
  if (liveValue && liveValue.length > 0) {
    return liveValue;
  }
  if (input.value && input.value.trim().length > 0) {
    return input.value.trim();
  }
  if (input.placeholder && input.placeholder.length > 0) {
    return input.placeholder;
  }
  return '';
}

function updateDelayFieldMode() {
  var enabled = document.getElementById('doDelayCheckbox').checked;
  var delayInput = document.getElementById('delaySecondsInput');
  delayInput.disabled = !enabled;
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
          // Keep polling until the device is reachable again.
        });
    }, 1200);
  }, 1500);
}

function onOpen() {
  websocketEverConnected = true;
  stopRestartRedirectWatcher();
  debugLog('Config websocket connected');
  // Force a fresh full state payload for this page.
  websocket.send('home');
  fetchNetInfo();
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

  if (jsonObj.boardName) {
    if (!suspendConfigFormHydration) {
      applyBoardName(jsonObj.boardName);
    }
  }

  if (wifiSsidDisplay) {
    var configuredSsid = String(jsonObj.wifiConfiguredSsid || jsonObj.ssid || '').trim();
    wifiSsidDisplay.value = configuredSsid.length > 0 ? configuredSsid : '(not configured)';
  }

  if (wifiConnectedSsidDisplay) {
    var connectedSsid = String(jsonObj.wifiConnectedSsid || '').trim();
    wifiConnectedSsidDisplay.value = connectedSsid.length > 0 ? connectedSsid : '-';
  }

  if (wifiStatusDisplay) {
    var connected = !!jsonObj.wifiConnected;
    wifiStatusDisplay.value = connected ? 'Connected' : 'Disconnected';
  }

  if (wifiRssiDisplay) {
    var isConnected = !!jsonObj.wifiConnected;
    var hasRssi = (typeof jsonObj.wifiRssi === 'number');
    wifiRssiDisplay.value = (isConnected && hasRssi) ? (String(jsonObj.wifiRssi) + ' dBm') : '-';
  }

  if (wifiRescanStatusDisplay) {
    var rescanStatusText = String(jsonObj.wifiRescanStatus || 'Idle');
    wifiRescanStatusDisplay.value = rescanStatusText;
  }
  if (rescanWifiButton) {
    var scanning = !!jsonObj.wifiRescanInProgress;
    rescanWifiButton.disabled = scanning;
    rescanWifiButton.textContent = scanning ? 'Rescanning...' : 'Rescan & Refresh Strongest AP';
  }

  if (jsonObj.mcuType) {
    document.getElementById('mcuTypeDisplay').value = String(jsonObj.mcuType);
  }

  if (jsonObj.hardwareVariant) {
    var variant = String(jsonObj.hardwareVariant).toLowerCase();
    if (variant !== '8relay' && variant !== '16relay') {
      variant = '8relay';
    }
    currentHardwareVariant = variant;
    if (!suspendConfigFormHydration) {
      document.getElementById('hardwareVariantSelect').value = variant;
    }
  }

  if (!suspendConfigFormHydration) {
    var hasAllIpFields = !!(jsonObj.ipAddress && jsonObj.dns && jsonObj.gateway && jsonObj.subnet);
    var useDhcp = jsonObj.hasOwnProperty('useDhcp')
      ? !!jsonObj.useDhcp
      : (jsonObj.hasOwnProperty('useStaticIp') ? !jsonObj.useStaticIp : !hasAllIpFields);
    currentUseStaticIp = !useDhcp;
    document.getElementById('useDhcpCheckbox').checked = useDhcp;

    applyNetworkState(
      useDhcp,
      jsonObj.ipAddress || '',
      jsonObj.dns || '',
      jsonObj.gateway || '',
      jsonObj.subnet || ''
    );

    if (currentUseStaticIp) {
      currentIpConfig = {
        ip: jsonObj.ipAddress || lastReportedNetwork.ip,
        dns: jsonObj.dns || lastReportedNetwork.dns,
        gateway: jsonObj.gateway || lastReportedNetwork.gateway,
        subnet: jsonObj.subnet || lastReportedNetwork.subnet
      };
    } else {
      currentIpConfig = {
        ip: getFallbackValue('ipInput', lastReportedNetwork.ip),
        dns: getFallbackValue('dnsInput', lastReportedNetwork.dns),
        gateway: getFallbackValue('gatewayInput', lastReportedNetwork.gateway),
        subnet: getFallbackValue('subnetInput', lastReportedNetwork.subnet)
      };
    }

    setIpInputValues(
      currentIpConfig.ip,
      currentIpConfig.dns,
      currentIpConfig.gateway,
      currentIpConfig.subnet
    );

    if (jsonObj.hasOwnProperty('doDelay')) {
      currentRelayModes.doDelay = !!jsonObj.doDelay;
      document.getElementById('doDelayCheckbox').checked = currentRelayModes.doDelay;
    }
    if (jsonObj.hasOwnProperty('startupDelaySeconds')) {
      document.getElementById('delaySecondsInput').value = jsonObj.startupDelaySeconds;
      currentRelayModes.delaySeconds = jsonObj.startupDelaySeconds;
    }
    if (jsonObj.hasOwnProperty('connectStrongestOnStartup')) {
      connectStrongestOnStartup = !!jsonObj.connectStrongestOnStartup;
      document.getElementById('connectStrongestCheckbox').checked = connectStrongestOnStartup;
    }

    updateIpFieldMode();
    updateDelayFieldMode();
  }
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
    if (!getInput('ipInput').value.trim()) getInput('ipInput').value = getFallbackValue('ipInput', lastReportedNetwork.ip);
    if (!getInput('dnsInput').value.trim()) getInput('dnsInput').value = getFallbackValue('dnsInput', lastReportedNetwork.dns);
    if (!getInput('gatewayInput').value.trim()) getInput('gatewayInput').value = getFallbackValue('gatewayInput', lastReportedNetwork.gateway);
    if (!getInput('subnetInput').value.trim()) getInput('subnetInput').value = getFallbackValue('subnetInput', lastReportedNetwork.subnet);
  }

  updateIpFieldMode();
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
  var hardwareVariantValue = document.getElementById('hardwareVariantSelect').value;
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
  currentHardwareVariant = (hardwareVariantValue === '16relay') ? '16relay' : '8relay';
  suspendConfigFormHydration = false;
  applyBoardName(newName);

  debugLog('saveConfig: name=' + newName + ', dhcp=' + useDhcpChecked + ', ip=' + ipValue + ', dns=' + dnsValue);

  var hasAnyStaticField = ipValue.length > 0 || dnsValue.length > 0 || gatewayValue.length > 0 || subnetValue.length > 0;
  var hasAllStaticFields = ipValue.length > 0 && dnsValue.length > 0 && gatewayValue.length > 0 && subnetValue.length > 0;

  var ipPayload;
  if (useDhcpChecked || !hasAnyStaticField) {
    ipPayload = {
      cmd: 'setBoardIP',
      useDhcp: true
    };
  } else {
    if (!hasAllStaticFields) {
      alert('Provide all static IP fields, or clear them all to use DHCP.');
      return;
    }

    ipPayload = {
      cmd: 'setBoardIP',
      useDhcp: false,
      ip: ipValue,
      dns: dnsValue,
      gateway: gatewayValue,
      subnet: subnetValue
    };
  }

  var relayPayload = {
    doDelay: doDelayChecked,
    connectStrongestOnStartup: strongestChecked
  };

  debugLog('saveConfig: relayModes=' + JSON.stringify(relayPayload));

  var saveButton = document.getElementById('saveConfig');
  var originalButtonText = saveButton.textContent;
  saveButton.disabled = true;
  saveButton.textContent = 'Saving...';

  var form = new URLSearchParams();
  form.set('name', newName);
  form.set('useDhcp', ipPayload.useDhcp ? '1' : '0');
  form.set('ip', ipPayload.ip || '');
  form.set('dns', ipPayload.dns || '');
  form.set('gateway', ipPayload.gateway || '');
  form.set('subnet', ipPayload.subnet || '');
  form.set('doDelay', relayPayload.doDelay ? '1' : '0');
  form.set('connectStrongestOnStartup', relayPayload.connectStrongestOnStartup ? '1' : '0');
  form.set('connectStrongest', relayPayload.connectStrongestOnStartup ? '1' : '0');
  form.set('delaySeconds', delaySecondsValue);
  form.set('hardwareVariant', currentHardwareVariant);

  debugLog('saveConfig: form data:', form.toString());

  fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
    },
    body: form.toString()
  })
    .then(function (response) {
      debugLog('saveConfig: fetch response status=' + response.status);
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        debugLog('saveConfig: fetch response body=' + JSON.stringify(json));
        return { status: response.status, ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'save failed');
      }

      debugLog('saveConfig: success, restart=' + result.body.restart + ', appliedIp=' + result.body.appliedIp);

      var currentHost = window.location.hostname;
      var currentOrigin = window.location.origin;
      var appliedIp = (result.body.appliedIp || '').trim();
      var useDhcp = !!result.body.useDhcp;
      var shouldRedirectToNewStaticIp = result.body.restart && !useDhcp && appliedIp.length > 0 && appliedIp !== currentHost;

      // When switching to DHCP, assume lease remains reachable at current address.
      var redirectTarget = shouldRedirectToNewStaticIp ? ('http://' + appliedIp + '/') : (result.body.restart && useDhcp ? (currentOrigin + '/') : '/');
      var redirectDelay = result.body.restart ? 2200 : 600;
      setTimeout(function () {
        window.location.href = redirectTarget;
      }, redirectDelay);
    })
    .catch(function (error) {
      console.error('saveConfig: error - ' + error.message);
      alert('Save failed: ' + error.message);
    })
    .finally(function () {
      saveButton.disabled = false;
      saveButton.textContent = originalButtonText;
    });
}

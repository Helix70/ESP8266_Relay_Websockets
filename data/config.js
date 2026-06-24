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
  doDelay: false,
  doLatched: false,
  doInterlocked: false,
  doPulsed: false
};
var currentHardwareVariant = '8relay';

window.addEventListener('load', onLoad);

function setIpPlaceholders(ip, dns, gateway, subnet) {
  document.getElementById('ipInput').placeholder = ip || '';
  document.getElementById('dnsInput').placeholder = dns || '';
  document.getElementById('gatewayInput').placeholder = gateway || '';
  document.getElementById('subnetInput').placeholder = subnet || '';
}

function onLoad() {
  document.getElementById('saveConfig').addEventListener('click', saveConfig);
  document.getElementById('clearWifiButton').addEventListener('click', clearWifiCredentials);
  document.getElementById('backButton').addEventListener('click', function () {
    window.location.href = '/';
  });
  document.getElementById('useDhcpCheckbox').addEventListener('change', onDhcpToggle);
  document.getElementById('doDelayCheckbox').addEventListener('change', function () {
    currentRelayModes.doDelay = this.checked;
    updateDelayFieldMode();
  });
  document.getElementById('doLatchedCheckbox').addEventListener('change', function () {
    currentRelayModes.doLatched = this.checked;
  });
  document.getElementById('doInterlockedCheckbox').addEventListener('change', function () {
    currentRelayModes.doInterlocked = this.checked;
  });
  document.getElementById('doPulsedCheckbox').addEventListener('change', function () {
    currentRelayModes.doPulsed = this.checked;
  });
  document.getElementById('delaySecondsInput').addEventListener('input', function () {
    currentRelayModes.delaySeconds = this.value;
  });
  fetchNetInfo();
  initWebSocket();
  updateDelayFieldMode();
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

function applyBoardName(name) {
  var normalized = (name || '').trim();
  if (!normalized) {
    normalized = 'Relay Board';
  }

  currentBoardName = normalized;
  document.getElementById('boardNameInput').value = normalized;

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

function onOpen() {
  console.log('Config websocket connected');
  // Force a fresh full state payload for this page.
  websocket.send('home');
  fetchNetInfo();
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

  if (jsonObj.hardwareVariant) {
    var variant = String(jsonObj.hardwareVariant).toLowerCase();
    if (variant !== '8relay' && variant !== '16relay') {
      variant = '8relay';
    }
    currentHardwareVariant = variant;
    document.getElementById('hardwareVariantSelect').value = variant;
  }

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
  if (jsonObj.hasOwnProperty('doLatched')) {
    currentRelayModes.doLatched = !!jsonObj.doLatched;
    document.getElementById('doLatchedCheckbox').checked = currentRelayModes.doLatched;
  }
  if (jsonObj.hasOwnProperty('doInterlocked')) {
    currentRelayModes.doInterlocked = !!jsonObj.doInterlocked;
    document.getElementById('doInterlockedCheckbox').checked = currentRelayModes.doInterlocked;
  }
  if (jsonObj.hasOwnProperty('doPulsed')) {
    currentRelayModes.doPulsed = !!jsonObj.doPulsed;
    document.getElementById('doPulsedCheckbox').checked = currentRelayModes.doPulsed;
  }

  updateIpFieldMode();
  updateDelayFieldMode();
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
  console.log('saveConfig() called');
  
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
  var hardwareVariantValue = document.getElementById('hardwareVariantSelect').value;
  var doLatchedChecked = document.getElementById('doLatchedCheckbox').checked;
  var doInterlockedChecked = document.getElementById('doInterlockedCheckbox').checked;
  var doPulsedChecked = document.getElementById('doPulsedCheckbox').checked;
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
  currentRelayModes.doLatched = doLatchedChecked;
  currentRelayModes.doInterlocked = doInterlockedChecked;
  currentRelayModes.doPulsed = doPulsedChecked;
  currentHardwareVariant = (hardwareVariantValue === '16relay') ? '16relay' : '8relay';

  console.log('saveConfig: name=' + newName + ', dhcp=' + useDhcpChecked + ', ip=' + ipValue + ', dns=' + dnsValue);

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
    cmd: 'setRelayModes',
    doDelay: doDelayChecked,
    doLatched: doLatchedChecked,
    doInterlocked: doInterlockedChecked,
    doPulsed: doPulsedChecked
  };

  console.log('saveConfig: relayModes=' + JSON.stringify(relayPayload));

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
  form.set('delaySeconds', delaySecondsValue);
  form.set('doLatched', relayPayload.doLatched ? '1' : '0');
  form.set('doInterlocked', relayPayload.doInterlocked ? '1' : '0');
  form.set('doPulsed', relayPayload.doPulsed ? '1' : '0');
  form.set('hardwareVariant', currentHardwareVariant);

  console.log('saveConfig: form data:', form.toString());

  fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
    },
    body: form.toString()
  })
    .then(function (response) {
      console.log('saveConfig: fetch response status=' + response.status);
      return response.json().catch(function () {
        return { ok: false, error: 'invalid response' };
      }).then(function (json) {
        console.log('saveConfig: fetch response body=' + JSON.stringify(json));
        return { status: response.status, ok: response.ok, body: json };
      });
    })
    .then(function (result) {
      if (!result.ok || !result.body.ok) {
        throw new Error(result.body.error || 'save failed');
      }

      console.log('saveConfig: success, restart=' + result.body.restart + ', appliedIp=' + result.body.appliedIp);

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

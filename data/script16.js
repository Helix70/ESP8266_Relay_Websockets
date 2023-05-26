var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
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

function setButtonOnOff(selector, value) {
  console.log('selector:' + selector + ' value:' + value);
  if (value) document.querySelector(selector).classList.add('on');
  else document.querySelector(selector).classList.remove('on');
}

function setButtonDisabledEnabled(selector, value) {
  //if (value) $(selector).prop('disabled',true);
  //else $(selector).prop('disabled',false);
  if (value) document.querySelector(selector).disabled = true;
  else document.querySelector(selector).disabled = false;
}

function onMessage(event) {
  msg = JSON.parse(event.data);
  console.log("Received message: ", msg);

  const keys = Object.keys(msg);
  for (let i = 0; i < keys.length; i++) {
    const key = keys[i];
    console.log(key, msg[key]);
    if (key.slice(0, 5) == "RELAY") {
      let btnName = "#relay" + key.slice(5, key.length) + "toggle";
      setButtonOnOff(btnName, msg[key]);
    }
    else if (key.slice(0, 7) == "DISABLE") {
      let btnName = "#relay" + key.slice(7, key.length) + "toggle";
      setButtonDisabledEnabled(btnName, msg[key]);
    }
  }
}

function onLoad(event) {
  initWebSocket();
  initButtons();
}
function initButtons() {
  //document.getElementById('toggle').addEventListener('click', toggle);
  document.getElementById('relay1toggle').addEventListener('click', toggle);
  document.getElementById('relay2toggle').addEventListener('click', toggle);
  document.getElementById('relay3toggle').addEventListener('click', toggle);
  document.getElementById('relay4toggle').addEventListener('click', toggle);
  document.getElementById('relay5toggle').addEventListener('click', toggle);
  document.getElementById('relay6toggle').addEventListener('click', toggle);
  document.getElementById('relay7toggle').addEventListener('click', toggle);
  document.getElementById('relay8toggle').addEventListener('click', toggle);
  document.getElementById('relay9toggle').addEventListener('click', toggle);
  document.getElementById('relay10toggle').addEventListener('click', toggle);
  document.getElementById('relay11toggle').addEventListener('click', toggle);
  document.getElementById('relay12toggle').addEventListener('click', toggle);
  document.getElementById('relay13toggle').addEventListener('click', toggle);
  document.getElementById('relay14toggle').addEventListener('click', toggle);
  document.getElementById('relay15toggle').addEventListener('click', toggle);
  document.getElementById('relay16toggle').addEventListener('click', toggle);
  document.getElementById('alloff').addEventListener('click', toggle);
  document.getElementById('home').addEventListener('click', toggle);
}

function toggle(callee) {
  callee = callee.srcElement.closest(".button")
  console.log("sending message: ", callee.id);
  websocket.send(callee.id);
}


var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);
function initWebSocket() {
  console.log('Trying to open a WebSocket connection...');
  websocket = new WebSocket(gateway);
  websocket.onopen    = onOpen;
  websocket.onclose   = onClose;
  websocket.onmessage = onMessage; // <-- add this line
}
function onOpen(event) {
  console.log('Connection opened');
}
function onClose(event) {
  console.log('Connection closed');
  setTimeout(initWebSocket, 2000);
}
function onMessage(event) {
  msg = JSON.parse(event.data);
  console.log("Received message: ", msg);

  const state = msg.LED ? "ON" : "OFF";

  document.getElementById('state').innerHTML = state;
  updateRelays(msg);
}

function updateRelays(event) {
  const relay1state = msg.RELAY1 ? "ON" : "OFF";
  const relay2state = msg.RELAY2 ? "ON" : "OFF";
  document.getElementById('relay1state').innerHTML = relay1state;
  document.getElementById('relay2state').innerHTML = relay2state;
  document.getElementById('relay1toggle').disabled = msg.RELAY1 || msg.RELAY2;
  document.getElementById('relay2toggle').disabled = msg.RELAY1 || msg.RELAY2;
}

function onLoad(event) {
  initWebSocket();
  initButtons();
}
function initButtons() {
  document.getElementById('toggle').addEventListener('click', toggle);
  document.getElementById('relay1toggle').addEventListener('click', toggle);
  document.getElementById('relay2toggle').addEventListener('click', toggle);
}

function toggle (callee) {
  updateRelays({
    RELAY1: callee.srcElement.id == 'relay1toggle',
    RELAY2: callee.srcElement.id == 'relay2toggle',
  });
  console.log("sending message: ", callee.srcElement.id);
  websocket.send(callee.srcElement.id);
}


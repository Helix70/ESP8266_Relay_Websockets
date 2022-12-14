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
  const relay3state = msg.RELAY3 ? "ON" : "OFF";
  const relay4state = msg.RELAY4 ? "ON" : "OFF";
  const relay5state = msg.RELAY5 ? "ON" : "OFF";
  const relay6state = msg.RELAY6 ? "ON" : "OFF";
  const relay7state = msg.RELAY7 ? "ON" : "OFF";
  const relay8state = msg.RELAY8 ? "ON" : "OFF";

  document.getElementById('relay1state').innerHTML = relay1state;
  document.getElementById('relay2state').innerHTML = relay2state;
  document.getElementById('relay3state').innerHTML = relay3state;
  document.getElementById('relay4state').innerHTML = relay4state;
  document.getElementById('relay5state').innerHTML = relay5state;
  document.getElementById('relay6state').innerHTML = relay6state;
  document.getElementById('relay7state').innerHTML = relay7state;
  document.getElementById('relay8state').innerHTML = relay8state;
  
  document.getElementById('relay1toggle').disabled = msg.RELAY1 || msg.RELAY2;
  document.getElementById('relay2toggle').disabled = msg.RELAY1 || msg.RELAY2;
  document.getElementById('relay5toggle').disabled = msg.RELAY5 || msg.RELAY6;
  document.getElementById('relay6toggle').disabled = msg.RELAY5 || msg.RELAY6;
  document.getElementById('relay7toggle').disabled = msg.RELAY7 || msg.RELAY8;
  document.getElementById('relay8toggle').disabled = msg.RELAY7 || msg.RELAY8;
}

function onLoad(event) {
  initWebSocket();
  initButtons();
}
function initButtons() {
  document.getElementById('toggle').addEventListener('click', toggle);
  document.getElementById('relay1toggle').addEventListener('click', toggle);
  document.getElementById('relay2toggle').addEventListener('click', toggle);
  document.getElementById('relay3toggle').addEventListener('click', toggle);
  document.getElementById('relay4toggle').addEventListener('click', toggle);
  document.getElementById('relay5toggle').addEventListener('click', toggle);
  document.getElementById('relay6toggle').addEventListener('click', toggle);
  document.getElementById('relay7toggle').addEventListener('click', toggle);
  document.getElementById('relay8toggle').addEventListener('click', toggle);
}

function toggle (callee) {
  updateRelays({
    RELAY1: callee.srcElement.id == 'relay1toggle',
    RELAY2: callee.srcElement.id == 'relay2toggle',
    RELAY3: callee.srcElement.id == 'relay3toggle',
    RELAY4: callee.srcElement.id == 'relay4toggle',
    RELAY5: callee.srcElement.id == 'relay5toggle',
    RELAY6: callee.srcElement.id == 'relay6toggle',
    RELAY7: callee.srcElement.id == 'relay7toggle',
    RELAY8: callee.srcElement.id == 'relay8toggle',
  });
  console.log("sending message: ", callee.srcElement.id);
  websocket.send(callee.srcElement.id);
}


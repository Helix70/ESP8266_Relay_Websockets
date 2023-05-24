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
function onMessage(event) {
  msg = JSON.parse(event.data);
  console.log("Received message: ", msg);

  const state = msg.LED ? "ON" : "OFF";

  //document.getElementById('state').innerHTML = state;
  updateRelays(msg);
}

function setButton(selector, value) {
  if (value) document.querySelector(selector).classList.add('on');
  else document.querySelector(selector).classList.remove('on');
}

function updateRelays(_msg) {
  if (typeof(_msg.RELAY8)== "boolean") {
  setButton("#relay1toggle", _msg.RELAY1);
  setButton("#relay2toggle", _msg.RELAY2);
  setButton("#relay3toggle", _msg.RELAY3);
  setButton("#relay4toggle", _msg.RELAY4);
  setButton("#relay5toggle", _msg.RELAY5);
  setButton("#relay6toggle", _msg.RELAY6);
  setButton("#relay7toggle", _msg.RELAY7);
  setButton("#relay8toggle", _msg.RELAY8);
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
}

function toggle(callee) {
  callee=callee.srcElement.closest(".button")
  console.log("sending message: ", callee.id);
  websocket.send(callee.id);
}


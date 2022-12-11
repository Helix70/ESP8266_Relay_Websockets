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
  var state;
  if (event.data == "1"){
    state = "ON";
  }
  else{
    state = "OFF";
  }
  document.getElementById('state').innerHTML = state;
}
function onLoad(event) {
  initWebSocket();
  initButton();
}
function initButton() {
  document.getElementById('button').addEventListener('click', toggle);
}
function toggle(){
  websocket.send('toggle');
}
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

function setButtonOn(selector, value) {
  console.log('selector:' + selector + ' value:' + value);
  if (value) document.querySelector(selector).classList.add('on');
  else document.querySelector(selector).classList.remove('on');
}

function setButtonLast(selector, value) {
  if (value) document.querySelector(selector).classList.add('last');
  else document.querySelector(selector).classList.remove('last');
}

function setButtonDisabled(selector, value) {
  if (value) document.querySelector(selector).disabled = true;
  else document.querySelector(selector).disabled = false;
}

function onMessage(event) {
  jsonObj = JSON.parse(event.data);
  console.log("Received message: ", jsonObj);

  var buttons = jsonObj['buttons'];
  for (var buttonName in buttons) {
    var button = buttons[buttonName];
    console.log(button);
    let btnName = "#button" + button.id;
    setButtonOn(btnName, button.on);
    setButtonLast(btnName, button.last);
    setButtonDisabled(btnName, button.disabled);
  }
}

function onLoad(event) {
  initWebSocket();
  initButtons();
}

function initButtons() {
  //document.getElementById('toggle').addEventListener('click', toggle);
  document.getElementById('button1').addEventListener('click', toggle);
  document.getElementById('button2').addEventListener('click', toggle);
  document.getElementById('button3').addEventListener('click', toggle);
  document.getElementById('button4').addEventListener('click', toggle);
  document.getElementById('button5').addEventListener('click', toggle);
  document.getElementById('button6').addEventListener('click', toggle);
  document.getElementById('button7').addEventListener('click', toggle);
  document.getElementById('button8').addEventListener('click', toggle);
  document.getElementById('button9').addEventListener('click', toggle);
  document.getElementById('button10').addEventListener('click', toggle);
  document.getElementById('button11').addEventListener('click', toggle);
  document.getElementById('button12').addEventListener('click', toggle);
  document.getElementById('button13').addEventListener('click', toggle);
  document.getElementById('button14').addEventListener('click', toggle);
  document.getElementById('button15').addEventListener('click', toggle);
  document.getElementById('button16').addEventListener('click', toggle);
  document.getElementById('alloff').addEventListener('click', toggle);
  document.getElementById('home').addEventListener('click', toggle);
}

function toggle(callee) {
  callee = callee.srcElement.closest(".button")
  console.log("sending message: ", callee.id);
  websocket.send(callee.id);
}


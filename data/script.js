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
  if (value) document.querySelector(selector).classList.add('on');
  else document.querySelector(selector).classList.remove('on');
}

function setButtonDisabledEnabled(selector, value) {
  if (value) $(selector).prop('disabled',true);
  else $(selector).prop('disabled',false);
  //if (value) document.querySelector(selector).prop('disabled',true);
  //else document.querySelector(selector).prop('disabled',false);
}

function onMessage(event) {
  msg = JSON.parse(event.data);
  console.log("Received message: ", msg);

  function Iterate(data)
  {
    $.each(data,function(key,val) {
      if (key.slice(0,5) == "RELAY") {
        let btnName = "#relaytoggle" + key.slice(5,key.length);
        setButtonOnOff(btnName,val);
      }
      else if (key.slice(0,7) == "DISABLE") {
        let btnName = "#relaytoggle" + key.slice(7,key.length);
        setButtonDisabledEnabled(btnName,val);
      }
    })
  }

  //updateRelays(msg);
}

/*
function updateRelays(_msg) {
  if (typeof(_msg.RELAY8)== "boolean") {
    setButtonOnOff("#relay1toggle", _msg.RELAY1);
    setButtonOnOff("#relay2toggle", _msg.RELAY2);
    setButtonOnOff("#relay3toggle", _msg.RELAY3);
    setButtonOnOff("#relay4toggle", _msg.RELAY4);
    setButtonOnOff("#relay5toggle", _msg.RELAY5);
    setButtonOnOff("#relay6toggle", _msg.RELAY6);
    setButtonOnOff("#relay7toggle", _msg.RELAY7);
    setButtonOnOff("#relay8toggle", _msg.RELAY8);
  }
}
*/

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


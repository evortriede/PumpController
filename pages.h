static const char otaUpdatePage[] PROGMEM =R"(
<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>
<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>
   <input type='file' name='update'>
        <input type='submit' value='Update'>
</form>
<div id='prg'>progress: 0%</div>
<script>
  $('form').submit(function(e)
  {
    e.preventDefault();
    var form = $('#upload_form')[0];
    var data = new FormData(form);
    $.ajax(
    {
      url: '/update',
      type: 'POST',
      data: data,
      contentType: false,
      processData:false,
      xhr: function() 
      {
        var xhr = new window.XMLHttpRequest();
        xhr.upload.addEventListener('progress', function(evt) 
        {
          if (evt.lengthComputable) 
          {
            var per = evt.loaded / evt.total;
            $('#prg').html('progress: ' + Math.round(per*100) + '%');
          }
        }, false);
        return xhr;
      },
      success:function(d, s) 
      {
        console.log('success!')
      },
      error: function (a, b, c) 
      {
      }
     });
   });
 </script>
)";

static const char rootPage[] PROGMEM =R"(
<html>
  <head>
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate" />
    <meta http-equiv="Pragma" content="no-cache" />
    <meta http-equiv="Expires" content="0" />
    <script>
      let pumpChanged=false;
      var gateway = `ws://${window.location.hostname}/ws`;
      var websocket;
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
      function onPumpChanged()
      {
        pumpChanged=true;
      }
      function onMessage(event) 
      {
        // message - raw,fill,text,style,pump,automatic
        //            0    1    2    3     4    5
        
        rg=event.data.split(',');
        document.getElementById('rawcl17reading').value=rg[0];
        if (pumpChanged==false)
        {
          document.getElementById('pump').value=rg[4];
        }

        document.getElementById("auto").value=rg[5];
        document.getElementById("testMode").value=rg[6];

        var canvas = document.getElementById("myCanvas");
        var ctx = canvas.getContext("2d");
        ctx.clearRect(0,0,500,100);
        let x=rg[1];
        ctx.fillStyle = rg[3];
        ctx.fillRect(0,12,x,73);
        ctx.font = "30px Arial";
        ctx.strokeText(rg[2],x/2,60);
      }
      function sendPumpSetting()
      {
        websocket.send(document.getElementById("pump").value);
        pumpChanged=false;
      }
      function toggleAutomatic()
      {
        websocket.send("toggleAuto");
      }
      function toggleTestMode()
      {
        websocket.send("toggleTest");
      }
      function sendClTest()
      {
        websocket.send(document.getElementById("cltest").value);
      }
    </script>

  </head>
  <body onLoad="initWebSocket();">
    <canvas id="myCanvas" width="500" height="100"
    style="border:1px solid #c3c3c3;">
    Your browser does not support the canvas element.
    </canvas>
    
    <div style='font-size:250%'>
      Raw CL17 Reading <input id='rawcl17reading' value='0' style='font-size:40px; border:none'></input><br><br>
      <form action="/setPump" method="GET">
        Pump Setting <input type="number" id="pump" name="pump" style="font-size:40px" value="0" oninput="onPumpChanged();"></input><br><br>
        <input type="button" id="sendPump" value="Change" onclick="sendPumpSetting();"></input><br>
        <input type="button" id="auto" value="Automatic" onclick="toggleAutomatic();"></input><br>
      </form><br>
      <br><a href='/config'>Config</a>
      <div hidden="hidden">
        <br><input type="button" value="StartTest" id="testMode" onclick="toggleTestMode();"></input>
        <br><input type="number" id="cltest" value=0></input> 
        <button onclick="sendClTest();">Send</button>
      </div>
    </div>
  </body>
</html>
)";

static const char configFmt[] PROGMEM =R"(
<html>
  <head>
  </head>
  <body>
    <div style='font-size:150%%'>
      <form action="/setConfig" method="GET">
        IP for Current Monitor <input type="text" name="cmip" style="font-size:30px" value="%s"></input><br>
        Max Pump Value <input type="number" name="pumpmax" style="font-size:30px" value="%i"></input><br>
        Max Chlorine Value <input type="number" name="chlmax" style="font-size:30px" value="%i"></input><br>
        Conversion factor <input type="number" step="0.1" name="factor" style="font-size:30px" value="%0.2f"></input><br>
        Update Frequency (sec) <input type="number" name="frequency" style="font-size:30px" value="%i"></input><br>
        Sweet Spot LOW (ppm) <input type="number" step="0.01" name="sweetlo" style="font-size:30px" value="%0.2f"></input><br>
        Sweet Spot HIGH (ppm) <input type="number" step="0.01" name="sweethi" style="font-size:30px" value="%0.2f"></input><br>
        Tolerance (ppm) <input type="number" step="0.01" name="tolerance" style="font-size:30px" value="%0.2f"></input><br>
        Initial Pump Setting <input type="number" step="1" name="autostart" style="font-size:30px" value="%i"></input><br>
        Adjust Frequency (minutes) <input type="number" step="1" name="adjustfreq" style="font-size:30px" value="%i"></input><br>
        SSID to join <input type="text" name="ssid" style="font-size:30px" value="%s"></input><br>
        Password for SSID to join <input type="text" name="pass" style="font-size:30px" value="%s"></input><br>
        SSID for Captive Net <input type="text" name="captive_ssid" style="font-size:30px" value="%s"></input><br>
        Password for Captive Net SSID <input type="text" name="captive_pass" style="font-size:30px" value="%s"></input><br>
        <input type="submit" style="font-size:40px"></input>
      </form>
      <a href="/">Home</a><br>
      <a href="/ota">OTA</a><br>
      <a href="/reboot">Reboot</a>
    </div>
  </body>
</html>
)";

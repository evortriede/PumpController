#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <AsyncWebSocket.h>
#include <AsyncWebSynchronization.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_log.h>
#include <nvs.h>

#define PUMP_PIN 25
#define CL17_PIN 34

bool useNVS=true;
bool automatic=false;
bool testing=false;
int testChlValue=0;
long autoTime=0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiClient telnetClient;

void* stkCheck()
{
  long x=millis();
  x=x;
  return (void*)&x;
}

typedef struct
{
  char currentMonitorIP[25];
  int pumpMax;
  int chlMax;
  float conversionFactor;
  int frequency;
  float sweetSpotLow;
  float sweetSpotHigh;
  char ssid[25];
  char pass[25];
  char captive_ssid[25];
  char captive_pass[25];
  int autoStartSetting;
  int adjustFrequency;
  float tolerance;
} config_data_t;

config_data_t configData={"192.168.1.2",255,3250,1632.0,10,0.50,1.50,"","","PumpController","",13,8,0.02};

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

char rootIndex[10240];


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
      <br><input type="button" value="StartTest" id="testMode" onclick="toggleTestMode();"></input>
      <br><input type="number" id="cltest" value=0></input> <button onclick="sendClTest();">Send</button>
      <br><br><a href='/config'>Config</a>
    </div>
  </body>
</html>
)";

void reboot()
{
  if (telnetClient)
  {
    telnetClient.stop();
  }
  ESP.restart();
}

bool shouldReboot=false;

void handleRoot(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", rootPage);
  if (shouldReboot)
  {
    reboot();
  }
}

// message - raw,fill,text,style,pump
//            0    1    2    3     4
static const char statusFmt[] = "%i,%i,%s,%s,%i,%s,%s";
char statusBuffer[128];
int pumpSetting=0;
int cl17Reading=0;
float ppm=0.0;

void handleAsyncStatusUpdate()
{
  int fill=(1000*cl17Reading)/((1000*configData.chlMax)/500);
  ppm=cl17Reading/configData.conversionFactor;
  char sPpm[16];
  sprintf(sPpm,"%1.2fppm",ppm);
  char *style;
  if (ppm<configData.sweetSpotLow)
  {
    style="#FFFF00"; // YELLOW
  }
  else if (ppm>=configData.sweetSpotHigh)
  {
    style="#FF0000"; // RED
  }
  else
  {
    style="#00FF00"; //GREEN
  }
  sprintf(statusBuffer,statusFmt,cl17Reading,fill,sPpm,style,pumpSetting,(automatic)?"Manual":"Automatic",(testing)?"Stop Test":"Start Test");
  ws.textAll(String(statusBuffer));
  delay(1000);
}

 
static const char configFmt[] PROGMEM =R"(
<html>
  <head>
  </head>
  <body>
    <div style='font-size:200%%'>
      <form action="/setConfig" method="GET">
        IP for Current Monitor <input type="text" name="cmip" style="font-size:40px" value="%s"></input><br>
        Max Pump Value <input type="number" name="pumpmax" style="font-size:40px" value="%i"></input><br>
        Max Chlorine Value <input type="number" name="chlmax" style="font-size:40px" value="%i"></input><br>
        Conversion factor <input type="number" step="0.1" name="factor" style="font-size:40px" value="%0.2f"></input><br>
        Update Frequency (sec) <input type="number" name="frequency" style="font-size:40px" value="%i"></input><br>
        Sweet Spot LOW <input type="number" step="0.01" name="sweetlo" style="font-size:40px" value="%0.2f"></input><br>
        Sweet Spot HIGH <input type="number" step="0.01" name="sweethi" style="font-size:40px" value="%0.2f"></input><br>
        Initial Pump Setting <input type="number" step="1" name="autostart" style="font-size:40px" value="%i"></input><br>
        Tolerance (ppm) <input type="number" step="0.01" name="tolerance" style="font-size:40px" value="%0.2f"></input><br>
        Adjust Frequency (seconds) <input type="number" step="1" name="adjustfreq" style="font-size:40px" value="%i"></input><br>
        SSID to join <input type="text" name="ssid" style="font-size:40px" value="%s"></input><br>
        Password for SSID to join <input type="text" name="pass" style="font-size:40px" value="%s"></input><br>
        SSID for Captive Net <input type="text" name="captive_ssid" style="font-size:40px" value="%s"></input><br>
        Password for Captive Net SSID <input type="text" name="captive_pass" style="font-size:40px" value="%s"></input><br>
        <input type="submit" style="font-size:40px"></input>
      </form>
      <a href="/">Home</a><br>
      <a href="/ota">OTA</a><br>
      <a href="/reboot">Reboot</a>
    </div>
  </body>
</html>
)";

char httpMsg[4096];

void handleConfig(AsyncWebServerRequest *request)
{
  sprintf(httpMsg
         ,configFmt
         ,configData.currentMonitorIP
         ,configData.pumpMax
         ,configData.chlMax
         ,configData.conversionFactor
         ,configData.frequency
         ,configData.sweetSpotLow
         ,configData.sweetSpotHigh
         ,configData.autoStartSetting
         ,configData.tolerance
         ,configData.adjustFrequency
         ,configData.ssid
         ,configData.pass
         ,configData.captive_ssid
         ,configData.captive_pass
         );
  request->send(200, "text/html", httpMsg);
}

void setPump(int setting)
{
  if (setting<0||setting>255) return;
  Serial.printf("setting pump to %i\n",setting);
  if (setting) // if non-zero we just write it
  {
    dacWrite(PUMP_PIN,setting);
  }
  else if (pumpSetting) // if zero and it was set to non zero we slam it to zero
  {
    Serial.printf("disabling DAC\n");
    dacWrite(PUMP_PIN,0);
    delay(100);
    pinMode(PUMP_PIN,INPUT);
//    dacDisable(PUMP_PIN);
    delay(100);
    pinMode(PUMP_PIN,OUTPUT);
    digitalWrite(PUMP_PIN,LOW);
  }
  pumpSetting=setting;
  if (telnetClient)
  {
    telnetClient.printf("P%i\r\n",pumpSetting);  
  }
}

void handleSetPump(AsyncWebServerRequest *request)
{
  setPump(atoi(request->arg("pump").c_str()));
  request->send(200, "text/html", 
    "<html><head></head><body onload=\"location.replace('/');\"></body></html>\n");
}
/* for refrence
 * 
typedef struct
{
  char currentMonitorIP[25];
  int pumpMax;
  int chlMax;
  float conversionFactor;
  int frequency;
  char ssid[25];
  char pass[25];
  char captive_ssid[25];
  char captive_pass[25];
} config_data_t;
 *
 */
void handleSetConfig(AsyncWebServerRequest *request)
{
  strcpy(configData.currentMonitorIP,request->arg("cmip").c_str());
  configData.pumpMax=atoi(request->arg("pumpmax").c_str());
  configData.chlMax=atoi(request->arg("chlmax").c_str());
  configData.conversionFactor=atof(request->arg("factor").c_str());
  configData.frequency=atoi(request->arg("frequency").c_str());
  configData.sweetSpotLow=atof(request->arg("sweetlo").c_str());
  configData.sweetSpotHigh=atof(request->arg("sweethi").c_str());
  configData.autoStartSetting=atoi(request->arg("autostart").c_str());
  configData.tolerance=atof(request->arg("tolerance").c_str());
  configData.autoStartSetting=atoi(request->arg("adjustfreq").c_str());
  strcpy(configData.ssid,request->arg("ssid").c_str());
  strcpy(configData.pass,request->arg("pass").c_str());
  strcpy(configData.captive_ssid, request->arg("captive_ssid").c_str());
  strcpy(configData.captive_pass, request->arg("captive_pass").c_str());

  if (useNVS)
  {
    nvs_handle handle;
    esp_err_t res = nvs_open("lwc_data", NVS_READWRITE, &handle);
    Serial.printf("nvs_open %i\n",res);
    res = nvs_set_blob(handle, "lwc_pump_cfg_v1", &configData, sizeof(configData));
    Serial.printf("nvs_set_blob %i\n",res);
    nvs_commit(handle);
    nvs_close(handle);
  }
  
  request->send(200, "text/html", 
    "<html><head></head><body onload=\"location.replace('/config');\"></body></html>\n");
}

void eepromSetup()
{
  if (!useNVS) return;
  
  nvs_handle handle;

  esp_err_t res = nvs_open("lwc_data", NVS_READWRITE, &handle);
  Serial.printf("nvs_open %i\n",res);
  size_t sz=sizeof(configData);
  res = nvs_get_blob(handle, "lwc_pump_cfg_v1", &configData, &sz);
  Serial.printf("nvs_get_blob %i; size %i\n",res,sz);
  nvs_close(handle);
  
  Serial.printf("IP=%s\npumpMax=%i\nchlMax=%i\nfactor=%0.1f\nfrequency=%i\nsweet lo %0.1f\nsweet hi %0.1f\ntolerance=%0.3f\nadj freq=%i\n"
               ,configData.currentMonitorIP
               ,configData.pumpMax
               ,configData.chlMax
               ,configData.conversionFactor
               ,configData.frequency
               ,configData.sweetSpotLow
               ,configData.sweetSpotHigh
               ,configData.tolerance
               ,configData.adjustFrequency
               );
  Serial.printf("ssid=%s\npass=%s\ncaptive_ssid=%s\ncaptive_pass=%s\n"
               ,configData.ssid
               ,configData.pass
               ,configData.captive_ssid
               ,configData.captive_pass
               );
}

void webServerSetup()
{
  server.on("/",handleRoot);
  server.on("/setPump",handleSetPump);
  server.on("/config",handleConfig);
  server.on("/setConfig",handleSetConfig);
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", 
          "<html><head></head><body onload=\"location.replace('/');\">Rebooting</body></html>\n");
      shouldReboot=true;
  });
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
    //request->sendHeader("Connection", "close");
    request->send(200, "text/html", otaUpdatePage);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    //request->sendHeader("Connection", "close");
    request->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    reboot();
  }, 
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
  {
    Serial.printf("index: %u len: %u\n",index, len);
    if (!index) // upload start
    {
      Serial.printf("Update: %s %u\n", filename.c_str(),len);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    }
     
    if (len) // write
    {
      /* flashing firmware to ESP*/
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    } 
    
    if (final) 
    {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", index);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
}

bool wifiStaConnected=false;

typedef struct 
{
  char ssid[25];
  char pass[25];
} saved_hotspot_t;

#include <hotspots.h>
                                
void *getLocalHotspot()
{
   Serial.println("scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) 
  {
      Serial.println("no networks found");
  } else 
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) 
    {
      String id = WiFi.SSID(i);
      Serial.printf("looking for %s\n",id.c_str());
      for (int j=0;j<sizeof(savedHotspots)/sizeof(savedHotspots[0]);j++)
      {
        Serial.printf("looking at %s\n",savedHotspots[j].ssid);
        if (strcmp(savedHotspots[j].ssid,id.c_str())==0)
        {
          Serial.println("found");
          return &savedHotspots[j];
        }
      }
    }
  }
  return (void *)&configData;
}

void wifiSTASetup()
{
  saved_hotspot_t *hotspot = (saved_hotspot_t*)getLocalHotspot();
  
  Serial.print("Connecting to ");
  Serial.println(hotspot->ssid);

  WiFi.begin(hotspot->ssid, hotspot->pass);
  Serial.println("");

  long wifiTimeOut=millis()+30000l;
  // Wait for connection
  while ((WiFi.status() != WL_CONNECTED) && (millis()<wifiTimeOut)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi STA setup timed out");
    reboot();
  }
  Serial.println("");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

char rgIPTxtAP[32];
char rgIPTxtSTN[32];

void displayIPs()
{

  IPAddress myIP = WiFi.softAPIP();
  sprintf(rgIPTxtAP,"%u.%u.%u.%u",myIP[0],myIP[1],myIP[2],myIP[3]);
  
  Serial.print("AP IP address: ");
  Serial.println(rgIPTxtAP);

  myIP = WiFi.localIP();
  sprintf(rgIPTxtSTN,"%u.%u.%u.%u",myIP[0],myIP[1],myIP[2],myIP[3]);
  Serial.print("Local IP address: ");
  Serial.println(rgIPTxtSTN);
}

IPAddress myAddress(192, 168, 4, 4);
IPAddress subNet(255, 255, 255, 0);

void wifiAPSetup()
{
  Serial.println("Configuring access point...");
  WiFi.mode(WIFI_AP_STA);
  
  wifiSTASetup();

  Serial.printf("Setting up soft AP for %s\n",configData.captive_ssid);

  WiFi.softAPConfig(myAddress, myAddress, subNet);
  
  WiFi.softAP(configData.captive_ssid,configData.captive_pass);

  displayIPs();
  
}

int pumpHigh=255;
int pumpLow=0;

void processWSData(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) 
  {
    data[len] = 0;
    if (strcmp((char*)data, "toggleAuto") == 0) 
    {
      automatic = !automatic;
      if (pumpSetting==0)
      {
        setPump(configData.autoStartSetting);
      }
    }
    else if (strcmp((char*)data, "toggleTest") == 0) 
    {
      testing = !testing;
      automatic = testing;
    }
    else
    {
      testChlValue=atoi((char*)data);
      if (testing)
      {
        autoTime=0;
      }
      else
      {
        setPump(testChlValue);
      }
    }
    handleAsyncStatusUpdate();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  void *stackCheck=stkCheck();
  void *stackError=stkCheck();
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      handleAsyncStatusUpdate();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      processWSData(arg, data, len);
      break;
    case WS_EVT_PONG:
      Serial.printf("WS Pong\n");
      break;
    case WS_EVT_ERROR:
      Serial.printf("WS Error\n");
      break;
  }
  stackError=stkCheck();
  if (stackCheck != stackError)
  {
    Serial.printf("Stack error at end of onEvent %ul %ul\n",(unsigned long)stackCheck,stackError);
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}


int myprintf(const char *format, va_list list)
{
  return Serial.printf(format,list);
}

long timeToConnect=0;

void telnetSetup()
{
  Serial.printf("Connecting to %s\n",configData.currentMonitorIP);
  if (telnetClient.connect(configData.currentMonitorIP,23))
  {
    Serial.printf("Connected to %s\n",configData.currentMonitorIP);
    while (telnetClient.available()) telnetClient.read();
    timeToConnect=0;
  }
  else
  {
    timeToConnect=millis()+60000;
    Serial.printf("failed to connect to %s\n",configData.currentMonitorIP);
  }
}

void setup() 
{
  Serial.begin(115200);
  esp_log_set_vprintf(&myprintf);
  pinMode(PUMP_PIN,INPUT);
  delay(100);
  pinMode(PUMP_PIN,OUTPUT);
  digitalWrite(PUMP_PIN,LOW);
//  dacWrite(PUMP_PIN,0);
  adcAttachPin(34);
  eepromSetup();
  wifiAPSetup();
  initWebSocket();
  webServerSetup();
  telnetSetup();
}

char telnetBuf[128];
char *telnetPut=telnetBuf;

/*
 * These messages come from the CurrentMonitor which receives them from
 * the CurrentRecorder vis LoRa. They represent pump settings to be
 * acted upon.
 */
void handleTelnetBuf()
{
  Serial.printf("From Telnet: %s\n",telnetBuf);

  setPump(atoi(&telnetBuf[1]));
  handleAsyncStatusUpdate();
}

void handleTelnetCharacter(char ch)
{
  if (ch=='\n')
  {
    handleTelnetBuf();
    telnetPut=telnetBuf;
  }
  else if (ch!='\r')
  {
    *telnetPut++=ch;
    *telnetPut=0;
  }
}

float integral=0.0;
float previous_error=0.0;
float dt=1.0;
float kp=0.250;
float ki=0.5;
float kd=0.15;

void adjustPumpPID()
{
  float sweet=configData.conversionFactor*(configData.sweetSpotLow+((configData.sweetSpotHigh-configData.sweetSpotLow)/2));
  float error = sweet-cl17Reading;
  integral = integral + error * dt;
  float derivative = (error - previous_error) / dt;
  float output = (kp * error) + (kd * derivative) + (ki * integral);
  previous_error=error;
  int pump=output/12.8;
  if (pump<0) pump=0;
  if (pump>255) pump=255;
  setPump(pump);
  handleAsyncStatusUpdate();
  Serial.printf("\n out=%f\n err=%f\n int=%f\n der=%f\n",output,error,integral,derivative);
}

void adjustPump()
{
  float sweet=configData.sweetSpotLow+((configData.sweetSpotHigh-configData.sweetSpotLow)/2);
  Serial.printf("Before adjustment: sweet=%0.2f ppm=%0.2f current=%i\n",sweet,ppm,pumpSetting);
  if (ppm < (sweet-configData.tolerance))
  {
    if (ppm<configData.sweetSpotLow)
    {
      setPump(pumpSetting+2);
    }
    else
    {
      setPump(pumpSetting+1);
    }
  }
  else if (ppm > (sweet+configData.tolerance))
  {
    if (ppm>configData.sweetSpotHigh)
    {
      setPump(pumpSetting-2);
    }
    else
    {
      setPump(pumpSetting-1);
    }
  }
  Serial.printf("After adjustment: sweet=%0.2f ppm=%0.2f current=%i\n",sweet,ppm,pumpSetting);
}

#define sampleSize 2048
int i=0;
int tot=0;
int rg[sampleSize];

long timeToSend=0;

void loop() 
{
  void *stackCheck=stkCheck();
  void *stackError=stkCheck();
  
  ws.cleanupClients();

  stackError=stkCheck();
  if (stackCheck != stackError)
  {
    Serial.printf("Stack error after ws.cleanupClients %ul %ul\n",(unsigned long)stackCheck,stackError);
  }
  
  if (telnetClient)
  {
    while (telnetClient.available())
    {
      handleTelnetCharacter(telnetClient.read());
    }
  }
  else if (timeToConnect && millis()>timeToConnect)
  {
    telnetSetup();
  }
  tot-=rg[i];
  rg[i]=analogRead(CL17_PIN);
  tot+=rg[i];
  i=(i+1)%sampleSize;
  if (i==0)
  {
    cl17Reading = (testing)?testChlValue:tot/sampleSize;
    //Serial.printf("Raw Chlorine=%i\n",cl17Reading);
    if (telnetClient && millis()>timeToSend)
    {
      telnetClient.printf("C%i\r\n",cl17Reading);
      timeToSend=millis()+(1000*configData.frequency);
    }
    handleAsyncStatusUpdate();
  }

  stackError=stkCheck();
  if (stackCheck != stackError)
  {
    Serial.printf("Stack error after handleAsyncStatusUpdate %ul %ul\n",(unsigned long)stackCheck,stackError);
  }

  if (automatic && autoTime<millis())
  {
    adjustPump();
    autoTime=millis()+(60000*configData.adjustFrequency);
  }
  
  if (pumpSetting)
  {
    dacWrite(PUMP_PIN,pumpSetting);
  }
  else
  {
    digitalWrite(PUMP_PIN,LOW);
  }
}

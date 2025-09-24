#include "PumpController.h"
#include "pages.h"

void* stkCheck()
{
  long x=millis();
  x=x;
  return (void*)&x;
}

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
         ,configData.tolerance
         ,configData.autoStartSetting
         ,configData.adjustFrequency
         ,configData.ssid
         ,configData.pass
         ,configData.captive_ssid
         ,configData.captive_pass
         );
  request->send(200, "text/html", httpMsg);
}
#ifdef USE_PULSE

void ARDUINO_ISR_ATTR onTimer() 
{
  if (fOn)
  {
    digitalWrite(PULSE_PIN, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    fOn=false;
    nextPulse=millis()+pulseInterval;
  }
  else if (nextPulse<millis())
  {
    fOn=true;
    digitalWrite(PULSE_PIN, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

/*
static bool IRAM_ATTR timer_group_isr_callback(void *args)
{
  if (fOn)// need to turn it off and wait for pulseInterval
  {
    timer_group_set_alarm_value_in_isr(TIMER_GROUP_1, TIMER_1, pulseInterval);
    digitalWrite(PULSE_PIN,LOW);
    fOn=false;
  }
  else // need to turn it on and wait for PULSE_WIDTH
  {
    timer_group_set_alarm_value_in_isr(TIMER_GROUP_1, TIMER_1, PULSE_WIDTH);
    digitalWrite(PULSE_PIN,HIGH);
    fOn=true;
  }
  return false; // return whether we need to yield at the end of ISR
}


void timerInit()
{
  timer_init(TIMER_GROUP_1, TIMER_1, &timerConfig);
  timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);
  timer_set_alarm_value(TIMER_GROUP_1, TIMER_1, TIMER_BASE_CLK);
  timer_enable_intr(TIMER_GROUP_1, TIMER_1);
  timer_isr_callback_add(TIMER_GROUP_1, TIMER_1, timer_group_isr_callback, &fOn, 0);
  Serial.printf("base=%lu scale=%lu pulse width=%lu\n",TIMER_BASE_CLK,TIMER_SCALE,PULSE_WIDTH);
}
*/
#endif


void setPump(int setting)
{
  if (setting>configData.pumpMax)
  {
    setting=configData.pumpMax;
  }
#ifdef USE_PULSE
  if (setting != 0)
  {
    // for now, it will be pulses per minute
    pulseInterval = (1000*60) / setting;
    Serial.printf("interval=%lu\n",pulseInterval);
    if (pumpSetting==0) // we need to start the timer
    {
        // Set timer frequency to 1Mhz
      timer = timerBegin(TIMER_SCALE);

      // Attach onTimer function to our timer.
      timerAttachInterrupt(timer, &onTimer);
      timerAlarm(timer, 30000, true, 1);
      digitalWrite(ONOFF_PIN,HIGH);// turn on pump
    }
    else
    {
      timerAlarm(timer, 30000, true, 1);
    }
  }
  else
  {
    if (timer) {
      // Stop and free timer
      timerEnd(timer);
      timer = NULL;
    }
    digitalWrite(ONOFF_PIN,LOW);//turn pump off
    digitalWrite(PULSE_PIN,LOW);// make sure pulse is off
  }
  
#else
#ifdef USE_MCP
  if (setting<0 || setting>4095) return;
  Serial.printf("setting pump to %i\n",setting);
  MCP.setValue(setting);
#else
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
#endif
#endif
  pumpSetting=setting;
  if (telnetClient)
  {
    telnetClient.printf("P%i\r\n",pumpSetting);  
  }
#ifdef USE_PULSE
  //pulse();
#endif
}

void handleSetPump(AsyncWebServerRequest *request)
{
  setPump(atoi(request->arg("pump").c_str()));
  request->send(200, "text/html", 
    "<html><head></head><body onload=\"location.replace('/');\"></body></html>\n");
}

void handleStartPump(AsyncWebServerRequest *request)
{
  automatic=true;
  setPump(configData.autoStartSetting);
  request->send(200, "text/html", 
    "<html><head></head><body onload=\"location.replace('/');\"></body></html>\n");
}

void handleStopPump(AsyncWebServerRequest *request)
{
  automatic=false;
  setPump(0);
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
  configData.adjustFrequency=atoi(request->arg("adjustfreq").c_str());
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
  server.on("/startPump",handleStartPump);
  server.on("/stopPump",handleStopPump);
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
  return (void *)&configData.ssid[0];
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
#ifdef USE_PULSE
  pinMode(PULSE_PIN,OUTPUT);
  digitalWrite(PULSE_PIN,LOW);
  pinMode(ONOFF_PIN,OUTPUT);
  digitalWrite(ONOFF_PIN,LOW);
  delay(1000);
  digitalWrite(ONOFF_PIN,HIGH);
  delay(1000);
  digitalWrite(ONOFF_PIN,LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

#else
#ifdef USE_MCP
  MCP.begin(21,22);
  setPump(0);
#else
  pinMode(PUMP_PIN,INPUT);
  delay(100);
  pinMode(PUMP_PIN,OUTPUT);
  digitalWrite(PUMP_PIN,LOW);
//  dacWrite(PUMP_PIN,0);
#endif
#endif
  analogReadResolution(12);
  eepromSetup();
  wifiAPSetup();
  
  MDNS.begin(configData.captive_ssid);
  MDNS.addService("http", "tcp", 80);

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
long lastPrint=0;

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

#ifdef USE_PULSE
#else
#ifndef USE_MCP
  if (pumpSetting)
  {
    dacWrite(PUMP_PIN,pumpSetting);
  }
  else
  {
    digitalWrite(PUMP_PIN,LOW);
  }
#endif
#endif
}

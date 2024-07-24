#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_log.h>
#include <nvs.h>
#include <ESPmDNS.h>

//define USE_MCP
#define USE_PULSE

#ifdef USE_PULSE
#define LED_BUILTIN 2
#define PULSE_PIN 23
#define ONOFF_PIN 21
#define TIMER_SCALE 1000000

hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

long lastPulse=0;
long pulseInterval=0;
long nextPulse=0;
bool fOn=false;
#else
#ifdef USE_MCP
#include "Wire.h"
#include "MCP4725.h"

MCP4725 MCP(0x62);  // 0x62 or 0x63
#else
#define PUMP_PIN 25
#endif
#endif

#define CL17_PIN 34

bool useNVS=true;
bool automatic=false;
bool testing=false;
int testChlValue=0;
long autoTime=0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiClient telnetClient;

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

config_data_t configData={"192.168.1.2",255,3250,1632.0,10,0.30,0.60,"","","PumpController","",13,8,0.02};

char rootIndex[10240];
char httpMsg[4096];

// message - raw,fill,text,style,pump
//            0    1    2    3     4
static const char statusFmt[] = "%i,%i,%s,%s,%i,%s,%s";
char statusBuffer[128];
int pumpSetting=0;
int cl17Reading=0;
float ppm=0.0;

bool wifiStaConnected=false;

typedef struct 
{
  char ssid[25];
  char pass[25];
} saved_hotspot_t;

#include <hotspots.h>

char rgIPTxtAP[32];
char rgIPTxtSTN[32];

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>

#include "IFTTTWebhook.h"

#define logToSerial Serial
#define eventProcessingDelay 3e3
#define socLoopDelay 1e2

#define logLength 10000

#define mDnsName "alarm"

uint8_t ledPin = 2;
uint8_t alarmPin = 14;
uint8_t guardPin = 12;

String logStorage = String();

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;


typedef enum GuardMode 
{
    GUARD_UNDEFINED = 0, GUARD_ON = 1, GUARD_OFF = 3
} GuardMode_t;

bool alarmEvent = false;
bool guardEvent = false;

bool alarmState = false;
GuardMode guardState = GUARD_UNDEFINED;

const int stationTimeoutSec = 20;
const int apnTimeoutSec = 120; 
unsigned long apnStartTime = 0;
unsigned long lastEventCheckTime = 0;
int eventDelayCountdown = 0;

class AcessPointCredentials {
    // Access specifier
    public:
 
    // Data Members
    char * name;
    char * ssid;

    AcessPointCredentials(char * apName,
            char * apSsid) {
      
      name = apName;
      ssid = apSsid;
    }
};

//AP
const char apnName[] = "Alarm";
const char apnPass[] = "alarmalarmalarm";

const IPAddress localIP(192,168,4,1);
const IPAddress gateway(192,168,4,1);
const IPAddress subnet(255,255,255,0);

// Station

// const char satationAp[]  = "puntodeacceso";
// const char satationPass[] = "hotspot131415";

const int stationCredsCount = 2;
AcessPointCredentials stationCreds[stationCredsCount] = { AcessPointCredentials("**", "**"),
                                                          AcessPointCredentials("**", "**") };


// IFTTT

char iftttEventName[] = "**";
char  iftttApiKey[] = "**";
//  "**"

IFTTTWebhook iftttWebHook(iftttApiKey, iftttEventName);

// ============================================

void cutLog() {

  if (logStorage.length() > logLength) {

    logStorage = logStorage.substring(100);
  }
}

template <typename T>
void LOG(T t) {
  
  logStorage += String(t);
  logStorage += String("\n");
  cutLog();

  #ifdef logToSerial
    logToSerial.println(t);
  #endif
}

template<typename T, typename... Args>
void LOG(T t, Args... args) {// recursive variadic function

    logStorage += String(t);
    cutLog();

    LOG(args...);
}

// ====================================

void applyEventDelay(int delay) {

  eventDelayCountdown = delay;
}

bool couldProcessNextEvent() {
  
  if (eventDelayCountdown <= 1) {

    eventDelayCountdown = 0;
    return true;
  }

  unsigned long currentTime = millis();
  unsigned long timeElapsed = currentTime - lastEventCheckTime;
  lastEventCheckTime = currentTime;

  // String uptimeMessage = String((F("event time elapsed: ")));
  // uptimeMessage += String(timeElapsed);
  // uptimeMessage += String((F("ms")));
  // LOG(uptimeMessage);

  eventDelayCountdown -= timeElapsed;
  
  return false;
}

// ===========================
// State

String statusString() {
  
  String result = String((F("<br/>in Access Point mode: ")));
  result += String(WiFi.getMode() == WIFI_AP ? (F("YES")):(F("NO")));
  result += String(F("<br/>apnStartTime: "));
  result += String(apnStartTime);

  result += String(F("<br/>Guard Pin Raised: "));
  result += String(digitalRead(guardPin) == 1 ? (F("YES")):(F("NO")));
  result += String(F("<br/>Alarm Pin Rased: "));
  result += String(digitalRead(alarmPin) == 1 ? (F("YES")):(F("NO")));
  result += String(F("<br/>Next Event Countdown: "));
  result += String(eventDelayCountdown);

  return result;
}

String stateString() {
  
  String result = String();

  result += F("<p><br>=====================");
  result += F("<br><br>device config\n");

  result += F("<br>Sketch size: ");
  result += String(ESP.getSketchSize());

  result += F("<br>Free size: ");
  result += String(ESP.getFreeSketchSpace());

  result += F("<br>Free Heap: ");
  result += String(ESP.getFreeHeap());

  result += F("<br>Alarm: ");
  result += String(alarmState? (F("YES")):(F("NO")));

  result += (F("<br>On Guard: "));

  switch (guardState) {
    case GUARD_UNDEFINED:

      result += F("UNDEFINED");
      break;

    case GUARD_OFF:

      result += F("OFF");
      break;

    case GUARD_ON:

      result += F("ON");
      break;
  }

  result += F("</p><br>==============</p>");
  result += statusString();
  result += F("</p><br>==============</p>");
  result += String(F("<br>Links:"));
  result += String(F("<br><li><a href=\"\\log\">log</a> - show device log"));
  result += String(F("<br><li><a href=\"\\update\">update</a> - update device "));
  
  result += F("</p><br>==============</p>");
  return result;
}

// =============================
// WiFi

String wifiApName() {

  byte mac[6];
   WiFi.macAddress(mac);

   Serial.print(F("MAC: "));
   Serial.print(mac[5],HEX);
   Serial.print(F(":"));
   Serial.print(mac[4],HEX);
   Serial.print(F(":"));
   Serial.print(mac[3],HEX);
   Serial.print(F(":"));
   Serial.print(mac[2],HEX);
   Serial.print(F(":"));
   Serial.print(mac[1],HEX);
   Serial.print(F(":"));
   Serial.println(mac[0],HEX);

   String result = String(apnName);
   result += String(" - ");
   result += String(mac[5], HEX);
   result += String(mac[4], HEX);

   return result;
}

void checkWifiConnection() {

  unsigned long apnUptime = 0;

  if (WiFi.getMode() == WIFI_AP) {

    apnUptime = millis() - apnStartTime;

    String uptimeMessage = String((F("Apn uptime: ")));
    uptimeMessage += String(apnUptime / 1000);
    uptimeMessage += String((F("s")));
    LOG(uptimeMessage);
  }

  if (WiFi.getMode() == WIFI_AP
     && WiFi.softAPgetStationNum() == 0 
     && apnUptime > (apnTimeoutSec * 1000)) {

    LOG((F("Disconnect AP during no clients")));
    WiFi.softAPdisconnect(true);
  }

  if (WiFi.getMode() == WIFI_AP) {

    LOG((F("WiFi AP - OK")));
    return;
  } 

  if (WiFi.status() == WL_CONNECTED) {

    LOG((F("WiFi Station - OK")));
    return;
  }

  LOG((F("Connecting station")));

  int ms = 0;
  int timeout = stationTimeoutSec * 1000;


  int stationCredsIndex = 0;

  while (stationCredsIndex < stationCredsCount) {

    WiFi.mode(WIFI_STA);
    WiFi.begin(stationCreds[stationCredsIndex].name,
               stationCreds[stationCredsIndex].ssid);
    
    String message = String(F("Connect to AP #"));
    message += String(stationCredsIndex);
    message += String(F(" : "));
    char * name = stationCreds[stationCredsIndex].name;
    message += String(name);
    LOG(message);

    while (WiFi.status() != WL_CONNECTED) {

      delay(500);
      ms += 500;
      LOG((F(".")));
      if (ms > timeout) {

        LOG((F("Aborted during timeout")));
        WiFi.disconnect();
        break;
      }
    }

    if  (WiFi.status() == WL_CONNECTED) {

      LOG((F("Station connected after ")));
      LOG(ms);
      LOG((F("ms, IP address: ")));
      LOG(WiFi.localIP().toString());
      return;
    }

    ms = 0;
    stationCredsIndex += 1;
  }

  LOG((F("Starting Access Point...")));
  WiFi.mode(WIFI_AP);

  WiFi.softAPConfig(localIP, gateway, subnet);
  if (WiFi.softAP(wifiApName().c_str(), apnPass) == false) {

    return;
  }

  apnStartTime = millis();

  LOG((F("Acceess Point is up, IP address: ")));
  LOG(WiFi.softAPIP().toString()); 
}

// ========================
// Events 

void handleOnGuardEvent() {

  guardState = GUARD_ON;

  iftttWebHook.trigger("Guard-On");

  LOG("\n on guard event initiated");
}

void handleOffGuardEvent() {

  guardState = GUARD_OFF;

  iftttWebHook.trigger("Guard-Off");

  LOG("\n off guard event initiated");
}

void handleGuardEvent() {

  guardEvent = false;

  if (digitalRead(guardPin) == 0) {

    handleOffGuardEvent();
  } else {
    
    handleOnGuardEvent();
  }
}

void toggleLED() {

  LOG(F("\ntoggle Led"));
  digitalWrite(ledPin,!digitalRead(ledPin));
  httpServer.send(204);
}

void handleAlarmEvent() {

  alarmEvent = false;
  alarmState = true;

  iftttWebHook.trigger("Alarm");

  LOG(F("\nALARM event initiated"));
}

// ========================

void initiateGuardEvent() {

  guardEvent = true;
}

void initiateAlarmEvent() {

  alarmEvent = true;
}

// =========================

void toggleOnGuardEvent() {

  handleOnGuardEvent();
  httpServer.send(204, "Ok");
}

void toggleOffGuardEvent() {

  handleOffGuardEvent();
  httpServer.send(204, "Ok");
}

void toggleAlarmEvent() {

  handleAlarmEvent();
  httpServer.send(204, "Ok");
}

void respondWithState() {

  String response = String((F("<html><body><p>")));
  response += stateString();
  response += F("</body></html>");

  httpServer.send(200, (F("text/html")), response.c_str() );
}

void respondWithLog() {

  String response = String(F("<html><body><p>"));
  response += statusString();
  response += String((F("</p><pre>")));
  response += logStorage;
  response += String((F("</pre></body></html>\n\r")));
  
  httpServer.send(200, (F("text/html")), response.c_str());
}

// =======================
// Setup

void setupServer() {

  MDNS.begin(mDnsName);

  httpUpdater.setup(&httpServer, "/update");

  // httpServer.on("/",[](){httpServer.send(200,"text/plain","Hello World!");});

  httpServer.on("/", respondWithState);
  httpServer.on("/log", respondWithLog);

  httpServer.on("/alarm", toggleAlarmEvent);
  httpServer.on("/guardon", toggleOnGuardEvent);
  httpServer.on("/guardoff", toggleOffGuardEvent);

  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
}

void setupPins() {

  pinMode(alarmPin, INPUT);
  pinMode(guardPin, INPUT);
  pinMode(ledPin, OUTPUT);
}

void setupInterrupts() {

  attachInterrupt(alarmPin,  initiateAlarmEvent, FALLING);
  attachInterrupt(guardPin,  initiateGuardEvent, CHANGE);
}

// ===================
// LifeCycle

void setup() {

  #ifdef logToSerial
    logToSerial.begin(74880);
  #endif

  setupPins();
  setupInterrupts();
  setupServer();
  
  WiFi.mode(WIFI_STA);
}

void loop() {

    if (couldProcessNextEvent()) {

      if (alarmEvent) {

        handleAlarmEvent();
        yield();
      }
       

    if (guardEvent) {

      handleGuardEvent();
      yield();
    }
  
    checkWifiConnection();
    applyEventDelay(eventProcessingDelay);
  }
  
  httpServer.handleClient();
  delay(socLoopDelay);
}



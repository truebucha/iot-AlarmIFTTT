#include <Arduino.h>
#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>

#include "IFTTTWebhook.h"

#define logToSerial Serial

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

//AP
const char apnName[] = "Alarm";
const char apnPass[] = "alarmalarmalarm";

const IPAddress localIP(192,168,4,1);
const IPAddress gateway(192,168,4,1);
const IPAddress subnet(255,255,255,0);

// Station
const char satationAp[]  = "puntodeacceso";
const char satationPass[] = "hotspot131415";


// IFTTT

 char iftttEventName[] = "Nalibokskaya1";
 char iftttApiKey[] = "lmUyJJg56fscFEvTWAUyg";
//  "cnQR7G8RpcLjzFkAKwyHtx"

IFTTTWebhook iftttWebHook(iftttApiKey, iftttEventName);

// ============================================

void cutLog() {

  if (logStorage.length() > 2000) {

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

String stateString() {
  
  String result = String();

  result += (F("\ndevice config\n"));

  result += ("\nSketch size: ");
  result += String(ESP.getSketchSize());

  result += (F("\nFree size: "));
  result += String(ESP.getFreeSketchSpace());

  result += (F("\nFree Heap: "));
  result += String(ESP.getFreeHeap());

  result += (F("\nAlarm: "));
  result += String(alarmState? (F("YES")):(F("NO")));

  result += (F("\nOn Guard: "));

  switch (guardState) {
    case GUARD_UNDEFINED:

      result += String((F("UNDEFINED")));
      break;

    case GUARD_OFF:

      result += String((F("OFF")));
      break;

    case GUARD_ON:

      result += String((F("ON")));
      break;
  }

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

  if (WiFi.getMode() == WIFI_AP
     || WiFi.status() == WL_CONNECTED) {

    LOG((F("WiFi Ok")));
    return;
  } 

  WiFi.mode(WIFI_STA);
  WiFi.begin(satationAp, satationPass);

  LOG((F("Connecting station")));

  int ms = 0;
  int timeout = stationTimeoutSec * 1000;

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

  iftttWebHook.trigger("Guard On");

  LOG("\n on guard event initiated");
}

void handleOffGuardEvent() {

  guardState = GUARD_OFF;

  iftttWebHook.trigger("Guard Off");

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
  httpServer.send(200, (F("text/plain")), logStorage.c_str());
}

void toggleOffGuardEvent() {

  handleOffGuardEvent();
  httpServer.send(200, (F("text/plain")), logStorage.c_str());
}

void toggleAlarmEvent() {

  handleAlarmEvent();
  httpServer.send(204, "Ok");
}

void respondWithState() {

  String response = stateString();

  httpServer.send(200, (F("text/plain")), response.c_str() );
}

void respondWithLog() {

  String response = String(F("<html><body><p>"));
  response += String(F("<br/>in Access Point mode: "));
  response += String(WiFi.getMode() == WIFI_AP ? (F("YES")):(F("NO")));
  response += String(F("<br/>apnStartTime: "));
  response += String(apnStartTime);

  response += String(F("<br/>Guard Pin Raised: "));
  response += String(digitalRead(guardPin) == 1 ? (F("YES")):(F("NO")));
  response += String(F("<br/>Alarm Pin Rased: "));
  response += String(digitalRead(alarmPin) == 1 ? (F("YES")):(F("NO")));
  response += String(F("<br/>Next Event Countdown: "));
  response += String(eventDelayCountdown);
  // response += String(F("<br/>Modem Network Connection Sequental Failures Count: "));
  // response += String(modemConnectionSequentalFailuresCount);
  response += String((F("</p><pre>")));
  response += logStorage;
  response += String((F("</pre></body></html>\n\r")));
  
  httpServer.send(200, (F("text/html")), response.c_str());
}

// =======================
// Setup

void setupServer() {

  MDNS.begin("esp");

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
}

void loop() {

  if (alarmEvent) {

    handleAlarmEvent();
  }
  yield();

  if (guardEvent) {

    handleGuardEvent();
  }
  yield();

  if (couldProcessNextEvent()) {

    checkWifiConnection();
    applyEventDelay(5000);
  }
  
  httpServer.handleClient();
  yield();
}



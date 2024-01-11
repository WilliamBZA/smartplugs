#include <WiFi.h>
// #include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPmDNS.h>
#include "time.h"
#include "Timer.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;

// WiFiManager wifiManager;
AsyncWebServer server(80);

String deviceName = "UnknownDevice";
int relay1Pin = 13;
bool relay1State = false;
String relay1Name = "Switch 1";
int relay1OnTime = 660;
int relay1OffTime = 960;
bool relay1TimeOverride = false;
bool relay1TimeState = false;

bool relay2Enabled = false;
int relay2Pin = 11;
bool relay2State = false;
String relay2Name = "Switch 2";
int relay2OnTime = 0;
int relay2OffTime = 0;

Timer timeSwitchChecker(5000);

void connectToWifi() {
  // wifiManager.autoConnect("smartplugs");

  WiFi.mode(WIFI_STA);
  WiFi.begin("", "");
  Serial.println("\nConnecting to WiFi Network ..");
 
  while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(100);
  }
 
  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void configureOTA() {
  // Make sure the flash size matches. For ESP8266 12F it should be 4MB.
  ArduinoOTA.setHostname(deviceName.c_str());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

String getSettings() {
  String settings = "{\"devicename\": \"";
  settings += deviceName;
  settings += "\", \"relay1Name\": \"";
  settings += relay1Name;
  settings += "\", \"relay1State\": \"";
  settings += relay1State;
  settings += "\", \"relay1Pin\": \"";
  settings += relay1Pin;
  settings += "\", \"relay1TimeOverride\": \"";
  settings += relay1TimeOverride;
  settings += "\", \"relay1TimeState\": \"";
  settings += relay1TimeState;
  
  settings += "\", \"hasSecondRelay\": \"";
  settings += relay2Enabled;

  settings += "\", \"relay2Name\": \"";
  settings += relay2Name;
  settings += "\", \"relay2State\": \"";
  settings += relay2State;
  settings += "\", \"relay2Pin\": \"";
  settings += relay2Pin;

  settings += "\", \"deviceTime\": \"";
  settings += getLocalTime(false);

  settings += "\"}";
  return settings;
}

String valueProcessor(const String& var) {
  Serial.print("Var: "); Serial.println(var);
  
  if (var == "DEVICE_NAME") {
    return deviceName;
  }

  if (var == "START_SETTINGS") {
    return getSettings();
  }

  return var;
}

void configureUrlRoutes() {
  server.serveStatic("/", SPIFFS, "").setTemplateProcessor(valueProcessor).setDefaultFile("index.html");

  server.on("/api/resetsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Resetting settings..."); Serial.println("");
    // wifiManager.resetSettings();

    request->send(200, "text/json", "OK");
    delay(500);
    ESP.restart();
  });

  server.on("/api/currentsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Sending settings..."); Serial.println("");
    request->send(200, "text/json", getSettings());
  });
  
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    Serial.printf("[REQUEST]\t%s\r\n", (const char*)data);
    Serial.print("URL: "); Serial.println(request->url());
    
    if (request->url() == "/api/savesettings") {
      saveSettings(request, data);
      request->send(200, "text/json", "OK");

      delay(500);
      ESP.restart();
    } else if (request->url() == "/api/togglerelay") {
      toggleRelay(request, data);
      request->send(200, "text/json", "OK");
    } else {
      request->send(404);
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
}

void toggleRelay(AsyncWebServerRequest *request, uint8_t *data) {
  Serial.println("Toggling relay...");

  StaticJsonDocument<256> doc;
  deserializeJson(doc, data);

  int relayNum = doc["relay"];
  int shouldBeOn = doc["state"];

  if (relayNum == 1) {
    Serial.print("Toggling PIN: "); Serial.print(relay1Pin); Serial.print(" to state: "); Serial.println(shouldBeOn);
    digitalWrite(relay1Pin, shouldBeOn);
    relay1State = shouldBeOn;
    
    relay1TimeOverride = (relay1State != relay1TimeState);
  } else if (relayNum == 2) {
    Serial.print("Toggling PIN: "); Serial.print(relay2Pin); Serial.print(" to state: "); Serial.println(shouldBeOn);
    digitalWrite(relay2Pin, shouldBeOn);
    relay2State = shouldBeOn;
  }
}

void saveSettings(AsyncWebServerRequest *request, uint8_t *data) {
  Serial.println("Saving settings...");

  StaticJsonDocument<256> doc;

  DeserializationError error = deserializeJson(doc, (const char *)data, request->contentLength());
  
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  const char* devicename = doc["devicename"]; // "Pool pump"
  int relay1pin = doc["relay1pin"]; // 13
  const char* relay1name = doc["relay1name"]; // "asdasd"
  bool relay2enabled = doc["relay2enabled"]; // true
  int relay2pin = doc["relay2pin"]; // 13
  const char* relay2name = doc["relay2name"]; // "asdasd"

  if (!isPinSafe(relay1pin)) {
    relay1pin = 13; // default 
  }

  if (!isPinSafe(relay2pin)) {
    relay2pin = 14;
  }

  deviceName = devicename;
  relay1Pin = relay1pin;
  relay1Name = relay1name;

  // restart if relay 2 was disabled but is now enabled
  bool mustRestart = relay2enabled != relay2Enabled && relay2enabled;

  relay2Enabled = relay2enabled;
  if (relay2Enabled) {
    relay2Pin = relay2pin;
    relay2Name = relay2name;
  }

  Serial.print("Device name: "); Serial.println(deviceName);
  Serial.print("Relay 1: "); Serial.print(relay1Name); Serial.print(" on pin: "); Serial.println(relay1Pin);

  Serial.print("Relay 2 enabled: "); Serial.println(relay2Enabled);
  if (relay2Enabled) {
    Serial.print("Relay 2: "); Serial.print(relay2Name); Serial.print(" on pin: "); Serial.println(relay2Pin);
  }
  
  File settingsFile = SPIFFS.open("/settings.json", "w");
  StaticJsonDocument<1024> settingsDoc;
  settingsDoc["devicename"] = deviceName;
  
  settingsDoc["relay1pin"] = relay1Pin;
  settingsDoc["relay1Name"] = relay1Name;

  settingsDoc["relay2Enabled"] = relay2Enabled;
  settingsDoc["relay2pin"] = relay2Pin;
  settingsDoc["relay2Name"] = relay2Name;

  if (serializeJson(settingsDoc, settingsFile) == 0) {
    Serial.println("Failed to write to file");
  }

  settingsFile.close();

  if (mustRestart) {
    ESP.restart();
  }
}

void loadDeviceSettings() {
  File settingsFile = SPIFFS.open("/settings.json", "r");
  if (!settingsFile) {
    Serial.println("No settings file found");
    deviceName = "UnknownDevice";
    return;
  }

  StaticJsonDocument<384> doc;

  DeserializationError error = deserializeJson(doc, settingsFile);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  const char* devicename = doc["devicename"]; // "12345678901234567890123456789012"
  int relay1pin = doc["relay1pin"]; // 13
  const char* relay1name = doc["relay1Name"]; // "12345678901234567890123456789012"
  bool relay2enabled = doc["relay2Enabled"]; // true
  int relay2pin = doc["relay2pin"]; // 18
  const char* relay2name = doc["relay2Name"]; // "12345678901234567890123456789012"

  if (!isPinSafe(relay1pin)) {
    relay1pin = 13; // default 
  }

  if (!isPinSafe(relay2pin)) {
    relay2pin = 14;
  }

  deviceName = devicename;
  relay1Pin = relay1pin;
  relay1Name = relay1name;

  relay2Enabled = relay2enabled;
  if (relay2Enabled) {
    relay2Pin = relay2pin;
    relay2Name = relay2name;
  }
  
  Serial.print("Device name: "); Serial.println(deviceName);
  Serial.print("Relay 1: "); Serial.print(relay1Name); Serial.print(" on pin: "); Serial.println(relay1Pin);

  Serial.print("Relay 2 enabled: "); Serial.println(relay2Enabled);
  if (relay2Enabled) {
    Serial.print("Relay 2: "); Serial.print(relay2Name); Serial.print(" on pin: "); Serial.println(relay2Pin);
  }
  
  settingsFile.close();
}

bool isPinSafe(int pinNo) {
  switch(pinNo) {
    case 4:
    case 13:
    case 14:
    case 16:
    case 17:
    case 18:
    case 19:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26:
    case 27:
    case 32:
    case 33:
      return true;
  }

  return false;
}

String getLocalTime(bool switchIfTrigger) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "00:00";
  }

  if (switchIfTrigger) {
    int currentTime = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    if (currentTime >= relay1OnTime && currentTime < relay1OffTime) {
      relay1TimeState = true;

      if (!relay1TimeOverride) {
        relay1State = true;
        relay1TimeOverride = (relay1State != relay1TimeState);
        Serial.println("Switching relay on");
        digitalWrite(relay1Pin, HIGH);
      } else {
        Serial.println("Not switching relay on as time switch is overridden");
      }
    } else {
      relay1TimeState = false;

      if (!relay1TimeOverride) {
        relay1State = false;
        relay1TimeOverride = (relay1State != relay1TimeState);
        Serial.println("Switching relay off");
        digitalWrite(relay1Pin, LOW);
      } else {
        Serial.println("Not switching relay off as time switch is overridden");
      }
    }
  }

  char timeHour[6];
  strftime(timeHour, 6, "%R", &timeinfo);
  Serial.print("Hour: ");
  Serial.println(timeHour);
  return timeHour;
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();

  loadDeviceSettings();
  connectToWifi();
  configureUrlRoutes();
  configureOTA();

  String dnsName = deviceName;
  dnsName.replace(" ", "");
  if(!MDNS.begin(dnsName)) {
     Serial.println("Error starting mDNS");
  }

  configTime(gmtOffset_sec, 0, ntpServer);

  Serial.print("relay1Pin name: "); Serial.println(relay1Pin);
  Serial.print("relay2Pin name: "); Serial.println(relay2Pin);

  pinMode(relay1Pin, OUTPUT);
  digitalWrite(relay1Pin, LOW);

  if (relay2Enabled) {
    pinMode(relay2Pin, OUTPUT);
    digitalWrite(relay2Pin, LOW);
  }
  
  server.begin();

  timeSwitchChecker.Start();
}

void loop() {
  ArduinoOTA.handle();

  if (timeSwitchChecker.Check()) {
    getLocalTime(true);
  }
}

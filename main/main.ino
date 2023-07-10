
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

WiFiManager wifiManager;
WiFiManagerParameter device_name_parameter("devicename", "Device Name", "", 40);

AsyncWebServer server(80);

String deviceName = "UnknownDevice";

void connectToWifi() {
  wifiManager.addParameter(&device_name_parameter);
  wifiManager.setSaveParamsCallback([]() {
    Serial.print("Device name: ");
    Serial.println(deviceName);
    deviceName = device_name_parameter.getValue();
  
    Serial.print("Device name: ");
    Serial.println(deviceName);

    File settingsFile = SPIFFS.open("/settings.json", "w");
    StaticJsonDocument<256> settingsDoc;
    settingsDoc["devicename"] = deviceName;

    if (serializeJson(settingsDoc, settingsFile) == 0) {
      Serial.println("Failed to write to file");
    }

    settingsFile.close();
  });

  wifiManager.autoConnect("smartplugs");
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

void configureUrlRoutes() {
  server.serveStatic("/", SPIFFS, "").setDefaultFile("index.html");

  server.on("/api/resetwifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Resetting settings..."); Serial.println("");
    wifiManager.resetSettings();

    request->send(200, "text/json", "OK");
    delay(500);
    ESP.restart();
  });

  server.on("/api/currentsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Sending settings..."); Serial.println("");
    request->send(200, "text/json", "{\"devicename\": \"" + deviceName + "\"}");
  });
  
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (request->url() == "/api/savesettings") {
      saveSettings(request, data);
      request->send(200, "text/json", "OK");

      delay(500);
      ESP.restart();
    } else {
      request->send(404);
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
}

void saveSettings(AsyncWebServerRequest *request, uint8_t *data) {
  Serial.println("Saving settings...");

  Serial.printf("[REQUEST]\t%s\r\n", (const char*)data);

  StaticJsonDocument<256> doc;
  deserializeJson(doc, data);
  Serial.println("Saving settings...");
  
  char name[64];
  strcpy(name, doc["devicename"]);
  deviceName = name;

  Serial.print("Device name: "); Serial.println(deviceName);
  
  File settingsFile = SPIFFS.open("/settings.json", "w");
  StaticJsonDocument<256> settingsDoc;
  settingsDoc["devicename"] = deviceName;

  if (serializeJson(settingsDoc, settingsFile) == 0) {
    Serial.println("Failed to write to file");
  }

  settingsFile.close();
}

void loadDeviceSettings() {
  File settingsFile = SPIFFS.open("/settings.json", "r");
  if (!settingsFile) {
    Serial.println("No settings file found");
    deviceName = "UnknownDevice";
    return;
  }

  StaticJsonDocument<512> settingsDoc;
  DeserializationError error = deserializeJson(settingsDoc, settingsFile);

  if (error) {
    Serial.print("Error parsing JSON: "); Serial.println(error.c_str());
    deviceName = "UnknownDevice";
    settingsFile.close();
    return;
  }

  char name[64];
  strcpy(name, settingsDoc["devicename"]);
  deviceName = name;
  Serial.print("Device Name: "); Serial.println(deviceName);
  
  settingsFile.close();
}

void setup() {
  Serial.begin(115200);
  SPIFFS.begin();

  loadDeviceSettings();
  connectToWifi();
  configureUrlRoutes();
  configureOTA();
  
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
}

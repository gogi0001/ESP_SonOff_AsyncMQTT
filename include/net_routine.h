#ifndef NET_ROUTINE_H_
#define NET_ROUTINE_H_
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ArduinoOTA.h>

const char * deviceId = NR_DEVICE_ID;
const char * mqttReportTopic = NR_MQTT_REPORT_TOPIC;
const char * mqttSetTopic = NR_MQTT_SET_TOPIC;
const char * mqttPingTopic = NR_MQTT_PING_TOPIC;
const char * deviceAlias = NR_DEVICE_ALIAS;

const char * ssid = NR_SSID;
const char * password = NR_PASSWORD;
const char * otaPassword = NR_OTA_PASSWORD;
const char * mqttUser = NR_MQTT_USER;
const char * mqttPassword = NR_MQTT_PASSWORD;
const char * mqttServer = NR_MQTT_SERVER_URL;
const uint16_t mqttPort = NR_MQTT_SERVER_PORT;

typedef void (*vMessageCB_t)(char* topic, char* payload, size_t len);
vMessageCB_t pvMessageCB = NULL;
vMessageCB_t pvPingCB = NULL;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void connectToWifi();
void connectToMqtt();
void setupOTAServer();

void connectToWifi() {
  Serial.printf("[ connectToWifi ] Connecting to Wi-Fi %s (%s) ...\n", ssid, password);
  WiFi.begin(ssid, password);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("[ onWifiConnect ] Connected to Wi-Fi. Interface settings:");
  Serial.print("\tIP:\t"); Serial.println(WiFi.localIP());
  Serial.print("\tMASK:\t"); Serial.println(WiFi.subnetMask());
  Serial.print("\tGW:\t"); Serial.println(WiFi.gatewayIP());
  Serial.print("\tDNS:\t"); Serial.println(WiFi.dnsIP());

  setupOTAServer();
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.printf("[ onWifiDisconnect ] Disconnected from Wi-Fi (Reason: %i)\n", (int)event.reason);
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("[ connectToMqtt ] Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
    Serial.printf("[ onMqttConnect ] Connected to MQTT broker: %s:%d\n", mqttServer, mqttPort);
    mqttClient.subscribe(mqttSetTopic, 1);
    Serial.printf("[ onMqttConnect ] Subscribed to: %s\n", mqttSetTopic);
    mqttClient.subscribe(mqttPingTopic, 1);
    Serial.printf("[ onMqttConnect ] Subscribed to: %s\n", mqttPingTopic);
    char cPayload[256];
    sprintf(cPayload, "{\"connected\":true, \"device_id\":\"" NR_DEVICE_ID "\", \"device_alias\":\"" NR_DEVICE_ALIAS "\", \"ip_address\":\"%s\"}", WiFi.localIP().toString().c_str());
    mqttClient.publish(mqttReportTopic, 0, false, cPayload);
    Serial.printf("[ onMqttConnect ] Welcome to %s\n", mqttReportTopic);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("[ onMqttDisconnect ] Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

// void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
// //   Serial.println("Subscribe acknowledged.");
// //   Serial.print("  packetId: ");
// //   Serial.println(packetId);
// //   Serial.print("  qos: ");
// //   Serial.println(qos);
// }

// void onMqttUnsubscribe(uint16_t packetId) {
// //   Serial.println("Unsubscribe acknowledged.");
// //   Serial.print("  packetId: ");
// //   Serial.println(packetId);
// }

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
//   Serial.println("Publish received.");
//   Serial.print("  topic: ");
//   Serial.println(topic);
//   Serial.print("  qos: ");
//   Serial.println(properties.qos);
//   Serial.print("  dup: ");
//   Serial.println(properties.dup);
//   Serial.print("  retain: ");
//   Serial.println(properties.retain);
//   Serial.print("  len: ");
//   Serial.println(len);
//   Serial.print("  index: ");
//   Serial.println(index);
//   Serial.print("  total: ");
//   Serial.println(total);
    if (len > 0) payload[len] = '\0';
    // Serial.printf("[ MQTT ] Message received. %s => %s\n", topic, payload);


    if (strcmp(topic, NR_MQTT_PING_TOPIC) == 0) {
        // Serial.printf("[ MQTT ] Ping received. Payload:%s\n", payload);
        if (pvPingCB) pvPingCB(topic, payload, len);
    } else if (strcmp(topic, NR_MQTT_SET_TOPIC) == 0) {
        // Serial.println("[ MQTT ] Command received.");
        if (pvMessageCB) pvMessageCB(topic, payload, len);
    }
}

// void onMqttPublish(uint16_t packetId) {
//   Serial.println("Publish acknowledged.");
//   Serial.print("  packetId: ");
//   Serial.println(packetId);
// }

void vPublishReport(String &sReport) {
    if (mqttClient.connected()) {
        Serial.printf("[ vPublishReport ] Publishing report...\n"); 
        mqttClient.publish(mqttReportTopic, 1, false, sReport.c_str());
    }
}

void netSetup() {
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    // mqttClient.onSubscribe(onMqttSubscribe);
    // mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onMessage(onMqttMessage);
    // mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCredentials(mqttUser, mqttPassword);
    Serial.printf("[ netSetup ] Starting connectToWifi();\n");
    connectToWifi();
}

void setupOTAServer() {
  ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    Serial.println("[ setupOTAServer ] Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[ setupOTAServer ] Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[ setupOTAServer ] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("[ setupOTAServer ] Ready");
  Serial.print("[ setupOTAServer ] IP address: ");
  Serial.println(WiFi.localIP());
}

void handleNetRoutine() {
  if (WiFi.isConnected()) ArduinoOTA.handle();
}

#endif  // NET_ROUTINE_H_

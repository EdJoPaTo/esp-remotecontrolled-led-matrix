#include <ArduinoOTA.h>
#include <credentials.h>
#include <EspMQTTClient.h>
#include <MqttKalmanPublish.h>
#include <vector>

#ifdef I2SMATRIX
#include "matrix-i2s.h"
#elif NEOMATRIX
#include "matrix-neomatrix.h"
#else
#include "matrix-testing.h"
#endif

#define CLIENT_NAME "espPixelmatrix"
const uint16_t LISTEN_PORT = 1337;

EspMQTTClient client(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_SERVER, // MQTT Broker server ip
  CLIENT_NAME, // Client name that uniquely identify your device
  1883 // The MQTT port, default to 1883. this line can be omitted
);

const bool MQTT_RETAINED = true;

// #define PRINT_TO_SERIAL

#define BASIC_TOPIC CLIENT_NAME "/"
#define BASIC_TOPIC_SET BASIC_TOPIC "set/"
#define BASIC_TOPIC_STATUS BASIC_TOPIC "status/"

#ifdef ESP8266
  #define LED_BUILTIN_ON LOW
  #define LED_BUILTIN_OFF HIGH
#else // for ESP32
  #define LED_BUILTIN_ON HIGH
  #define LED_BUILTIN_OFF LOW
#endif

WiFiServer server(LISTEN_PORT);
std::vector<WiFiClient> clients;

MQTTKalmanPublish mkCommandsPerSecond(client, BASIC_TOPIC_STATUS "commands-per-second", false, 12 * 1 /* every 1 min */, 10);
MQTTKalmanPublish mkKilobytesPerSecond(client, BASIC_TOPIC_STATUS "kilobytes-per-second", false, 12 * 1 /* every 1 min */, 10);
MQTTKalmanPublish mkRssi(client, BASIC_TOPIC_STATUS "rssi", MQTT_RETAINED, 12 * 5 /* every 5 min */, 10);

boolean on = true;
uint8_t mqttBri = 2;

uint32_t commands = 0;
uint32_t bytes = 0;
size_t lastPublishedClientAmount = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println();

  matrix_setup(mqttBri << BRIGHTNESS_SCALE);

  ArduinoOTA.setHostname(CLIENT_NAME);

#ifdef PRINT_TO_SERIAL
  client.enableDebuggingMessages();
#endif
  client.enableHTTPWebUpdater();
  client.enableLastWillMessage(BASIC_TOPIC "connected", "0", MQTT_RETAINED);

  // well, hope we are OK, let's draw some colors first :)
  Serial.println("Fill screen: RED");
  matrix_fill(255, 0, 0);
  matrix_update();
  delay(200);

  Serial.println("Fill screen: GREEN");
  matrix_fill(0, 255, 0);
  matrix_update();
  delay(200);

  Serial.println("Fill screen: BLUE");
  matrix_fill(0, 0, 255);
  matrix_update();
  delay(200);

  Serial.println("Fill screen: BLACK");
  matrix_fill(0, 0, 0);
  matrix_update();
  delay(200);

  Serial.println("Setup done...");
}

void onConnectionEstablished() {
  client.subscribe(BASIC_TOPIC_SET "bri", [](const String &payload) {
    int value = strtol(payload.c_str(), 0, 10);
    mqttBri = max(1, min(255 >> BRIGHTNESS_SCALE, value));
    matrix_brightness((mqttBri << BRIGHTNESS_SCALE) * on);
    client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  });

  client.subscribe(BASIC_TOPIC_SET "on", [](const String &payload) {
    boolean value = payload != "0";
    on = value;
    matrix_brightness((mqttBri << BRIGHTNESS_SCALE) * on);
    client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
  });

  client.publish(BASIC_TOPIC "connected", "2", MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);

  ArduinoOTA.begin();

  server.begin();
  server.setNoDelay(true);
  Serial.print("Now listening to tcp://");
  Serial.print(CLIENT_NAME);
  Serial.print(":");
  Serial.println(LISTEN_PORT);
}

void pixelclientUpdateClients() {
  for (auto i = clients.size(); i > 0; i--) {
    auto client = clients[i - 1];

    if (!client.connected()) {
      clients.erase(clients.begin() + i - 1);

#ifdef PRINT_TO_SERIAL
      Serial.print("Client left. Remaining: ");
      Serial.println(clients.size());
#endif
    }
  }

  while (server.hasClient()) {
    auto client = server.available();
    client.setNoDelay(true);
    client.write(TOTAL_WIDTH);
    client.write(TOTAL_HEIGHT);
    client.flush();

#ifdef PRINT_TO_SERIAL
    Serial.print("Client new: ");
    Serial.print(client.remoteIP().toString());
    Serial.print(":");
    Serial.println(client.remotePort());
#endif

    clients.push_back(client);
  }
}

unsigned long nextCommandsUpdate = 0;
unsigned long nextMeasure = 0;

void loop() {
  client.loop();
  digitalWrite(LED_BUILTIN, client.isConnected() ? LED_BUILTIN_OFF : LED_BUILTIN_OFF);
  ArduinoOTA.handle();
  pixelclientUpdateClients();

  auto now = millis();

  if (client.isConnected()) {
    if (lastPublishedClientAmount != clients.size()) {
      lastPublishedClientAmount = clients.size();
      client.publish(BASIC_TOPIC_STATUS "clients", String(lastPublishedClientAmount), MQTT_RETAINED);
    }

    if (now >= nextCommandsUpdate) {
      nextCommandsUpdate = now + 5000;
      auto commands_per_second = commands / 5.0f;
      commands = 0;
      float avgCps = mkCommandsPerSecond.addMeasurement(commands_per_second);
#ifdef PRINT_TO_SERIAL
      Serial.print("Commands per Second:  ");
      Serial.print(String(commands_per_second).c_str());
      Serial.print("   Average: ");
      Serial.println(String(avgCps).c_str());
#endif

      auto kB_per_second = bytes / (1024.0f * 5.0f);
      bytes = 0;
      float avgKbps = mkKilobytesPerSecond.addMeasurement(kB_per_second);
#ifdef PRINT_TO_SERIAL
      Serial.print("Kilobytes per Second:  ");
      Serial.print(String(kB_per_second).c_str());
      Serial.print("   Average: ");
      Serial.println(String(avgKbps).c_str());
#endif
    }

    if (now >= nextMeasure) {
      nextMeasure = now + 5000;
      long rssi = WiFi.RSSI();
      float avgRssi = mkRssi.addMeasurement(rssi);
#ifdef PRINT_TO_SERIAL
      Serial.print("RSSI        in dBm:     ");
      Serial.print(String(rssi).c_str());
      Serial.print("   Average: ");
      Serial.println(String(avgRssi).c_str());
#endif
    }
  }

  // 50 ms -> 20 FPS
  // 25 ms -> 40 FPS
  // 20 ms -> 50 FPS
  // 16 ms -> 62.2 FPS
  auto until = millis() + 25;
  while (millis() < until) {
    for (auto client : clients) {
      if (client.available()) {
        uint8_t kind = client.read();
        if (kind == 1) { // Fill
          uint8_t red = client.read();
          uint8_t green = client.read();
          uint8_t blue = client.read();
          commands += 1;
          bytes += 4;
          matrix_fill(red, green, blue);
        } else if (kind == 2) { // Pixel
          uint8_t x = client.read();
          uint8_t y = client.read();
          uint8_t red = client.read();
          uint8_t green = client.read();
          uint8_t blue = client.read();
          commands += 1;
          bytes += 6;
          matrix_pixel(x, y, red, green, blue);
        }
      }
    }
  }

  matrix_update();
}

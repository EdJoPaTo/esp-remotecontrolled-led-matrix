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
const bool MQTT_RETAINED = false;

EspMQTTClient client(
    WIFI_SSID,
    WIFI_PASSWORD,
    MQTT_SERVER,
    MQTT_USERNAME,
    MQTT_PASSWORD,
    CLIENT_NAME, // Client name that uniquely identify your device
    1883         // The MQTT port, default to 1883. this line can be omitted
);

// #define PRINT_TO_SERIAL

#define BASE_TOPIC CLIENT_NAME "/"
#define BASE_TOPIC_SET BASE_TOPIC "set/"
#define BASE_TOPIC_STATUS BASE_TOPIC "status/"

#ifdef ESP8266
  #define LED_BUILTIN_ON LOW
  #define LED_BUILTIN_OFF HIGH
#else // for ESP32
  #define LED_BUILTIN_ON HIGH
  #define LED_BUILTIN_OFF LOW
#endif

WiFiServer server(LISTEN_PORT);
std::vector<WiFiClient> clients;

MQTTKalmanPublish mkCommandsPerSecond(client, BASE_TOPIC_STATUS "commands-per-second", false, 30 /* every 30 sec */, 10);
MQTTKalmanPublish mkErrorsPerSecond(client, BASE_TOPIC_STATUS "errors-per-second", false, 30 /* every 30 sec */, 2);
MQTTKalmanPublish mkKilobytesPerSecond(client, BASE_TOPIC_STATUS "kilobytes-per-second", false, 30 /* every 30 sec */, 2);
MQTTKalmanPublish mkRssi(client, BASE_TOPIC_STATUS "rssi", MQTT_RETAINED, 12 * 5 /* every 5 min */, 10);

boolean on = true;
uint8_t mqttBri = 2;

uint32_t commands = 0;
uint32_t bytes = 0;
uint32_t errors = 0;
size_t lastPublishedClientAmount = 0;

void testMatrix()
{
  Serial.println("Fill screen: RED");
  matrix_fill(255, 0, 0);
  matrix_update();
  delay(250);

  Serial.println("Fill screen: GREEN");
  matrix_fill(0, 255, 0);
  matrix_update();
  delay(250);

  Serial.println("Fill screen: BLUE");
  matrix_fill(0, 0, 255);
  matrix_update();
  delay(250);

  Serial.println("Pixel: top right and bottom left, top left shows an arrow pointing up");
  matrix_fill(0, 0, 0);
  matrix_pixel(TOTAL_WIDTH - 2, 1, 255, 255, 255);
  matrix_pixel(1, TOTAL_HEIGHT - 2, 255, 255, 255);

  matrix_pixel(3, 1, 255, 255, 255);
  matrix_pixel(3, 2, 255, 255, 255);
  matrix_pixel(3, 3, 255, 255, 255);
  matrix_pixel(3, 4, 255, 255, 255);
  matrix_pixel(3, 5, 255, 255, 255);
  matrix_pixel(2, 2, 255, 255, 255);
  matrix_pixel(4, 2, 255, 255, 255);
  matrix_pixel(1, 3, 255, 255, 255);
  matrix_pixel(5, 3, 255, 255, 255);

  matrix_update();
  delay(1000);
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println();

  matrix_setup(mqttBri << BRIGHTNESS_SCALE);

#ifdef PRINT_TO_SERIAL
  client.enableDebuggingMessages();
#endif
  client.enableHTTPWebUpdater();
  client.enableOTA();
  client.enableLastWillMessage(BASE_TOPIC "connected", "0", MQTT_RETAINED);

  // well, hope we are OK, let's draw some colors first :)
  testMatrix();

  matrix_fill(0, 0, 0);
  matrix_update();

  Serial.println("Setup done...");
}

void onConnectionEstablished()
{
  client.subscribe(BASE_TOPIC_SET "bri", [](const String &payload) {
    int value = strtol(payload.c_str(), 0, 10);
    mqttBri = max(1, min(255 >> BRIGHTNESS_SCALE, value));
    matrix_brightness((mqttBri << BRIGHTNESS_SCALE) * on);
    client.publish(BASE_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  });

  client.subscribe(BASE_TOPIC_SET "on", [](const String &payload) {
    boolean value = payload != "0";
    on = value;
    matrix_brightness((mqttBri << BRIGHTNESS_SCALE) * on);
    client.publish(BASE_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
  });

  server.begin();
  server.setNoDelay(true);
  Serial.printf("Now listening to tcp://" CLIENT_NAME ":%d\n", LISTEN_PORT);

  client.publish(BASE_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  client.publish(BASE_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
  client.publish(BASE_TOPIC "git-version", GIT_VERSION, MQTT_RETAINED);
  client.publish(BASE_TOPIC "connected", "2", MQTT_RETAINED);
}

void pixelclientUpdateClients()
{
  for (auto i = clients.size(); i > 0; i--)
  {
    auto client = clients[i - 1];

    if (!client.connected())
    {
      clients.erase(clients.begin() + i - 1);

#ifdef PRINT_TO_SERIAL
      Serial.printf("Client left. Remaining: %d\n", clients.size());
#endif
    }
  }

  while (server.hasClient())
  {
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

void loop()
{
  client.loop();
  digitalWrite(LED_BUILTIN, client.isConnected() ? LED_BUILTIN_OFF : LED_BUILTIN_ON);
  pixelclientUpdateClients();

  auto now = millis();

  if (client.isConnected())
  {
    if (lastPublishedClientAmount != clients.size())
    {
      lastPublishedClientAmount = clients.size();
      client.publish(BASE_TOPIC_STATUS "clients", String(lastPublishedClientAmount), MQTT_RETAINED);
    }

    if (now >= nextCommandsUpdate)
    {
      nextCommandsUpdate = now + 1000;
      float avgCps = mkCommandsPerSecond.addMeasurement(commands);
#ifdef PRINT_TO_SERIAL
      Serial.printf("Commands  per Second: %8d    Average: %10.2f\n", commands, avgCps);
#endif

      float avgErrors = mkErrorsPerSecond.addMeasurement(errors);
#ifdef PRINT_TO_SERIAL
      Serial.printf("Errors    per Second: %8d    Average: %10.2f\n", errors, avgErrors);
#endif

      auto kB = bytes / 1024.0f;
      float avgKbps = mkKilobytesPerSecond.addMeasurement(kB);
#ifdef PRINT_TO_SERIAL
      Serial.printf("Kilobytes per Second: %10.1f  Average: %10.2f\n", kB, avgKbps);
#endif

      bytes = 0;
      commands = 0;
      errors = 0;
    }

    if (now >= nextMeasure)
    {
      nextMeasure = now + 5000;
      long rssi = WiFi.RSSI();
      float avgRssi = mkRssi.addMeasurement(rssi);
#ifdef PRINT_TO_SERIAL
      Serial.printf("RSSI          in dBm: %8ld    Average: %10.2f\n", rssi, avgRssi);
#endif
    }
  }

  // 50 ms -> 20 FPS
  // 25 ms -> 40 FPS
  // 20 ms -> 50 FPS
  // 16 ms -> 62.2 FPS
  auto until = millis() + 25;
  while (millis() < until)
  {
    for (auto client : clients)
    {
      if (client.available())
      {
        uint8_t kind = client.read();
        if (kind == 1) // Fill
        {
          uint8_t red = client.read();
          uint8_t green = client.read();
          uint8_t blue = client.read();
          commands += 1;
          bytes += 4;
          matrix_fill(red, green, blue);
        }
        else if (kind == 2) // Pixel
        {
          uint8_t x = client.read();
          uint8_t y = client.read();
          uint8_t red = client.read();
          uint8_t green = client.read();
          uint8_t blue = client.read();
          commands += 1;
          bytes += 6;
          matrix_pixel(x, y, red, green, blue);
        }
        else if (kind == 3) // Rectangle
        {
          uint8_t x_start = client.read();
          uint8_t y_start = client.read();
          uint8_t width = client.read();
          uint8_t height = client.read();
          uint8_t red = client.read();
          uint8_t green = client.read();
          uint8_t blue = client.read();
          commands += 1;
          bytes += 8;
          for (auto x = x_start; x < x_start + width; x++)
          {
            for (auto y = y_start; y < y_start + height; y++)
            {
              matrix_pixel(x, y, red, green, blue);
            }
          }
        }
        else
        {
          bytes += 1;
          errors += 1;
        }
      }
    }
  }

  matrix_update();
}

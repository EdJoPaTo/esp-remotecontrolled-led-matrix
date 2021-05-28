#include <ArduinoOTA.h>
#include <credentials.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <EspMQTTClient.h>
#include <LinkedList.h>
#include <MqttKalmanPublish.h>

#define CLIENT_NAME "espPixelflut"

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


// Configure for your panel(s) as appropriate!
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64
#define PANELS_NUMBER 1
#define PIN_E 18

#define PANE_WIDTH PANEL_WIDTH * PANELS_NUMBER
#define PANE_HEIGHT PANEL_HEIGHT

// placeholder for the matrix object
MatrixPanel_I2S_DMA *dma_display = nullptr;


WiFiServer pixelflutServer(1337);
LinkedList<WiFiClient> pixelflutClients;

MQTTKalmanPublish mkCommandsPerSecond(client, BASIC_TOPIC_STATUS "commands-per-second", false, 12 * 1 /* every 1 min */, 10);
MQTTKalmanPublish mkRssi(client, BASIC_TOPIC_STATUS "rssi", MQTT_RETAINED, 12 * 5 /* every 5 min */, 10);

boolean on = true;
uint8_t mqttBri = 100;

uint32_t commands = 0;
int lastPublishedClientAmount = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  HUB75_I2S_CFG mxconfig;
  mxconfig.mx_height = PANEL_HEIGHT;
  mxconfig.chain_length = PANELS_NUMBER;
  mxconfig.gpio.e = PIN_E;
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->setBrightness8(mqttBri); // range is 0-255, 0 - 0%, 255 - 100%

  // Allocate memory and start DMA display
  if( not dma_display->begin() ) {
    Serial.println("****** !KABOOM! I2S memory allocation failed ***********");
  }

  ArduinoOTA.setHostname(CLIENT_NAME);

  #ifdef PRINT_TO_SERIAL
  client.enableDebuggingMessages();
  #endif
  client.enableHTTPWebUpdater();
  client.enableLastWillMessage(BASIC_TOPIC "connected", "0", MQTT_RETAINED);

  // well, hope we are OK, let's draw some colors first :)
  Serial.println("Fill screen: RED");
  dma_display->fillScreenRGB888(255, 0, 0);
  delay(1000);

  Serial.println("Fill screen: GREEN");
  dma_display->fillScreenRGB888(0, 255, 0);
  delay(1000);

  Serial.println("Fill screen: BLUE");
  dma_display->fillScreenRGB888(0, 0, 255);
  delay(1000);

  Serial.println("Fill screen: Neutral White");
  dma_display->fillScreenRGB888(64, 64, 64);
  delay(1000);

  Serial.println("Fill screen: black");
  dma_display->fillScreenRGB888(0, 0, 0);
  delay(1000);

  Serial.println("Setup done...");
}

void onConnectionEstablished() {
  client.subscribe(BASIC_TOPIC_SET "bri", [](const String & payload) {
    int value = strtol(payload.c_str(), 0, 10);
    mqttBri = max(1, min(255, value));
    dma_display->setBrightness8(mqttBri * on);
    client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  });

  client.subscribe(BASIC_TOPIC_SET "on", [](const String & payload) {
    boolean value = payload != "0";
    on = value;
    dma_display->setBrightness8(mqttBri * on);
    client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
  });

  client.publish(BASIC_TOPIC "connected", "2", MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
  client.publish(BASIC_TOPIC_STATUS "on", String(on), MQTT_RETAINED);

  ArduinoOTA.begin();

  pixelflutServer.begin();
  pixelflutServer.setNoDelay(true);
}

void handlePixelflutInput(WiFiClient &client, String str) {
  #ifdef PRINT_TO_SERIAL
  Serial.print("Pixelflut Command: ");
  Serial.println(str);
  #endif

  commands += 1;

  str.toLowerCase();

  if (str == "help") {
    client.println("there is no help yet");
  } else if (str == "size") {
    char buffer[20];
    if (snprintf(buffer, sizeof(buffer), "SIZE %d %d", PANE_WIDTH, PANE_HEIGHT) >= 0) {
      client.println(buffer);
    }
  } else if (str.startsWith("px")) {
    auto s = str.c_str();
    s += 3;

    char *ptr;

    int16_t x = strtol(s, &ptr, 10);
    int16_t y = strtol(ptr, &ptr, 10);

    auto length = strnlen(ptr, 20);

    if (length == 0) {
      char buffer[20];
      if (snprintf(buffer, sizeof(buffer), "PX %d %d unknown", x, y) >= 0) {
        client.println(buffer);
      }
    } else if (length == 6 + 1) {
      // normal hex color
      char buffer[20];

      strncpy(buffer, ptr + 1, 2);
      auto red = strtoul(buffer, 0, 16);
      strncpy(buffer, ptr + 3, 2);
      auto green = strtoul(buffer, 0, 16);
      strncpy(buffer, ptr + 5, 2);
      auto blue = strtoul(buffer, 0, 16);

      dma_display->drawPixelRGB888(x, y, red, green, blue);
    } else {
      client.println("colorcode not implemented. Use 6 digit hex color.");
    }
  } else {
    client.println("unknown command. try help");
  }
}

void pixelflutLoop() {
  for (auto i = pixelflutClients.size(); i > 0; i--) {
    auto client = pixelflutClients.get(i - 1);

    if (!client.connected()) {
      pixelflutClients.remove(i - 1);

      #ifdef PRINT_TO_SERIAL
      Serial.print("Client left. Remaining: ");
      Serial.println(pixelflutClients.size());
      #endif
    }
  }

  while (pixelflutServer.hasClient()) {
    auto client = pixelflutServer.available();
    client.setNoDelay(true);
    client.flush();

    #ifdef PRINT_TO_SERIAL
    Serial.print("Client new: ");
    Serial.print(client.remoteIP().toString());
    Serial.print(":");
    Serial.println(client.remotePort());
    #endif

    pixelflutClients.add(client);
  }

  for (auto i = 0; i < pixelflutClients.size(); i++) {
    auto client = pixelflutClients.get(i);

    String input;
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        handlePixelflutInput(client, input);
        input = "";
      } else if (c >= 32) {
        input += c;
      }
    }
  }
}

unsigned long nextCommandsUpdate = 0;
unsigned long nextMeasure = 0;

void loop() {
  client.loop();
  ArduinoOTA.handle();
  pixelflutLoop();
  digitalWrite(LED_BUILTIN, client.isConnected() ? HIGH : LOW);

  auto now = millis();

  if (client.isConnected()) {
    if (lastPublishedClientAmount != pixelflutClients.size()) {
      lastPublishedClientAmount = pixelflutClients.size();
      client.publish(BASIC_TOPIC_STATUS "clients", String(lastPublishedClientAmount), MQTT_RETAINED);
    }

    if (now > nextCommandsUpdate) {
      nextCommandsUpdate = now + 5000;
      auto commands_per_second = commands / 5.0;
      commands = 0;
      float avgCps = mkCommandsPerSecond.addMeasurement(commands_per_second);
      #ifdef PRINT_TO_SERIAL
      Serial.print("Commands per Second:  ");
      Serial.print(String(commands_per_second).c_str());
      Serial.print("   Average: ");
      Serial.println(String(avgCps).c_str());
      #endif
    }

    if (now > nextMeasure) {
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
}

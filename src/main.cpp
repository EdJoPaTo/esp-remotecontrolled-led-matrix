#include <credentials.h>
#include <EspMQTTClient.h>
#include <MqttKalmanPublish.h>
#include <vector>

#include "matrix-neomatrix.h"

#define CLIENT_NAME "espPixelmatrix"
const uint16_t LISTEN_PORT = 1337;
const bool MQTT_RETAINED = false;

EspMQTTClient mqttClient(
	WIFI_SSID,
	WIFI_PASSWORD,
	MQTT_SERVER,
	MQTT_USERNAME,
	MQTT_PASSWORD,
	CLIENT_NAME,
	1883);

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

const uint8_t PROTOCOL_VERSION = 1; // breaking changes increase this number

WiFiServer pixelServer(LISTEN_PORT);
std::vector<WiFiClient> pixelClients;

MQTTKalmanPublish mkCommandsPerSecond(mqttClient, BASE_TOPIC_STATUS "commands-per-second", false, 30 /* every 30 sec */, 10);
MQTTKalmanPublish mkErrorsPerSecond(mqttClient, BASE_TOPIC_STATUS "errors-per-second", false, 30 /* every 30 sec */, 2);
MQTTKalmanPublish mkKilobytesPerSecond(mqttClient, BASE_TOPIC_STATUS "kilobytes-per-second", false, 30 /* every 30 sec */, 2);
MQTTKalmanPublish mkRssi(mqttClient, BASE_TOPIC_STATUS "rssi", MQTT_RETAINED, 12 * 5 /* every 5 min */, 10);

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

	matrix_setup(mqttBri);

#ifdef PRINT_TO_SERIAL
	mqttClient.enableDebuggingMessages();
#endif
	mqttClient.enableHTTPWebUpdater();
	mqttClient.enableOTA();
	mqttClient.enableLastWillMessage(BASE_TOPIC "connected", "0", MQTT_RETAINED);

	// well, hope we are OK, let's draw some colors first :)
	testMatrix();

	matrix_fill(0, 0, 0);
	matrix_update();

	Serial.println("Setup done...");
}

void onConnectionEstablished()
{
	mqttClient.subscribe(BASE_TOPIC_SET "bri", [](const String &payload) {
		int value = strtol(payload.c_str(), 0, 10);
		mqttBri = max(1, min(255, value));
		matrix_brightness(mqttBri * on);
		mqttClient.publish(BASE_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
	});

	mqttClient.subscribe(BASE_TOPIC_SET "on", [](const String &payload) {
		boolean value = payload != "0";
		on = value;
		matrix_brightness(mqttBri * on);
		mqttClient.publish(BASE_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
	});

	pixelServer.begin();
	pixelServer.setNoDelay(true);
	Serial.printf("Now listening to tcp://" CLIENT_NAME ":%d\n", LISTEN_PORT);

	mqttClient.publish(BASE_TOPIC_STATUS "bri", String(mqttBri), MQTT_RETAINED);
	mqttClient.publish(BASE_TOPIC_STATUS "on", String(on), MQTT_RETAINED);
	mqttClient.publish(BASE_TOPIC "git-version", GIT_VERSION, MQTT_RETAINED);
	mqttClient.publish(BASE_TOPIC "protocol-version", String(PROTOCOL_VERSION), MQTT_RETAINED);
	mqttClient.publish(BASE_TOPIC "connected", "2", MQTT_RETAINED);
}

void updateWifiClientVector()
{
	for (auto i = pixelClients.size(); i > 0; i--)
	{
		auto pixelClient = pixelClients[i - 1];
		if (!pixelClient.connected())
		{
			pixelClients.erase(pixelClients.begin() + i - 1);

#ifdef PRINT_TO_SERIAL
			Serial.printf("Client left. Remaining: %d\n", pixelClients.size());
#endif
		}
	}

	while (pixelServer.hasClient())
	{
		auto pixelClient = pixelServer.available();
		pixelClient.setNoDelay(true);
		pixelClient.write(PROTOCOL_VERSION);
		pixelClient.write(TOTAL_WIDTH);
		pixelClient.write(TOTAL_HEIGHT);
		pixelClient.flush();

#ifdef PRINT_TO_SERIAL
		Serial.print("Client new: ");
		Serial.print(pixelClient.remoteIP().toString());
		Serial.print(":");
		Serial.println(pixelClient.remotePort());
#endif

		pixelClients.push_back(pixelClient);
	}
}

void loop()
{
	mqttClient.loop();
	digitalWrite(LED_BUILTIN, mqttClient.isConnected() ? LED_BUILTIN_OFF : LED_BUILTIN_ON);
	if (!mqttClient.isWifiConnected())
	{
		return;
	}

	auto now = millis();

	static unsigned long nextMqttStats = 0;
	if (now >= nextMqttStats)
	{
		nextMqttStats = now + 1000;
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

	static unsigned long nextMeasure = 0;
	if (now >= nextMeasure)
	{
		nextMeasure = now + 5000;
		long rssi = WiFi.RSSI();
		float avgRssi = mkRssi.addMeasurement(rssi);
#ifdef PRINT_TO_SERIAL
		Serial.printf("RSSI          in dBm: %8ld    Average: %10.2f\n", rssi, avgRssi);
#endif
	}

	updateWifiClientVector();
	if (lastPublishedClientAmount != pixelClients.size())
	{
		auto next = pixelClients.size();
		bool success = mqttClient.publish(BASE_TOPIC_STATUS "clients", String(next), MQTT_RETAINED);
		if (success)
		{
			lastPublishedClientAmount = next;
		}
	}

	// 50 ms -> 20 FPS
	// 25 ms -> 40 FPS
	// 20 ms -> 50 FPS
	// 16 ms -> 62.2 FPS
	auto until = millis() + 25;
	while (millis() < until)
	{
		for (auto pixelClient : pixelClients)
		{
			if (pixelClient.available())
			{
				const size_t BUFFER_COLOR_SIZE = 3;
				static uint8_t buffer_color[BUFFER_COLOR_SIZE];
				const size_t BUFFER_RECT_SIZE = 4;
				static uint8_t buffer_rect[BUFFER_RECT_SIZE];

				uint8_t kind = pixelClient.read();
				if (kind == 1) // Fill
				{
					auto size = pixelClient.readBytes(buffer_color, BUFFER_COLOR_SIZE);
					bytes += 1 + size;
					if (size != BUFFER_COLOR_SIZE)
					{
						errors += 1;
						continue;
					}
					commands += 1;

					auto red = buffer_color[0];
					auto green = buffer_color[1];
					auto blue = buffer_color[2];
					matrix_fill(red, green, blue);
				}
				else if (kind == 2) // Pixel
				{
					const size_t buffer_size = 5;
					static uint8_t buffer[buffer_size];
					auto size = pixelClient.readBytes(buffer, buffer_size);
					bytes += 1 + size;
					if (size != buffer_size)
					{
						errors += 1;
						continue;
					}
					commands += 1;

					auto x = buffer[0];
					auto y = buffer[1];
					auto red = buffer[2];
					auto green = buffer[3];
					auto blue = buffer[4];
					matrix_pixel(x, y, red, green, blue);
				}
				else if (kind == 3 || kind == 4) // Rectangle Solid / Contiguous
				{
					auto rect_size = pixelClient.readBytes(buffer_rect, BUFFER_RECT_SIZE);
					bytes += 1 + rect_size;
					if (rect_size != BUFFER_RECT_SIZE)
					{
						errors += 1;
						continue;
					}

					auto x_start = buffer_rect[0];
					auto y_start = buffer_rect[1];
					auto width = buffer_rect[2];
					auto height = buffer_rect[3];

					if (kind == 3) // Solid
					{
						auto size = pixelClient.readBytes(buffer_color, BUFFER_COLOR_SIZE);
						bytes += size;
						if (size != BUFFER_COLOR_SIZE)
						{
							errors += 1;
							continue;
						}
						commands += 1;

						auto red = buffer_color[0];
						auto green = buffer_color[1];
						auto blue = buffer_color[2];
						for (auto x = x_start; x < x_start + width; x++)
						{
							for (auto y = y_start; y < y_start + height; y++)
							{
								matrix_pixel(x, y, red, green, blue);
							}
						}
					}
					else // Contiguous
					{
						size_t buffer_size = width * height * 3;
						uint8_t buffer[buffer_size];
						auto size = pixelClient.readBytes(buffer, buffer_size);
						bytes += size;
						if (size != buffer_size)
						{
							errors += 1;
							continue;
						}
						commands += 1;
						size_t index = 0;
						for (auto y = y_start; y < y_start + height; y++)
						{
							for (auto x = x_start; x < x_start + width; x++)
							{
								auto red = buffer[index++];
								auto green = buffer[index++];
								auto blue = buffer[index++];
								matrix_pixel(x, y, red, green, blue);
							}
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

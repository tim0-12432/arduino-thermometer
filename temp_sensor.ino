#include <Arduino_LSM6DSOX.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include "secrets.h"
#include "configuration.h"

float calibrationOffset = 0.0;

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

const char broker[] = BROKER_HOST;
int        port     = BROKER_PORT;

const char deviceName[]  = "arduino_TEMP";
char       topicState[200];
char       topicSensor[100];

unsigned long previousMillis  = 0;
unsigned long countDownMillis = 0;
const long    interval        = INTERVAL;

extern "C" char* sbrk(int incr);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

void setup() {
  Serial.begin(9600);

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    Serial.print(".");
    delay(5000);
  }
  Serial.println();

  Serial.println("Successfully connected to the network!");
  Serial.println();

  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.print(broker);
  Serial.print(":");
  Serial.println(port);

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("Successfully connected to the MQTT broker!");
  Serial.println();

  snprintf(topicState, sizeof(topicState), "tele/%s/STATE", deviceName);
  snprintf(topicSensor, sizeof(topicSensor), "tele/%s/SENSOR", deviceName);
}

// TODO: find better method to get percentage of used SRAM
int usedRam() {
  char top;
  int totalSRAM = 264 * 1024;
  int usedSRAM = totalSRAM - (&top - reinterpret_cast<char*>(sbrk(0)));
  int percentageUsed = (usedSRAM * 100) / totalSRAM;
  return percentageUsed;
}

void loop() {
  mqttClient.poll();

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    countDownMillis = currentMillis;

    if (IMU.temperatureAvailable()) {
      int temperatureDegInt = 0;
      IMU.readTemperature(temperatureDegInt);
      
      float temperatureDeg = static_cast<float>(temperatureDegInt);
      float calibratedTemperatureDeg = temperatureDeg + calibrationOffset;

      Serial.print("LSM6DSOX Temperature = ");
      Serial.print(calibratedTemperatureDeg);
      Serial.println(" °C");
      
      unsigned long secondsSinceEpoch = currentMillis / 1000;
      int days = secondsSinceEpoch / 86400;
      int hours = (secondsSinceEpoch % 86400) / 3600;
      int minutes = (secondsSinceEpoch % 3600) / 60;
      int seconds = secondsSinceEpoch % 60;
      char timestamp[25];
      snprintf(timestamp, sizeof(timestamp), "1970-%02d-%02dT%02d:%02d:%02d", days / 30 + 1, days % 30 + 1, hours, minutes, seconds);

      const char* wifiSSID = WiFi.SSID();
      long wifiRSSI = WiFi.RSSI();
      int wifiChannel = WiFi.channel(0);
      auto heap = usedRam();
      
      byte bssid[6];
      WiFi.BSSID(bssid);
      char wifiBSSID[18];
      snprintf(wifiBSSID, sizeof(wifiBSSID), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

      char msgState[200];
      snprintf(msgState, sizeof(msgState), "{\"Time\":\"%s\",\"Uptime\":\"%dT%02d:%02d:%02d\",\"UptimeSec\":%lu,\"Heap\":%d,\"LoadAvg\":100,\"POWER\":\"ON\",\"Wifi\":{\"AP\":1,\"SSId\":\"%s\",\"BSSId\":\"%s\",\"Channel\":%d,\"Signal\":%ld}}",
          timestamp, days, hours, minutes, seconds, secondsSinceEpoch, heap, wifiSSID, wifiBSSID, wifiChannel, wifiRSSI);
      mqttClient.beginMessage(topicState);
      mqttClient.print(msgState);
      mqttClient.endMessage();

      char msgSensor[100];
      snprintf(msgSensor, sizeof(msgSensor), "{\"Time\":\"%s\",\"TEMP\":%.2f,\"TempUnit\":\"C\"}", timestamp, calibratedTemperatureDeg);
      mqttClient.beginMessage(topicSensor);
      mqttClient.print(msgSensor);
      mqttClient.endMessage();
    } else {
      Serial.println("No IMU available!");
    }
  } else if (currentMillis - countDownMillis >= 1000) {
    countDownMillis = currentMillis;
    unsigned long timeRemaining = (interval - (currentMillis - previousMillis)) / 1000;
    Serial.print("Next message will be sent in ");
    Serial.print(timeRemaining);
    Serial.println(" seconds");
  }

  if (!mqttClient.connected()) {
    Serial.print("Attempting to reconnect to the MQTT broker: ");
    Serial.print(broker);
    Serial.print(":");
    Serial.println(port);

    if (!mqttClient.connect(broker, port)) {
      Serial.print("MQTT reconnection failed! Error code = ");
      Serial.println(mqttClient.connectError());
    } else {
      Serial.println("Successfully reconnected to the MQTT broker!");
    }
  }
}

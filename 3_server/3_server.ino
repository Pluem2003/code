#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID "Test_server"
#define WIFI_PASSWORD "76543210"
#define MQTT_SERVER "49.228.131.61"  // IP ของ MQTT Broker
#define MQTT_PORT 4663
#define MQTT_TOPIC "sensor/data"

// กำหนด UUID ของ Service และ Characteristic ของ BLE
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcdabcd-1234-5678-abcd-123456789abc"

// ตัวแปรสำหรับจัดการ BLE และ MQTT
BLEScan* pBLEScan;
BLEClient* pClient1;
BLEClient* pClient2;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

String sensorData1 = "";  // เก็บข้อมูลจากตัวที่ 1
String sensorData2 = "";  // เก็บข้อมูลจากตัวที่ 2

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
}

void connectToMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect("ESP32_C3_Client")) {
      Serial.println("Connected to MQTT");
    } else {
      Serial.print("Failed with state ");
      Serial.print(mqttClient.state());
      delay(2000);
    }
  }
}

void sendDataToMQTT() {
  String payload = sensorData1 + "\n" + sensorData2;  // รวมข้อมูลจากทั้งสองตัว
  mqttClient.publish(MQTT_TOPIC, payload.c_str());
  Serial.println("Data sent to MQTT: ");
  Serial.println(payload);
}

void readSensorData(BLEClient* pClient, String &sensorData) {
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic->canRead()) {
    std::string value = std::string(pRemoteCharacteristic->readValue().c_str());
    sensorData = String(value.c_str());
    Serial.print("Received sensor data: ");
    Serial.println(sensorData);
    Serial.print("Size data: ");
    Serial.println(value.size());
  }
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32_C3_Central");

  connectToWiFi();
  connectToMQTT();

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
}

bool connectToSensor(BLEClient* &pClient, BLEAddress address) {
  pClient = BLEDevice::createClient();
  Serial.println("Attempting to connect to sensor...");

  if (!pClient->connect(address)) {
    Serial.println("Failed to connect to sensor");
    pClient->disconnect();
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find BLE service");
    pClient->disconnect();
    return false;
  }
  
  BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Failed to find BLE characteristic");
    pClient->disconnect();
    return false;
  }

  Serial.println("Connected to sensor successfully");
  return true;
}

void loop() {
  Serial.println("Scanning for BLE devices...");
  BLEScanResults* scanResults = pBLEScan->start(10, false);
  
  for (int i = 0; i < scanResults->getCount(); i++) {
    BLEAdvertisedDevice advertisedDevice = scanResults->getDevice(i);

    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().toString() == SERVICE_UUID) {
      if (pClient1 == nullptr && !pClient1->isConnected()) {
        if (connectToSensor(pClient1, advertisedDevice.getAddress())) {
          Serial.println("Connected to Sensor 1");
        }
      } 
      else if (pClient2 == nullptr && !pClient2->isConnected()) {
        if (connectToSensor(pClient2, advertisedDevice.getAddress())) {
          Serial.println("Connected to Sensor 2");
        }
      }
    }
  }

  // เช็คสถานะการเชื่อมต่อของ Sensor 1 และ 2
  if (pClient1 && pClient1->isConnected()) {
    readSensorData(pClient1, sensorData1);
  }

  if (pClient2 && pClient2->isConnected()) {
    readSensorData(pClient2, sensorData2);
  }

  // ส่งข้อมูล MQTT
  if (!sensorData1.isEmpty() && !sensorData2.isEmpty()) {
    sendDataToMQTT();
    sensorData1 = "";
    sensorData2 = "";
  }

  mqttClient.loop();
  delay(1000);  // เพิ่มเวลาเล็กน้อยระหว่างการตรวจสอบและสแกน BLE ใหม่
}
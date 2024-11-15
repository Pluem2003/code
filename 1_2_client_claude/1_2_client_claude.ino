#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcdabcd-1234-5678-abcd-123456789abc"

#define SENSOR_READ_INTERVAL 500
#define SENSOR_DATA_COUNT 20
#define ADC_PIN 34

BLECharacteristic *pCharacteristic;
BLEServer *pServer;
bool deviceConnected = false;
float sensorReadings[SENSOR_DATA_COUNT];
int readingCount = 0;

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Device connected");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        // Restart advertising when disconnected
        pServer->getAdvertising()->start();
    }
};

float generateRandomSensorData() {
    return random(100, 1000) / 100.0;
}

void setup() {
    Serial.begin(115200);
    BLEDevice::init("ESP32-Sensor-Device");

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | 
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pService->start();
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    pAdvertising->start();

    esp_sleep_enable_timer_wakeup(SENSOR_READ_INTERVAL * 1000); // Convert to microseconds
}

void sendSensorData() {
    if (!deviceConnected) {
        Serial.println("No device connected, storing data...");
        return;
    }

    String dataToSend = "";
    for (int i = 0; i < readingCount; i++) {
        if (i > 0) dataToSend += ",";
        dataToSend += String(sensorReadings[i], 2);
    }
    
    // Break data into chunks if needed
    const int chunkSize = 20;
    int len = dataToSend.length();
    for (int i = 0; i < len; i += chunkSize) {
        String chunk = dataToSend.substring(i, min(i + chunkSize, len));
        pCharacteristic->setValue(chunk.c_str());
        pCharacteristic->notify();
        delay(20); // Give some time between chunks
    }
    
    Serial.println("Data Sent: " + dataToSend);
}

void loop() {
    if (readingCount < SENSOR_DATA_COUNT) {
        float sensorData = generateRandomSensorData();
        sensorReadings[readingCount++] = sensorData;
        Serial.println("Sensor Reading: " + String(sensorData));
    }

    if (readingCount == SENSOR_DATA_COUNT) {
        sendSensorData();
        if (deviceConnected) {
            readingCount = 0; // Only reset if data was sent successfully
        }
    }

    delay(100);
    if (!deviceConnected) {
        esp_deep_sleep_start();
    }
}
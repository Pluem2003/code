#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// กำหนด UUID สำหรับบริการและลักษณะ
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcdabcd-1234-5678-abcd-123456789abc"

BLECharacteristic *pCharacteristic;

// ตั้งค่าตัวแปร
#define SENSOR_READ_INTERVAL 500  // อ่านเซ็นเซอร์ทุกๆ 1 วินาที
#define SENSOR_DATA_COUNT 20       // เก็บข้อมูล 60 ค่า (1 นาที)
#define ADC_PIN 34                 // กำหนดพิน ADC

float sensorReadings[SENSOR_DATA_COUNT];
int readingCount = 0;

float generateRandomSensorData() {
    return random(100, 1000) / 100.0;
}

void setup() {
    Serial.begin(115200);
    BLEDevice::init("ESP32-Sensor-Device");

    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    esp_sleep_enable_timer_wakeup(SENSOR_READ_INTERVAL);
}

void loop() {
    if (readingCount < SENSOR_DATA_COUNT) {
        float sensorData = generateRandomSensorData();
        sensorReadings[readingCount++] = sensorData;
        Serial.println("Sensor Reading: " + String(sensorData));
    }

    if (readingCount == SENSOR_DATA_COUNT) {
        sendSensorData();
        readingCount = 0;
    }

    // รอให้ BLE ส่งข้อมูลเสร็จสมบูรณ์ก่อนที่จะเข้าสู่โหมด Sleep
    Serial.println("Going to sleep...");
    delay(100); // ให้เวลาส่งข้อมูล
    Serial.flush();  // รอให้ข้อมูลใน Serial ส่งออกให้หมด
    esp_deep_sleep_start();  // เข้าสู่โหมด Deep Sleep
}

void sendSensorData() {
    Serial.println("Sending Sensor Data...");
    
    String dataToSend = "";
    for (int i = 0; i < SENSOR_DATA_COUNT; i++) {
        dataToSend += String(sensorReadings[i], 2) + ",";
    }
    
    pCharacteristic->setValue(dataToSend.c_str());
    pCharacteristic->notify();
    Serial.println("Data Sent: " + dataToSend);
}

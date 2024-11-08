#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// กำหนด UUID ของ service และ characteristic
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789012"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-210987654321"

// สร้าง BLECharacteristic
BLECharacteristic *pCharacteristic;
BLEServer *pServer; // เก็บ BLEServer ไว้ในตัวแปร
bool deviceConnected = false;
unsigned long lastReadTime = 0;
const unsigned long readInterval = 250; // กำหนดเวลาการอ่านข้อมูลทุก 0.25 วินาที
const int maxReadings = 60; // จำนวนข้อมูลสูงสุดที่จะส่ง
String sensorData[maxReadings]; // อาร์เรย์เพื่อเก็บข้อมูลเซ็นเซอร์
int readingCount = 0; // นับจำนวนการอ่านข้อมูล

// ฟังก์ชัน callback เมื่อ client เชื่อมต่อหรือยกเลิกการเชื่อมต่อ
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

// ฟังก์ชันจำลองการอ่านข้อมูลจากเซ็นเซอร์
String readSensorData() {
  // ตัวอย่างการอ่านค่าจากเซ็นเซอร์: อุณหภูมิ
  float temperature = 25.0 + random(5);  // จำลองค่าอุณหภูมิ
  return String(temperature);
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3 is starting...");
  BLEDevice::init("ESP32-C3 Sensor Server");

  // สร้าง BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // สร้าง BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // สร้าง BLE Characteristic สำหรับส่งข้อมูล CSV
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pCharacteristic->addDescriptor(new BLE2902());

  // เริ่มโฆษณา BLE
  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("Waiting for a client connection to notify...");
}

void loop() {
  // ตรวจสอบว่ามี client เชื่อมต่ออยู่หรือไม่
  if (deviceConnected) {
    // อ่านข้อมูลทุกๆ 0.25 วินาที
    if (millis() - lastReadTime > readInterval) {
      lastReadTime = millis();

      // อ่านค่าจากเซ็นเซอร์และเก็บข้อมูล
      String sensorReading = readSensorData();
      sensorData[readingCount++] = sensorReading; // เก็บข้อมูลในอาร์เรย์

      // ตรวจสอบว่ามีการเก็บข้อมูลครบ 60 ชุดหรือไม่
      if (readingCount >= maxReadings) {
        String csvData = "";
        for (int i = 0; i < maxReadings; i++) {
          csvData += sensorData[i]; // รวมข้อมูลเป็น CSV
          if (i < maxReadings - 1) {
            csvData += ","; // แยกข้อมูลด้วยเครื่องหมายจุลภาค
          }
        }
        Serial.print("Sending CSV data: ");
        Serial.println(csvData);

        // ส่งข้อมูล CSV ผ่าน BLE characteristic
        pCharacteristic->setValue(csvData.c_str());
        pCharacteristic->notify();  // ส่ง notification ไปยัง client

        // รีเซ็ตตัวแปรหลังจากส่งข้อมูล
        readingCount = 0; // ตั้งค่าจำนวนการอ่านกลับเป็น 0
      }
    }
  }

  // จำลองว่าหลังจาก disconnect แล้ว server พร้อมรอ client ใหม่
  if (!deviceConnected) {
    delay(500); // ให้เวลาสำหรับการตัดการเชื่อมต่อที่สมบูรณ์
    pServer->getAdvertising()->start();  // เริ่มโฆษณาใหม่อีกครั้ง
    Serial.println("Start advertising...");
  }
}

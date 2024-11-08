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
unsigned long lastTime = 0;
unsigned long interval = 1000;  // กำหนดเวลาการส่งข้อมูลทุก 1 วินาที

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
  // ตัวอย่างการอ่านค่าจากเซ็นเซอร์: อุณหภูมิ, ความชื้น (เช่นจาก DHT22)
  float temperature = 25.0 + random(5);  // จำลองค่าอุณหภูมิ
  float humidity = 60.0 + random(10);    // จำลองค่าความชื้น
  
  // แปลงข้อมูลเซ็นเซอร์เป็นรูปแบบ CSV
  String csvData = String(temperature) + "," + String(humidity);
  return csvData;
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
    // ส่งข้อมูลทุกๆ 1 วินาที
    if (millis() - lastTime > interval) {
      lastTime = millis();

      // อ่านค่าจากเซ็นเซอร์และแปลงเป็น CSV
      String csvData = readSensorData();
      Serial.print("Sending CSV data: ");
      Serial.println(csvData);

      // ส่งข้อมูล CSV ผ่าน BLE characteristic
      pCharacteristic->setValue(csvData.c_str());
      pCharacteristic->notify();  // ส่ง notification ไปยัง client
    }
  }

  // จำลองว่าหลังจาก disconnect แล้ว server พร้อมรอ client ใหม่
  if (!deviceConnected && lastTime != 0) {
    delay(500); // ให้เวลาสำหรับการตัดการเชื่อมต่อที่สมบูรณ์
    pServer->getAdvertising()->start();  // เริ่มโฆษณาใหม่อีกครั้ง
    Serial.println("Start advertising...");
    lastTime = 0;
  }
}

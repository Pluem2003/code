#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// Configs
#define DEVICE_NAME "ESP32-Current-Sensor1"
#define SERVICE_UUID "12345678-1234-1234-1234-123456789ab1"
#define CHAR_UUID "abcdabcd-1234-5678-abcd-123456789abc"
#define SLEEP_DURATION 1000000 // เวลา deep sleep ในไมโครวินาที
#define NUM_READINGS 20    // จำนวนครั้งที่ต้องการอ่านค่า
#define SEND_INTERVAL 1000  // ระยะเวลาระหว่างการส่งแต่ละ chunk
#define CHUNK_SIZE 1       
#define MAX_RETRIES 10
#define SENSOR_PIN 34      // PIN สำหรับอ่านค่าเซนเซอร์

// Variables
Preferences preferences;
RTC_DATA_ATTR float readings[NUM_READINGS];  // เก็บค่าใน RTC memory
bool readingsComplete = false; // สถานะการอ่านครบ

// BLE
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;
bool allDataSent = false;

// โครงสร้างข้อมูลการส่ง
struct SendBuffer {
    bool isActive = false;
    int measurementCycle = 0;
    int currentChunk = 0;
    int totalChunks;
    bool chunkSent = false;
    int retryCount = 0;
} sendBuffer;

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
        if (s == Status::SUCCESS_NOTIFY) {
            sendBuffer.chunkSent = true;
            Serial.println("Notification success!");
        } else {
            sendBuffer.chunkSent = false;
            Serial.printf("Notification failed: status=%d, code=%d\n", s, code);
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Device connected");
    }
    
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        BLEDevice::startAdvertising();
    }
};

void resetSendBuffer() {
    sendBuffer.isActive = false;
    sendBuffer.chunkSent = false;
    sendBuffer.retryCount = 0;
}

void initBLE() {
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE initialized and advertising...");
}

void readSensorData() {
    preferences.begin("sensor", false);
    int currentReading = preferences.getInt("readCount", 0);
    
    if (currentReading < NUM_READINGS) {
        // อ่านค่าจากเซนเซอร์
        float sensorValue = (random(0, 3300) / 1000.0);
        readings[currentReading] = sensorValue;
        
        Serial.printf("Reading %d: %.3f V\n", currentReading + 1, readings[currentReading]);
        currentReading++;
        
        // บันทึกค่า currentReading ลงใน Preferences
        preferences.putInt("readCount", currentReading);
        
        if (currentReading >= NUM_READINGS) {
            readingsComplete = true;
            Serial.println("All readings collected!");
            // รีเซ็ตค่า readCount เมื่ออ่านครบแล้ว
            preferences.putInt("readCount", 0);
        }
    }
    preferences.end();
}

void sendDataChunk() {
    static unsigned long lastNotifyTime = 0;
    
    if (!deviceConnected || !readingsComplete) {
        return;
    }

    unsigned long currentTime = millis();
    if (currentTime - lastNotifyTime < SEND_INTERVAL) {
        return;
    }

    if (!sendBuffer.isActive) {
        sendBuffer.isActive = true;
        sendBuffer.measurementCycle++;
        sendBuffer.currentChunk = 0;
        sendBuffer.totalChunks = (NUM_READINGS + CHUNK_SIZE - 1) / CHUNK_SIZE;
        sendBuffer.retryCount = 0;
        sendBuffer.chunkSent = false;
    }

    if (!sendBuffer.chunkSent && sendBuffer.retryCount >= MAX_RETRIES) {
        Serial.println("Failed to send chunk after max retries");
        resetSendBuffer();
        return;
    }

    if (!sendBuffer.chunkSent) {
        int startIdx = sendBuffer.currentChunk * CHUNK_SIZE;
        int endIdx = min(startIdx + CHUNK_SIZE, NUM_READINGS);
        
        String data = String(sendBuffer.measurementCycle) + "," +
                     String(sendBuffer.currentChunk) + "," +
                     String(sendBuffer.totalChunks) + ",";
        
        for (int i = startIdx; i < endIdx; i++) {
            data += String(readings[i], 3);
            if (i < endIdx - 1) {
                data += ",";
            }
        }

        Serial.printf("Sending chunk %d of %d: %s\n", 
                    sendBuffer.currentChunk + 1, sendBuffer.totalChunks, data.c_str());

        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
        lastNotifyTime = currentTime;
        sendBuffer.retryCount++;
    } else {
        sendBuffer.currentChunk++;
        sendBuffer.retryCount = 0;
        sendBuffer.chunkSent = false;

        if (sendBuffer.currentChunk >= sendBuffer.totalChunks) {
            Serial.println("All data sent successfully!");
            allDataSent = true;
            readingsComplete = false;
            resetSendBuffer();
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // ตั้งค่า PIN สำหรับอ่านค่าเซนเซอร์
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(SENSOR_PIN, INPUT);
    
    preferences.begin("sensor", false);
    int currentReading = preferences.getInt("readCount", 0);
    preferences.end();
    
    if (currentReading < NUM_READINGS) {
        // โหมดอ่านค่า
        readSensorData();
        // เข้า deep sleep
        if (currentReading < NUM_READINGS) {
            Serial.println("Entering deep sleep...");
            delay(1000);
            Serial.printf("Going to sleep for %d seconds...\n", SLEEP_DURATION/1000000);
            esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
            esp_deep_sleep_start();
        }
    }
    
    // ถ้าอ่านค่าครบแล้ว เริ่ม BLE เพื่อส่งข้อมูล
    if (readingsComplete) {
        Serial.println("Readings complete, starting BLE...");
        initBLE();
    }
}

void loop() {
    if (readingsComplete && !allDataSent) {
        sendDataChunk();
    }
    
    // ถ้าส่งข้อมูลครบแล้ว กลับไปนอน
    if (allDataSent) {
        Serial.println("All data sent, going to deep sleep...");
        delay(1000);
        esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
        esp_deep_sleep_start();
    }
}
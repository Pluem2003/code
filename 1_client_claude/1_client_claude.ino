#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Configs
#define DEVICE_NAME "ESP32-Current-Sensor1"
#define SERVICE_UUID "12345678-1234-1234-1234-123456789ab1"
#define CHAR_UUID "abcdabcd-1234-5678-abcd-123456789abc"
#define DATA_INTERVAL 500    
#define SEND_INTERVAL 200   
#define NUM_READINGS 20     
#define CHUNK_SIZE 1       
#define MAX_RETRIES 10      

// Variables
float readings[NUM_READINGS];
int currentReading = 0;
bool readyToSend = false;
unsigned long lastNotifyTime = 0;

// BLE
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;

// ปรับปรุงโครงสร้างข้อมูลการส่ง
struct SendBuffer {
    bool isActive = false;
    int measurementCycle = 0;
    int currentChunk = 0;
    int totalChunks;
    bool chunkSent = false;
    int retryCount = 0;
} sendBuffer;

// Forward declaration
void resetSendBuffer();

// Callback สำหรับตรวจสอบการส่งข้อมูล
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
        resetSendBuffer();
        BLEDevice::startAdvertising();
    }
};

// ฟังก์ชันรีเซ็ตบัฟเฟอร์การส่ง
void resetSendBuffer() {
    sendBuffer.isActive = false;
    currentReading = 0;
    readyToSend = false;
    sendBuffer.chunkSent = false;
    sendBuffer.retryCount = 0;
}

void readSensorData() {
    if (currentReading < NUM_READINGS) {
        readings[currentReading] = (random(0, 3300) / 1000.0);
        Serial.printf("Reading %d: %.3f\n", currentReading + 1, readings[currentReading]);
        currentReading++;
        
        if (currentReading == NUM_READINGS) {
            readyToSend = true;
            sendBuffer.isActive = true;
            sendBuffer.measurementCycle++;
            sendBuffer.currentChunk = 0;
            sendBuffer.totalChunks = (NUM_READINGS + CHUNK_SIZE - 1) / CHUNK_SIZE;
            sendBuffer.retryCount = 0;
            sendBuffer.chunkSent = false;
        }
    }
}

void sendDataChunk() {
    if (!readyToSend || !deviceConnected || !sendBuffer.isActive) {
        return;
    }

    unsigned long currentTime = millis();
    if (currentTime - lastNotifyTime < SEND_INTERVAL) {
        return;
    }

    // ตรวจสอบว่าชุดก่อนหน้าส่งสำเร็จหรือครบจำนวนครั้งที่ลองซ้ำ
    if (!sendBuffer.chunkSent && sendBuffer.retryCount >= MAX_RETRIES) {
        Serial.println("Failed to send chunk after max retries");
        resetSendBuffer();
        return;
    }

    if (!sendBuffer.chunkSent) {
        // ส่งข้อมูลใหม่หรือส่งซ้ำ
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

        if (sendBuffer.retryCount > 0) {
            Serial.printf("Retrying chunk %d (attempt %d)\n", 
                        sendBuffer.currentChunk + 1, sendBuffer.retryCount + 1);
        } else {
            Serial.printf("Sending chunk %d of %d: %s\n", 
                        sendBuffer.currentChunk + 1, sendBuffer.totalChunks, data.c_str());
        }

        pCharacteristic->setValue(data.c_str());
        pCharacteristic->notify();
        lastNotifyTime = currentTime;
        sendBuffer.retryCount++;
    } else {
        // ข้อมูลถูกส่งสำเร็จ, เตรียมส่งชุดถัดไป
        sendBuffer.currentChunk++;
        sendBuffer.retryCount = 0;
        sendBuffer.chunkSent = false;

        if (sendBuffer.currentChunk >= sendBuffer.totalChunks) {
            Serial.println("All chunks sent successfully!");
            resetSendBuffer();
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize BLE
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
    
    Serial.println("Sensor ready and advertising...");
}

void loop() {
    static unsigned long lastReadingTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastReadingTime >= DATA_INTERVAL) {
        lastReadingTime = currentTime;
        readSensorData();
    }
    
    sendDataChunk();
}
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Configs
#define DEVICE_NAME "ESP32-Current-Sensor1"
#define SERVICE_UUID "12345678-1234-1234-1234-123456789ab1"
#define CHAR_UUID "abcdabcd-1234-5678-abcd-123456789abc"
#define DATA_INTERVAL 1000    // Interval between sensor readings
#define SEND_INTERVAL 1000    // Interval between sending data

// Variables for real-time streaming
float currentReading = 0.0;
unsigned long lastNotifyTime = 0;
unsigned long lastReadingTime = 0;

// BLE
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;

// Measurement cycle tracking
int measurementCycle = 0;

class CharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
        if (s == Status::SUCCESS_NOTIFY) {
            Serial.println("Notification success!");
        } else {
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

void readSensorData() {
    // Simulate sensor reading (replace with actual sensor reading)
    currentReading = (random(0, 3300) / 1000.0);
    Serial.printf("Current Reading: %.3f\n", currentReading);
}

void sendRealtimeData() {
    if (!deviceConnected) {
        return;
    }

    unsigned long currentTime = millis();
    if (currentTime - lastNotifyTime < SEND_INTERVAL) {
        return;
    }

    // Prepare data string with measurement cycle and timestamp
    String data = String(measurementCycle) + "," + 
                  String(millis()) + "," + 
                  String(currentReading, 3);

    Serial.printf("Sending real-time data: %s\n", data.c_str());
    
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();
    
    lastNotifyTime = currentTime;
    measurementCycle++;
}

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(0));  // Initialize random seed
    
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
    unsigned long currentTime = millis();
    
    // Read sensor data at specified interval
    if (currentTime - lastReadingTime >= DATA_INTERVAL) {
        lastReadingTime = currentTime;
        readSensorData();
    }
    
    // Send real-time data
    sendRealtimeData();
}
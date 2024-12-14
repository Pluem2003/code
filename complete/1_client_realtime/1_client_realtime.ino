#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <math.h>

// Configs
#define DEVICE_NAME "ESP32-Current-Sensor1"
#define SERVICE_UUID "12345678-1234-1234-1234-123456789ab1"
#define CHAR_UUID "abcdabcd-1234-5678-abcd-123456789abc"
#define DATA_INTERVAL 1000    // Interval between sensor readings
#define SEND_INTERVAL 1000    // Interval between sending data

// MCP3221 Configuration
#define SDA_PIN 8   // SDA ของ ESP32-C3 Supermini
#define SCL_PIN 9   // SCL ของ ESP32-C3 Supermini
#define MCP3221_ADDRESS 0x4D // I2C Address ของ MCP3221
#define clockFrequency 400000
#define offset 126

// Variables for current measurement
unsigned long long sum_all = 0;
unsigned long long check_overflow = 0;
uint16_t count_Value = 0;
double currentReading = 0;
double voltage = 0;

// BLE Variables
BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;
unsigned long lastNotifyTime = 0;
unsigned long lastReadingTime = 0;
unsigned long lastSendingTime = 0;
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

// MCP3221 Reading Function
uint16_t readMCP3221() {
    unsigned int rawData = 0;
    Wire.beginTransmission(MCP3221_ADDRESS);
    Wire.requestFrom(MCP3221_ADDRESS, 2);
    if (Wire.available() == 2) {
        rawData = (Wire.read() << 8) | (Wire.read());
    } else {
        Wire.beginTransmission(MCP3221_ADDRESS);
        Wire.endTransmission();
    }
    return rawData;
}

void readSensorData() {
    uint16_t adcValue = readMCP3221(); // Read ADC value
    
    // Update running sum for RMS calculation
    sum_all += (adcValue - offset) * (adcValue - offset);
    count_Value++;
    
    // Check for overflow
    if(sum_all < check_overflow) {
        sum_all = 0;
        count_Value = 0;
        sum_all += (adcValue - offset) * (adcValue - offset);
        count_Value++;
    }
    
    // Calculate RMS current every 2000 samples
    if(count_Value == 1000) {
        currentReading = sqrt(sum_all / count_Value);
        voltage = (currentReading * 3.3) / 4096.0; // Calculate voltage
        
        sum_all = 0;
        count_Value = 0;
        
        Serial.printf("Current: %.3f | Voltage: %.3f V\n", currentReading, voltage);
    }
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
                  String(currentReading);

    Serial.printf("Sending real-time data: %s\n", data.c_str());
    
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();
    
    lastNotifyTime = currentTime;
    measurementCycle++;
}

void setup() {
    Serial.begin(115200);
    
    // Initialize I2C
    Wire.begin();
    Wire.setClock(clockFrequency);
    Serial.println("MCP3221 I2C Reader Initialized");
    
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
    
    Serial.println("Current Sensor ready and advertising...");
}

void loop() {
    unsigned long currentTime = millis();
    if (currentTime - lastReadingTime >= 1) {
        lastReadingTime = currentTime;
        readSensorData();
    }
    // Read sensor data at specified interval
    if (currentTime - lastSendingTime >= DATA_INTERVAL) {
        lastSendingTime = currentTime;
        sendRealtimeData();
    }
    
}
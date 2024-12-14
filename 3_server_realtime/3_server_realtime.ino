#include <BLEDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Configs
const char* WIFI_SSID = "Test_server";
const char* WIFI_PASSWORD = "76543210";
const char* MQTT_SERVER = "49.228.131.61";
const char* MQTT_CLIENT_ID = "ESP32_Gateway";
#define MQTT_PORT 4663
#define MQTT_TOPIC "sensor/realtime"
#define MQTT_STATUS_TOPIC "sensor/status"
#define timescan 10

// BLE configs
static const BLEUUID SERVICE_UUID_1("12345678-1234-1234-1234-123456789ab1");
static const BLEUUID SERVICE_UUID_2("12345678-1234-1234-1234-123456789ab2");
static const BLEUUID CHAR_UUID("abcdabcd-1234-5678-abcd-123456789abc");
static const BLEUUID SERVICE_UUIDS[] = {SERVICE_UUID_1, SERVICE_UUID_2};

// Updated RealTimeSensorData structure to handle streaming
struct RealTimeSensorData {
    int measurementCycle;
    unsigned long timestamp;
    float reading;
    String deviceName;
};

// Global variables
static RealTimeSensorData sensorDataBuffer[2];
static unsigned long lastWiFiRetry = 0;
static unsigned long lastMQTTRetry = 0;
static unsigned long lastScan = 0;
static unsigned long lastStatusReport = 0;
static unsigned long lastDataReceived[2] = {0, 0};
static String deviceNames[2] = {"", ""};
static bool deviceFound[2] = {false, false};

// Timing constants
const unsigned long WIFI_RETRY_DELAY = 5000;
const unsigned long MQTT_RETRY_DELAY = 5000;
const unsigned long SCAN_INTERVAL = 1000;
const unsigned long STATUS_REPORT_INTERVAL = 60000;
const unsigned long CONNECT_TIMEOUT = 5000;

// Global objects
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
static BLEScan* pBLEScan = nullptr;
static BLEClient* pClients[2] = {nullptr, nullptr};

void sendStatusUpdate() {
    if (!mqttClient.connected()) {
        Serial.println("MQTT client not connected, cannot send status");
        return;
    }
    
    String status = "{";
    status += "\"gateway_uptime\":\"" + String(millis()/1000) + "\",";
    status += "\"wifi_strength\":\"" + String(WiFi.RSSI()) + "\",";
    status += "\"devices\":[";
    
    for (int i = 0; i < 2; i++) {
        if (i > 0) status += ",";
        status += "{";
        status += "\"sensor_" + String(i+1) + "\":{";
        status += "\"status\":\"" + String(deviceFound[i] ? "found" : "not_found") + "\",";
        status += "\"name\":\"" + deviceNames[i] + "\",";
        status += "\"last_data\":\"" + 
                 (lastDataReceived[i] > 0 ? String((millis() - lastDataReceived[i])/1000) : "never") + 
                 "\"";
        status += "}}";
    }
    status += "]}";
    
    mqttClient.publish(MQTT_STATUS_TOPIC, status.c_str());
}

void publishRealTimeData(uint8_t clientIdx) {
    String jsonData = "{";
    jsonData += "\"device_id\":" + String(clientIdx + 1) + ",";
    jsonData += "\"device_name\":\"" + sensorDataBuffer[clientIdx].deviceName + "\",";
    jsonData += "\"measurement_cycle\":" + String(sensorDataBuffer[clientIdx].measurementCycle) + ",";
    jsonData += "\"timestamp\":" + String(sensorDataBuffer[clientIdx].timestamp) + ",";
    jsonData += "\"reading\":" + String(sensorDataBuffer[clientIdx].reading, 3);
    jsonData += "}";
    
    String topic = String(MQTT_TOPIC) + "/" + String(clientIdx + 1);
    if (mqttClient.connected()) {
        mqttClient.publish(topic.c_str(), jsonData.c_str());
        Serial.println("Published to MQTT: " + jsonData);
        lastDataReceived[clientIdx] = millis();
    } else {
        Serial.println("MQTT not connected, cannot publish");
    }
}

void processIncomingData(uint8_t clientIdx, String value) {
    Serial.println("\n-------- Incoming Real-Time Data --------");
    Serial.printf("Device %d Raw Data: %s\n", clientIdx + 1, value.c_str());

    // Split the incoming data
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);
    
    if (firstComma == -1 || secondComma == -1) {
        Serial.println("Invalid data format");
        return;
    }
    
    // Parse data
    int measurementCycle = value.substring(0, firstComma).toInt();
    unsigned long timestamp = value.substring(firstComma + 1, secondComma).toInt();
    float reading = value.substring(secondComma + 1).toFloat();
    
    // Store in buffer
    sensorDataBuffer[clientIdx].measurementCycle = measurementCycle;
    sensorDataBuffer[clientIdx].timestamp = timestamp;
    sensorDataBuffer[clientIdx].reading = reading;
    sensorDataBuffer[clientIdx].deviceName = deviceNames[clientIdx];
    
    // Publish data
    publishRealTimeData(clientIdx);
    
    Serial.println("\n-------- Parsed Real-Time Data --------");
    Serial.printf("Measurement Cycle: %d\n", measurementCycle);
    Serial.printf("Timestamp: %lu\n", timestamp);
    Serial.printf("Reading: %.3f\n", reading);
    Serial.println("----------------------------------------\n");
}

class MyClientCallback : public BLEClientCallbacks {
    private:
        uint8_t clientIndex;
    
    public:
        MyClientCallback(uint8_t index) : clientIndex(index) {}
        
        void onConnect(BLEClient* pclient) {
            deviceFound[clientIndex] = true;
            Serial.printf("Connected to device %d: %s\n", clientIndex + 1, deviceNames[clientIndex].c_str());
        }

        void onDisconnect(BLEClient* pclient) {
            deviceFound[clientIndex] = false;
            Serial.printf("Disconnected from device %d: %s\n", clientIndex + 1, deviceNames[clientIndex].c_str());
        }
};

bool connectBLE(uint16_t clientIdx, const BLEAddress& addr) {
    unsigned long startTime = millis();
    
    if (pClients[clientIdx] != nullptr) {
        pClients[clientIdx]->disconnect();
        delete pClients[clientIdx];
        pClients[clientIdx] = nullptr;
    }
    
    pClients[clientIdx] = BLEDevice::createClient();
    pClients[clientIdx]->setClientCallbacks(new MyClientCallback(clientIdx));
    
    // Connect with timeout
    if (!pClients[clientIdx]->connect(addr, BLE_ADDR_TYPE_PUBLIC)) {
        if (millis() - startTime > CONNECT_TIMEOUT) {
            Serial.printf("Connection timeout for device %d\n", clientIdx + 1);
            delete pClients[clientIdx];
            pClients[clientIdx] = nullptr;
            return false;
        }
    }
    
    BLERemoteService* service = pClients[clientIdx]->getService(SERVICE_UUIDS[clientIdx]);
    if (!service) {
        pClients[clientIdx]->disconnect();
        delete pClients[clientIdx];
        pClients[clientIdx] = nullptr;
        return false;
    }
    
    BLERemoteCharacteristic* chr = service->getCharacteristic(CHAR_UUID);
    if (!chr) {
        pClients[clientIdx]->disconnect();
        delete pClients[clientIdx];
        pClients[clientIdx] = nullptr;
        return false;
    }
    
    // Register for notifications with the new data processing
    chr->registerForNotify([clientIdx](BLERemoteCharacteristic* chr, uint8_t* data, size_t length, bool isNotify) {
        String value = String((char*)data, length);
        processIncomingData(clientIdx, value);
    });
    
    return true;
}

void setup() {
    Serial.begin(115200);
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    
    BLEDevice::init("ESP32_Gateway");
    pBLEScan = BLEDevice::getScan();
    if (pBLEScan) {
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
    }
    
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setBufferSize(512);
    
    // Initialize sensor data buffers
    for (int i = 0; i < 2; i++) {
        memset(&sensorDataBuffer[i], 0, sizeof(RealTimeSensorData));
    }
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    
    unsigned long currentMillis = millis();
    if (currentMillis - lastWiFiRetry < WIFI_RETRY_DELAY) return false;
    
    lastWiFiRetry = currentMillis;
    Serial.println("Connecting to WiFi...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        timeout--;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected");
        Serial.println("IP: " + WiFi.localIP().toString());
        return true;
    }
    
    Serial.println("WiFi connection failed");
    return false;
}

bool connectMQTT() {
    if (!WiFi.isConnected()) return false;
    if (mqttClient.connected()) return true;
    
    unsigned long currentMillis = millis();
    if (currentMillis - lastMQTTRetry < MQTT_RETRY_DELAY) return false;
    
    lastMQTTRetry = currentMillis;
    Serial.println("Connecting to MQTT...");
    
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
        Serial.println("MQTT connected");
        mqttClient.subscribe(MQTT_TOPIC "/#");
        
        sendStatusUpdate();
        return true;
    }
    
    Serial.print("MQTT connection failed, rc=");
    Serial.println(mqttClient.state());
    return false;
}

void loop() {
    static unsigned long lastCheck = 0;
    unsigned long currentMillis = millis();
    
    // Basic rate limiting
    if (currentMillis - lastCheck < 50) {
        delay(10);
        return;
    }
    lastCheck = currentMillis;
    
    // Ensure connections are maintained
    if (!connectWiFi() || !connectMQTT()) {
        delay(100);
        return;
    }
    
    // Handle MQTT loop
    mqttClient.loop();
    
    // Perform BLE scan at intervals
    if (currentMillis - lastScan >= SCAN_INTERVAL) {
        if (pBLEScan) {
            Serial.println("\n-------- Starting BLE Scan --------");
            BLEScanResults* results = pBLEScan->start(timescan, false);
            Serial.printf("Scan Bluetooth %d second\n", timescan);
            Serial.printf("Devices found: %d\n", results->getCount());
            
            if (results) {
                for (int i = 0; i < results->getCount(); i++) {
                    BLEAdvertisedDevice device = results->getDevice(i);
                    
                    if (device.haveServiceUUID()) {
                        Serial.printf("Service UUID: %s\n", device.getServiceUUID().toString().c_str());
                        BLEUUID deviceUUID = device.getServiceUUID();
                        
                        for (uint8_t j = 0; j < 2; j++) {
                            if (deviceUUID.equals(SERVICE_UUIDS[j])) {
                                Serial.printf("*** Matched with Sensor %d! ***\n", j + 1);
                                
                                if (!pClients[j] || !pClients[j]->isConnected()) {
                                    deviceNames[j] = device.getName();
                                    if (connectBLE(j, device.getAddress())) {
                                        Serial.printf("Connected to sensor %d\n", j + 1);
                                        break;
                                    } else {
                                        Serial.printf("Failed to connect to sensor %d\n", j + 1);
                                    }
                                }
                            }
                        }
                    }
                }
                Serial.println("\n-------- Scan Complete --------\n");
                pBLEScan->clearResults();
            }
        }
        lastScan = currentMillis;
    }
    if (currentMillis - lastStatusReport >= STATUS_REPORT_INTERVAL) {
        sendStatusUpdate();
        lastStatusReport = currentMillis;
    }
    
    mqttClient.loop();
}
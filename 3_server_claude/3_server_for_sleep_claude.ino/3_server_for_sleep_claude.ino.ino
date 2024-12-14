#include <BLEDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

// Constants and configurations
#define MAX_SENSORS 2
#define MAX_READINGS 20
#define MIN_READING_VALUE 0.0
#define MAX_READING_VALUE 3.3
#define WDT_TIMEOUT 30  // Watchdog timeout in seconds
#define MQTT_BUFFER_SIZE 512
#define MAX_RETRY_COUNT 5
#define JSON_BUFFER_SIZE 1024

// WiFi and MQTT Configs
const char* WIFI_SSID = "Test_server";
const char* WIFI_PASSWORD = "76543210";
const char* MQTT_SERVER = "49.228.131.61";
const char* MQTT_CLIENT_ID = "ESP32_Gateway";
#define MQTT_PORT 4663
#define MQTT_TOPIC "sensor/data"
#define MQTT_STATUS_TOPIC "sensor/status"
#define SCAN_TIME 10

// BLE configs
static const BLEUUID SERVICE_UUID_1("12345678-1234-1234-1234-123456789ab1");
static const BLEUUID SERVICE_UUID_2("12345678-1234-1234-1234-123456789ab2");
static const BLEUUID CHAR_UUID("abcdabcd-1234-5678-abcd-123456789abc");
static const BLEUUID SERVICE_UUIDS[] = {SERVICE_UUID_1, SERVICE_UUID_2};

// Ring buffer for storing failed MQTT messages
struct MQTTMessage {
    String topic;
    String payload;
    bool used;
};

#define MQTT_BUFFER_COUNT 10
MQTTMessage mqttBuffer[MQTT_BUFFER_COUNT];
int mqttBufferIndex = 0;

// ปรับปรุงโครงสร้างข้อมูล
struct SensorData {
    int measurementCycle;
    int currentChunk;
    int totalChunks;
    float readings[MAX_READINGS];
    String chunks[MAX_READINGS];
    bool chunkReceived[MAX_READINGS];
    bool dataComplete;
    unsigned long lastChunkTime;
    int retryCount;
};

// Global variables
static SensorData sensorDataBuffer[MAX_SENSORS];
static unsigned long lastWiFiRetry = 0;
static unsigned long lastMQTTRetry = 0;
static unsigned long lastScan = 0;
static unsigned long lastStatusReport = 0;
static unsigned long lastDataReceived[MAX_SENSORS] = {0, 0};
static String deviceNames[MAX_SENSORS] = {"", ""};
static bool deviceFound[MAX_SENSORS] = {false, false};
static uint32_t timescan = SCAN_TIME;

// Timing constants
const unsigned long WIFI_RETRY_DELAY = 5000;
const unsigned long MQTT_RETRY_DELAY = 5000;
const unsigned long SCAN_INTERVAL = 1000;
const unsigned long STATUS_REPORT_INTERVAL = 60000;
const unsigned long CONNECT_TIMEOUT = 5000;
const unsigned long CHUNK_TIMEOUT = 10000;

// Global objects
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
static BLEScan* pBLEScan = nullptr;
static BLEClient* pClients[MAX_SENSORS] = {nullptr, nullptr};

// Utility functions
bool isValidReading(float reading) {
    return reading >= MIN_READING_VALUE && reading <= MAX_READING_VALUE;
}

void addToMQTTBuffer(const char* topic, const char* payload) {
    mqttBuffer[mqttBufferIndex].topic = String(topic);
    mqttBuffer[mqttBufferIndex].payload = String(payload);
    mqttBuffer[mqttBufferIndex].used = true;
    mqttBufferIndex = (mqttBufferIndex + 1) % MQTT_BUFFER_COUNT;
}

void processMQTTBuffer() {
    for (int i = 0; i < MQTT_BUFFER_COUNT; i++) {
        if (mqttBuffer[i].used) {
            if (mqttClient.publish(mqttBuffer[i].topic.c_str(), mqttBuffer[i].payload.c_str())) {
                mqttBuffer[i].used = false;
                Serial.println("Successfully published buffered message");
            }
        }
    }
}

void setupOTA() {
    ArduinoOTA.setHostname(MQTT_CLIENT_ID);
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void resetSensorBuffer(uint8_t clientIdx) {
    memset(&sensorDataBuffer[clientIdx], 0, sizeof(SensorData));
    sensorDataBuffer[clientIdx].retryCount = 0;
    Serial.printf("Reset buffer for sensor %d\n", clientIdx + 1);
}

void checkChunkTimeout() {
    unsigned long currentTime = millis();
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (sensorDataBuffer[i].lastChunkTime > 0 && 
            currentTime - sensorDataBuffer[i].lastChunkTime > CHUNK_TIMEOUT) {
            Serial.printf("Chunk timeout for sensor %d\n", i + 1);
            if (++sensorDataBuffer[i].retryCount >= MAX_RETRY_COUNT) {
                resetSensorBuffer(i);
            }
        }
    }
}

void sendStatusUpdate() {
    if (!mqttClient.connected()) {
        Serial.println("MQTT client not connected, buffering status");
        String status = createStatusJson();
        addToMQTTBuffer(MQTT_STATUS_TOPIC, status.c_str());
        return;
    }
    
    String status = createStatusJson();
    if (!mqttClient.publish(MQTT_STATUS_TOPIC, status.c_str())) {
        Serial.println("Failed to publish status, buffering");
        addToMQTTBuffer(MQTT_STATUS_TOPIC, status.c_str());
    }
}

String createStatusJson() {
    char jsonBuffer[JSON_BUFFER_SIZE];
    snprintf(jsonBuffer, JSON_BUFFER_SIZE,
        "{\"gateway_uptime\":\"%lu\","
        "\"wifi_strength\":\"%d\","
        "\"heap_free\":\"%u\","
        "\"devices\":[",
        millis()/1000, WiFi.RSSI(), ESP.getFreeHeap());
    
    String status = String(jsonBuffer);
    
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (i > 0) status += ",";
        status += "{\"sensor_" + String(i+1) + "\":{";
        status += "\"status\":\"" + String(deviceFound[i] ? "found" : "not_found") + "\",";
        status += "\"name\":\"" + deviceNames[i] + "\",";
        status += "\"last_data\":\"" + 
                 (lastDataReceived[i] > 0 ? String((millis() - lastDataReceived[i])/1000) : "never") + 
                 "\"";
        status += "}}";
    }
    status += "]}";
    
    return status;
}

void processCompleteData(uint8_t clientIdx) {
    char jsonBuffer[JSON_BUFFER_SIZE];
    int offset = snprintf(jsonBuffer, JSON_BUFFER_SIZE,
        "{\"device_id\":%d,"
        "\"device_name\":\"%s\","
        "\"measurement_cycle\":%d,"
        "\"readings\":[",
        clientIdx + 1,
        deviceNames[clientIdx].c_str(),
        sensorDataBuffer[clientIdx].measurementCycle);
    
    bool hasValidReadings = false;
    for (int i = 0; i < MAX_READINGS; i++) {
        if (i > 0) {
            offset += snprintf(jsonBuffer + offset, JSON_BUFFER_SIZE - offset, ",");
        }
        if (isValidReading(sensorDataBuffer[clientIdx].readings[i])) {
            offset += snprintf(jsonBuffer + offset, JSON_BUFFER_SIZE - offset, "%.3f",
                             sensorDataBuffer[clientIdx].readings[i]);
            hasValidReadings = true;
        } else {
            offset += snprintf(jsonBuffer + offset, JSON_BUFFER_SIZE - offset, "0");
        }
    }
    
    snprintf(jsonBuffer + offset, JSON_BUFFER_SIZE - offset, "]}");
    
    if (hasValidReadings) {
        String topic = String(MQTT_TOPIC) + "/" + String(clientIdx + 1);
        if (mqttClient.connected()) {
            if (!mqttClient.publish(topic.c_str(), jsonBuffer)) {
                Serial.println("Failed to publish data, buffering");
                addToMQTTBuffer(topic.c_str(), jsonBuffer);
            }
        } else {
            Serial.println("MQTT not connected, buffering data");
            addToMQTTBuffer(topic.c_str(), jsonBuffer);
        }
    } else {
        Serial.println("No valid readings found, skipping MQTT publish");
    }
    
    resetSensorBuffer(clientIdx);
    lastDataReceived[clientIdx] = millis();
}

// [Previous processIncomingData, MyClientCallback, connectBLE functions remain the same]

void setup() {
    Serial.begin(115200);
    
    // Initialize WDT
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };

     esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
    
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
    mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    
    // Initialize sensor data buffers
    for (int i = 0; i < MAX_SENSORS; i++) {
        resetSensorBuffer(i);
    }
    
    // Initialize MQTT message buffer
    memset(mqttBuffer, 0, sizeof(mqttBuffer));
    
    // Setup OTA
    setupOTA();
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
        Serial.print(".");
        timeout--;
    }
    Serial.println();
    
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
    
    // Reset watchdog timer
    esp_task_wdt_reset();
    
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Basic rate limiting
    if (currentMillis - lastCheck < 50) {
        delay(10);
        return;
    }
    lastCheck = currentMillis;
    
    // Check for chunk timeouts
    checkChunkTimeout();
    
    // Ensure connections are maintained
     if (!connectWiFi() || !connectMQTT()) {
        delay(100);
        return;
    }
    
    // Handle MQTT loop and buffered messages
    mqttClient.loop();
    processMQTTBuffer();
    
    // Perform BLE scan at intervals
    if (currentMillis - lastScan >= SCAN_INTERVAL) {
        // [Previous BLE scanning code remains the same]
        lastScan = currentMillis;
    }
    
    // Send periodic status updates
    if (currentMillis - lastStatusReport >= STATUS_REPORT_INTERVAL) {
        sendStatusUpdate();
        lastStatusReport = currentMillis;
    }
    
    mqttClient.loop();
}
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT Configuration
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";
const int mqtt_port = ;
const char* mqtt_user = "";
const char* mqtt_password = "";
const char* mqtt_topic = "";

WiFiClient espClient;
PubSubClient client(espClient);

// Structure for CAN message
typedef struct struct_message {
    unsigned long canId;
    uint8_t len;
    uint8_t data[8];
} struct_message;

struct_message canMessage;

// Storage for all values
struct DataStorage {
    float v1_16[16];    // V1 to V16
    float s1_7[7];      // S1 to S7
    float t1_4[4];      // T1 to T4
    float vt, a, v0, a2;
    bool received[7];   // Track which CAN IDs were received
    unsigned long timestamp[7]; // Track when each data type was received
} storedData;

bool dataComplete = false;
unsigned long lastDataTime = 0;
unsigned long lastPublishTime = 0;
const unsigned long timeout = 10000; // 10 seconds timeout
const unsigned long mqttRetryDelay = 2000; // 2 seconds between MQTT retry attempts
const int mqttMaxRetries = 3; // Maximum MQTT reconnection attempts
const unsigned long dataStalenessThreshold = 5000; // Consider data stale if older than 5 seconds

// Initialize storage
void initStorage() {
    memset(&storedData, 0, sizeof(storedData));
    for(int i = 0; i < 7; i++) {
        storedData.received[i] = false;
        storedData.timestamp[i] = 0;
    }
    dataComplete = false;
    lastDataTime = 0;
}

// Debug function to print current data status
void printDataStatus() {
    Serial.println("Data Status:");
    unsigned long now = millis();
    for (int i = 0; i < 7; i++) {
        Serial.print("CAN ID ");
        Serial.print(i);
        Serial.print(": ");
        if (storedData.received[i]) {
            Serial.print("Received ");
            Serial.print((now - storedData.timestamp[i])/1000.0, 1);
            Serial.println(" seconds ago");
        } else {
            Serial.println("Missing");
        }
    }
}

// Callback function for ESP-NOW data
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len != sizeof(canMessage)) {
        Serial.println("Received incorrect data size");
        return;
    }
    
    memcpy(&canMessage, incomingData, sizeof(canMessage));
    lastDataTime = millis(); // Update last data received time
    
    Serial.print("Received CAN ID: 0x");
    Serial.println(canMessage.canId, HEX);
    
    if (canMessage.canId == 2281734144) { // V1-V4 (0x880001A0)
        storedData.received[0] = true;
        storedData.timestamp[0] = millis();
        for (int i = 0; i < min(8, (int)canMessage.len); i += 2) {
            if (i + 1 < canMessage.len) {
                uint16_t word = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                storedData.v1_16[i/2] = (float)word / 1000.0;
            }
        }
    }
    else if (canMessage.canId == 2281799680) { // V5-V8 (0x88000220)
        storedData.received[1] = true;
        storedData.timestamp[1] = millis();
        for (int i = 0; i < min(8, (int)canMessage.len); i += 2) {
            if (i + 1 < canMessage.len) {
                uint16_t word = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                storedData.v1_16[4 + i/2] = (float)word / 1000.0;
            }
        }
    }
    else if (canMessage.canId == 2281865216) { // V9-V12 (0x880002A0)
        storedData.received[2] = true;
        storedData.timestamp[2] = millis();
        for (int i = 0; i < min(8, (int)canMessage.len); i += 2) {
            if (i + 1 < canMessage.len) {
                uint16_t word = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                storedData.v1_16[8 + i/2] = (float)word / 1000.0;
            }
        }
    }
    else if (canMessage.canId == 2281930752) { // V13-V16 (0x88000320)
        storedData.received[3] = true;
        storedData.timestamp[3] = millis();
        for (int i = 0; i < min(8, (int)canMessage.len); i += 2) {
            if (i + 1 < canMessage.len) {
                uint16_t word = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                storedData.v1_16[12 + i/2] = (float)word / 1000.0;
            }
        }
    }
    else if (canMessage.canId == 2214625280) { // VT,A,V0,A2 (0x84000100)
        storedData.received[4] = true;
        storedData.timestamp[4] = millis();
        for (int i = 0; i < min(8, (int)canMessage.len); i += 2) {
            if (i + 1 < canMessage.len) {
                uint16_t word = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                float value = (i == 0 || i == 4) ? (float)word / 10.0 : ((float)word - 30000.0) / 10.0;
                if (i == 0) storedData.vt = value;
                else if (i == 2) storedData.a = value;
                else if (i == 4) storedData.v0 = value;
                else if (i == 6) storedData.a2 = value;
            }
        }
    }
    else if (canMessage.canId == 2415951872) { // T1-T4 (0x90000100)
        storedData.received[5] = true;
        storedData.timestamp[5] = millis();
        for (int i = 0; i < min(4, (int)canMessage.len); i++) {
            storedData.t1_4[i] = (float)(canMessage.data[i] - 40);
        }
    }
    else if (canMessage.canId == 2214756352) { // S1-S7 (0x84000180)
        storedData.received[6] = true;
        storedData.timestamp[6] = millis();
        for (int i = 0; i < min(7, (int)canMessage.len); i++) {
            if (i == 5 && i + 1 < canMessage.len) {
                storedData.s1_7[i] = (float)((canMessage.data[i] << 8) | canMessage.data[i + 1]) / 160.0;
                i++; // Skip next byte as it's part of S6
            } else {
                storedData.s1_7[i] = (float)canMessage.data[i] / 160.0;
            }
        }
    }

    // Check if all data is received and not stale
    bool allDataFresh = true;
    dataComplete = true;
    unsigned long now = millis();
    
    for(int i = 0; i < 7; i++) {
        if(!storedData.received[i]) {
            dataComplete = false;
            break;
        }
        
        // Check if any data is stale
        if (now - storedData.timestamp[i] > dataStalenessThreshold) {
            allDataFresh = false;
        }
    }
    
    if (dataComplete && allDataFresh) {
        Serial.println("All data received and fresh!");
    } else if (dataComplete) {
        Serial.println("All data received but some may be stale!");
    }
}

// Connect to WiFi
bool connectWiFi() {
    Serial.print("Connecting to WiFi ");
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("\nWiFi connection failed");
        return false;
    }
}

// Connect to MQTT broker
bool connectToMQTT() {
    client.setServer(mqtt_server, mqtt_port);
    
    int retries = 0;
    Serial.print("Connecting to MQTT... ");
    
    // Generate a unique client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    while (!client.connected() && retries < mqttMaxRetries) {
        if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("connected");
            return true;
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying...");
            delay(mqttRetryDelay);
            retries++;
        }
    }
    
    if (!client.connected()) {
        Serial.println("MQTT connection failed after multiple attempts");
        return false;
    }
    
    return true;
}

// Publish data to MQTT with confirmation
bool publishWithConfirmation(const char* topic, const char* payload) {
    Serial.print("Publishing: ");
    Serial.println(payload);
    
    if (!client.connected()) {
        Serial.println("MQTT not connected, attempting to reconnect");
        if (!connectToMQTT()) {
            return false;
        }
    }
    
    bool published = client.publish(topic, payload);
    
    if (published) {
        Serial.println("Published successfully");
    } else {
        Serial.println("Publish failed");
    }
    
    // Allow time for the publish to complete
    client.loop();
    delay(100);
    
    return published;
}

// Publish all data to MQTT
bool publishAllData() {
    bool success = true;
    char payload[256];
    
    // Publish V1-V16 in groups of 4
    for(int i = 0; i < 16; i += 4) {
        sprintf(payload, "{\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f}", 
                i+1, storedData.v1_16[i], i+2, storedData.v1_16[i+1],
                i+3, storedData.v1_16[i+2], i+4, storedData.v1_16[i+3]);
        if (!publishWithConfirmation(mqtt_topic, payload)) {
            success = false;
        }
        delay(250); // Increased delay between publishes
    }

    // Publish S1-S4 and S5-S7 separately
    sprintf(payload, "{\"S1\":%.3f,\"S2\":%.3f,\"S3\":%.3f,\"S4\":%.3f}", 
            storedData.s1_7[0], storedData.s1_7[1], storedData.s1_7[2], storedData.s1_7[3]);
    if (!publishWithConfirmation(mqtt_topic, payload)) {
        success = false;
    }
    delay(250);
    
    sprintf(payload, "{\"S5\":%.3f,\"S6\":%.3f,\"S7\":%.3f}", 
            storedData.s1_7[4], storedData.s1_7[5], storedData.s1_7[6]);
    if (!publishWithConfirmation(mqtt_topic, payload)) {
        success = false;
    }
    delay(250);

    // Publish T1-T4
    sprintf(payload, "{\"T1\":%.0f,\"T2\":%.0f,\"T3\":%.0f,\"T4\":%.0f}", 
            storedData.t1_4[0], storedData.t1_4[1], storedData.t1_4[2], storedData.t1_4[3]);
    if (!publishWithConfirmation(mqtt_topic, payload)) {
        success = false;
    }
    delay(250);

    // Publish VT,A,V0,A2
    sprintf(payload, "{\"VT\":%.3f,\"A\":%.3f,\"V0\":%.3f,\"A2\":%.3f}", 
            storedData.vt, storedData.a, storedData.v0, storedData.a2);
    if (!publishWithConfirmation(mqtt_topic, payload)) {
        success = false;
    }
    
    lastPublishTime = millis();
    return success;
}

// Initialize ESP-NOW
bool initEspNow() {
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }
    
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW Receiver Ready...");
    Serial.print("Receiver MAC Address: ");
    Serial.println(WiFi.macAddress());
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow serial to initialize
    Serial.println("\n\n--- ESP32 ESP-NOW to MQTT Gateway ---");
    
    initStorage();
    
    // Initialize ESP-NOW
    if (!initEspNow()) {
        Serial.println("ESP-NOW initialization failed, restarting...");
        delay(1000);
        ESP.restart();
    }
}

void loop() {
    // Handle ESP-NOW data collection
    if (!dataComplete) {
        // Check if we've been waiting too long for data
        if (millis() - lastDataTime > timeout && lastDataTime != 0) {
            Serial.println("Timeout waiting for complete data");
            printDataStatus(); // Show which data we're missing
            
            // If we have at least some data, try to publish what we have
            bool anyData = false;
            for (int i = 0; i < 7; i++) {
                if (storedData.received[i]) {
                    anyData = true;
                    break;
                }
            }
            
            if (anyData) {
                Serial.println("Publishing partial data before reset");
                // Switch to WiFi mode
                esp_now_deinit();
                
                // Try to publish what we have
                if (connectWiFi() && connectToMQTT()) {
                    publishAllData();
                    client.disconnect();
                }
                WiFi.disconnect(true);
                delay(500);
            }
            
            // Reset and try again
            Serial.println("Resetting ESP32...");
            delay(1000);
            ESP.restart();
        }
        delay(50); // Small delay to prevent CPU hogging
        return;
    }

    // We have complete data, switch to WiFi mode
    Serial.println("\n--- All data received, switching to WiFi mode ---");
    esp_now_deinit();
    
    // Connect to WiFi and MQTT
    bool wifiConnected = connectWiFi();
    if (!wifiConnected) {
        Serial.println("WiFi connection failed, resetting...");
        delay(1000);
        ESP.restart();
        return;
    }
    
    bool mqttConnected = connectToMQTT();
    if (!mqttConnected) {
        Serial.println("MQTT connection failed, resetting...");
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
        return;
    }
    
    // Publish data with retry mechanism
    Serial.println("Publishing all data...");
    bool publishSuccess = publishAllData();
    
    // Clean disconnect
    client.disconnect();
    WiFi.disconnect(true);
    delay(500);
    
    if (publishSuccess) {
        Serial.println("All data published successfully! Restarting to receive new data...");
    } else {
        Serial.println("Some data publishing failed. Restarting to try again...");
    }
    
    // Wait a moment before restarting
    delay(1000);
    
    // Restart to begin receiving again
    ESP.restart();
}
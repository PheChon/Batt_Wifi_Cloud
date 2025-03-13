#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT Configuration
const char* ssid = "ROYPOW_AP";
const char* password = "hack123456789";
const char* mqtt_server = "202.44.12.37";
const int mqtt_port = 1883;
const char* mqtt_user = "student";
const char* mqtt_password = "idealab2024";
const char* mqtt_topic = "Test2";

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
} storedData;

bool dataComplete = false;
unsigned long lastDataTime = 0;
const unsigned long timeout = 10000; // 10 seconds timeout

// Initialize storage
void initStorage() {
    memset(&storedData, 0, sizeof(storedData));
    for(int i = 0; i < 7; i++) storedData.received[i] = false;
}

// Callback function for ESP-NOW data
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    memcpy(&canMessage, incomingData, sizeof(canMessage));
    lastDataTime = millis(); // Update last data received time
    
    Serial.print("Received CAN ID: ");
    Serial.println(canMessage.canId);
    
    if (canMessage.canId == 2281734144) { // V1-V4
        storedData.received[0] = true;
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            storedData.v1_16[i/2] = (float)word / 1000.0;
        }
    }
    else if (canMessage.canId == 2281799680) { // V5-V8
        storedData.received[1] = true;
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            storedData.v1_16[4 + i/2] = (float)word / 1000.0;
        }
    }
    else if (canMessage.canId == 2281865216) { // V9-V12
        storedData.received[2] = true;
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            storedData.v1_16[8 + i/2] = (float)word / 1000.0;
        }
    }
    else if (canMessage.canId == 2281930752) { // V13-V16
        storedData.received[3] = true;
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            storedData.v1_16[12 + i/2] = (float)word / 1000.0;
        }
    }
    else if (canMessage.canId == 2214625280) { // VT,A,V0,A2
        storedData.received[4] = true;
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float value = (i == 0 || i == 4) ? (float)word / 10.0 : ((float)word - 30000.0) / 10.0;
            if (i == 0) storedData.vt = value;
            else if (i == 2) storedData.a = value;
            else if (i == 4) storedData.v0 = value;
            else if (i == 6) storedData.a2 = value;
        }
    }
    else if (canMessage.canId == 2415951872) { // T1-T4
        storedData.received[5] = true;
        for (int i = 0; i < min(4, (int)canMessage.len); i++) {
            storedData.t1_4[i] = (float)(canMessage.data[i] - 40);
        }
    }
    else if (canMessage.canId == 2214756352) { // S1-S7
        storedData.received[6] = true;
        for (int i = 0; i < canMessage.len; i++) {
            float value = (i == 5 && i + 1 < canMessage.len) ? 
                (float)((canMessage.data[i] << 8) | canMessage.data[i + 1]) / 160.0 : 
                (float)canMessage.data[i] / 160.0;
            if (i < 7) storedData.s1_7[i] = value;
            if (i == 5) i++; // Skip next byte as it's part of S6
        }
    }

    // Check if all data is received
    dataComplete = true;
    for(int i = 0; i < 7; i++) {
        if(!storedData.received[i]) {
            dataComplete = false;
            break;
        }
    }
    if (dataComplete) {
        Serial.println("All data received!");
    }
}

// Connect to WiFi and MQTT
void connectMQTT() {
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
    } else {
        Serial.println("\nWiFi connection failed");
        return;
    }

    client.setServer(mqtt_server, mqtt_port);
    startTime = millis();
    while (!client.connected() && millis() - startTime < 5000) {
        if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
            Serial.println("MQTT connected");
        } else {
            delay(500);
        }
    }
    if (!client.connected()) {
        Serial.println("MQTT connection failed");
    }
}

// Publish data to MQTT (4 values at a time)
void publishData() {
    if (!client.connected()) {
        Serial.println("MQTT not connected, skipping publish");
        return;
    }
    
    char payload[256];
    
    // Publish V1-V16 in groups of 4
    for(int i = 0; i < 16; i += 4) {
        sprintf(payload, "{\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f}", 
                i+1, storedData.v1_16[i], i+2, storedData.v1_16[i+1],
                i+3, storedData.v1_16[i+2], i+4, storedData.v1_16[i+3]);
        client.publish(mqtt_topic, payload);
        delay(100);
    }

    // Publish S1-S4 and S5-S7 separately
    sprintf(payload, "{\"S1\":%.3f,\"S2\":%.3f,\"S3\":%.3f,\"S4\":%.3f}", 
            storedData.s1_7[0], storedData.s1_7[1], storedData.s1_7[2], storedData.s1_7[3]);
    client.publish(mqtt_topic, payload);
    delay(100);
    
    sprintf(payload, "{\"S5\":%.3f,\"S6\":%.3f,\"S7\":%.3f}", 
            storedData.s1_7[4], storedData.s1_7[5], storedData.s1_7[6]);
    client.publish(mqtt_topic, payload);
    delay(100);

    // Publish T1-T4
    sprintf(payload, "{\"T1\":%.0f,\"T2\":%.0f,\"T3\":%.0f,\"T4\":%.0f}", 
            storedData.t1_4[0], storedData.t1_4[1], storedData.t1_4[2], storedData.t1_4[3]);
    client.publish(mqtt_topic, payload);
    delay(100);

    // Publish VT,A,V0,A2
    sprintf(payload, "{\"VT\":%.3f,\"A\":%.3f,\"V0\":%.3f,\"A2\":%.3f}", 
            storedData.vt, storedData.a, storedData.v0, storedData.a2);
    client.publish(mqtt_topic, payload);
}

void setup() {
    Serial.begin(115200);
    initStorage();

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        while (1) delay(100);
    }
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW Receiver Ready...");
    Serial.print("Receiver MAC Address: ");
    Serial.println(WiFi.macAddress());
}

void loop() {
    // 1. Stay in ESP-NOW mode until all data is received or timeout occurs
    if (!dataComplete) {
        if (millis() - lastDataTime > timeout && lastDataTime != 0) {
            Serial.println("Timeout waiting for data, proceeding with available data...");
            dataComplete = true; // Proceed with whatever data was received
        }
        delay(100);
        return;
    }

    // 2. All data received (or timeout), switch to WiFi mode
    Serial.println("Switching to WiFi mode...");
    esp_now_deinit();
    WiFi.mode(WIFI_STA);
    
    // 3. Connect to WiFi and MQTT, then publish data
    connectMQTT();
    publishData();
    
    // 4. Disconnect WiFi and MQTT
    if (client.connected()) {
        client.disconnect();
    }
    WiFi.disconnect();
    delay(1000); // Ensure WiFi is fully disconnected
    
    // 5. Reset the board to restart in ESP-NOW mode
    Serial.println("Resetting ESP32 to restart in ESP-NOW mode...");
    delay(2000); // Small delay to allow Serial output to complete
    ESP.restart(); // Reset the board
}
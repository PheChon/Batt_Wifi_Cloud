#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Preferences.h>

// WiFi and MQTT Configuration (dynamically configurable)
char mqtt_server[40] = "";
int mqtt_port = 1883;
char mqtt_user[20] = "";
char mqtt_password[20] = "";
const char* mqtt_topic = "";

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// CAN Message Structure
typedef struct struct_message {
    unsigned long canId;
    uint8_t len;
    uint8_t data[8];
} struct_message;

struct_message canMessage;

// Data Storage Structure
struct DataStorage {
    float v1_16[16];    // V1 to V16
    float s1_7[7];      // S1 to S7
    float t1_4[4];      // T1 to T4
    float vt;           // VT
    float a;            // A
    float v0;           // V0
    float a2;           // A2
    bool received[7];   // Flags for each CAN ID
    unsigned long timestamp[7]; // Timestamps for received data
} storedData;

// State Machine States
enum GatewayState {
    ESPNOW_RECEIVE,     // Receiving ESP-NOW data
    WIFI_CONNECT,       // Connecting to WiFi
    MQTT_CONNECT,       // Connecting to MQTT broker
    PUBLISH_DATA,       // Publishing data to MQTT
    ERROR_STATE         // Error state
};

GatewayState currentState = WIFI_CONNECT;

// Timing and Retry Configuration
const unsigned long INITIAL_TIMEOUT = 20000;      // 20 seconds initial timeout
const unsigned long MAX_TIMEOUT = 30000;          // Maximum timeout
const int MAX_INCOMPLETE_RETRIES = 3;             // Max retries with incomplete data

unsigned long timeout = INITIAL_TIMEOUT;
unsigned long stateStartTime = 0;
unsigned long lastDataTime = 0;
int incompleteDataRetries = 0;

// Initialize Storage
void initStorage() {
    memset(&storedData, 0, sizeof(storedData));
    for (int i = 0; i < 7; i++) {
        storedData.received[i] = false;
        storedData.timestamp[i] = 0;
    }
    lastDataTime = 0;
    incompleteDataRetries = 0;
    timeout = INITIAL_TIMEOUT;
}

// **New Function**: Check if all data is zero
bool isAllDataZero() {
    for (int i = 0; i < 16; i++) {
        if (storedData.v1_16[i] != 0.0) return false;
    }
    for (int i = 0; i < 7; i++) {
        if (storedData.s1_7[i] != 0.0) return false;
    }
    for (int i = 0; i < 4; i++) {
        if (storedData.t1_4[i] != 0.0) return false;
    }
    if (storedData.vt != 0.0 || storedData.a != 0.0 || storedData.v0 != 0.0 || storedData.a2 != 0.0) {
        return false;
    }
    return true;
}

// ESP-NOW Data Receive Callback
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len != sizeof(canMessage)) {
        Serial.println("Received incorrect data size");
        return;
    }

    memcpy(&canMessage, incomingData, sizeof(canMessage));
    lastDataTime = millis();

    Serial.print("Received CAN ID: 0x");
    Serial.print(canMessage.canId, HEX);
    Serial.print(" Length: ");
    Serial.print(canMessage.len);
    Serial.print(" Time: ");
    Serial.println(millis());

    // Handle CAN IDs (example for V1-V4; add others as needed)
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
                i++; // Skip next byte as it’s part of S6
            } else {
                storedData.s1_7[i] = (float)canMessage.data[i] / 160.0;
            }
        }
    }

    // Debug output for received status
    Serial.println("Current Received Status:");
    for (int i = 0; i < 7; i++) {
        Serial.print("ID ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(storedData.received[i] ? "Received" : "Pending");
    }
}

// Handle Data Reception Logic
void handleDataReceive() {
    unsigned long currentTime = millis();

    bool criticalDataReceived = storedData.received[0] && storedData.received[1];
    bool allDataReceived = true;
    for (int i = 0; i < 7; i++) {
        if (!storedData.received[i]) {
            allDataReceived = false;
            break;
        }
    }

    if (currentTime - stateStartTime > timeout) {
        Serial.println("Timeout waiting for complete data");
        if (criticalDataReceived || incompleteDataRetries >= MAX_INCOMPLETE_RETRIES) {
            Serial.println("Switching to WiFi with available data");
            currentState = WIFI_CONNECT;
            stateStartTime = currentTime;
            timeout = min(timeout * 2, MAX_TIMEOUT);
            incompleteDataRetries++;
        } else {
            initStorage();
            stateStartTime = currentTime;
        }
        return;
    }

    if (allDataReceived) {
        Serial.println("All data received. Switching to WiFi mode.");
        currentState = WIFI_CONNECT;
        stateStartTime = currentTime;
    }
}

// Publish Data with Confirmation
bool publishWithConfirmation(const char* topic, const char* payload) {
    if (!client.connected()) {
        Serial.println("MQTT not connected");
        return false;
    }

    Serial.print("Publishing: ");
    Serial.println(payload);
    bool success = client.publish(topic, payload);
    Serial.println(success ? "Published successfully" : "Publish failed");
    client.loop();
    delay(100); // Small delay to ensure message is sent
    return success;
}

// Publish All Stored Data
bool publishAllData() {
    bool success = true;
    char payload[512];

    // Publish V1-V16 in groups of 4
    for (int i = 0; i < 16; i += 4) {
        sprintf(payload, "{\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f,\"V%d\":%.3f}",
                i+1, storedData.v1_16[i], i+2, storedData.v1_16[i+1],
                i+3, storedData.v1_16[i+2], i+4, storedData.v1_16[i+3]);
        if (!publishWithConfirmation(mqtt_topic, payload)) success = false;
        delay(250);
    }

    // Publish S1-S7
    sprintf(payload, "{\"S1\":%.3f,\"S2\":%.3f,\"S3\":%.3f,\"S4\":%.3f,\"S5\":%.3f,\"S6\":%.3f,\"S7\":%.3f}",
            storedData.s1_7[0], storedData.s1_7[1], storedData.s1_7[2],
            storedData.s1_7[3], storedData.s1_7[4], storedData.s1_7[5], storedData.s1_7[6]);
    if (!publishWithConfirmation(mqtt_topic, payload)) success = false;
    delay(250);

    // Publish T1-T4
    sprintf(payload, "{\"T1\":%.3f,\"T2\":%.3f,\"T3\":%.3f,\"T4\":%.3f}",
            storedData.t1_4[0], storedData.t1_4[1], storedData.t1_4[2], storedData.t1_4[3]);
    if (!publishWithConfirmation(mqtt_topic, payload)) success = false;
    delay(250);

    // Publish VT, A, V0, A2
    sprintf(payload, "{\"VT\":%.3f,\"A\":%.3f,\"V0\":%.3f,\"A2\":%.3f}",
            storedData.vt, storedData.a, storedData.v0, storedData.a2);
    if (!publishWithConfirmation(mqtt_topic, payload)) success = false;

    return success;
}

// Switch to WiFi Mode
void switchToWiFiMode() {
    esp_now_deinit();
    WiFi.begin();

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        currentState = MQTT_CONNECT;
    } else {
        Serial.println("\nFailed to connect to WiFi");
        currentState = ERROR_STATE;
    }
    stateStartTime = millis();
}

// Connect to MQTT Broker
void connectToMQTTBroker() {
    client.setServer(mqtt_server, mqtt_port);
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    Serial.print("Connecting to MQTT... ");
    int retries = 0;
    while (!client.connected() && retries < 3) {
        if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
            Serial.println("connected");
            currentState = PUBLISH_DATA;
            stateStartTime = millis();
            return;
        }
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" retrying...");
        delay(2000);
        retries++;
    }
    currentState = ERROR_STATE;
    stateStartTime = millis();
}

// **Modified Function**: Publish Data and Reset
void publishData() {
    if (isAllDataZero()) {
        Serial.println("All data is zero. Skipping publish.");
    } else {
        bool publishSuccess = publishAllData();
        if (!publishSuccess) {
            Serial.println("Failed to publish some data");
        }
    }
    client.disconnect();
    WiFi.disconnect(true);

    initStorage();
    currentState = ESPNOW_RECEIVE;

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error reinitializing ESP-NOW");
        currentState = ERROR_STATE;
    } else {
        esp_now_register_recv_cb(OnDataRecv);
    }

    stateStartTime = millis();
}

// Handle Errors
void handleError() {
    Serial.println("Error state. Attempting recovery...");
    WiFi.disconnect(true);
    client.disconnect();
    delay(1000);

    initStorage();
    currentState = ESPNOW_RECEIVE;
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Persistent ESP-NOW initialization error");
    } else {
        esp_now_register_recv_cb(OnDataRecv);
    }

    stateStartTime = millis();
}

// Setup Function
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32 ESP-NOW to MQTT Gateway");

    // Load saved MQTT parameters
    preferences.begin("mqtt-config", true);
    String savedServer = preferences.getString("server", "");
    if (savedServer != "") {
        strcpy(mqtt_server, savedServer.c_str());
        mqtt_port = preferences.getInt("port", 1883);
        strcpy(mqtt_user, preferences.getString("user", "").c_str());
        strcpy(mqtt_password, preferences.getString("password", "").c_str());
    }
    preferences.end();

    // WiFi configuration with WiFiManager
    if (WiFi.SSID() == "") {
        WiFiManager wifiManager;
        WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
        WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", String(mqtt_port).c_str(), 6);
        WiFiManagerParameter custom_mqtt_user("user", "MQTT Username", mqtt_user, 20);
        WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 20);

        wifiManager.addParameter(&custom_mqtt_server);
        wifiManager.addParameter(&custom_mqtt_port);
        wifiManager.addParameter(&custom_mqtt_user);
        wifiManager.addParameter(&custom_mqtt_password);

        if (!wifiManager.startConfigPortal("ESP_CONFIG", "idealab2024")) {
            Serial.println("Failed to configure WiFi");
            ESP.restart();
        } else {
            preferences.begin("mqtt-config", false);
            preferences.putString("server", custom_mqtt_server.getValue());
            preferences.putInt("port", String(custom_mqtt_port.getValue()).toInt());
            preferences.putString("user", custom_mqtt_user.getValue());
            preferences.putString("password", custom_mqtt_password.getValue());
            preferences.end();
        }
    }

    initStorage();
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        currentState = ERROR_STATE;
    } else {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.println("ESP-NOW Receiver Ready...");
        Serial.print("Receiver MAC Address: ");
        Serial.println(WiFi.macAddress());
    }
    stateStartTime = millis();
}

// Main Loop
void loop() {
    switch (currentState) {
        case ESPNOW_RECEIVE:
            handleDataReceive();
            break;
        case WIFI_CONNECT:
            switchToWiFiMode();
            break;
        case MQTT_CONNECT:
            connectToMQTTBroker();
            break;
        case PUBLISH_DATA:
            publishData();
            break;
        case ERROR_STATE:
            handleError();
            break;
    }
}
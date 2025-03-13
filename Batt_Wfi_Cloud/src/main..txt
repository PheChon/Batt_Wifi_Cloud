#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Structure to hold CAN message data (must match sender's structure)
typedef struct struct_message {
    unsigned long canId;
    uint8_t len;
    uint8_t data[8];
} struct_message;

struct_message canMessage;

// Callback function that executes when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    memcpy(&canMessage, incomingData, sizeof(canMessage));
    
    Serial.print("Received CAN ID: ");
    Serial.print(canMessage.canId);
    Serial.print(" | Converted Values: ");
    
    String payload = "{";

    if (canMessage.canId == 2281734144) {
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float decimalValue = (float)word / 1000.0;
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"V1\": " + String(dec);
            else if (i == 2) payload += ", \"V2\": " + String(dec);
            else if (i == 4) payload += ", \"V3\": " + String(dec);
            else if (i == 6) payload += ", \"V4\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2281799680) {
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float decimalValue = (float)word / 1000.0;
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"V5\": " + String(dec);
            else if (i == 2) payload += ", \"V6\": " + String(dec);
            else if (i == 4) payload += ", \"V7\": " + String(dec);
            else if (i == 6) payload += ", \"V8\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2281865216) {
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float decimalValue = (float)word / 1000.0;
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"V9\": " + String(dec);
            else if (i == 2) payload += ", \"V10\": " + String(dec);
            else if (i == 4) payload += ", \"V11\": " + String(dec);
            else if (i == 6) payload += ", \"V12\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2281930752) {
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float decimalValue = (float)word / 1000.0;
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"V13\": " + String(dec);
            else if (i == 2) payload += ", \"V14\": " + String(dec);
            else if (i == 4) payload += ", \"V15\": " + String(dec);
            else if (i == 6) payload += ", \"V16\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2214625280) {
        for (int i = 0; i < canMessage.len; i += 2) {
            uint16_t word = (canMessage.data[i] << 8) | ((i + 1 < canMessage.len) ? canMessage.data[i + 1] : 0);
            float decimalValue;
            if (i == 0 || i == 4) {
                decimalValue = (float)word / 10.0;
            } else {
                decimalValue = ((float)word - 30000.0) / 10.0;
            }
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"VT\": " + String(dec);
            else if (i == 2) payload += ", \"A\": " + String(dec);
            else if (i == 4) payload += ", \"VO\": " + String(dec);
            else if (i == 6) payload += ", \"A2\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2415951872) {
        for (int i = 0; i < min(4, (int)canMessage.len); i++) {
            int decimalValue = canMessage.data[i] - 40;
            char dec[10];
            sprintf(dec, "%d", decimalValue);
            if (i == 0) payload += "\"T1\": " + String(dec);
            else if (i == 1) payload += ", \"T2\": " + String(dec);
            else if (i == 2) payload += ", \"T3\": " + String(dec);
            else if (i == 3) payload += ", \"T4\": " + String(dec);
        }
    }
    else if (canMessage.canId == 2214756352) {
        for (int i = 0; i < canMessage.len; i++) {
            float decimalValue;
            if (i == 5 && i + 1 < canMessage.len) {
                uint16_t byte56 = (canMessage.data[i] << 8) | canMessage.data[i + 1];
                decimalValue = (float)byte56 / 160.0;
                i++;
            } else {
                decimalValue = (float)canMessage.data[i] / 160.0;
            }
            char dec[10];
            sprintf(dec, "%.3f", decimalValue);
            if (i == 0) payload += "\"S1\": " + String(dec);
            else if (i == 1) payload += ", \"S2\": " + String(dec);
            else if (i == 2) payload += ", \"S3\": " + String(dec);
            else if (i == 3) payload += ", \"S4\": " + String(dec);
            else if (i == 4) payload += ", \"S5\": " + String(dec);
            else if (i == 5) payload += ", \"S6\": " + String(dec);
            else if (i == 6) payload += ", \"S7\": " + String(dec);
        }
    }

    payload += "}";
    Serial.println(payload);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for Serial to initialize
    }
    Serial.println("ESP32 ESP-NOW Receiver with Decimal Conversion");

    WiFi.mode(WIFI_STA);
    delay(100);

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
    delay(10);
}
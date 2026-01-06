#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= WiFi =================
const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// ================= UART =================
#define UART_RX 16
#define UART_TX 17
#define UART_BAUD 9600

// ================= Protocol =================
#define DEV_PUMP     0x01
#define DEV_LIGHT    0x02
#define DEV_FEEDER   0x03
#define DEV_DISPLAY  0x05

#define STATE_OFF    0x00
#define STATE_ON     0x01

#define MAX_RETRY    3
#define ACK_TIMEOUT  300   // ms

// ================= UART SEND =================
void sendUART(uint8_t device, uint8_t state, uint8_t value = 0) {
  uint8_t checksum = (device + state + value) % 256;

  Serial2.print("<");
  Serial2.print(device);
  Serial2.print(",");
  Serial2.print(state);

  if (value > 0) {
    Serial2.print(",");
    Serial2.print(value);
  }

  Serial2.print(",");
  Serial2.print(checksum);
  Serial2.println(">");

  Serial.print("UART TX → <");
  Serial.print(device);
  Serial.print(",");
  Serial.print(state);
  if (value > 0) {
    Serial.print(",");
    Serial.print(value);
  }
  Serial.print(",");
  Serial.print(checksum);
  Serial.println(">");
}

// ================= WAIT FOR ACK =================
bool waitForAck(uint16_t timeoutMs) {
  unsigned long start = millis();
  String response = "";
  bool receiving = false;

  while (millis() - start < timeoutMs) {
    while (Serial2.available()) {
      char c = Serial2.read();

      // Wait for start of frame
      if (!receiving) {
        if (c == '<') {
          response = "<";
          receiving = true;
        }
        continue;
      }

      // Receiving frame
      response += c;

      if (c == '>') {
        receiving = false;

        if (response == "<ACK>") {
          Serial.println("ACK received");
          return true;
        }

        if (response == "<ERR>") {
          Serial.println("ERR received");
          return false;
        }

        // Unknown frame, reset
        response = "";
      }
    }
  }

  Serial.println("ACK timeout");
  return false;
}


// ================= SEND WITH RETRY =================
bool sendWithRetry(uint8_t device, uint8_t state, uint8_t value = 0) {
  for (int i = 0; i < MAX_RETRY; i++) {
    sendUART(device, state, value);
    if (waitForAck(ACK_TIMEOUT)) {
      return true;
    }
    Serial.println("Retrying...");
    delay(100);
  }
  Serial.println("Command failed after retries");
  return false;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX);

  pinMode(13, OUTPUT);

  delay(3000);
  Serial.println("ESP32 STARTED");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");
}

// ================= LOOP =================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin("https://smart-aquarium-web.vercel.app/api/control/latest");
    http.setTimeout(5000);

    int httpCode = http.GET();
    if (httpCode > 0) {

      String payload = http.getString();
      Serial.println("JSON:");
      Serial.println(payload);

      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, payload);

      if (!err) {
        bool hasCommand = doc["has_command"];

        if (hasCommand) {
          const char* type  = doc["command"]["type"];
          const char* value = doc["command"]["value"];

          Serial.print("Type: ");
          Serial.println(type);
          Serial.print("Value: ");
          Serial.println(value);

          // ================= PUMP =================
          if (strcmp(type, "PUMP") == 0) {
            if (strcmp(value, "ON") == 0) {
              sendWithRetry(DEV_PUMP, STATE_ON);
              digitalWrite(13, HIGH);
            } else if (strcmp(value, "OFF") == 0) {
              sendWithRetry(DEV_PUMP, STATE_OFF);
              digitalWrite(13, LOW);
            }
          }

          // ================= FEED =================
          else if (strcmp(type, "FEED") == 0) {
            uint8_t qty = atoi(value); // number of scoops
            sendWithRetry(DEV_FEEDER, STATE_ON, qty);
          }
        } else {
          Serial.println("No command available");
        }
      } else {
        Serial.println("JSON parse failed");
      }
    } else {
      Serial.println("HTTP error");
    }
    http.end();
  }

  delay(2000);
}

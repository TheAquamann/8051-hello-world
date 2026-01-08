#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= WiFi =================
const char* ssid = "auto8888"; // [cite: 1]
const char* password = "auto8888"; // [cite: 1]

// ================= UART =================
#define UART_RX 16 // [cite: 2]
#define UART_TX 17 // [cite: 2]
#define UART_BAUD 9600 // [cite: 2]

// ================= Protocol =================
#define DEV_PUMP     0x01 // [cite: 2]
#define DEV_LIGHT    0x02 // [cite: 2]
#define DEV_FEEDER   0x03 // [cite: 2]
#define DEV_DISPLAY  0x05 // [cite: 2]
#define DEV_TEMP     0x06 // New: Water Temperature device

#define STATE_OFF    0x00 // [cite: 2]
#define STATE_ON     0x01 // [cite: 2]

#define MAX_RETRY    3 // [cite: 2]
#define ACK_TIMEOUT  300 // [cite: 2]

// ================= API UPLOAD =================
void postTemperature(int temp) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://smart-aquarium-web.vercel.app/api/upload");
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<128> doc;
    doc["temperature"] = temp;

    String requestBody;
    serializeJson(doc, requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      Serial.print("Data Uploaded. Response: ");
      Serial.println(http.getString());
    } else {
      Serial.print("Error on uploading data: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

// ================= UART RECEIVE & PROCESS =================
void processIncomingData(String frame) {
  // Remove brackets and split by comma
  frame.replace("<", "");
  frame.replace(">", "");
  
  int values[4]; 
  int count = 0;
  int pos = 0;
  
  while ((pos = frame.indexOf(',')) != -1 && count < 3) {
    values[count++] = frame.substring(0, pos).toInt();
    frame = frame.substring(pos + 1);
  }
  values[count] = frame.toInt(); // Checksum [cite: 5]

  // Validate Checksum: (Dev + State + Val) % 256 [cite: 5]
  uint8_t calculatedSum = (values[0] + values[1] + values[2]) % 256;
  if (calculatedSum == values[3]) {
    if (values[0] == DEV_TEMP) {
      Serial.print("Received Temp from 8051: ");
      Serial.println(values[2]);
      postTemperature(values[2]); // Trigger the API POST
    }
  } else {
    Serial.println("8051 Data Checksum Error");
  }
}

// ================= MODIFIED WAIT FOR ACK =================
bool waitForAck(uint16_t timeoutMs) {
  unsigned long start = millis();
  String response = "";
  bool receiving = false;

  while (millis() - start < timeoutMs) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (!receiving) {
        if (c == '<') { response = "<"; receiving = true; } // [cite: 7]
        continue;
      }
      response += c; // [cite: 9]
      if (c == '>') { // [cite: 10]
        receiving = false;
        if (response == "<ACK>") { // [cite: 11]
          Serial.println("ACK received");
          return true;
        }
        if (response == "<ERR>") { // [cite: 12]
          Serial.println("ERR received");
          return false;
        }
        // Handle custom data frames (e.g., <06,01,26,33>)
        if (response.indexOf(',') != -1) {
          processIncomingData(response);
        }
        response = "";
      }
    }
  }
  return false;
}

// ================= UART SEND =================
void sendUART(uint8_t device, uint8_t state, uint8_t value = 0) {
  uint8_t checksum = (device + state + value) % 256; // [cite: 2]
  Serial2.print("<"); // [cite: 3]
  Serial2.print(device);
  Serial2.print(",");
  Serial2.print(state);

  if (value > 0) {
    Serial2.print(",");
    Serial2.print(value);
  }

  Serial2.print(",");
  Serial2.print(checksum);
  Serial2.print(">"); // [cite: 3]
}

// ================= SEND WITH RETRY =================
bool sendWithRetry(uint8_t device, uint8_t state, uint8_t value = 0) {
  for (int i = 0; i < MAX_RETRY; i++) { // [cite: 15]
    sendUART(device, state, value);
    if (waitForAck(ACK_TIMEOUT)) { // [cite: 16]
      return true;
    }
    delay(100); // [cite: 16]
  }
  return false;
}

void setup() {
  Serial.begin(115200); // [cite: 17]
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX); // [cite: 17]
  pinMode(13, OUTPUT); // [cite: 18]
  pinMode(14, OUTPUT);

  WiFi.begin(ssid, password); // [cite: 18]
  while (WiFi.status() != WL_CONNECTED) { delay(1000); } // [cite: 18]
  Serial.println("WiFi connected"); // [cite: 19]
}

void loop() {
  // Continuously check for spontaneous temperature updates from 8051
  if (Serial2.available()) {
    waitForAck(50); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(13, HIGH);
    HTTPClient http;
    http.begin("https://smart-aquarium-web.vercel.app/api/control/latest"); // [cite: 20]
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, payload);

      if (!err && doc["has_command"]) { // [cite: 21, 22]
        const char* type  = doc["command"]["type"]; // [cite: 22]
        const char* value = doc["command"]["value"]; // [cite: 23]

        if (strcmp(type, "PUMP") == 0) { // [cite: 24]
          if (strcmp(value, "ON") == 0) {
            sendWithRetry(DEV_PUMP, STATE_ON); // [cite: 24]
            digitalWrite(13, HIGH); // [cite: 25]
          } else {
            sendWithRetry(DEV_PUMP, STATE_OFF); // [cite: 25]
            digitalWrite(13, LOW); // [cite: 26]
          }
        } else if (strcmp(type, "FEED") == 0) { // [cite: 26]
          sendWithRetry(DEV_FEEDER, STATE_ON, atoi(value)); // [cite: 27]
        }
      }
    }
    http.end(); // [cite: 30]
  }
  delay(2000); // [cite: 31]
}
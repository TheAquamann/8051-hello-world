#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================= WiFi =================
const char* ssid = "auto8888"; 
const char* password = "auto8888"; 

// ================= UART =================
#define UART_RX 16 
#define UART_TX 17 
#define UART_BAUD 9600 

// ================= Protocol =================
#define DEV_PUMP     0x01 
#define DEV_LIGHT    0x02 
#define DEV_FEEDER   0x03 
//OTW
#define DEV_TEMP     0x05 

#define STATE_OFF    0x00 
#define STATE_ON     0x01 

#define MAX_RETRY    3 
#define ACK_TIMEOUT  300 

// ================= API UPLOAD =================
void postTemperature(int temp) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://smart-aquarium-web.vercel.app/api/sensors/upload");
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<128> doc;
    doc["temperature"] = temp;

    String requestBody;
    serializeJson(doc, requestBody);

    // [DEBUG] Show what we are sending to the cloud
    Serial.print("[DEBUG] API POST Payload: ");
    Serial.println(requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      Serial.print("Data Uploaded. Response: ");
      Serial.println(http.getString());
    } else {
      Serial.print("[ERROR] uploading data code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

// ================= UART RECEIVE & PROCESS =================
void processIncomingData(String frame) {
  Serial.print("[DEBUG] Processing Frame: ");
  Serial.println(frame);

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
  values[count] = frame.toInt(); // Checksum 

  // Validate Checksum: (Dev + State + Val) % 256
  uint8_t calculatedSum = (values[0] + values[1] + values[2]) % 256;
  
  // [DEBUG] Checksum verification
  Serial.printf("[DEBUG] Checksum - Calc: %d, Recv: %d\n", calculatedSum, values[3]);

  if (calculatedSum == values[3]) {
    if (values[0] == DEV_TEMP) {
      Serial.print("Received Temp from 8051: ");
      Serial.println(values[2]);
      postTemperature(values[2]); 
    }
  } else {
    Serial.println("[ERROR] 8051 Data Checksum Mismatch");
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
      
      // [DEBUG] Print every char received (useful to see noise)
      // Serial.print(c); 

      if (!receiving) {
        if (c == '<') { response = "<"; receiving = true; } 
        continue;
      }
      response += c; 
      if (c == '>') { 
        receiving = false;
        
        Serial.print("[DEBUG] RX from 8051: ");
        Serial.println(response);

        if (response == "<ACK>") { 
          Serial.println("[DEBUG] Valid ACK received");
          return true;
        }
        if (response == "<ERR>") { 
          Serial.println("[DEBUG] ERR received from 8051");
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
  uint8_t checksum = (device + state + value) % 256; 
  
  // [DEBUG] Show exactly what is being sent
  Serial.printf("[DEBUG] TX -> 8051: <%d,%d,%d,%d>\n", device, state, value, checksum);

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
  Serial2.print(">"); 
}

// ================= SEND WITH RETRY =================
bool sendWithRetry(uint8_t device, uint8_t state, uint8_t value = 0) {
  for (int i = 0; i < MAX_RETRY; i++) { 
    Serial.printf("[DEBUG] Attempt %d/%d\n", i+1, MAX_RETRY);
    sendUART(device, state, value);
    if (waitForAck(ACK_TIMEOUT)) { 
      return true;
    }
    Serial.println("[DEBUG] Timeout waiting for ACK");
    delay(100); 
  }
  Serial.println("[ERROR] Failed to send command after retries");
  return false;
}

void setup() {
  Serial.begin(115200); 
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX); 
  pinMode(13, OUTPUT); 
  pinMode(14, OUTPUT);
  
  Serial.println("--------------------------------");
  Serial.println("ESP32 Starting...");
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password); 
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  
  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("--------------------------------");
}

void loop() {
  // Continuously check for spontaneous temperature updates from 8051
  if (Serial2.available()) {
    // Serial.println("[DEBUG] Data detected in buffer...");
    waitForAck(50); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(13, HIGH);
    HTTPClient http;
    // Serial.println("[DEBUG] Polling API for commands...");
    
    http.begin("https://smart-aquarium-web.vercel.app/api/control/latest"); 
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      
      // Only print payload if it's NOT just an empty object or common heartbeat
      // Serial.print("[DEBUG] API Response: ");
      // Serial.println(payload);

      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, payload);

      if (err) {
        Serial.print("[ERROR] JSON Deserialization failed: ");
        Serial.println(err.c_str());
      } else if (doc["has_command"]) { 
        
        Serial.println("\n[INFO] Command Detected!");
        const char* type  = doc["command"]["type"]; 
        const char* value = doc["command"]["value"]; 
        
        Serial.printf("[DEBUG] Command Type: %s, Value: %s\n", type, value);

        if (strcmp(type, "PUMP") == 0) { 
          if (strcmp(value, "ON") == 0) {
            if(sendWithRetry(DEV_PUMP, STATE_ON)) digitalWrite(14, HIGH); 
          } else {
            if(sendWithRetry(DEV_PUMP, STATE_OFF)) digitalWrite(14, LOW); 
          }
        } else if (strcmp(type, "FEED") == 0) { 
          sendWithRetry(DEV_FEEDER, STATE_ON, atoi(value)); 
        }
      } else {
         // Use this to confirm we are reaching the API but just have no commands
         Serial.print("."); 
      }
    } else {
      Serial.printf("[ERROR] HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); 
  } else {
    Serial.println("[ERROR] WiFi Disconnected");
    // Attempt reconnection could go here
  }
  
  // Note: Printing . every 2 seconds implies the loop is running
  delay(2000); 
}
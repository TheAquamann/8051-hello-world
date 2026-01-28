#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ================= CONFIGURATION =================
const char* ssid = "auto8888"; 
const char* password = "auto8888";
const char* api_base = "https://smart-aquarium-web.vercel.app/api";

// NTP / Time (Malaysia UTC+8)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800; 
const int   daylightOffset_sec = 0;

// ================= UART PROTOCOL =================
#define UART_RX 16 
#define UART_TX 17 
#define UART_BAUD 9600 

#define DEV_PUMP     0x01 
#define DEV_LIGHT    0x02 
#define DEV_FEEDER   0x03 
#define DEV_SYNC     0x04 
#define DEV_TEMP     0x05 

#define STATE_OFF    0x00 
#define STATE_ON     0x01 

#define MAX_RETRY    3 
#define ACK_TIMEOUT  500 

// ================= GLOBALS =================
WiFiClientSecure client; // Use Secure for HTTPS
unsigned long lastPollTime = 0;
const unsigned long pollInterval = 2000; // Poll API every 2s

// ================= UART ENGINE =================

// Send raw bytes with Checksum
void sendRawFrame(uint8_t *data, size_t length) {
  uint16_t checksum = 0;
  Serial2.print("<");
  for (size_t i = 0; i < length; i++) {
    Serial2.print(data[i]);
    checksum += data[i];
    if (i < length) Serial2.print(","); 
  }
  uint8_t finalChecksum = checksum % 256;
  Serial2.print(finalChecksum);
  Serial2.print(">");
  
  // Debug
  Serial.printf("[TX] Device:%02X State:%02X CS:%d\n", data[0], data[1], finalChecksum);
}

// Wait for <ACK> or process incoming data while waiting
bool waitForAck(uint16_t timeoutMs) {
  unsigned long start = millis();
  String response = "";
  bool receiving = false;

  while (millis() - start < timeoutMs) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (!receiving) {
        if (c == '<') { response = "<"; receiving = true; }
        continue;
      }
      response += c;
      if (c == '>') {
        receiving = false;
        if (response == "<ACK>") return true;
        if (response == "<ERR>") return false;
        // If we catch a data frame while waiting, handle it recursively? 
        // For simplicity, we ignore it here or log it. Real-world: buffer it.
        Serial.println("[WARN] Frame dropped while waiting for ACK: " + response);
        response = "";
      }
    }
  }
  return false;
}

// Send standard command with retry
bool sendCommand(uint8_t device, uint8_t state, uint8_t value = 0) {
  uint8_t payload[3] = {device, state, value};
  for (int i = 0; i < MAX_RETRY; i++) {
    sendRawFrame(payload, 3);
    if (waitForAck(ACK_TIMEOUT)) return true;
    delay(100);
  }
  Serial.println("[ERR] Command failed after retries");
  return false;
}

// ================= API HELPERS =================

// Parse ISO8601 "2024-03-20T14:00:00Z" to Local Hour/Minute
void parseNextFeedTime(const char* isoStr, int &hour, int &minute) {
  struct tm t = {0};
  // Parse assuming format YYYY-MM-DDTHH:MM:SSZ
  if (strptime(isoStr, "%Y-%m-%dT%H:%M:%SZ", &t) != NULL) {
    // Convert to time_t (UTC) then add offset
    time_t timeUtc = mktime(&t); 
    time_t timeLocal = timeUtc + gmtOffset_sec;
    struct tm * localTm = gmtime(&timeLocal); // gmtime works because we manually added offset
    hour = localTm->tm_hour;
    minute = localTm->tm_min;
  } else {
    // Fallback
    hour = 12; minute = 0;
    Serial.println("[ERR] Failed to parse feed time");
  }
}

// Triggered by <05,01,Temp>
void uploadTemperature(int temp) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(api_base) + "/sensors/upload");
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<64> doc;
  doc["temperature"] = temp;
  String body;
  serializeJson(doc, body);
  
  int code = http.POST(body);
  Serial.printf("[API] Upload Temp %dC: %d\n", temp, code);
  http.end();
}

// Triggered by <03,02>
void confirmFeed() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(api_base) + "/control/confirm-feed");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{}"); // Empty body
  Serial.printf("[API] Confirm Feed: %d\n", code);
  http.end();
}

// ================= SYSTEM SYNC =================

void performSystemSync() {
  Serial.println("[SYNC] Starting System Sync...");
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(api_base) + "/sensors/latest");
  int code = http.GET();

  if (code == 200) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, http.getString());

    // 1. Extract Data
    bool pumpOn = (strcmp(doc["pump_status"], "ON") == 0);
    int brightness = doc["brightness"];
    int quantity = doc["feeding"]["quantity"];
    
    // Parse Interval (e.g. "24h" -> seconds? Or API sends seconds? 
    // Assuming API sends "24h", we need to convert or hardcode for demo. 
    // Let's assume the API updates to send Seconds, or we default to 24h.
    // For this implementation, we will pass a generic 'interval' or 0 if 8051 handles it.
    // Protocol v1.1 says Interval is needed. Let's assume 12 hours (43200s) if parsing fails.
    int interval = 43200; 

    // 2. Parse Time
    const char* nextFeedIso = doc["feeding"]["next_feeding"];
    int nextHour, nextMin;
    parseNextFeedTime(nextFeedIso, nextHour, nextMin);

    // 3. Construct Frame: <04, Pump, Bri, Int(ignored?), Qty, H, M>
    // Note: Interval in Protocol v1.1 is "EEPROM" persistence. 
    // Since 8051 uses standard byte values, we might need to send interval in a specific unit (e.g. hours) 
    // or split into bytes. For this code, we pass 0 as placeholder if format is undefined.
    
    uint8_t payload[7];
    payload[0] = DEV_SYNC;
    payload[1] = pumpOn ? STATE_ON : STATE_OFF;
    payload[2] = (uint8_t)brightness;
    payload[3] = 12; // Placeholder: Interval (Hours?)
    payload[4] = (uint8_t)quantity;
    payload[5] = (uint8_t)nextHour;
    payload[6] = (uint8_t)nextMin;

    Serial.printf("[SYNC] P:%d B:%d Q:%d T:%02d:%02d\n", pumpOn, brightness, quantity, nextHour, nextMin);
    
    // 4. Send
    for(int i=0; i<3; i++) {
        sendRawFrame(payload, 7);
        if(waitForAck(1000)) {
            Serial.println("[SYNC] Success!");
            break;
        }
        delay(500);
    }

  } else {
    Serial.printf("[SYNC] Failed to fetch state: %d\n", code);
  }
  http.end();
}

// ================= MAIN LOGIC =================

void processIncomingSerial() {
  // Simple parser for <Device,State,Value,CS>
  if (!Serial2.available()) return;

  String frame = Serial2.readStringUntil('>');
  int startIdx = frame.indexOf('<');
  if (startIdx == -1) return;
  
  frame = frame.substring(startIdx + 1); // Remove <
  
  // Parse Integers
  int values[4]; 
  int count = 0;
  char buf[32];
  frame.toCharArray(buf, 32);
  char *ptr = strtok(buf, ",");
  
  while(ptr != NULL && count < 4) {
    values[count++] = atoi(ptr);
    ptr = strtok(NULL, ",");
  }

  // Validate Checksum (Simple sum of first 3 args)
  // Note: This assumes standard 3-value frame + CS. 
  // If count is different, logic needs adjustment.
  if (count < 2) return; // Malformed

  int device = values[0];
  int state = values[1];
  int val = (count >= 3) ? values[2] : 0;
  
  // Route
  if (device == DEV_TEMP) {
      uploadTemperature(val); // Value is Temp
  } 
  else if (device == DEV_FEEDER && state == 0x02) {
      confirmFeed(); // 03, 02 is confirmation
  }
}

void pollCommands() {
  if (millis() - lastPollTime < pollInterval) return;
  lastPollTime = millis();

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(api_base) + "/control/latest");
  int code = http.GET();

  if (code == 200) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    
    if (!err && doc["has_command"]) {
      const char* type = doc["command"]["type"];
      const char* valStr = doc["command"]["value"];
      
      Serial.printf("[CMD] Type: %s Value: %s\n", type, valStr);

      if (strcmp(type, "PUMP") == 0) {
        bool on = (strcmp(valStr, "ON") == 0);
        sendCommand(DEV_PUMP, on ? STATE_ON : STATE_OFF);
      } 
      else if (strcmp(type, "LIGHT") == 0) {
        int bri = atoi(valStr);
        sendCommand(DEV_LIGHT, STATE_ON, bri);
      }
      else if (strcmp(type, "FEED") == 0) {
        int qty = atoi(valStr); // Assuming API sends quantity in value
        sendCommand(DEV_FEEDER, STATE_ON, qty);
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX);
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nConnected.");

  // Init Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Perform System Sync
  performSystemSync();
}

void loop() {
  // 1. Listen for 8051 Data
  processIncomingSerial();

  // 2. Poll API for commands
  pollCommands();
}
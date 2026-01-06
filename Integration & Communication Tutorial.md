# **Smart Aquarium System: Integration & Communication Tutorial**

## **Part 1: The Core Concepts**

### **1. What is UART?**

**UART (Universal Asynchronous Receiver-Transmitter)** is the "phone line" connecting our two main brains: the ESP32 (Cloud Handler) and the 8051 (Hardware Controller).

- **How it works:** It uses two wires. One to speak (**TX** - Transmit) and one to listen (**RX** - Receive).
- **The Golden Rule:** They must share a common **Ground (GND)** wire, or they won't agree on what "0" and "1" voltages are.
- **Speed:** We use a baud rate of **9600**. If one speaks fast (115200) and the other listens slow (9600), the message will be garbage.

### **2. Our Custom Protocol (The Language)**

UART transmits raw text. To prevent the 8051 from getting confused by random noise, we designed a specific grammar based on the provided documentation1.



**Frame Structure:** `<DeviceID, State, Value, Checksum>`

- **Start (`<`):** "Attention 8051, a message is starting."
- **Body:** The actual instructions.
- **Checksum:** A math problem to prove the message didn't get corrupted.
- **End (`>`):** "Message over."

Example:

To turn the PUMP (ID 01) to ON (State 01):

1. **ESP32 Sends:** `<01,01,02>`
2. **8051 Checks Math:** Adds $01 + 01 = 02$. Since the calculated sum (2) matches the received checksum (2), the command is valid.

------

## **Part 2: 8051 Simulation Tutorials (Proteus)**

### **Tutorial 1: Testing UART Basic Echo**

*Goal: Prove the 8051 can hear and speak.*

1. **Proteus Setup:**
   - Place an **AT89C51**.
   - Place two **Virtual Terminal** (Instruments Mode).
   - One connect **P3.1 (TXD)** to Terminal **RXD**.
   - Another one connect **P3.0 (RXD)** to Terminal **TXD**.
   - Add an **11.0592 MHz** crystal oscillator to pins 18 & 19 (crucial for accurate baud rate).
2. **Code (Concept):**
   - Configure `TMOD` for Timer 1 Mode 2 (Auto-reload).
   - Set `TH1` to `0xFD` (9600 baud).
   - Loop: Wait for data (`RI` bit), read it, then send it back (`SBUF = SBUF`).
3. **Test:** Run simulation. Right click in the Virtual Terminal that connects to 8051 RxD, select `Type Echo`. Type "Hello" in the terminal. If "Hello" appears, UART is working.

### **Tutorial 2: Implementing Our Protocol**

*Goal: Teach 8051 to filter data using the `<...>` format.*

Code and simulation is uploaded in this repo: `TheAquamann/8051-hello-world/tree/main/8051UART/`

1. **Code Logic:**
   - Create a state machine: `WAIT_START` $\rightarrow$ `READ_DATA` $\rightarrow$ `PROCESS`.
   - **Step 1:** Ignore everything until you receive `<`.
   - **Step 2:** Store incoming characters into an array `buffer[]`.
   - **Step 3:** When `>` is received, stop recording.
   - **Step 4:** Parse the buffer. If `buffer[0]` is `'1'` (Pump) and `buffer[2]` is `'1'` (On), set **P1.0 to HIGH**.
2. **Test:** In Virtual Terminal, type `<01,01,02>`. The LED on P1.0 should light up.

------

## **Part 3: ESP32 & Cloud Tutorials**

### **3. What is an API?**

The **API (Application Programming Interface)** is the messenger between the ESP32 and our Database.

- **The Problem:** ESP32 cannot talk directly to the database.

- **The Solution:** ESP32 asks the API "Is there a new command?" (GET request). The API checks the database and replies "Yes, turn on the pump" (JSON response)2.

  

### **4. The Data Flow**

1. **User:** Clicks "Feed" on the website.
2. **Cloud:** Updates database `command` table.
3. **ESP32:** Polls API `/latest` endpoint3.
4. **ESP32:** Receives JSON: `{"type": "FEED", "value": "1"}`.
5. **ESP32:** Converts to UART: `<03,01,1,5>` and sends it to 8051.
6. **8051:** Receives frame, validates checksum, runs motor, and replies `<ACK>`.

### **Tutorial 2: Connect ESP32 to Wi-Fi**

*Goal: Get the ESP32 connected to the internet.*

In Arduino IDE,

```
#include <WiFi.h>
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASS";

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Connected!");
}
```

*Verify: Check Serial Monitor for "Connected!".*

### **Tutorial 3: Connect ESP32 to API**

*Goal: Fetch raw data from the server.*

```
#include <HTTPClient.h>

// Inside loop():
if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://smart-aquarium-web.vercel.app/api/control/latest"); // 
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println(payload); // Prints: {"has_command":true, ...}
    }
    http.end();
}
delay(2000); // Don't spam the server!
```

### **Tutorial 4: Parsing JSON & Formatting**

*Goal: Read the JSON and extract specific instructions.*

1. **Install Library:** ArduinoJson by Benoit Blanchon.
2. **Code Update:**

```
#include <ArduinoJson.h>

// Inside loop after getting payload:
StaticJsonDocument<512> doc;
deserializeJson(doc, payload);

bool hasCommand = doc["has_command"]; // [cite: 22]
if (hasCommand) {
    const char* type = doc["command"]["type"];
    const char* value = doc["command"]["value"];
    
    Serial.print("Command Received: ");
    Serial.println(type); // Prints "PUMP"
}
```

------

## **Part 4: Final Hardware Integration**

### **Tutorial 5: Breadboard Test (ESP32 + 8051)**

*Goal: Control the 8051 hardware via the Web Dashboard.*

**Steps:**

1. **Code ESP32:** Upload the provided `esp32_sample_code.ino`. It is already set up to read the API and send `Serial2` messages 
2. **Code 8051:** Upload the code from *Tutorial 2* (Protocol Receiver).
3. **Power Up:** Reset both boards.
4. **Action:**
   - Open the Web App.
   - Click "Pump ON".
5. **Observation:**
   - ESP32 Serial Monitor: `UART TX -> <01,01,02>`
   - 8051: LED/Motor turns ON.
   - ESP32 Serial Monitor: `ACK received`
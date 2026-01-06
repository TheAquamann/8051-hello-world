#include <reg52.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- HARDWARE MAPPING ---
// P2 LEDs used to indicate which device is active
sbit LED_PUMP   = P2^0;
sbit LED_LIGHT  = P2^1;
sbit LED_FEEDER = P2^2;
sbit LED_DISP   = P2^3;

// --- VARIABLES ---
char rx_buffer[40];  // Buffer to store incoming frame
unsigned char rx_index = 0;
bit frame_received = 0;

// --- FUNCTION PROTOTYPES ---
void UART_Init();
void UART_TxChar(char dat);
void UART_SendString(char *str);
void DelayMs(unsigned int ms);
void Process_Frame();
int Parse_Int(char **str); // Helper to grab next number from string

void main() {
    UART_Init();
    
    // Welcome Message
    UART_SendString("SYSTEM READY. Type frame like: <01,01,02>\r\n");
    
    // Turn off LEDs initially (assuming 1=ON, 0=OFF for your setup)
    P2 = 0xFF; 

    while(1) {
        if(frame_received) {
            Process_Frame();
            frame_received = 0; // Reset flag
            rx_index = 0;       // Reset buffer
        }
    }
}

// --- UART INTERRUPT (Receives Data) ---
void Serial_ISR() interrupt 4 {
    char c;
    if(RI) {
        c = SBUF;
        RI = 0;
        
        // Protocol: Frame starts with '<' and ends with '>'
        if(c == '<') {
            rx_index = 0; // Start recording
        }
        else if(c == '>') {
            rx_buffer[rx_index] = '\0'; // Null-terminate string
            frame_received = 1;         // Signal main loop
        }
        else {
            if(rx_index < 39) {
                rx_buffer[rx_index++] = c; // Add char to buffer
            }
        }
    }
}

// --- COMMAND PROCESSOR ---
void Process_Frame() {
    // Buffer now contains "01,01,02" (without < or >)
    char *ptr = rx_buffer;
    int deviceCode, stateCode, val1, val2, val3, checksum_received;
    int calculated_sum = 0;
    
    // 1. Parse Device Code
    deviceCode = Parse_Int(&ptr);
    if(deviceCode == -1) return; // Error
    
    // 2. Parse State Code
    stateCode = Parse_Int(&ptr);
    
    // 3. Parse Optional Values based on Device Code
    // Note: This logic simplifies parsing by attempting to read remaining values
    // In a strict implementation, you'd switch(deviceCode) first.
    
    // Initialize defaults
    val1 = 0; val2 = 0; val3 = 0;
    
    // We calculate checksum as we go. 
    // The protocol says Checksum is the LAST field.
    // We need to read until we hit the end.
    
    // Simplified Test Parser:
    // We assume the user types exactly what the example frames show.
    
    // --- PARSING LOGIC SPECIFIC TO EXAMPLES ---
    
    if (deviceCode == 1) { // PUMP: <01,01,CHK>
        checksum_received = Parse_Int(&ptr); // The 3rd number is checksum
        calculated_sum = (deviceCode + stateCode) % 256;
        
        if(calculated_sum == checksum_received) {
            LED_PUMP = (stateCode == 1) ? 0 : 1; // 1=ON, 0=OFF
            UART_SendString("ACK: Pump Updated\r\n");
						UART_SendString("<ACK>");
        } else goto CHECKSUM_ERR;
    }
    
    else if (deviceCode == 2) { // LIGHT: <02,01,VAL,CHK>
        val1 = Parse_Int(&ptr); // Brightness
        checksum_received = Parse_Int(&ptr);
        
        calculated_sum = (deviceCode + stateCode + val1) % 256;
        
        if(calculated_sum == checksum_received) {
            LED_LIGHT = (stateCode == 1);
            UART_SendString("ACK: Light Set. Brightness: ");
            // (Print brightness code omitted for brevity in pure 8051, sending static text)
            UART_SendString("UPDATED\r\n"); 
						UART_SendString("<ACK>");
        } else goto CHECKSUM_ERR;
    }
    
    else if (deviceCode == 3) { // FEEDER: <03,01,QTY,CHK>
        val1 = Parse_Int(&ptr); // Qty
        checksum_received = Parse_Int(&ptr);
        
        calculated_sum = (deviceCode + stateCode + val1) % 256;
        
        if(calculated_sum == checksum_received) {
            LED_FEEDER = 1; 
            DelayMs(500); // Simulate feeding
            LED_FEEDER = 0;
            UART_SendString("ACK: Feeding Done\r\n");
						UART_SendString("<ACK>");
        } else goto CHECKSUM_ERR;
    }
    
    else if (deviceCode == 5) { // DISPLAY: <05,01,HR,MIN,QTY,CHK>
        val1 = Parse_Int(&ptr); // Hour
        val2 = Parse_Int(&ptr); // Min
        val3 = Parse_Int(&ptr); // Qty
        checksum_received = Parse_Int(&ptr);
        
        calculated_sum = (deviceCode + stateCode + val1 + val2 + val3) % 256;
        
        if(calculated_sum == checksum_received) {
            LED_DISP = !LED_DISP; // Toggle to show update
            UART_SendString("ACK: Display Schedule Updated\r\n");
						UART_SendString("<ACK>");
        } else goto CHECKSUM_ERR;
    }
    
    else {
        UART_SendString("ERR: Unknown Device\r\n");
				UART_SendString("<ERR>");
    }
    return;

CHECKSUM_ERR:
    UART_SendString("ERR: Checksum Fail\r\n");
		UART_SendString("<ERR>");
}

// --- HELPER FUNCTIONS ---

// Parses the next integer from the comma-separated string
// Updates the pointer to the next position
int Parse_Int(char **str) {
    int val = 0;
    char *s = *str;
    
    if(*s == '\0') return -1; // End of string
    
    // Skip commas
    while(*s == ',') s++;
    
    // Read digits
    while(*s >= '0' && *s <= '9') {
        val = (val * 10) + (*s - '0');
        s++;
    }
    
    *str = s; // Update original pointer
    return val;
}

void UART_Init() {
    TMOD = 0x20;    // Timer 1 Mode 2
    TH1 = 0xFD;     // 9600 Baud @ 11.0592MHz
    SCON = 0x50;    // Mode 1, REN Enabled
    EA = 1;         // Enable Global Interrupts
    ES = 1;         // Enable Serial Interrupts
    TR1 = 1;        // Start Timer 1
}

void UART_TxChar(char dat) {
    SBUF = dat;
    while(!TI);
    TI = 0;
}

void UART_SendString(char *str) {
    while(*str) {
        UART_TxChar(*str++);
    }
}

void DelayMs(unsigned int ms) {
    unsigned int i, j;
    for(i=0; i<ms; i++)
        for(j=0; j<120; j++);
}
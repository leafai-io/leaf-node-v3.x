#include "RS485.h"
#include "../LeafNodeTypes.h"

RS485::RS485(int deRePin, unsigned long responseTimeout) 
    : deRePin_(deRePin), responseTimeout_(responseTimeout), baudRate_(9600), initialized_(false) {
}

void RS485::begin(unsigned long baudRate) {
    baudRate_ = baudRate;
    
    // Configure direction control pin
    pinMode(deRePin_, OUTPUT);
    setTransmitMode(false); // Start in receive mode
    
    // Initialize Serial1 (RX=14, TX=13) with configurable baud rate
    Serial1.begin(baudRate_, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    
    initialized_ = true;
    
    Serial.println("[RS485] Initialized - RX=" + String(RS485_RX_PIN) + 
                   ", TX=" + String(RS485_TX_PIN) + 
                   ", DE/RE=" + String(deRePin_) +
                   ", Baud=" + String(baudRate_));
}

void RS485::setBaudRate(unsigned long baudRate) {
    if (baudRate_ != baudRate) {
        baudRate_ = baudRate;
        if (initialized_) {
            // Re-initialize Serial1 with new baud rate
            Serial1.end();
            Serial1.begin(baudRate_, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
            Serial.println("[RS485] Baud rate changed to " + String(baudRate_));
        }
    }
}

bool RS485::sendCommand(const byte* command, size_t commandLength, 
                       byte* response, size_t maxResponseLength, size_t& responseLength) {
    if (!initialized_) {
        Serial.println("[RS485] ERROR: Not initialized");
        return false;
    }
    
    if (!command || commandLength == 0 || !response || maxResponseLength == 0) {
        Serial.println("[RS485] ERROR: Invalid parameters");
        return false;
    }
    
    // Clear any pending data
    clearReceiveBuffer();
    
    // DEBUG: Print what we're sending
    Serial.print("[RS485] SENDING: ");
    for (size_t i = 0; i < commandLength; i++) {
        Serial.printf("0x%02X ", command[i]);
    }
    Serial.println();
    
    // EXACT timing from working code
    setTransmitMode(true);
    delay(2);
    
    Serial1.write(command, commandLength);
    Serial1.flush();
    
    setTransmitMode(false);
    delay(200); // Your exact timing
    
    // Wait for response
    responseLength = 0;
    unsigned long startTime = millis();
    
    while (millis() - startTime < responseTimeout_) {
        if (Serial1.available()) {
            if (responseLength < maxResponseLength) {
                response[responseLength] = Serial1.read();
                responseLength++;
            } else {
                // Buffer full, discard additional data
                Serial1.read();
            }
            // Reset timeout on each received byte
            startTime = millis();
        }
        delay(1);
    }
    
    // DEBUG: Print what we received
    Serial.print("[RS485] RECEIVED: ");
    for (size_t i = 0; i < responseLength; i++) {
        Serial.printf("0x%02X ", response[i]);
    }
    Serial.println(" (Length: " + String(responseLength) + ")");
    
    return responseLength > 0;
}

void RS485::clearReceiveBuffer() {
    while (Serial1.available()) {
        Serial1.read();
    }
}

void RS485::setTransmitMode(bool transmit) {
    digitalWrite(deRePin_, transmit ? HIGH : LOW);
}

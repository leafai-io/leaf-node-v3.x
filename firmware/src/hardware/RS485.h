#pragma once

#include <Arduino.h>
#include "../LeafNodeTypes.h"

/**
 * @brief RS485 Communication Manager
 * 
 * Handles RS485 serial communication with proper direction control
 * for half-duplex communication protocols.
 */
class RS485 {
public:
    /**
     * @brief Constructor
     * @param deRePin Direction control pin (DE/RE combined)
     * @param responseTimeout Timeout for response waiting in milliseconds
     */
    RS485(int deRePin, unsigned long responseTimeout = 500);
    
    /**
     * @brief Initialize RS485 communication
     * Sets up Serial1 (RX=14, TX=13) with configurable baud rate, 8N1
     * @param baudRate Baud rate for communication (default: 9600)
     */
    void begin(unsigned long baudRate = 9600);
    
    /**
     * @brief Change baud rate for RS485 communication
     * @param baudRate New baud rate
     */
    void setBaudRate(unsigned long baudRate);
    
    /**
     * @brief Get current baud rate
     * @return Current baud rate
     */
    unsigned long getBaudRate() const { return baudRate_; }
    
    /**
     * @brief Send command and wait for response
     * @param command Command bytes to send
     * @param commandLength Length of command
     * @param response Buffer for response
     * @param maxResponseLength Maximum response buffer size
     * @param responseLength Actual received response length
     * @return true if response received, false if timeout
     */
    bool sendCommand(const byte* command, size_t commandLength, 
                    byte* response, size_t maxResponseLength, size_t& responseLength);
    
    /**
     * @brief Check if RS485 is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief Set response timeout
     * @param timeout Timeout in milliseconds
     */
    void setResponseTimeout(unsigned long timeout) { responseTimeout_ = timeout; }
    
    /**
     * @brief Get current response timeout
     * @return Timeout in milliseconds
     */
    unsigned long getResponseTimeout() const { return responseTimeout_; }

private:
    int deRePin_;                    // Direction control pin
    unsigned long responseTimeout_;  // Response timeout in ms
    unsigned long baudRate_;         // Baud rate for RS485 communication
    bool initialized_;               // Initialization flag
    
    /**
     * @brief Clear receive buffer
     */
    void clearReceiveBuffer();
    
    /**
     * @brief Set transmission mode (HIGH = transmit, LOW = receive)
     * @param transmit true for transmit mode, false for receive mode
     */
    void setTransmitMode(bool transmit);
};

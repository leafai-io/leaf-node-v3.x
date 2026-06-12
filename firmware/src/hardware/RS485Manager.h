#pragma once

#include <Arduino.h>
#include "RS485.h"
#include "../diagnostics/Logger.h"
#include "../LeafNodeTypes.h"

/**
 * @brief RS485 Manager - High-level RS485 communication management
 * 
 * Provides a higher-level interface for RS485 communication with
 * logging, error handling, and protocol abstractions.
 */
class RS485Manager {
public:
    /**
     * @brief Constructor
     * @param logger Logger instance for debug output
     */
    RS485Manager(Logger& logger);
    
    /**
     * @brief Destructor
     */
    ~RS485Manager();
    
    /**
     * @brief Initialize RS485 manager
     * @param deRePin Direction control pin (DE/RE combined)
     * @param responseTimeout Response timeout in milliseconds
     * @param baudRate Baud rate for RS485 communication (default: 9600)
     * @return true if initialization successful
     */
    bool initialize(int deRePin = 16, unsigned long responseTimeout = 500, unsigned long baudRate = 9600);
    
    /**
     * @brief Change baud rate for RS485 communication
     * @param baudRate New baud rate
     */
    void setBaudRate(unsigned long baudRate);
    
    /**
     * @brief Get current baud rate
     * @return Current baud rate (0 if not initialized)
     */
    unsigned long getBaudRate() const;
    
    /**
     * @brief Check if RS485 is initialized and ready
     * @return true if ready for communication
     */
    bool isReady() const;
    
    /**
     * @brief Shutdown RS485 manager and release resources
     * Cleans up pins, serial ports, and frees memory
     */
    void shutdown();
    
    /**
     * @brief Send raw command and receive response
     * @param command Command bytes to send
     * @param commandLength Length of command
     * @param response Buffer for response
     * @param maxResponseLength Maximum response buffer size
     * @param responseLength Actual received response length
     * @return true if response received, false if timeout or error
     */
    bool sendRawCommand(const byte* command, size_t commandLength, 
                       byte* response, size_t maxResponseLength, size_t& responseLength);
    
    /**
     * @brief Send command with automatic retry
     * @param command Command bytes to send
     * @param commandLength Length of command
     * @param response Buffer for response
     * @param maxResponseLength Maximum response buffer size
     * @param responseLength Actual received response length
     * @param maxRetries Maximum number of retries (default: 3)
     * @return true if response received, false if all retries failed
     */
    bool sendCommandWithRetry(const byte* command, size_t commandLength, 
                             byte* response, size_t maxResponseLength, size_t& responseLength,
                             int maxRetries = 3);
    
    /**
     * @brief Set response timeout
     * @param timeout Timeout in milliseconds
     */
    void setResponseTimeout(unsigned long timeout);
    
    /**
     * @brief Get current response timeout
     * @return Timeout in milliseconds
     */
    unsigned long getResponseTimeout() const;
    
    /**
     * @brief Get communication statistics
     */
    struct Statistics {
        unsigned long totalCommands;
        unsigned long successfulCommands;
        unsigned long failedCommands;
        unsigned long timeouts;
        unsigned long retries;
    };
    
    /**
     * @brief Get communication statistics
     * @return Current statistics
     */
    const Statistics& getStatistics() const { return stats_; }
    
    /**
     * @brief Reset communication statistics
     */
    void resetStatistics();

private:
    Logger& logger_;
    RS485* rs485_;
    bool initialized_;
    Statistics stats_;
    
    /**
     * @brief Log communication attempt
     * @param command Command being sent
     * @param commandLength Length of command
     * @param attempt Attempt number (1-based)
     */
    void logCommunicationAttempt(const byte* command, size_t commandLength, int attempt);
    
    /**
     * @brief Log communication result
     * @param success Whether communication was successful
     * @param responseLength Length of received response
     */
    void logCommunicationResult(bool success, size_t responseLength);
};

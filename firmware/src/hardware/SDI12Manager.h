#pragma once

#include <Arduino.h>
#include <SDI12.h>
#include "../diagnostics/Logger.h"

/**
 * @brief SDI-12 Manager - High-level SDI-12 communication management
 * 
 * Provides a higher-level interface for SDI-12 communication with
 * logging, error handling, and protocol abstractions. Supports
 * multiple SDI-12 sensors on a single bus through addressing.
 */
class SDI12Manager {
public:
    /**
     * @brief Constructor
     * @param logger Logger instance for debug output
     */
    SDI12Manager(Logger& logger);
    
    /**
     * @brief Destructor
     */
    ~SDI12Manager();
    
    /**
     * @brief Initialize SDI-12 manager
     * @param dataPin SDI-12 data pin (single wire bidirectional)
     * @return true if initialization successful
     */
    bool initialize(int dataPin);
    
    /**
     * @brief Check if SDI-12 is initialized and ready
     * @return true if ready for communication
     */
    bool isReady() const;
    
    /**
     * @brief Shutdown SDI-12 manager and release resources
     * Cleans up pins, ISR service, and frees memory
     */
    void shutdown();
    
    /**
     * @brief Send identification command to sensor
     * @param address Sensor address (0-9, a-z, A-Z)
     * @param response Response string from sensor
     * @return true if response received
     */
    bool sendIdentification(char address, String& response);
    
    /**
     * @brief Start concurrent measurement
     * @param address Sensor address
     * @param waitTime Time to wait for measurement completion (seconds)
     * @param numValues Number of values to be returned
     * @return true if measurement started successfully
     */
    bool startMeasurement(char address, int& waitTime, int& numValues);
    
    /**
     * @brief Start standard measurement
     * @param address Sensor address
     * @param waitTime Time to wait for measurement completion (seconds)
     * @param numValues Number of values to be returned
     * @return true if measurement started successfully
     */
    bool startStandardMeasurement(char address, int& waitTime, int& numValues);
    
    /**
     * @brief Get measurement data
     * @param address Sensor address
     * @param valueIndex Value index to retrieve (0-9)
     * @param values Array to store retrieved values
     * @param maxValues Maximum number of values to retrieve
     * @param numValues Actual number of values retrieved
     * @return true if data retrieved successfully
     */
    bool getData(char address, int valueIndex, float* values, int maxValues, int& numValues);
    
    /**
     * @brief Send raw SDI-12 command
     * @param command Full SDI-12 command string (including address and !)
     * @param response Response string from sensor
     * @param timeout Response timeout in milliseconds (default: 300ms)
     * @return true if response received
     */
    bool sendCommand(const String& command, String& response, unsigned long timeout = 300);
    
    /**
     * @brief Verify sensor is active on bus
     * @param address Sensor address to verify
     * @return true if sensor responds
     */
    bool verifySensor(char address);
    
    /**
     * @brief Change sensor address
     * @param oldAddress Current sensor address
     * @param newAddress New sensor address
     * @return true if address changed successfully
     */
    bool changeAddress(char oldAddress, char newAddress);
    
    /**
     * @brief Get communication statistics
     */
    struct Statistics {
        unsigned long totalCommands;
        unsigned long successfulCommands;
        unsigned long failedCommands;
        unsigned long timeouts;
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
    SDI12* sdi12_;
    int dataPin_;
    bool initialized_;
    Statistics stats_;
    
    /**
     * @brief Parse measurement response (aM! or aC!)
     * @param response Response string from sensor
     * @param waitTime Parsed wait time
     * @param numValues Parsed number of values
     * @return true if parsing successful
     */
    bool parseMeasurementResponse(const String& response, int& waitTime, int& numValues);
    
    /**
     * @brief Parse data response (aD0! - aD9!)
     * @param response Response string from sensor
     * @param values Array to store parsed values
     * @param maxValues Maximum number of values to parse
     * @param numValues Actual number of values parsed
     * @return true if parsing successful
     */
    bool parseDataResponse(const String& response, float* values, int maxValues, int& numValues);
    
    /**
     * @brief Clear SDI-12 buffer
     */
    void clearBuffer();
    
    /**
     * @brief Wait for response with timeout
     * @param timeout Timeout in milliseconds
     * @return true if response available
     */
    bool waitForResponse(unsigned long timeout);
};

#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../diagnostics/Logger.h"

/**
 * @brief OneWire Manager - High-level OneWire/Dallas Temperature communication management
 * 
 * Provides a higher-level interface for OneWire communication with
 * logging, error handling, and device discovery. Supports multiple
 * DS18B20 temperature sensors on a single bus.
 */
class OneWireManager {
public:
    /**
     * @brief Constructor
     * @param logger Logger instance for debug output
     */
    OneWireManager(Logger& logger);
    
    /**
     * @brief Destructor
     */
    ~OneWireManager();
    
    /**
     * @brief Initialize OneWire manager
     * @param dataPin OneWire data pin (single wire bidirectional)
     * @return true if initialization successful
     */
    bool initialize(int dataPin);
    
    /**
     * @brief Check if OneWire is initialized and ready
     * @return true if ready for communication
     */
    bool isReady() const;
    
    /**
     * @brief Shutdown OneWire manager and release resources
     * Cleans up pins and frees memory
     */
    void shutdown();
    
    /**
     * @brief Discover all DS18B20 devices on the bus
     * @return Number of devices found
     */
    uint8_t discoverDevices();
    
    /**
     * @brief Get number of discovered devices
     * @return Number of devices on bus
     */
    uint8_t getDeviceCount() const;
    
    /**
     * @brief Get device address by index
     * @param index Device index (0-based)
     * @param address Buffer to store 8-byte device address
     * @return true if device exists at index
     */
    bool getDeviceAddress(uint8_t index, uint8_t* address);
    
    /**
     * @brief Request temperature reading from all devices
     * This is asynchronous - use getTemperature() after delay
     */
    void requestTemperatures();
    
    /**
     * @brief Get temperature from specific device by index
     * @param index Device index (0-based)
     * @return Temperature in Celsius, or DEVICE_DISCONNECTED_C on error
     */
    float getTemperatureByIndex(uint8_t index);
    
    /**
     * @brief Get temperature from specific device by address
     * @param address 8-byte device address
     * @return Temperature in Celsius, or DEVICE_DISCONNECTED_C on error
     */
    float getTemperatureByAddress(const uint8_t* address);
    
    /**
     * @brief Check if device is connected and responding
     * @param address 8-byte device address
     * @return true if device is connected
     */
    bool isDeviceConnected(const uint8_t* address);
    
    /**
     * @brief Get device address as hex string
     * @param address 8-byte device address
     * @return Hex string representation
     */
    static String addressToString(const uint8_t* address);
    
    /**
     * @brief Get communication statistics
     */
    struct Statistics {
        unsigned long totalRequests;
        unsigned long successfulReads;
        unsigned long failedReads;
        unsigned long crcErrors;
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
    OneWire* oneWire_;
    DallasTemperature* sensors_;
    int dataPin_;
    bool initialized_;
    uint8_t deviceCount_;
    Statistics stats_;
};

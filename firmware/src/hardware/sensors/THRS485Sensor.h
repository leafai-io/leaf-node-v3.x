#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../RS485Manager.h"
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"

/**
 * @brief Base class for Temperature-Humidity RS485 sensors
 * 
 * This template class provides common functionality for RS485 sensors that:
 * - Measure two related values (typically temperature and humidity)
 * - Use two sequential read commands
 * - Return 16-bit values in bytes [3] and [4] of the response
 * - Apply the same scaling factor (divide by 10.0)
 * 
 * Template parameters:
 * - TData: The sensor-specific data structure type
 * 
 * Derived classes must implement:
 * - getSensorName(): Return sensor type name
 * - createJsonPayload(data): Create JSON from sensor data
 * - populateDataStruct(): Fill the data structure with readings
 */
template <typename TData>
class THRS485Sensor {
public:
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     * @param cmd1 First read command (8 bytes)
     * @param cmd2 Second read command (8 bytes)
     * @param baudRate Baud rate for RS485 communication
     */
    THRS485Sensor(RS485Manager* rs485Manager, Logger* logger,
                  const byte* cmd1, const byte* cmd2, unsigned long baudRate)
        : rs485Manager_(rs485Manager)
        , logger_(logger)
        , mqttManager_(nullptr)
        , cmd1_(cmd1)
        , cmd2_(cmd2)
        , baudRate_(baudRate)
        , initialized_(false) {
        memset(responseBuffer_, 0, sizeof(responseBuffer_));
    }

    virtual ~THRS485Sensor() = default;

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize() {
        if (!rs485Manager_) {
            logger_->error(getSensorName(), "RS485Manager instance is null");
            return false;
        }
        
        logger_->info(getSensorName(), "Initializing sensor with baud rate " + String(baudRate_));
        
        // Set correct baud rate
        rs485Manager_->setBaudRate(baudRate_);
        
        initialized_ = true;
        return true;
    }

    /**
     * @brief Read sensor data (both values)
     * @return Sensor data structure with readings
     */
    TData readSensor() {
        TData result = createInvalidData();
        
        if (!initialized_) {
            logger_->error(getSensorName(), "Sensor not initialized");
            return result;
        }
        
        logger_->debug(getSensorName(), "Reading sensor data");
        
        // Read first value
        double value1 = 0.0;
        if (!readValue(cmd1_, getValueName1(), value1)) {
            logger_->error(getSensorName(), "Failed to read " + getValueName1());
            return result;
        }
        
        // Read second value
        double value2 = 0.0;
        if (!readValue(cmd2_, getValueName2(), value2)) {
            logger_->error(getSensorName(), "Failed to read " + getValueName2());
            return result;
        }
        
        // Populate the data structure
        result = populateDataStruct(value1, value2, true, millis());
        
        logger_->info(getSensorName(), "Sensor reading successful - " + 
                    getValueName1() + ": " + String(value1) + 
                    ", " + getValueName2() + ": " + String(value2));
        
        return result;
    }

    /**
     * @brief Read sensor data and publish directly to MQTT
     * @param topic MQTT topic to publish to
     * @return true if reading and publishing successful
     */
    bool readAndPublishData(const String& topic) {
        if (!initialized_) {
            logger_->error(getSensorName(), "Sensor not initialized");
            return false;
        }
        
        if (!mqttManager_) {
            logger_->error(getSensorName(), "MQTT manager not set");
            return false;
        }
        
        if (!mqttManager_->isConnected()) {
            logger_->warning(getSensorName(), "MQTT not connected");
            return false;
        }
        
        // Read sensor data
        TData sensorData = readSensor();
        if (!isDataValid(sensorData)) {
            logger_->error(getSensorName(), "Failed to read sensor data");
            return false;
        }
        
        // Create JSON payload
        String jsonPayload = createJsonPayload(sensorData);
        if (jsonPayload.isEmpty()) {
            logger_->error(getSensorName(), "Failed to create JSON payload");
            return false;
        }
        
        // Publish to MQTT
        if (mqttManager_->publish(topic, jsonPayload)) {
            logger_->info(getSensorName(), "Sensor data published to MQTT: " + topic);
            return true;
        } else {
            logger_->error(getSensorName(), "Failed to publish sensor data to MQTT");
            return false;
        }
    }

    /**
     * @brief Set MQTT manager instance for direct publishing
     * @param mqttManager Pointer to MQTTManager instance
     */
    void setMQTTManager(MQTTManager* mqttManager) {
        mqttManager_ = mqttManager;
    }

    /**
     * @brief Check if sensor is available/responding
     * @return true if sensor is available
     */
    bool isAvailable() {
        if (!initialized_) {
            return false;
        }
        
        size_t responseLength;
        // Try to read first value as a simple availability check
        return rs485Manager_->sendCommandWithRetry(cmd1_, 8, 
                                  responseBuffer_, sizeof(responseBuffer_), responseLength);
    }

    /**
     * @brief Print raw response for debugging
     */
    void printRawResponse() {
        if (!initialized_) {
            logger_->error(getSensorName(), "Sensor not initialized");
            return;
        }
        
        size_t responseLength;
        
        // Print first command response
        if (rs485Manager_->sendCommandWithRetry(cmd1_, 8, 
                               responseBuffer_, sizeof(responseBuffer_), responseLength)) {
            String rawData = getSensorName() + " CMD1 response: ";
            for (size_t i = 0; i < responseLength; i++) {
                rawData += "0x" + String(responseBuffer_[i], HEX) + " ";
            }
            logger_->debug(getSensorName(), rawData);
        } else {
            logger_->error(getSensorName(), "Failed to read CMD1 raw response");
        }
        
        // Print second command response
        if (rs485Manager_->sendCommandWithRetry(cmd2_, 8, 
                               responseBuffer_, sizeof(responseBuffer_), responseLength)) {
            String rawData = getSensorName() + " CMD2 response: ";
            for (size_t i = 0; i < responseLength; i++) {
                rawData += "0x" + String(responseBuffer_[i], HEX) + " ";
            }
            logger_->debug(getSensorName(), rawData);
        } else {
            logger_->error(getSensorName(), "Failed to read CMD2 raw response");
        }
    }

protected:
    RS485Manager* rs485Manager_;
    Logger* logger_;
    MQTTManager* mqttManager_;
    const byte* cmd1_;
    const byte* cmd2_;
    unsigned long baudRate_;
    byte responseBuffer_[64];
    bool initialized_;

    /**
     * @brief Read a single value using the specified command
     * @param cmd Command bytes (8 bytes)
     * @param valueName Name for logging
     * @param value Reference to store the read value
     * @return true if read successful
     */
    bool readValue(const byte* cmd, const String& valueName, double& value) {
        size_t responseLength;
        
        // Send command
        if (!rs485Manager_->sendCommandWithRetry(cmd, 8, 
                                responseBuffer_, sizeof(responseBuffer_), responseLength)) {
            logger_->error(getSensorName(), "Failed to read " + valueName + " - no response");
            return false;
        }
        
        // Validate response length (expecting at least 7 bytes)
        if (responseLength < 7) {
            logger_->error(getSensorName(), "Invalid " + valueName + " response length: " + 
                         String(responseLength) + " (expected ≥7)");
            return false;
        }
        
        // Extract value from register 3+4
        // Response format: [Address][Function][ByteCount][Data_High][Data_Low][CRC_Low][CRC_High]
        uint16_t rawValue = (responseBuffer_[3] << 8) | responseBuffer_[4];
        
        // Convert: divide by 10.0
        value = rawValue / 10.0;
        
        logger_->debug(getSensorName(), "Raw " + valueName + ": " + String(rawValue) + 
                     " -> " + String(value));
        
        return true;
    }

    // Pure virtual methods to be implemented by derived classes
    virtual String getSensorName() const = 0;
    virtual String getValueName1() const = 0;  // e.g., "soil_humidity" or "leaf_humidity"
    virtual String getValueName2() const = 0;  // e.g., "soil_temperature" or "leaf_temperature"
    virtual String createJsonPayload(const TData& data) const = 0;
    virtual TData createInvalidData() const = 0;
    virtual TData populateDataStruct(double value1, double value2, bool valid, unsigned long timestamp) const = 0;
    virtual bool isDataValid(const TData& data) const = 0;
};

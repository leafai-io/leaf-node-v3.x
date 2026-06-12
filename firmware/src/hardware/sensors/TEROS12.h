#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../SDI12Manager.h"
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief TEROS12 Soil Moisture Sensor Driver
 * 
 * This class provides interface for the METER TEROS 12 soil moisture sensor
 * using SDI-12 communication protocol with direct MQTT publishing capability.
 * 
 * TEROS 12 measures:
 * - Volumetric Water Content (VWC) in m³/m³
 * - Soil Temperature in °C
 * - Electrical Conductivity (EC) in dS/m (optional, depends on sensor version)
 * 
 * SDI-12 Communication:
 * - Default address: '0' (configurable 0-9, a-z, A-Z)
 * - Measurement time: ~150ms
 * - Standard SDI-12 protocol at 1200 baud
 */
class TEROS12 {
public:
    /**
     * @brief Constructor
     * @param sdi12Manager Pointer to SDI12Manager communication instance
     * @param logger Pointer to Logger instance
     * @param sensorAddress SDI-12 sensor address (default: '0')
     */
    TEROS12(SDI12Manager* sdi12Manager, Logger* logger, char sensorAddress = '0');
    
    /**
     * @brief Destructor
     */
    ~TEROS12();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data
     * @return TEROS12Data structure with sensor readings
     */
    TEROS12Data readSensor();

    /**
     * @brief Read sensor data and publish directly to MQTT
     * @param topic MQTT topic to publish to
     * @return true if reading and publishing successful
     */
    bool readAndPublishData(const String& topic);

    /**
     * @brief Set MQTT manager instance for direct publishing
     * @param mqttManager Pointer to MQTTManager instance
     */
    void setMQTTManager(MQTTManager* mqttManager);

    /**
     * @brief Check if sensor is available/responding
     * @return true if sensor is available
     */
    bool isAvailable();

    /**
     * @brief Get sensor identification information
     * @return Identification string from sensor
     */
    String getSensorInfo();

    /**
     * @brief Change sensor address
     * @param newAddress New SDI-12 address
     * @return true if address changed successfully
     */
    bool changeAddress(char newAddress);

    /**
     * @brief Get current sensor address
     * @return Current SDI-12 address
     */
    char getAddress() const { return sensorAddress_; }

    /**
     * @brief Print raw sensor response for debugging
     */
    void printRawResponse();

private:
    SDI12Manager* sdi12Manager_;
    Logger* logger_;
    MQTTManager* mqttManager_;
    char sensorAddress_;
    bool initialized_;
    
    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data to convert
     * @return JSON string for MQTT publishing
     */
    String createJsonPayload(const TEROS12Data& data);
    
    /**
     * @brief Validate sensor data
     * @param data Sensor data to validate
     * @return true if data is within valid ranges
     */
    bool validateData(const TEROS12Data& data);
};

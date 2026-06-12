#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../RS485Manager.h"
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief CWTPSS PAR Sensor Driver
 * 
 * This class provides interface for the CWT-PS-S PAR (Photosynthetically Active Radiation) sensor
 * using RS485 communication protocol with direct MQTT publishing capability.
 */
class CWTPSS {
public:
    // RS485 communication settings
    static const unsigned long BAUDRATE = 4800;  // CWTPSS uses 4800 baud (assumed - verify with datasheet)
    
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     */
    CWTPSS(RS485Manager* rs485Manager, Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~CWTPSS();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data
     * @return CWTPSSData structure with sensor reading
     */
    CWTPSSData readSensor();

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
     * @brief Print raw response for debugging
     */
    void printRawResponse();

private:
    RS485Manager* rs485Manager_;
    Logger* logger_;
    MQTTManager* mqttManager_;
    byte responseBuffer_[64];
    bool initialized_;
    
    // CWTPSS Command (Read PAR value)
    static const byte CMD_READ_PAR[8];
    
    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data to convert
     * @return JSON string for MQTT publishing
     */
    String createJsonPayload(const CWTPSSData& data);
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../RS485Manager.h"
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief SLT5007 Soil Moisture Sensor Driver
 * 
 * This class provides interface for the SLT5007 soil moisture sensor
 * using RS485 communication protocol with direct MQTT publishing capability.
 */
class SLT5007 {
public:
    // RS485 communication settings
    static const unsigned long BAUDRATE = 9600;  // SLT5007 uses 9600 baud
    
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     */
    SLT5007(RS485Manager* rs485Manager, Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~SLT5007();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data
     * @return SLT5007Data structure with sensor readings
     */
    SLT5007Data readSensor();

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
    
    // SLT5007 Commands
    static const byte CMD_MEASURE_START[6];
    static const byte CMD_READ_STATE[5];
    static const byte CMD_READ_DATA[5];
    
    // Internal sensor data structure (detailed)
    struct InternalSensorData {
        double temperature;   // °C
        double bulkEC;       // dS/m
        double vwcRock;      // %
        double vwc;          // %
        double vwcCoco;      // %
        double poreEC;       // dS/m
        bool valid;
    };
    
    /**
     * @brief Read detailed sensor data (internal format)
     * @return InternalSensorData structure
     */
    InternalSensorData readDetailedData();
    
    /**
     * @brief Convert internal data to standard format
     * @param internal Internal sensor data
     * @return SLT5007Data in standard format
     */
    SLT5007Data convertToStandardFormat(const InternalSensorData& internal);
    
    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data to convert
     * @return JSON string for MQTT publishing
     */
    String createJsonPayload(const SLT5007Data& data);
    
    /**
     * @brief Wait for measurement completion
     * @param maxAttempts Maximum polling attempts
     * @return true if measurement ready
     */
    bool waitForMeasurement(int maxAttempts = 10);
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief SHT31 Temperature & Humidity Sensor Driver
 * 
 * This class provides interface for the SHT31 temperature and humidity sensor
 * using I2C communication protocol with direct MQTT publishing capability.
 */
class SHT31 {
public:
    /**
     * @brief Constructor
     * @param logger Pointer to Logger instance
     */
    SHT31(Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~SHT31();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data
     * @return SHT31Data structure with sensor readings
     */
    SHT31Data readSensor();

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
     * @brief Get sensor status for debugging
     * @return Status string
     */
    String getStatus();

private:
    Logger* logger_;
    MQTTManager* mqttManager_;
    Adafruit_SHT31* sht31_;
    bool initialized_;
    uint8_t i2cAddress_;
    
    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data to convert
     * @return JSON string for MQTT publishing
     */
    String createJsonPayload(const SHT31Data& data);
    
    /**
     * @brief Initialize I2C bus
     * @return true if I2C initialization successful
     */
    bool initializeI2C();
};

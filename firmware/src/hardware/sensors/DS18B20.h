#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../OneWireManager.h"
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief DS18B20 Temperature Sensor Driver
 * 
 * This class provides interface for the Dallas DS18B20 temperature sensor
 * using OneWire communication protocol with direct MQTT publishing capability.
 * Supports multiple sensors on the same bus (reads first device by default).
 */
class DS18B20 {
public:
    /**
     * @brief Constructor
     * @param oneWireManager Pointer to OneWireManager communication instance
     * @param logger Pointer to Logger instance
     * @param deviceIndex Index of device to read (default: 0 for first device)
     */
    DS18B20(OneWireManager* oneWireManager, Logger* logger, uint8_t deviceIndex = 0);
    
    /**
     * @brief Destructor
     */
    ~DS18B20();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data
     * @return DS18B20Data structure with sensor readings
     */
    DS18B20Data readSensor();

    /**
     * @brief Read sensor data and publish directly to MQTT
     * @param topic MQTT topic to publish to
     * @return true if reading and publishing successful
     */
    bool readAndPublishData(const String& topic);

    /**
     * @brief Set MQTT manager instance for direct publishing
     * @param mqttManager Pointer to MQTT manager
     */
    void setMQTTManager(MQTTManager* mqttManager);

private:
    OneWireManager* oneWireManager_;
    Logger* logger_;
    MQTTManager* mqttManager_;
    uint8_t deviceIndex_;
    uint8_t deviceAddress_[8];
    bool initialized_;
    bool hasValidAddress_;
    
    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data structure
     * @return JSON string
     */
    String createJsonPayload(const DS18B20Data& data);
    
    /**
     * @brief Validate sensor data
     * @param data Sensor data to validate
     * @return true if data is valid
     */
    bool validateData(const DS18B20Data& data);
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../LeafNodeTypes.h"
#include "../runtime/RuntimeConfig.h"
#include "../network/MQTTManager.h"
#include "../diagnostics/Logger.h"

/**
 * @brief MQTT Sensor Data Publisher
 * 
 * This class handles publishing sensor data to MQTT topics
 * in the correct format based on sensor type.
 */
class MQTTSensorPublisher {
public:
    /**
     * @brief Constructor
     * @param config Configuration instance
     * @param mqttManager MQTT manager instance
     * @param logger Logger instance
     */
    MQTTSensorPublisher(RuntimeConfig* config, MQTTManager* mqttManager, Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~MQTTSensorPublisher();

    /**
     * @brief Publish sensor data to MQTT
     * @param sensorData Sensor data to publish
     * @return true if published successfully
     */
    bool publishSensorData(const SensorData& sensorData);

    /**
     * @brief Get the MQTT topic for sensor data
     * @return MQTT topic string
     */
    String getSensorDataTopic() const;

private:
    RuntimeConfig* config_;
    MQTTManager* mqttManager_;
    Logger* logger_;
    
    /**
     * @brief Create JSON payload for SLT5007 sensor data
     * @param data SLT5007 sensor data
     * @return JSON string payload
     */
    String createSLT5007Payload(const SLT5007Data& data);
    
    /**
     * @brief Create JSON payload based on sensor profile
     * @param sensorData Generic sensor data
     * @return JSON string payload
     */
    String createPayload(const SensorData& sensorData);
    
    /**
     * @brief Validate sensor data before publishing
     * @param sensorData Sensor data to validate
     * @return true if data is valid for publishing
     */
    bool validateSensorData(const SensorData& sensorData);
};

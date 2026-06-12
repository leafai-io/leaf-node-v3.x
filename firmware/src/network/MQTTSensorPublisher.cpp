#include "MQTTSensorPublisher.h"
#include "../diagnostics/Logger.h"
#include "config.h"

MQTTSensorPublisher::MQTTSensorPublisher(RuntimeConfig* config, MQTTManager* mqttManager, Logger* logger)
    : config_(config), mqttManager_(mqttManager), logger_(logger) {
}

MQTTSensorPublisher::~MQTTSensorPublisher() {
}

bool MQTTSensorPublisher::publishSensorData(const SensorData& sensorData) {
    if (!mqttManager_) {
        logger_->error("MQTTSensorPublisher", "MQTT manager not available");
        return false;
    }
    
    if (!validateSensorData(sensorData)) {
        logger_->error("MQTTSensorPublisher", "Invalid sensor data");
        return false;
    }
    
    String topic = getSensorDataTopic();
    String payload = createPayload(sensorData);
    
    if (topic.isEmpty() || payload.isEmpty()) {
        logger_->error("MQTTSensorPublisher", "Empty topic or payload");
        return false;
    }
    
    logger_->info("MQTTSensorPublisher", "Publishing sensor data to topic: " + topic);
    logger_->debug("MQTTSensorPublisher", "Payload: " + payload);
    
    bool result = mqttManager_->publish(topic, payload);
    
    if (result) {
        logger_->info("MQTTSensorPublisher", "Sensor data published successfully");
    } else {
        logger_->error("MQTTSensorPublisher", "Failed to publish sensor data");
    }
    
    return result;
}

String MQTTSensorPublisher::getSensorDataTopic() const {
    if (config_->getSerialNumber().isEmpty()) {
        logger_->error("MQTTSensorPublisher", "Serial number not configured");
        return "";
    }
    
    return config_->getSensorTopicData();
}

String MQTTSensorPublisher::createPayload(const SensorData& sensorData) {
    switch (sensorData.profile) {
        case SensorProfile::SLT5007:
            return createSLT5007Payload(sensorData.data.slt5007);
            
        case SensorProfile::NONE:
            logger_->debug("MQTTSensorPublisher", "No sensor profile configured");
            return "";
            
        default:
            logger_->error("MQTTSensorPublisher", "Unknown sensor profile: " + String(static_cast<int>(sensorData.profile)));
            return "";
    }
}

String MQTTSensorPublisher::createSLT5007Payload(const SLT5007Data& data) {
    StaticJsonDocument<400> doc;  // Increased size for 6 values
    
    // Round values to appropriate precision
    doc["VWC Soil"] = round(data.VWC_Soil * 10.0) / 10.0;           // Soil VWC (1 decimal)
    doc["Bulk_EC"] = round(data.Bulk_EC * 10.0) / 10.0;        // Bulk EC (1 decimal)
    doc["Soil_Temp"] = round(data.Soil_Temp * 10.0) / 10.0; // Temperature (1 decimal)
    doc["VWC_Rock"] = round(data.VWC_Rock * 10.0) / 10.0;    // VWC Rockwool (1 decimal)
    doc["VWC_Coco"] = round(data.VWC_Coco * 10.0) / 10.0;    // VWC Coconut (1 decimal)
    doc["Pore_EC"] = round(data.Pore_EC * 10.0) / 10.0;      // Pore EC (1 decimal)
    
    // Add timestamp if needed
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    return jsonString;
}

bool MQTTSensorPublisher::validateSensorData(const SensorData& sensorData) {
    if (!sensorData.valid) {
        logger_->error("MQTTSensorPublisher", "Sensor data marked as invalid");
        return false;
    }
    
    if (sensorData.profile == SensorProfile::NONE) {
        logger_->debug("MQTTSensorPublisher", "No sensor profile configured");
        return false;
    }
    
    // Profile-specific validation
    switch (sensorData.profile) {
        case SensorProfile::SLT5007:
            {
                const SLT5007Data& data = sensorData.data.slt5007;
                
                // Check for reasonable value ranges
                if (data.VWC_Soil < 0.0 || data.VWC_Soil > 100.0) {
                    logger_->error("MQTTSensorPublisher", "VWC Soil value out of range: " + String(data.VWC_Soil));
                    return false;
                }
                
                if (data.Bulk_EC < 0.0 || data.Bulk_EC > 10.0) { // Max 10 dS/m (reasonable for soil)
                    logger_->error("MQTTSensorPublisher", "Bulk_EC value out of range: " + String(data.Bulk_EC));
                    return false;
                }
                
                if (data.Soil_Temp < -40.0 || data.Soil_Temp > 85.0) { // Sensor operating range
                    logger_->error("MQTTSensorPublisher", "Temperature out of range: " + String(data.Soil_Temp));
                    return false;
                }
                
                // Validate additional VWC values
                if (data.VWC_Rock < 0.0 || data.VWC_Rock > 100.0) {
                    logger_->error("MQTTSensorPublisher", "VWC_Rock value out of range: " + String(data.VWC_Rock));
                    return false;
                }
                
                if (data.VWC_Coco < 0.0 || data.VWC_Coco > 100.0) {
                    logger_->error("MQTTSensorPublisher", "VWC_Coco value out of range: " + String(data.VWC_Coco));
                    return false;
                }
                
                if (data.Pore_EC < 0.0 || data.Pore_EC > 10.0) { // Max 10 dS/m (reasonable for soil)
                    logger_->error("MQTTSensorPublisher", "Pore_EC value out of range: " + String(data.Pore_EC));
                    return false;
                }
                
                break;
            }
            
        default:
            break;
    }
    
    return true;
}

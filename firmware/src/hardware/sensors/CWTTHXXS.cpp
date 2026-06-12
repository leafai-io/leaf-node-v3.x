#include "CWTTHXXS.h"

// Static command definitions (identical to LEAFTHSN and CWTSoilTHS)
// Command to read air humidity (register 0x0000)
const byte CWTTHXXS::CMD_READ_HUMIDITY[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
// Command to read air temperature (register 0x0001)
const byte CWTTHXXS::CMD_READ_TEMPERATURE[8] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xd5, 0xca};

CWTTHXXS::CWTTHXXS(RS485Manager* rs485Manager, Logger* logger) 
    : THRS485Sensor<CWTTHXXSData>(rs485Manager, logger, 
                                   CMD_READ_HUMIDITY, CMD_READ_TEMPERATURE, BAUDRATE) {
}

double CWTTHXXS::calculateVPD(double temperature, double humidity) {
    // Saturated Vapor Pressure (SVP) using Magnus formula
    // SVP = 0.6107 × 10^(7.5 × T / (237.3 + T)) [kPa]
    double svp = 0.6107 * pow(10.0, (7.5 * temperature) / (237.3 + temperature));
    
    // Vapor Pressure Deficit
    // VPD = SVP × (1 - RH/100) [kPa]
    double vpd = svp * (1.0 - humidity / 100.0);
    
    return vpd;
}

CWTTHXXSData CWTTHXXS::readSensor() {
    // Call base class readSensor to get temperature and humidity
    CWTTHXXSData result = THRS485Sensor<CWTTHXXSData>::readSensor();
    
    // Calculate VPD if data is valid
    if (result.valid) {
        result.vpd = calculateVPD(result.air_temperature, result.air_humidity);
        
        if (logger_) {
            logger_->debug("CWTTHXXS", "Calculated VPD: " + String(result.vpd, 2) + " kPa");
        }
    } else {
        result.vpd = 0.0;
    }
    
    return result;
}

bool CWTTHXXS::readAndPublishData(const String& topic) {
    if (!logger_) {
        return false;
    }
    
    // Read sensor data with VPD calculation
    CWTTHXXSData sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("CWTTHXXS", "Failed to read sensor data");
        return false;
    }
    
    // Get MQTT manager from base class
    if (!mqttManager_) {
        logger_->error("CWTTHXXS", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("CWTTHXXS", "MQTT not connected");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("CWTTHXXS", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("CWTTHXXS", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("CWTTHXXS", "Failed to publish sensor data to MQTT");
        return false;
    }
}

String CWTTHXXS::createJsonPayload(const CWTTHXXSData& data) const {
    DynamicJsonDocument doc(384);  // Increased for VPD field
    
    // Sensor identification
    doc["sensor_type"] = "CWTTHXXS";
    
    // Sensor data (rounded to 2 decimal places)
    doc["humidity"] = round(data.air_humidity * 100.0) / 100.0;
    doc["temperature"] = round(data.air_temperature * 100.0) / 100.0;
    doc["vpd"] = round(data.vpd * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        if (logger_) {
            logger_->error("CWTTHXXS", "Failed to serialize JSON");
        }
        return "";
    }
    
    return jsonString;
}

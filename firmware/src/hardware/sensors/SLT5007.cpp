#include "SLT5007.h"
#include "../../diagnostics/Logger.h"

// Static command definitions
const byte SLT5007::CMD_MEASURE_START[6] = {0x02, 0x07, 0x01, 0x01, 0x0D, 0x70};
const byte SLT5007::CMD_READ_STATE[5] = {0x00, 0x08, 0x01, 0xC0, 0xB7};
const byte SLT5007::CMD_READ_DATA[5] = {0x00, 0x13, 0x10, 0x3C, 0x7D};

SLT5007::SLT5007(RS485Manager* rs485Manager, Logger* logger) : rs485Manager_(rs485Manager), logger_(logger), mqttManager_(nullptr), initialized_(false) {
    memset(responseBuffer_, 0, sizeof(responseBuffer_));
}

SLT5007::~SLT5007() {
    // Cleanup if needed
}

bool SLT5007::initialize() {
    if (!rs485Manager_) {
        logger_->error("SLT5007", "RS485Manager instance is null");
        return false;
    }
    
    logger_->info("SLT5007", "Initializing sensor");
    
    // Set correct baud rate for SLT5007
    rs485Manager_->setBaudRate(BAUDRATE);
    
    initialized_ = true;
    return true;
}

SLT5007Data SLT5007::readSensor() {
    SLT5007Data result = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, millis()};
    
    if (!initialized_) {
        logger_->error("SLT5007", "Sensor not initialized");
        return result;
    }
    
    InternalSensorData internalData = readDetailedData();
    if (internalData.valid) {
        result = convertToStandardFormat(internalData);
        logger_->info("SLT5007", "Sensor reading successful - VWC Soil: " + String(result.VWC_Soil) + 
                    "%, EC Bulk: " + String(result.Bulk_EC) + " dS/m, Soil Temp: " + String(result.Soil_Temp) + 
                    "°C, VWC Rock: " + String(result.VWC_Rock) + "%, VWC Coco: " + String(result.VWC_Coco) + 
                    "%, EC Pore: " + String(result.Pore_EC) + " dS/m");
    } else {
        logger_->error("SLT5007", "Failed to read sensor data");
    }
    
    return result;
}

bool SLT5007::readAndPublishData(const String& topic) {
    if (!initialized_) {
        logger_->error("SLT5007", "Sensor not initialized");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->error("SLT5007", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("SLT5007", "MQTT not connected");
        return false;
    }
    
    // Read sensor data
    SLT5007Data sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("SLT5007", "Failed to read sensor data");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("SLT5007", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("SLT5007", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("SLT5007", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void SLT5007::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

SLT5007::InternalSensorData SLT5007::readDetailedData() {
    InternalSensorData data = {0, 0, 0, 0, 0, 0, false};
    size_t responseLength;
    
    logger_->debug("SLT5007", "Starting measurement sequence");
    
    // Step 1: Start measurement
    if (!rs485Manager_->sendCommandWithRetry(CMD_MEASURE_START, sizeof(CMD_MEASURE_START), 
                            responseBuffer_, sizeof(responseBuffer_), responseLength)) {
        logger_->error("SLT5007", "Failed to start measurement - no response");
        return data;
    }
    
    if (responseLength != 6) {
        logger_->error("SLT5007", "Invalid start response length: " + String(responseLength) + " (expected 6)");
        return data;
    }
    
    logger_->debug("SLT5007", "Measurement started successfully");
    
    // Step 2: Wait for measurement completion
    if (!waitForMeasurement()) {
        logger_->error("SLT5007", "Measurement timeout");
        return data;
    }
    
    // Step 3: Read measurement data
    if (!rs485Manager_->sendCommandWithRetry(CMD_READ_DATA, sizeof(CMD_READ_DATA), 
                            responseBuffer_, sizeof(responseBuffer_), responseLength)) {
        logger_->error("SLT5007", "Failed to read measurement data");
        return data;
    }
    
    if (responseLength < 21) {
        logger_->error("SLT5007", "Invalid data response length: " + String(responseLength) + " (expected ≥21)");
        return data;
    }
    
    // Step 4: Parse data
    data.temperature = ((responseBuffer_[4] << 8) + responseBuffer_[3]) * 0.0625;
    data.bulkEC = ((responseBuffer_[6] << 8) + responseBuffer_[5]) * 0.001;
    data.vwcRock = ((responseBuffer_[8] << 8) + responseBuffer_[7]) * 0.1;
    data.vwc = ((responseBuffer_[10] << 8) + responseBuffer_[9]) * 0.1;
    data.vwcCoco = ((responseBuffer_[12] << 8) + responseBuffer_[11]) * 0.1;
    data.poreEC = ((responseBuffer_[16] << 8) + responseBuffer_[15]) * 0.001;
    
    data.valid = true;
    
    logger_->debug("SLT5007", "Data parsed - Temp: " + String(data.temperature) + 
                 "°C, BulkEC: " + String(data.bulkEC) + " dS/m, VWC: " + String(data.vwc) + "%");
    
    return data;
}

SLT5007Data SLT5007::convertToStandardFormat(const InternalSensorData& internal) {
    SLT5007Data result;
    
    result.VWC_Soil = internal.vwc;          // Soil VWC value (%)
    result.Bulk_EC = internal.bulkEC;        // Bulk EC (dS/m) - NO conversion needed!
    result.Soil_Temp = internal.temperature;  // Temperature (°C)
    result.VWC_Rock = internal.vwcRock;    // VWC for Rockwool (%)
    result.VWC_Coco = internal.vwcCoco;    // VWC for Coconut (%)
    result.Pore_EC = internal.poreEC;      // Pore EC (dS/m) - NO conversion needed!
    result.valid = internal.valid;
    result.timestamp = millis();
    
    return result;
}

String SLT5007::createJsonPayload(const SLT5007Data& data) {
    DynamicJsonDocument doc(512);
    
    // Sensor identification
    doc["sensor_type"] = "SLT5007";
    
    // Sensor data (rounded to 2 decimal places)
    doc["VWC Soil"] = round(data.VWC_Soil * 100.0) / 100.0;           
    doc["EC Bulk"] = round(data.Bulk_EC * 100.0) / 100.0;  
    doc["Soil Temp"] = round(data.Soil_Temp * 100.0) / 100.0; 
    doc["VWC Rock"] = round(data.VWC_Rock * 100.0) / 100.0;   
    doc["VWC Coco"] = round(data.VWC_Coco * 100.0) / 100.0;   
    doc["EC Pore"] = round(data.Pore_EC * 100.0) / 100.0;     
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        logger_->error("SLT5007", "Failed to serialize JSON");
        return "";
    }
    
    return jsonString;
}

bool SLT5007::waitForMeasurement(int maxAttempts) {
    size_t responseLength;
    
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        if (rs485Manager_->sendCommandWithRetry(CMD_READ_STATE, sizeof(CMD_READ_STATE), 
                               responseBuffer_, sizeof(responseBuffer_), responseLength)) {
            
            if (responseLength >= 4 && responseBuffer_[3] == 1) {
                logger_->debug("SLT5007", "Measurement ready after " + String(attempt + 1) + " attempts");
                return true;
            }
        }
        
        delay(100);  // Wait 100ms between attempts
    }
    
    logger_->error("SLT5007", "Measurement timeout after " + String(maxAttempts) + " attempts");
    return false;
}

bool SLT5007::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    size_t responseLength;
    // Try to read sensor state as a simple availability check
    return rs485Manager_->sendCommandWithRetry(CMD_READ_STATE, sizeof(CMD_READ_STATE), 
                              responseBuffer_, sizeof(responseBuffer_), responseLength);
}

void SLT5007::printRawResponse() {
    if (!initialized_) {
        logger_->error("SLT5007", "Sensor not initialized");
        return;
    }
    
    size_t responseLength;
    if (rs485Manager_->sendCommandWithRetry(CMD_READ_DATA, sizeof(CMD_READ_DATA), 
                           responseBuffer_, sizeof(responseBuffer_), responseLength)) {
        
        String rawData = "SLT5007 Raw response: ";
        for (size_t i = 0; i < responseLength; i++) {
            rawData += "0x" + String(responseBuffer_[i], HEX) + " ";
        }
        logger_->debug("SLT5007", rawData);
    } else {
        logger_->error("SLT5007", "Failed to read raw response");
    }
}

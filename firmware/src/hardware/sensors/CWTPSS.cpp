#include "CWTPSS.h"
#include "../../diagnostics/Logger.h"

// Static command definition: Read PAR value
// Command: {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a}
const byte CWTPSS::CMD_READ_PAR[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};

CWTPSS::CWTPSS(RS485Manager* rs485Manager, Logger* logger) 
    : rs485Manager_(rs485Manager), logger_(logger), mqttManager_(nullptr), initialized_(false) {
    memset(responseBuffer_, 0, sizeof(responseBuffer_));
}

CWTPSS::~CWTPSS() {
    // Cleanup if needed
}

bool CWTPSS::initialize() {
    if (!rs485Manager_) {
        logger_->error("CWTPSS", "RS485Manager instance is null");
        return false;
    }
    
    logger_->info("CWTPSS", "Initializing PAR sensor");
    
    // Set correct baud rate for CWTPSS
    rs485Manager_->setBaudRate(BAUDRATE);
    
    initialized_ = true;
    return true;
}

CWTPSSData CWTPSS::readSensor() {
    CWTPSSData result = {0.0, false, millis()};
    
    if (!initialized_) {
        logger_->error("CWTPSS", "Sensor not initialized");
        return result;
    }
    
    size_t responseLength;
    
    logger_->debug("CWTPSS", "Reading PAR value");
    
    // Send command and receive response
    if (!rs485Manager_->sendCommandWithRetry(CMD_READ_PAR, sizeof(CMD_READ_PAR), 
                            responseBuffer_, sizeof(responseBuffer_), responseLength)) {
        logger_->error("CWTPSS", "Failed to read PAR value - no response");
        return result;
    }
    
    // Validate response length (expecting at least 7 bytes: Address(1) + Function(1) + ByteCount(1) + Data(2) + CRC(2))
    if (responseLength < 7) {
        logger_->error("CWTPSS", "Invalid response length: " + String(responseLength) + " (expected ≥7)");
        return result;
    }
    
    // Extract PAR value from register 4 (0-based indexing)
    // Response format: [Address][Function][ByteCount][Data_High][Data_Low][CRC_Low][CRC_High]
    // Register 4 means: responseBuffer_[3] (high byte) and responseBuffer_[4] (low byte)
    uint16_t rawPAR = (responseBuffer_[3] << 8) | responseBuffer_[4];
    
    // Store as double (assuming no scaling needed - adjust if sensor requires conversion)
    result.PAR = static_cast<double>(rawPAR);
    result.valid = true;
    result.timestamp = millis();
    
    logger_->info("CWTPSS", "PAR reading successful: " + String(result.PAR) + " μmol/m²/s");
    
    return result;
}

bool CWTPSS::readAndPublishData(const String& topic) {
    if (!initialized_) {
        logger_->error("CWTPSS", "Sensor not initialized");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->error("CWTPSS", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("CWTPSS", "MQTT not connected");
        return false;
    }
    
    // Read sensor data
    CWTPSSData sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("CWTPSS", "Failed to read sensor data");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("CWTPSS", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("CWTPSS", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("CWTPSS", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void CWTPSS::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

String CWTPSS::createJsonPayload(const CWTPSSData& data) {
    DynamicJsonDocument doc(256);
    
    // Sensor identification
    doc["sensor_type"] = "CWTPSS";
    
    // Sensor data (rounded to 2 decimal places)
    doc["PPFD"] = round(data.PAR * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        logger_->error("CWTPSS", "Failed to serialize JSON");
        return "";
    }
    
    return jsonString;
}

bool CWTPSS::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    size_t responseLength;
    // Try to read sensor as a simple availability check
    return rs485Manager_->sendCommandWithRetry(CMD_READ_PAR, sizeof(CMD_READ_PAR), 
                              responseBuffer_, sizeof(responseBuffer_), responseLength);
}

void CWTPSS::printRawResponse() {
    if (!initialized_) {
        logger_->error("CWTPSS", "Sensor not initialized");
        return;
    }
    
    size_t responseLength;
    if (rs485Manager_->sendCommandWithRetry(CMD_READ_PAR, sizeof(CMD_READ_PAR), 
                           responseBuffer_, sizeof(responseBuffer_), responseLength)) {
        
        String rawData = "CWTPSS Raw response: ";
        for (size_t i = 0; i < responseLength; i++) {
            rawData += "0x" + String(responseBuffer_[i], HEX) + " ";
        }
        logger_->debug("CWTPSS", rawData);
    } else {
        logger_->error("CWTPSS", "Failed to read raw response");
    }
}

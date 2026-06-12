#include "EZOph.h"
#include "../../diagnostics/Logger.h"
#include "config.h"

// Fallback I2C address if not defined
#ifndef EZOPH_I2C_ADDRESS
#define EZOPH_I2C_ADDRESS 0x63  // 99 decimal
#endif

EZOph::EZOph(Logger* logger) 
    : logger_(logger), 
      mqttManager_(nullptr), 
      initialized_(false), 
      i2cAddress_(EZOPH_I2C_ADDRESS),
      lastResponseCode_(ResponseCode::NO_DATA),
      lastCommandTime_(0),
      readingInProgress_(false) {
}

EZOph::~EZOph() {
    // Cleanup if needed
}

bool EZOph::initialize() {
    if (initialized_) {
        logger_->info("EZOph", "Already initialized");
        return true;
    }
    
    logger_->info("EZOph", "Initializing EZO pH sensor...");
    
    // Initialize I2C bus
    if (!initializeI2C()) {
        logger_->error("EZOph", "Failed to initialize I2C bus");
        return false;
    }
    
    // Check if sensor is available on I2C bus
    Wire.beginTransmission(i2cAddress_);
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOph", "EZO pH sensor not found at address 0x" + String(i2cAddress_, HEX) + 
                       " (Error: " + String(error) + "). Use 'i2cscan' command to find devices!");
        return false;
    }
    
    logger_->info("EZOph", "EZO pH sensor detected at address 0x" + String(i2cAddress_, HEX));
    
    // Small delay to let sensor stabilize
    delay(100);
    
    // Verify sensor responds with info command
    if (!sendI2CCommand("i", false)) {
        logger_->warning("EZOph", "Failed to query sensor info, but sensor detected");
    }
    
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    if (bytesRead > 0 && lastResponseCode_ == ResponseCode::SUCCESS) {
        logger_->info("EZOph", "Sensor info: " + String(buffer));
    }
    
    // CRITICAL: Disable continuous reading mode to ensure fresh readings on each "R" command
    // In continuous mode, sensor may return old/buffered values
    logger_->info("EZOph", "Disabling continuous reading mode...");
    if (!sendI2CCommand("C,0", false)) {
        logger_->warning("EZOph", "Failed to disable continuous mode - readings may be unstable");
    } else {
        // Read and discard the response to clear I2C buffer
        char tempBuffer[RESPONSE_BUFFER_SIZE];
        readI2CResponse(tempBuffer, RESPONSE_BUFFER_SIZE);
        logger_->info("EZOph", "Continuous mode disabled - sensor will give fresh readings on each request");
    }
    
    // Perform a dummy reading to clear any old buffered values in the sensor
    logger_->info("EZOph", "Performing dummy read to clear sensor buffer...");
    sendI2CCommand("R", true); // Send read command with full delay
    char dummyBuffer[RESPONSE_BUFFER_SIZE];
    readI2CResponse(dummyBuffer, RESPONSE_BUFFER_SIZE); // Discard this reading
    logger_->info("EZOph", "Sensor buffer cleared, initialization complete");
    
    initialized_ = true;
    logger_->info("EZOph", "EZO pH sensor initialized successfully");
    
    return true;
}

bool EZOph::initializeI2C() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    logger_->debug("EZOph", "I2C initialized - SDA: GPIO" + String(I2C_SDA_PIN) + 
                   ", SCL: GPIO" + String(I2C_SCL_PIN));
    
    // Give I2C bus time to stabilize
    delay(10);
    
    return true;
}

bool EZOph::sendI2CCommand(const String& command, bool isReadingCommand) {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    logger_->debug("EZOph", "Sending command: " + command);
    
    Wire.beginTransmission(i2cAddress_);
    Wire.write(command.c_str());
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOph", "Failed to send command (I2C error: " + String(error) + ")");
        return false;
    }
    
    // Store timestamp for async operations
    lastCommandTime_ = millis();
    
    // Wait appropriate time based on command type
    if (isReadingCommand) {
        delay(READING_DELAY_MS);
    } else {
        delay(COMMAND_DELAY_MS);
    }
    
    return true;
}

uint8_t EZOph::readI2CResponse(char* buffer, uint8_t bufferSize) {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return 0;
    }
    
    // Request data from sensor
    uint8_t bytesRequested = Wire.requestFrom((int)i2cAddress_, (int)bufferSize);
    
    if (bytesRequested == 0) {
        logger_->error("EZOph", "No response from sensor");
        lastResponseCode_ = ResponseCode::NO_DATA;
        return 0;
    }
    
    // Read response code (first byte)
    if (Wire.available()) {
        lastResponseCode_ = static_cast<ResponseCode>(Wire.read());
        
        logger_->debug("EZOph", "Response code: " + String((uint8_t)lastResponseCode_) + 
                      " (" + getResponseCodeDescription(lastResponseCode_) + ")");
    }
    
    // Read remaining data
    uint8_t index = 0;
    while (Wire.available() && index < (bufferSize - 1)) {
        char c = Wire.read();
        if (c == 0) {  // Null terminator
            break;
        }
        buffer[index++] = c;
    }
    buffer[index] = '\0';  // Ensure null termination
    
    logger_->debug("EZOph", "Received: " + String(buffer));
    
    return index;
}

bool EZOph::startReading() {
    if (readingInProgress_) {
        logger_->warning("EZOph", "Reading already in progress");
        return false;
    }
    
    // Send reading command without waiting
    Wire.beginTransmission(i2cAddress_);
    Wire.write("r");  // Reading command
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOph", "Failed to start reading (I2C error: " + String(error) + ")");
        return false;
    }
    
    lastCommandTime_ = millis();
    readingInProgress_ = true;
    
    logger_->debug("EZOph", "Async reading started");
    
    return true;
}

bool EZOph::getReading(EZOphData& data) {
    data.pH = 0.0;
    data.valid = false;
    data.timestamp = millis();
    
    if (!readingInProgress_) {
        logger_->error("EZOph", "No reading in progress");
        return false;
    }
    
    // Check if enough time has passed
    unsigned long elapsed = millis() - lastCommandTime_;
    if (elapsed < READING_DELAY_MS) {
        logger_->debug("EZOph", "Reading not ready yet (" + String(elapsed) + "ms elapsed)");
        return false;
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    readingInProgress_ = false;
    
    // Check response code
    if (lastResponseCode_ != ResponseCode::SUCCESS) {
        logger_->error("EZOph", "Reading failed: " + getResponseCodeDescription(lastResponseCode_));
        return false;
    }
    
    if (bytesRead == 0) {
        logger_->error("EZOph", "No data received from sensor");
        return false;
    }
    
    // Parse pH value
    float phValue;
    if (!parsePhValue(buffer, phValue)) {
        logger_->error("EZOph", "Failed to parse pH value from: " + String(buffer));
        return false;
    }
    
    // Populate result
    data.pH = phValue;
    data.valid = true;
    data.timestamp = millis();
    
    logger_->info("EZOph", "pH reading: " + String(data.pH, 2));
    
    return true;
}

EZOphData EZOph::readSensor() {
    EZOphData result = {0.0, false, millis()};
    
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return result;
    }
    
    // Send reading command and wait
    if (!sendI2CCommand("r", true)) {
        logger_->error("EZOph", "Failed to send reading command");
        return result;
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    // Check response code
    if (lastResponseCode_ != ResponseCode::SUCCESS) {
        logger_->error("EZOph", "Reading failed: " + getResponseCodeDescription(lastResponseCode_));
        return result;
    }
    
    if (bytesRead == 0) {
        logger_->error("EZOph", "No data received from sensor");
        return result;
    }
    
    // Parse pH value
    float phValue;
    if (!parsePhValue(buffer, phValue)) {
        logger_->error("EZOph", "Failed to parse pH value from: " + String(buffer));
        return result;
    }
    
    // Populate result structure
    result.pH = phValue;
    result.valid = true;
    result.timestamp = millis();
    
    logger_->info("EZOph", "Sensor reading successful - pH: " + String(result.pH, 2));
    
    return result;
}

bool EZOph::parsePhValue(const char* response, float& phValue) {
    if (response == nullptr || strlen(response) == 0) {
        return false;
    }
    
    // Convert string to float
    phValue = atof(response);
    
    // Validate pH range (typical pH range is 0-14)
    if (phValue < 0.0 || phValue > 14.0) {
        logger_->warning("EZOph", "pH value out of typical range: " + String(phValue, 2));
        // Still return true, but log warning
    }
    
    return true;
}

bool EZOph::readAndPublishData(const String& topic) {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->error("EZOph", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("EZOph", "MQTT not connected");
        return false;
    }
    
    // Read sensor data
    EZOphData sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("EZOph", "Failed to read sensor data");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("EZOph", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("EZOph", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("EZOph", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void EZOph::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

String EZOph::createJsonPayload(const EZOphData& data) {
    DynamicJsonDocument doc(512);
    
    // Sensor identification
    doc["sensor_type"] = "EZOph";
    
    // Sensor data (rounded to 2 decimal places)
    doc["pH"] = round(data.pH * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        logger_->error("EZOph", "Failed to serialize JSON");
        return "";
    }
    
    return jsonString;
}

bool EZOph::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    // Try to communicate with sensor
    Wire.beginTransmission(i2cAddress_);
    byte error = Wire.endTransmission();
    
    return (error == 0);
}

bool EZOph::sleep() {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOph", "Putting sensor to sleep");
    
    Wire.beginTransmission(i2cAddress_);
    Wire.write("sleep");
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOph", "Failed to send sleep command (I2C error: " + String(error) + ")");
        return false;
    }
    
    // Note: After sleep command, we don't request data as it would wake the sensor
    logger_->info("EZOph", "Sensor is now sleeping");
    
    return true;
}

bool EZOph::wake() {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOph", "Waking sensor");
    
    // Any I2C communication will wake the sensor
    // Send a simple status command
    Wire.beginTransmission(i2cAddress_);
    Wire.write("status");
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOph", "Failed to wake sensor (I2C error: " + String(error) + ")");
        return false;
    }
    
    delay(COMMAND_DELAY_MS);
    
    logger_->info("EZOph", "Sensor is awake");
    
    return true;
}

bool EZOph::sendCommand(const String& command) {
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOph", "Sending custom command: " + command);
    
    // Determine if this is a reading/calibration command
    bool isReadingCommand = (command.startsWith("r") || command.startsWith("c") || 
                            command.startsWith("R") || command.startsWith("C"));
    
    return sendI2CCommand(command, isReadingCommand);
}

bool EZOph::sendCommandAndGetResponse(const String& command, String& response, ResponseCode& responseCode) {
    response = "";
    responseCode = ResponseCode::NO_DATA;
    
    if (!initialized_) {
        logger_->error("EZOph", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOph", "Sending custom command with response: " + command);
    
    // Determine if this is a reading/calibration command (needs longer delay)
    bool isReadingCommand = (command.startsWith("r") || command.startsWith("c") || 
                            command.startsWith("R") || command.startsWith("C"));
    
    // Send command
    if (!sendI2CCommand(command, isReadingCommand)) {
        logger_->error("EZOph", "Failed to send command");
        return false;
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    // Store response code
    responseCode = lastResponseCode_;
    
    // Check if we got a valid response
    if (bytesRead == 0) {
        logger_->warning("EZOph", "No response data from sensor");
        response = "";
        return (lastResponseCode_ == ResponseCode::SUCCESS);
    }
    
    // Store response string
    response = String(buffer);
    
    logger_->info("EZOph", "Response [" + getResponseCodeDescription(responseCode) + "]: " + response);
    
    return (responseCode == ResponseCode::SUCCESS);
}

String EZOph::getStatus() {
    if (!initialized_) {
        return "Not initialized";
    }
    
    String statusStr = "EZO pH Sensor - Address: 0x" + String(i2cAddress_, HEX);
    statusStr += " | Last Response: " + getResponseCodeDescription(lastResponseCode_);
    statusStr += " | Available: " + String(isAvailable() ? "Yes" : "No");
    
    return statusStr;
}

EZOph::ResponseCode EZOph::getLastResponseCode() const {
    return lastResponseCode_;
}

String EZOph::getResponseCodeDescription(ResponseCode code) const {
    switch (code) {
        case ResponseCode::SUCCESS:
            return "Success";
        case ResponseCode::FAILED:
            return "Failed";
        case ResponseCode::PENDING:
            return "Pending";
        case ResponseCode::NO_DATA:
            return "No Data";
        default:
            return "Unknown (" + String((uint8_t)code) + ")";
    }
}

void EZOph::setI2CAddress(uint8_t address) {
    i2cAddress_ = address;
    logger_->info("EZOph", "I2C address changed to 0x" + String(i2cAddress_, HEX));
}

String EZOph::scanI2CBus() {
    logger_->info("EZOph", "Scanning I2C bus for devices...");
    
    String result = "I2C Scan Results:\n";
    bool deviceFound = false;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
            result += "  Device found at 0x" + String(addr, HEX) + " (" + String(addr) + " decimal)\n";
            deviceFound = true;
            
            // Common I2C device addresses
            if (addr == 0x44) result += "    -> Could be SHT31\n";
            if (addr == 0x63 || addr == 99) result += "    -> Could be EZO pH\n";
            if (addr == 0x64 || addr == 100) result += "    -> Could be EZO sensor\n";
        }
        delay(1);
    }
    
    if (!deviceFound) {
        result += "  No I2C devices found!\n";
        result += "  Check:\n";
        result += "    - Wiring (SDA: GPIO" + String(I2C_SDA_PIN) + ", SCL: GPIO" + String(I2C_SCL_PIN) + ")\n";
        result += "    - Power supply\n";
        result += "    - Pull-up resistors (usually 4.7kΩ)\n";
    }
    
    logger_->info("EZOph", result);
    return result;
}

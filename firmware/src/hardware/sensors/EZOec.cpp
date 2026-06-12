#include "EZOec.h"
#include "../../diagnostics/Logger.h"
#include "config.h"

// Fallback I2C address if not defined
#ifndef EZOEC_I2C_ADDRESS
#define EZOEC_I2C_ADDRESS 0x64  // 100 decimal
#endif

EZOec::EZOec(Logger* logger) 
    : logger_(logger), 
      mqttManager_(nullptr), 
      initialized_(false), 
      i2cAddress_(EZOEC_I2C_ADDRESS),
      lastResponseCode_(ResponseCode::NO_DATA),
      lastCommandTime_(0),
      readingInProgress_(false) {
}

EZOec::~EZOec() {
    // Cleanup if needed
}

bool EZOec::initialize() {
    if (initialized_) {
        logger_->info("EZOec", "Already initialized");
        return true;
    }
    
    logger_->info("EZOec", "Initializing EZO EC sensor...");
    
    // Initialize I2C bus
    if (!initializeI2C()) {
        logger_->error("EZOec", "Failed to initialize I2C bus");
        return false;
    }
    
    // Check if sensor is available on I2C bus
    Wire.beginTransmission(i2cAddress_);
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOec", "EZO EC sensor not found at address 0x" + String(i2cAddress_, HEX) + 
                       " (Error: " + String(error) + "). Use 'i2cscan' command to find devices!");
        return false;
    }
    
    logger_->info("EZOec", "EZO EC sensor detected at address 0x" + String(i2cAddress_, HEX));
    
    // Small delay to let sensor stabilize
    delay(100);
    
    // Verify sensor responds with info command
    if (!sendI2CCommand("i", false)) {
        logger_->warning("EZOec", "Failed to query sensor info, but sensor detected");
    }
    
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    if (bytesRead > 0 && lastResponseCode_ == ResponseCode::SUCCESS) {
        logger_->info("EZOec", "Sensor info: " + String(buffer));
    }
    
    initialized_ = true;
    
    // Enable all output parameters (EC, TDS, SAL, GRAV)
    logger_->info("EZOec", "Configuring output parameters...");
    if (!enableAllOutputs()) {
        logger_->warning("EZOec", "Failed to enable all outputs, sensor will only return EC value");
    }
    
    // Query current output configuration
    delay(300);
    String outputConfig = getOutputConfig();
    logger_->info("EZOec", "Output configuration: " + outputConfig);
    
    logger_->info("EZOec", "EZO EC sensor initialized successfully");
    
    return true;
}

bool EZOec::initializeI2C() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    logger_->debug("EZOec", "I2C initialized - SDA: GPIO" + String(I2C_SDA_PIN) + 
                   ", SCL: GPIO" + String(I2C_SCL_PIN));
    
    // Give I2C bus time to stabilize
    delay(10);
    
    return true;
}

bool EZOec::sendI2CCommand(const String& command, bool isReadingCommand) {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    logger_->debug("EZOec", "Sending command: " + command);
    
    Wire.beginTransmission(i2cAddress_);
    Wire.write(command.c_str());
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOec", "Failed to send command (I2C error: " + String(error) + ")");
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

uint8_t EZOec::readI2CResponse(char* buffer, uint8_t bufferSize) {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return 0;
    }
    
    // Request data from sensor
    uint8_t bytesRequested = Wire.requestFrom((int)i2cAddress_, (int)bufferSize);
    
    if (bytesRequested == 0) {
        logger_->error("EZOec", "No response from sensor");
        lastResponseCode_ = ResponseCode::NO_DATA;
        return 0;
    }
    
    // Read response code (first byte)
    if (Wire.available()) {
        lastResponseCode_ = static_cast<ResponseCode>(Wire.read());
        
        logger_->debug("EZOec", "Response code: " + String((uint8_t)lastResponseCode_) + 
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
    
    logger_->debug("EZOec", "Received: " + String(buffer));
    
    return index;
}

bool EZOec::startReading() {
    if (readingInProgress_) {
        logger_->warning("EZOec", "Reading already in progress");
        return false;
    }
    
    // Send reading command without waiting
    Wire.beginTransmission(i2cAddress_);
    Wire.write("r");  // Reading command
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOec", "Failed to start reading (I2C error: " + String(error) + ")");
        return false;
    }
    
    lastCommandTime_ = millis();
    readingInProgress_ = true;
    
    logger_->debug("EZOec", "Async reading started");
    
    return true;
}

bool EZOec::getReading(EZOecData& data) {
    data.EC = 0.0;
    data.TDS = 0.0;
    data.SAL = 0.0;
    data.GRAV = 0.0;
    data.valid = false;
    data.timestamp = millis();
    
    if (!readingInProgress_) {
        logger_->error("EZOec", "No reading in progress");
        return false;
    }
    
    // Check if enough time has passed
    unsigned long elapsed = millis() - lastCommandTime_;
    if (elapsed < READING_DELAY_MS) {
        logger_->debug("EZOec", "Reading not ready yet (" + String(elapsed) + "ms elapsed)");
        return false;
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    readingInProgress_ = false;
    
    // Check response code
    if (lastResponseCode_ != ResponseCode::SUCCESS) {
        logger_->error("EZOec", "Reading failed: " + getResponseCodeDescription(lastResponseCode_));
        return false;
    }
    
    if (bytesRead == 0) {
        logger_->error("EZOec", "No data received from sensor");
        return false;
    }
    
    // Parse EC values
    if (!parseEcValues(buffer, data)) {
        logger_->error("EZOec", "Failed to parse EC values from: " + String(buffer));
        return false;
    }
    
    logger_->info("EZOec", "EC: " + String(data.EC, 2) + " µS/cm, TDS: " + String(data.TDS, 0) + 
                  " ppm, SAL: " + String(data.SAL, 2) + " PSU, GRAV: " + String(data.GRAV, 3));
    
    return true;
}

EZOecData EZOec::readSensor() {
    EZOecData result = {0.0, 0.0, 0.0, 0.0, false, millis()};
    
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return result;
    }
    
    // Send reading command and wait
    if (!sendI2CCommand("r", true)) {
        logger_->error("EZOec", "Failed to send reading command");
        return result;
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    // Check response code
    if (lastResponseCode_ != ResponseCode::SUCCESS) {
        logger_->error("EZOec", "Reading failed: " + getResponseCodeDescription(lastResponseCode_));
        return result;
    }
    
    if (bytesRead == 0) {
        logger_->error("EZOec", "No data received from sensor");
        return result;
    }
    
    // Parse EC values
    if (!parseEcValues(buffer, result)) {
        logger_->error("EZOec", "Failed to parse EC values from: " + String(buffer));
        return result;
    }
    
    logger_->info("EZOec", "Sensor reading successful - EC: " + String(result.EC, 2) + 
                  " µS/cm, TDS: " + String(result.TDS, 0) + " ppm, SAL: " + String(result.SAL, 2) + 
                  " PSU, GRAV: " + String(result.GRAV, 3));
    
    return result;
}

bool EZOec::parseEcValues(const char* response, EZOecData& data) {
    if (response == nullptr || strlen(response) == 0) {
        return false;
    }
    
    // Debug: Log raw response
    logger_->debug("EZOec", "Raw response: '" + String(response) + "' (length: " + String(strlen(response)) + ")");
    
    // Make a mutable copy of the response string
    char responseCopy[RESPONSE_BUFFER_SIZE];
    strncpy(responseCopy, response, RESPONSE_BUFFER_SIZE - 1);
    responseCopy[RESPONSE_BUFFER_SIZE - 1] = '\0';
    
    // Parse comma-separated values: EC,TDS,SAL,GRAV
    char* token;
    int index = 0;
    
    token = strtok(responseCopy, ",");
    while (token != nullptr && index < 4) {
        float value = atof(token);
        
        logger_->debug("EZOec", "Parsed token[" + String(index) + "]: '" + String(token) + "' -> " + String(value));
        
        switch (index) {
            case 0:  // EC (Electrical Conductivity in µS/cm)
                data.EC = value;
                break;
            case 1:  // TDS (Total Dissolved Solids in ppm)
                data.TDS = value;
                break;
            case 2:  // SAL (Salinity in PSU)
                data.SAL = value;
                break;
            case 3:  // GRAV (Specific Gravity)
                data.GRAV = value;
                break;
        }
        
        token = strtok(nullptr, ",");
        index++;
    }
    
    logger_->debug("EZOec", "Parsed " + String(index) + " value(s) from response");
    
    // Validate that we got at least EC value
    if (index < 1) {
        logger_->error("EZOec", "Failed to parse any values from response");
        return false;
    }
    
    // Mark as valid and set timestamp
    data.valid = true;
    data.timestamp = millis();
    
    return true;
}

bool EZOec::readAndPublishData(const String& topic) {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->error("EZOec", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("EZOec", "MQTT not connected");
        return false;
    }
    
    // Read sensor data
    EZOecData sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("EZOec", "Failed to read sensor data");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("EZOec", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("EZOec", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("EZOec", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void EZOec::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

String EZOec::createJsonPayload(const EZOecData& data) {
    DynamicJsonDocument doc(512);
    
    // Sensor identification
    doc["sensor_type"] = "EZOec";
    
    // Sensor data (rounded to appropriate decimal places)
    doc["EC"] = round(data.EC * 100.0) / 100.0;           // 2 decimal places
    doc["TDS"] = round(data.TDS);                         // No decimal places
    doc["SAL"] = round(data.SAL * 100.0) / 100.0;         // 2 decimal places
    doc["GRAV"] = round(data.GRAV * 1000.0) / 1000.0;     // 3 decimal places
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        logger_->error("EZOec", "Failed to serialize JSON");
        return "";
    }
    
    return jsonString;
}

bool EZOec::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    // Try to communicate with sensor
    Wire.beginTransmission(i2cAddress_);
    byte error = Wire.endTransmission();
    
    return (error == 0);
}

bool EZOec::sleep() {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOec", "Putting sensor to sleep");
    
    Wire.beginTransmission(i2cAddress_);
    Wire.write("sleep");
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOec", "Failed to send sleep command (I2C error: " + String(error) + ")");
        return false;
    }
    
    // Note: After sleep command, we don't request data as it would wake the sensor
    logger_->info("EZOec", "Sensor is now sleeping");
    
    return true;
}

bool EZOec::wake() {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOec", "Waking sensor");
    
    // Any I2C communication will wake the sensor
    // Send a simple status command
    Wire.beginTransmission(i2cAddress_);
    Wire.write("status");
    byte error = Wire.endTransmission();
    
    if (error != 0) {
        logger_->error("EZOec", "Failed to wake sensor (I2C error: " + String(error) + ")");
        return false;
    }
    
    delay(COMMAND_DELAY_MS);
    
    logger_->info("EZOec", "Sensor is awake");
    
    return true;
}

bool EZOec::enableAllOutputs() {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOec", "Enabling all output parameters (EC, TDS, SAL, GRAV)");
    
    // Send command to enable all outputs: O,EC,1 O,TDS,1 O,S,1 O,SG,1
    // Or use: O,EC,TDS,S,SG,1 to enable all at once
    if (!sendI2CCommand("O,EC,1", false)) {
        logger_->error("EZOec", "Failed to enable EC output");
        return false;
    }
    
    delay(100);
    
    if (!sendI2CCommand("O,TDS,1", false)) {
        logger_->error("EZOec", "Failed to enable TDS output");
        return false;
    }
    
    delay(100);
    
    if (!sendI2CCommand("O,S,1", false)) {
        logger_->error("EZOec", "Failed to enable Salinity output");
        return false;
    }
    
    delay(100);
    
    if (!sendI2CCommand("O,SG,1", false)) {
        logger_->error("EZOec", "Failed to enable Specific Gravity output");
        return false;
    }
    
    logger_->info("EZOec", "All output parameters enabled successfully");
    
    return true;
}

String EZOec::getOutputConfig() {
    if (!initialized_) {
        return "Sensor not initialized";
    }
    
    logger_->debug("EZOec", "Querying output configuration");
    
    // Send output query command
    if (!sendI2CCommand("O,?", false)) {
        return "Failed to query output configuration";
    }
    
    // Read response
    char buffer[RESPONSE_BUFFER_SIZE];
    uint8_t bytesRead = readI2CResponse(buffer, RESPONSE_BUFFER_SIZE);
    
    if (bytesRead > 0 && lastResponseCode_ == ResponseCode::SUCCESS) {
        return String(buffer);
    }
    
    return "No response from sensor";
}

bool EZOec::sendCommand(const String& command) {
    if (!initialized_) {
        logger_->error("EZOec", "Sensor not initialized");
        return false;
    }
    
    logger_->info("EZOec", "Sending custom command: " + command);
    
    // Determine if this is a reading/calibration command
    bool isReadingCommand = (command.startsWith("r") || command.startsWith("c") || 
                            command.startsWith("R") || command.startsWith("C"));
    
    return sendI2CCommand(command, isReadingCommand);
}

String EZOec::getStatus() {
    if (!initialized_) {
        return "Not initialized";
    }
    
    String statusStr = "EZO EC Sensor - Address: 0x" + String(i2cAddress_, HEX);
    statusStr += " | Last Response: " + getResponseCodeDescription(lastResponseCode_);
    statusStr += " | Available: " + String(isAvailable() ? "Yes" : "No");
    
    return statusStr;
}

EZOec::ResponseCode EZOec::getLastResponseCode() const {
    return lastResponseCode_;
}

String EZOec::getResponseCodeDescription(ResponseCode code) const {
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

void EZOec::setI2CAddress(uint8_t address) {
    i2cAddress_ = address;
    logger_->info("EZOec", "I2C address changed to 0x" + String(i2cAddress_, HEX));
}

String EZOec::scanI2CBus() {
    logger_->info("EZOec", "Scanning I2C bus for devices...");
    
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
            if (addr == 0x64 || addr == 100) result += "    -> Could be EZO EC\n";
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
    
    logger_->info("EZOec", result);
    return result;
}

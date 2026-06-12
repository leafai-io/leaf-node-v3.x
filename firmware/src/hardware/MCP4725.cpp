#include "MCP4725.h"
#include "config.h"

// MCP4725 I2C Commands
#define MCP4725_CMD_WRITEDAC        0x40  // Fast mode: write to DAC register
#define MCP4725_CMD_WRITEDACEEPROM  0x60  // Write to DAC register and EEPROM

// Logger helper macros - use Serial if logger is nullptr
#define LOG_INFO(tag, msg) if (logger_) logger_->info(tag, msg); else Serial.println("[" + String(tag) + "] " + msg)
#define LOG_ERROR(tag, msg) if (logger_) logger_->error(tag, msg); else Serial.println("[" + String(tag) + "] ERROR: " + msg)
#define LOG_WARNING(tag, msg) if (logger_) logger_->warning(tag, msg); else Serial.println("[" + String(tag) + "] WARNING: " + msg)
#define LOG_DEBUG(tag, msg) if (logger_) logger_->debug(tag, msg); else Serial.println("[" + String(tag) + "] DEBUG: " + msg)

MCP4725::MCP4725(Logger* logger, float vdd, float outputGain) 
    : logger_(logger)
    , mqttManager_(nullptr)
    , i2cAddress_(0x60)
    , vdd_(vdd)
    , outputGain_(outputGain)
    , currentValue_(0)
    , initialized_(false)
    , ledUpdateCallback_(nullptr)
    , lastCommandType_("") {
}

MCP4725::~MCP4725() {
    if (initialized_) {
        reset();  // Safety: Reset to zero on destruction
    }
}

bool MCP4725::initialize(uint8_t address) {
    if (initialized_) {
        LOG_INFO("MCP4725", "Already initialized");
        return true;
    }
    
    i2cAddress_ = address;
    
    LOG_INFO("MCP4725", "Initializing MCP4725 DAC at address 0x" + String(i2cAddress_, HEX));
    
    // Initialize I2C bus
    if (!initializeI2C()) {
        LOG_ERROR("MCP4725", "Failed to initialize I2C bus");
        return false;
    }
    
    // Check if device is present on I2C bus
    Wire.beginTransmission(i2cAddress_);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        LOG_ERROR("MCP4725", "Device not found at address 0x" + String(i2cAddress_, HEX));
        return false;
    }
    
    // Read current settings
    uint16_t dacValue, eepromValue;
    PowerDownMode powerDown;
    if (readSettings(dacValue, eepromValue, powerDown)) {
        currentValue_ = dacValue;
        LOG_INFO("MCP4725", "Current DAC value: " + String(dacValue) + 
                         " (" + String(valueToVoltage(dacValue), 3) + "V)");
    }
    
    initialized_ = true;
    
    // Set DAC to 0V as safe default
    LOG_INFO("MCP4725", "Setting DAC to safe default (0V)");
    setValue(0);
    
    LOG_INFO("MCP4725", "MCP4725 DAC initialized successfully");
    
    return true;
}

bool MCP4725::initializeI2C() {
    // Initialize I2C with custom pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    LOG_DEBUG("MCP4725", "I2C initialized - SDA: GPIO" + String(I2C_SDA_PIN) + 
                      ", SCL: GPIO" + String(I2C_SCL_PIN));
    
    // Give I2C bus time to stabilize
    delay(10);
    
    return true;
}

bool MCP4725::setValue(uint16_t value, PowerDownMode powerDown) {
    if (!initialized_) {
        LOG_ERROR("MCP4725", "DAC not initialized");
        return false;
    }
    
    value = clampValue(value);
    
    if (writeDAC(value, MCP4725_CMD_WRITEDAC, powerDown)) {
        currentValue_ = value;
        lastCommandType_ = "value";
        LOG_INFO("MCP4725", "DAC set to value " + String(value) + 
                     " (" + String(getCurrentVoltage(), 3) + "V)");
        
        // Notify LED update if callback is set
        if (ledUpdateCallback_) {
            ledUpdateCallback_();
        }
        
        return true;
    }
    
    return false;
}

bool MCP4725::setVoltage(float voltage) {
    // Clamp to output range (accounting for amplifier gain)
    float maxVoltage = vdd_ * outputGain_;
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > maxVoltage) voltage = maxVoltage;
    
    // Calculate required DAC voltage (before amplifier)
    float dacVoltage = voltage / outputGain_;
    
    uint16_t value = voltageToValue(dacVoltage);
    
    LOG_DEBUG("MCP4725", "Setting output voltage " + String(voltage, 3) + "V " +
              "(DAC voltage: " + String(dacVoltage, 3) + "V, value: " + String(value) + ")");
    
    bool success = setValue(value);
    if (success) {
        lastCommandType_ = "voltage";
    }
    return success;
}

bool MCP4725::setRawVoltage(float voltage) {
    // Set raw DAC voltage (without accounting for amplifier)
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > vdd_) voltage = vdd_;
    
    uint16_t value = voltageToValue(voltage);
    
    LOG_DEBUG("MCP4725", "Setting raw DAC voltage " + String(voltage, 3) + "V (value: " + String(value) + ")");
    
    return setValue(value);
}

bool MCP4725::setPercent(float percent) {
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    
    uint16_t value = (uint16_t)((percent / 100.0f) * 4095.0f);
    
    LOG_DEBUG("MCP4725", "Setting " + String(percent, 1) + "% (value: " + String(value) + ")");
    
    bool success = setValue(value);
    if (success) {
        lastCommandType_ = "percent";
    }
    return success;
}

bool MCP4725::setValueWithEEPROM(uint16_t value, PowerDownMode powerDown) {
    if (!initialized_) {
        LOG_ERROR("MCP4725", "DAC not initialized");
        return false;
    }
    
    value = clampValue(value);
    
    LOG_WARNING("MCP4725", "Writing to EEPROM (limited write cycles!)");
    
    if (writeDAC(value, MCP4725_CMD_WRITEDACEEPROM, powerDown)) {
        currentValue_ = value;
        LOG_INFO("MCP4725", "DAC and EEPROM set to value " + String(value) + 
                     " (" + String(getCurrentVoltage(), 3) + "V)");
        
        // EEPROM write takes ~50ms according to datasheet
        delay(50);
        
        return true;
    }
    
    return false;
}

uint16_t MCP4725::readValue() {
    if (!initialized_) {
        LOG_ERROR("MCP4725", "DAC not initialized");
        return 0xFFFF;
    }
    
    // Request 5 bytes from device
    Wire.requestFrom(i2cAddress_, (uint8_t)5);
    
    if (Wire.available() < 3) {
        LOG_ERROR("MCP4725", "Failed to read from device");
        return 0xFFFF;
    }
    
    // Read status byte (unused but required to read from I2C buffer)
    (void)Wire.read();
    
    // Read DAC value (12 bits across 2 bytes)
    uint8_t upperByte = Wire.read();
    uint8_t lowerByte = Wire.read();
    
    // Combine into 12-bit value
    uint16_t value = ((uint16_t)upperByte << 4) | (lowerByte >> 4);
    
    // Read remaining bytes to clear buffer
    while (Wire.available()) {
        Wire.read();
    }
    
    return value;
}

bool MCP4725::readSettings(uint16_t& dacValue, uint16_t& eepromValue, PowerDownMode& powerDown) {
    if (!initialized_) {
        LOG_ERROR("MCP4725", "DAC not initialized");
        return false;
    }
    
    // Request 5 bytes from device
    Wire.requestFrom(i2cAddress_, (uint8_t)5);
    
    if (Wire.available() < 5) {
        LOG_ERROR("MCP4725", "Failed to read settings from device");
        return false;
    }
    
    // Byte 1: Status/PowerDown
    uint8_t status = Wire.read();
    
    // Byte 2-3: Current DAC value
    uint8_t dacUpper = Wire.read();
    uint8_t dacLower = Wire.read();
    dacValue = ((uint16_t)dacUpper << 4) | (dacLower >> 4);
    
    // Byte 4-5: EEPROM value
    uint8_t eepromUpper = Wire.read();
    uint8_t eepromLower = Wire.read();
    eepromValue = ((uint16_t)(eepromUpper & 0x0F) << 8) | eepromLower;
    
    // Extract power-down mode from status
    powerDown = (PowerDownMode)((status >> 1) & 0x03);
    
    return true;
}

float MCP4725::getCurrentVoltage() const {
    // Return final output voltage (after amplifier)
    return valueToVoltage(currentValue_) * outputGain_;
}

float MCP4725::getCurrentPercent() const {
    return (currentValue_ / 4095.0f) * 100.0f;
}

bool MCP4725::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    Wire.beginTransmission(i2cAddress_);
    uint8_t error = Wire.endTransmission();
    
    return (error == 0);
}

void MCP4725::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

void MCP4725::setLEDUpdateCallback(void (*callback)()) {
    ledUpdateCallback_ = callback;
}

bool MCP4725::publishStatus(const String& topic) {
    if (!mqttManager_ || !mqttManager_->isConnected()) {
        LOG_WARNING("MCP4725", "MQTT not available for publishing");
        return false;
    }
    
    String payload = createStatusPayload();
    if (payload.isEmpty()) {
        LOG_ERROR("MCP4725", "Failed to create status payload");
        return false;
    }
    
    if (mqttManager_->publish(topic, payload)) {
        LOG_INFO("MCP4725", "Status published to: " + topic);
        return true;
    }
    
    LOG_ERROR("MCP4725", "Failed to publish status");
    return false;
}

bool MCP4725::handleCommand(const String& command, JsonVariantConst params) {
    if (!initialized_) {
        LOG_ERROR("MCP4725", "DAC not initialized");
        return false;
    }
    
    LOG_INFO("MCP4725", "Executing command: " + command);
    
    // Extract optional save_eeprom flag
    bool saveEEPROM = false;
    if (params.containsKey("save_eeprom") && params["save_eeprom"].is<bool>()) {
        saveEEPROM = params["save_eeprom"].as<bool>();
    }
    
    // Execute command based on command name
    bool success = false;
    
    if (command == "set_value") {
        if (!params.containsKey("value")) {
            LOG_ERROR("MCP4725", "Missing 'value' parameter for set_value command");
            return false;
        }
        
        // Handle both string and numeric value
        uint16_t value = 0;
        if (params["value"].is<int>() || params["value"].is<unsigned int>()) {
            value = params["value"].as<uint16_t>();
        } else if (params["value"].is<const char*>()) {
            // Parse string to int
            String valueStr = params["value"].as<const char*>();
            value = valueStr.toInt();
        }
        
        LOG_INFO("MCP4725", "Parsed value: " + String(value));
        
        success = saveEEPROM ? setValueWithEEPROM(value) : setValue(value);
    }
    else if (command == "set_voltage") {
        if (!params.containsKey("voltage")) {
            LOG_ERROR("MCP4725", "Missing 'voltage' parameter for set_voltage command");
            return false;
        }
        
        // Handle both string and numeric voltage values
        float voltage = 0.0;
        if (params["voltage"].is<float>() || params["voltage"].is<double>()) {
            voltage = params["voltage"].as<float>();
        } else if (params["voltage"].is<const char*>()) {
            // Parse string to float
            String voltageStr = params["voltage"].as<const char*>();
            voltage = voltageStr.toFloat();
        } else if (params["voltage"].is<int>()) {
            voltage = params["voltage"].as<int>();
        }
        
        LOG_INFO("MCP4725", "Parsed voltage: " + String(voltage, 3) + "V");
        
        success = setVoltage(voltage);
        if (success && saveEEPROM) {
            success = setValueWithEEPROM(currentValue_);
        }
    }
    else if (command == "set_percent") {
        if (!params.containsKey("percent")) {
            LOG_ERROR("MCP4725", "Missing 'percent' parameter for set_percent command");
            return false;
        }
        
        // Handle both string and numeric percent values
        float percent = 0.0;
        if (params["percent"].is<float>() || params["percent"].is<double>()) {
            percent = params["percent"].as<float>();
        } else if (params["percent"].is<const char*>()) {
            // Parse string to float
            String percentStr = params["percent"].as<const char*>();
            percent = percentStr.toFloat();
        } else if (params["percent"].is<int>()) {
            percent = params["percent"].as<int>();
        }
        
        LOG_INFO("MCP4725", "Parsed percent: " + String(percent, 1) + "%");
        
        success = setPercent(percent);
        if (success && saveEEPROM) {
            success = setValueWithEEPROM(currentValue_);
        }
    }
    else if (command == "power_down") {
        PowerDownMode mode = PD_1K;
        if (params.containsKey("mode") && params["mode"].is<int>()) {
            int modeVal = params["mode"].as<int>();
            if (modeVal >= 0 && modeVal <= 3) {
                mode = (PowerDownMode)modeVal;
            }
        }
        success = setValue(0, mode);
    }
    else if (command == "reset") {
        success = reset();
    }
    else if (command == "read") {
        uint16_t value = readValue();
        if (value != 0xFFFF) {
            LOG_INFO("MCP4725", "Current DAC value: " + String(value) + 
                         " (" + String(getCurrentVoltage(), 3) + "V)");
            success = true;
        }
    }
    else if (command == "status") {
        LOG_INFO("MCP4725", getStatus());
        success = true;
    }
    else {
        LOG_ERROR("MCP4725", "Unknown DAC command: " + command);
        return false;
    }
    
    if (success) {
        LOG_INFO("MCP4725", "Command executed successfully");
    } else {
        LOG_ERROR("MCP4725", "Command execution failed");
    }
    
    return success;
}

String MCP4725::getStatus() {
    if (!initialized_) {
        return "Not initialized";
    }
    
    String status = "MCP4725 @ 0x" + String(i2cAddress_, HEX);
    status += " | Value: " + String(currentValue_);
    status += " | DAC: " + String(valueToVoltage(currentValue_), 3) + "V";
    status += " | Output: " + String(getCurrentVoltage(), 3) + "V";
    status += " | Percent: " + String(getCurrentPercent(), 1) + "%";
    
    return status;
}

bool MCP4725::reset() {
    LOG_INFO("MCP4725", "Resetting DAC to zero");
    return setValue(0);
}

// Private helper methods

bool MCP4725::writeDAC(uint16_t value, uint8_t mode, uint8_t powerDown) {
    value = clampValue(value);
    
    Wire.beginTransmission(i2cAddress_);
    
    // MCP4725 Fast Mode: Send 3 bytes
    // Byte 1: Command/Power-Down bits [C2][C1][C0][0][PD1][PD0][X][X]
    // Byte 2: Upper 8 bits of 12-bit value [D11][D10][D9][D8][D7][D6][D5][D4]
    // Byte 3: Lower 4 bits of 12-bit value [D3][D2][D1][D0][X][X][X][X]
    
    uint8_t byte1 = mode | (powerDown << 1);  // Command byte
    uint8_t byte2 = (value >> 4) & 0xFF;      // Upper 8 bits
    uint8_t byte3 = (value << 4) & 0xFF;      // Lower 4 bits, left-aligned
    
    Wire.write(byte1);
    Wire.write(byte2);
    Wire.write(byte3);
    
    Serial.println("[MCP4725] Writing to I2C (3 bytes): cmd=0x" + String(byte1, HEX) + 
                   " upper=0x" + String(byte2, HEX) + " lower=0x" + String(byte3, HEX) +
                   " (value=" + String(value) + ")");
    
    uint8_t error = Wire.endTransmission();
    
    Serial.println("[MCP4725] I2C endTransmission result: " + String(error));
    
    if (error != 0) {
        LOG_ERROR("MCP4725", "I2C write failed with error: " + String(error));
        return false;
    }
    
    // Verify: Read back the value
    delay(5);  // Give DAC time to settle
    uint16_t readBack = readValue();
    if (readBack != 0xFFFF) {
        Serial.println("[MCP4725] Read back value: " + String(readBack) + " (expected: " + String(value) + ")");
    }
    
    return true;
}

float MCP4725::valueToVoltage(uint16_t value) const {
    return (value / 4095.0f) * vdd_;
}

uint16_t MCP4725::voltageToValue(float voltage) const {
    return (uint16_t)((voltage / vdd_) * 4095.0f);
}

String MCP4725::createStatusPayload() {
    DynamicJsonDocument doc(512);
    
    doc["device_type"] = "MCP4725";
    doc["i2c_address"] = "0x" + String(i2cAddress_, HEX);
    doc["value"] = currentValue_;
    doc["voltage"] = round(getCurrentVoltage() * 1000.0f) / 1000.0f;  // 3 decimals
    doc["percent"] = round(getCurrentPercent() * 10.0f) / 10.0f;      // 1 decimal
    doc["vdd"] = vdd_;
    doc["timestamp"] = millis();
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        LOG_ERROR("MCP4725", "Failed to serialize status JSON");
        return "";
    }
    
    return jsonString;
}

uint16_t MCP4725::clampValue(uint16_t value) const {
    if (value > 4095) {
        LOG_WARNING("MCP4725", "Value " + String(value) + " clamped to 4095");
        return 4095;
    }
    return value;
}

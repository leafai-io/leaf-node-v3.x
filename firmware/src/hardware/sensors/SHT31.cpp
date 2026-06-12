#include "SHT31.h"
#include "../../diagnostics/Logger.h"
#include "config.h"

// Fallback I2C address if not defined
#ifndef SHT31_I2C_ADDRESS
#define SHT31_I2C_ADDRESS 0x44
#endif

SHT31::SHT31(Logger* logger) 
    : logger_(logger), mqttManager_(nullptr), sht31_(nullptr), initialized_(false), i2cAddress_(SHT31_I2C_ADDRESS) {
}

SHT31::~SHT31() {
    if (sht31_) {
        delete sht31_;
        sht31_ = nullptr;
    }
}

bool SHT31::initialize() {
    if (initialized_) {
        logger_->info("SHT31", "Already initialized");
        return true;
    }
    
    logger_->info("SHT31", "Initializing SHT31 sensor...");
    
    // Initialize I2C bus
    if (!initializeI2C()) {
        logger_->error("SHT31", "Failed to initialize I2C bus");
        return false;
    }
    
    // Create sensor instance
    sht31_ = new Adafruit_SHT31();
    if (!sht31_) {
        logger_->error("SHT31", "Failed to allocate SHT31 instance");
        return false;
    }
    
    // Initialize sensor with I2C address
    if (!sht31_->begin(i2cAddress_)) {
        logger_->error("SHT31", "Failed to initialize SHT31 sensor at address 0x" + String(i2cAddress_, HEX));
        delete sht31_;
        sht31_ = nullptr;
        return false;
    }
    
    // Enable heater for initial test (optional, helps with reliability)
    sht31_->heater(false);
    
    initialized_ = true;
    logger_->info("SHT31", "SHT31 sensor initialized successfully at address 0x" + String(i2cAddress_, HEX));
    
    return true;
}

bool SHT31::initializeI2C() {
    // Check if I2C is already initialized - avoid re-initialization warnings
    static bool i2cInitialized = false;
    
    if (!i2cInitialized) {
        // Initialize I2C with custom pins
        Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
        i2cInitialized = true;
        
        logger_->debug("SHT31", "I2C initialized - SDA: GPIO" + String(I2C_SDA_PIN) + ", SCL: GPIO" + String(I2C_SCL_PIN));
        
        // Give I2C bus time to stabilize
        delay(10);
    } else {
        logger_->debug("SHT31", "I2C already initialized, skipping re-initialization");
    }
    
    return true;
}

SHT31Data SHT31::readSensor() {
    SHT31Data result = {0.0, 0.0, false, millis()};
    
    if (!initialized_ || !sht31_) {
        logger_->debug("SHT31", "Sensor not initialized - skipping read");
        return result;
    }
    
    // Read temperature and humidity
    float temperature = sht31_->readTemperature();
    float humidity = sht31_->readHumidity();
    
    // Check if readings are valid
    if (isnan(temperature) || isnan(humidity)) {
        logger_->warning("SHT31", "Failed to read sensor data - check I2C connection");
        return result;
    }
    
    // Populate result structure
    result.temperature = temperature;
    result.humidity = humidity;
    result.valid = true;
    result.timestamp = millis();
    
    logger_->info("SHT31", "Sensor reading successful - Temp: " + String(result.temperature, 2) + 
                "°C, Humidity: " + String(result.humidity, 2) + "%");
    
    return result;
}

bool SHT31::readAndPublishData(const String& topic) {
    if (!initialized_ || !sht31_) {
        logger_->debug("SHT31", "Sensor not initialized - skipping publish");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->debug("SHT31", "MQTT manager not set - skipping publish");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->debug("SHT31", "MQTT not connected - skipping publish");
        return false;
    }
    
    // Read sensor data
    SHT31Data sensorData = readSensor();
    if (!sensorData.valid) {
        // Error already logged in readSensor()
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("SHT31", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("SHT31", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->warning("SHT31", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void SHT31::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

String SHT31::createJsonPayload(const SHT31Data& data) {
    DynamicJsonDocument doc(512);
    
    // Sensor identification
    doc["sensor_type"] = "SHT31";
    
    // Sensor data (rounded to 2 decimal places)
    doc["temperature"] = round(data.temperature * 100.0) / 100.0;
    doc["humidity"] = round(data.humidity * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        logger_->error("SHT31", "Failed to serialize JSON");
        return "";
    }
    
    return jsonString;
}

bool SHT31::isAvailable() {
    if (!initialized_ || !sht31_) {
        return false;
    }
    
    // Try to read status register as availability check
    uint16_t status = sht31_->readStatus();
    
    return (status != 0xFFFF);  // 0xFFFF indicates communication error
}

String SHT31::getStatus() {
    if (!initialized_ || !sht31_) {
        return "Not initialized";
    }
    
    uint16_t status = sht31_->readStatus();
    
    String statusStr = "SHT31 Status: 0x" + String(status, HEX);
    statusStr += " - Heater: " + String(sht31_->isHeaterEnabled() ? "ON" : "OFF");
    
    return statusStr;
}

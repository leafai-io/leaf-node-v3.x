#include "DS18B20.h"
#include "config.h"

DS18B20::DS18B20(OneWireManager* oneWireManager, Logger* logger, uint8_t deviceIndex)
    : oneWireManager_(oneWireManager), logger_(logger), mqttManager_(nullptr), 
      deviceIndex_(deviceIndex), initialized_(false), hasValidAddress_(false) {
    memset(deviceAddress_, 0, 8);
}

DS18B20::~DS18B20() {
    // Cleanup handled by manager
}

bool DS18B20::initialize() {
    logger_->info("DS18B20", "Initializing DS18B20 sensor...");
    
    if (!oneWireManager_ || !oneWireManager_->isReady()) {
        logger_->error("DS18B20", "OneWire manager not ready");
        return false;
    }
    
    // Check if devices were discovered
    uint8_t deviceCount = oneWireManager_->getDeviceCount();
    
    if (deviceCount == 0) {
        logger_->error("DS18B20", "No OneWire devices found on bus");
        return false;
    }
    
    if (deviceIndex_ >= deviceCount) {
        logger_->error("DS18B20", "Device index " + String(deviceIndex_) + 
                      " out of range (found " + String(deviceCount) + " device(s))");
        return false;
    }
    
    // Get device address
    if (!oneWireManager_->getDeviceAddress(deviceIndex_, deviceAddress_)) {
        logger_->error("DS18B20", "Failed to get device address for index " + String(deviceIndex_));
        return false;
    }
    
    hasValidAddress_ = true;
    
    initialized_ = true;
    logger_->info("DS18B20", "DS18B20 sensor initialized successfully");
    logger_->info("DS18B20", "Device index: " + String(deviceIndex_));
    logger_->info("DS18B20", "Device address: " + OneWireManager::addressToString(deviceAddress_));
    logger_->info("DS18B20", "Total devices on bus: " + String(deviceCount));
    
    return true;
}

DS18B20Data DS18B20::readSensor() {
    DS18B20Data result = {0.0, "", false, millis()};
    
    if (!initialized_ || !hasValidAddress_) {
        logger_->debug("DS18B20", "Sensor not initialized - skipping read");
        return result;
    }
    
    if (!oneWireManager_ || !oneWireManager_->isReady()) {
        logger_->warning("DS18B20", "OneWire manager not ready");
        return result;
    }
    
    // Request temperature reading
    oneWireManager_->requestTemperatures();
    
    // Wait for conversion (12-bit resolution = 750ms max)
    delay(800);
    
    // Read temperature
    float temperature = oneWireManager_->getTemperatureByIndex(deviceIndex_);
    
    // Check for valid reading
    if (temperature == DEVICE_DISCONNECTED_C) {
        logger_->warning("DS18B20", "Failed to read temperature - device disconnected or error");
        return result;
    }
    
    // Populate result structure
    result.temperature = temperature;
    result.deviceAddress = OneWireManager::addressToString(deviceAddress_);
    result.valid = true;
    result.timestamp = millis();
    
    // Validate data
    if (!validateData(result)) {
        logger_->warning("DS18B20", "Sensor data validation failed");
        result.valid = false;
        return result;
    }
    
    logger_->info("DS18B20", "Sensor reading successful - Temp: " + 
                 String(result.temperature, 2) + "°C, Device: " + result.deviceAddress);
    
    return result;
}

bool DS18B20::readAndPublishData(const String& topic) {
    if (!mqttManager_) {
        logger_->error("DS18B20", "MQTT manager not set");
        return false;
    }
    
    // Read sensor data
    DS18B20Data data = readSensor();
    
    if (!data.valid) {
        logger_->warning("DS18B20", "Failed to read valid sensor data");
        return false;
    }
    
    // Create JSON payload
    String payload = createJsonPayload(data);
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, payload)) {
        logger_->info("DS18B20", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("DS18B20", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void DS18B20::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

String DS18B20::createJsonPayload(const DS18B20Data& data) {
    StaticJsonDocument<256> doc;
    
    // Sensor identification
    doc["sensor_type"] = "DS18B20";
    
    // Sensor data
    doc["temperature"] = serialized(String(data.temperature, 2));
    
    // Metadata
    doc["timestamp"] = data.timestamp;
    
    String payload;
    serializeJson(doc, payload);
    
    return payload;
}

bool DS18B20::validateData(const DS18B20Data& data) {
    // Temperature range check (-55°C to +125°C is DS18B20 spec)
    if (data.temperature < -55.0 || data.temperature > 125.0) {
        logger_->error("DS18B20", "Temperature out of range: " + String(data.temperature, 2) + "°C");
        return false;
    }
    
    // Check for typical error values
    if (data.temperature == 85.0) {
        logger_->warning("DS18B20", "Temperature is 85°C - this may indicate power-on reset value");
        // Don't fail, but log warning (85°C is default value after power-on)
    }
    
    // Sanity check for typical environmental range (optional warning)
    if (data.temperature < -40.0 || data.temperature > 85.0) {
        logger_->warning("DS18B20", "Temperature outside typical range: " + String(data.temperature, 2) + "°C");
        // Don't fail, just warn
    }
    
    return true;
}

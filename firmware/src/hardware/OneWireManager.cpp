#include "OneWireManager.h"

OneWireManager::OneWireManager(Logger& logger) 
    : logger_(logger), oneWire_(nullptr), sensors_(nullptr), dataPin_(-1), 
      initialized_(false), deviceCount_(0) {
    resetStatistics();
}

OneWireManager::~OneWireManager() {
    shutdown();
}

bool OneWireManager::initialize(int dataPin) {
    logger_.info("OneWireManager", "Initializing OneWire communication...");
    
    dataPin_ = dataPin;
    
    // Create OneWire instance
    oneWire_ = new OneWire(dataPin);
    
    if (!oneWire_) {
        logger_.error("OneWireManager", "Failed to create OneWire instance");
        return false;
    }
    
    // Create DallasTemperature instance
    sensors_ = new DallasTemperature(oneWire_);
    
    if (!sensors_) {
        logger_.error("OneWireManager", "Failed to create DallasTemperature instance");
        delete oneWire_;
        oneWire_ = nullptr;
        return false;
    }
    
    // Initialize sensors
    sensors_->begin();
    
    // Mark as initialized before discovering (needed for isReady() check)
    initialized_ = true;
    
    // Discover devices
    deviceCount_ = discoverDevices();
    
    logger_.info("OneWireManager", "OneWire manager initialized successfully");
    logger_.info("OneWireManager", "Data Pin: " + String(dataPin));
    logger_.info("OneWireManager", "Devices found: " + String(deviceCount_));
    
    return true;
}

bool OneWireManager::isReady() const {
    return initialized_ && oneWire_ != nullptr && sensors_ != nullptr;
}

void OneWireManager::shutdown() {
    if (!initialized_) {
        return; // Already shutdown
    }
    
    logger_.info("OneWireManager", "Shutting down OneWire communication...");
    
    if (sensors_) {
        delete sensors_;
        sensors_ = nullptr;
    }
    
    if (oneWire_) {
        delete oneWire_;
        oneWire_ = nullptr;
    }
    
    initialized_ = false;
    dataPin_ = -1;
    deviceCount_ = 0;
    
    logger_.info("OneWireManager", "OneWire manager shutdown complete");
}

uint8_t OneWireManager::discoverDevices() {
    if (!isReady()) {
        logger_.error("OneWireManager", "OneWire not initialized");
        return 0;
    }
    
    logger_.info("OneWireManager", "Discovering OneWire devices on pin " + String(dataPin_) + "...");
    
    // Get device count
    deviceCount_ = sensors_->getDeviceCount();
    
    logger_.debug("OneWireManager", "DallasTemperature reports " + String(deviceCount_) + " device(s)");
    
    if (deviceCount_ == 0) {
        logger_.warning("OneWireManager", "No OneWire devices found on bus");
        logger_.info("OneWireManager", "Check: 1) Hardware bridge position, 2) Pull-up resistor (4.7kΩ), 3) Sensor connection");
        return 0;
    }
    
    // Log discovered devices
    for (uint8_t i = 0; i < deviceCount_; i++) {
        uint8_t address[8];
        if (sensors_->getAddress(address, i)) {
            logger_.info("OneWireManager", "Device " + String(i) + ": " + addressToString(address));
            
            // Set resolution to 12-bit (highest precision)
            sensors_->setResolution(address, 12);
        }
    }
    
    logger_.info("OneWireManager", "Discovery complete - found " + String(deviceCount_) + " device(s)");
    
    return deviceCount_;
}

uint8_t OneWireManager::getDeviceCount() const {
    return deviceCount_;
}

bool OneWireManager::getDeviceAddress(uint8_t index, uint8_t* address) {
    if (!isReady()) {
        logger_.error("OneWireManager", "OneWire not initialized");
        return false;
    }
    
    if (index >= deviceCount_) {
        logger_.error("OneWireManager", "Device index out of range: " + String(index));
        return false;
    }
    
    return sensors_->getAddress(address, index);
}

void OneWireManager::requestTemperatures() {
    if (!isReady()) {
        logger_.error("OneWireManager", "OneWire not initialized");
        return;
    }
    
    stats_.totalRequests++;
    sensors_->requestTemperatures();
}

float OneWireManager::getTemperatureByIndex(uint8_t index) {
    if (!isReady()) {
        logger_.error("OneWireManager", "OneWire not initialized");
        stats_.failedReads++;
        return DEVICE_DISCONNECTED_C;
    }
    
    if (index >= deviceCount_) {
        logger_.error("OneWireManager", "Device index out of range: " + String(index));
        stats_.failedReads++;
        return DEVICE_DISCONNECTED_C;
    }
    
    float temp = sensors_->getTempCByIndex(index);
    
    if (temp == DEVICE_DISCONNECTED_C) {
        logger_.warning("OneWireManager", "Failed to read temperature from device " + String(index));
        stats_.failedReads++;
        return DEVICE_DISCONNECTED_C;
    }
    
    stats_.successfulReads++;
    logger_.debug("OneWireManager", "Device " + String(index) + " temperature: " + String(temp, 2) + "°C");
    
    return temp;
}

float OneWireManager::getTemperatureByAddress(const uint8_t* address) {
    if (!isReady()) {
        logger_.error("OneWireManager", "OneWire not initialized");
        stats_.failedReads++;
        return DEVICE_DISCONNECTED_C;
    }
    
    float temp = sensors_->getTempC(address);
    
    if (temp == DEVICE_DISCONNECTED_C) {
        logger_.warning("OneWireManager", "Failed to read temperature from device " + addressToString(address));
        stats_.failedReads++;
        return DEVICE_DISCONNECTED_C;
    }
    
    stats_.successfulReads++;
    logger_.debug("OneWireManager", "Device " + addressToString(address) + " temperature: " + String(temp, 2) + "°C");
    
    return temp;
}

bool OneWireManager::isDeviceConnected(const uint8_t* address) {
    if (!isReady()) {
        return false;
    }
    
    return sensors_->isConnected(address);
}

String OneWireManager::addressToString(const uint8_t* address) {
    String str = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (address[i] < 16) str += "0";
        str += String(address[i], HEX);
        if (i < 7) str += ":";
    }
    str.toUpperCase();
    return str;
}

void OneWireManager::resetStatistics() {
    stats_.totalRequests = 0;
    stats_.successfulReads = 0;
    stats_.failedReads = 0;
    stats_.crcErrors = 0;
    
    logger_.info("OneWireManager", "Statistics reset");
}

#include "SensorManager.h"

SensorManager::SensorManager(RuntimeConfig* config, RS485Manager* rs485Manager, SDI12Manager* sdi12Manager,
                             OneWireManager* oneWireManager, MQTTManager* mqttManager, Logger* logger)
    : config_(config), rs485Manager_(rs485Manager), sdi12Manager_(sdi12Manager), oneWireManager_(oneWireManager),
      mqttManager_(mqttManager), logger_(logger),
      initialized_(false), paused_(false), lastReading_(0), readingInterval_(60000), currentProfile_(SensorProfile::NONE),
      rs485Initialized_(false), sdi12Initialized_(false), oneWireInitialized_(false),
      consecutiveErrors_(0), lastErrorTime_(0), errorBackoffInterval_(0),
      slt5007Sensor_(nullptr), sht31Sensor_(nullptr), cwtpssSensor_(nullptr), leafthsnSensor_(nullptr), 
      cwtsoilthsSensor_(nullptr), teros12Sensor_(nullptr), ezophSensor_(nullptr), ezoecSensor_(nullptr), ds18b20Sensor_(nullptr),
      cwtthxxsSensor_(nullptr)
{
}

SensorManager::~SensorManager() {
    cleanupSensors();
    cleanupBusManagers();
}

bool SensorManager::initialize() {
    if (initialized_) {
        return true;
    }

    logger_->info("SensorManager", "Initializing universal sensor manager...");
    
    // Load sensor configuration from runtime config
    currentProfile_ = config_->getSensorProfile();
    readingInterval_ = config_->getSensorReadingInterval();
    
    logger_->info("SensorManager", "Configured sensor profile: " + String(static_cast<int>(currentProfile_)));
    logger_->info("SensorManager", "Reading interval: " + String(readingInterval_) + "ms");
    
    // Initialize the configured sensor
    if (currentProfile_ != SensorProfile::NONE) {
        if (!initializeSensor()) {
            logger_->error("SensorManager", "Failed to initialize configured sensor");
            logger_->warning("SensorManager", "Sensor profile remains configured - will retry with exponential backoff");
            logger_->info("SensorManager", "If sensor is connected later, it will automatically recover");
            // DO NOT set profile to NONE - that would reset to factory mode!
            // The sensor instance is already nullptr, and backoff mechanism will handle retries
        }
    } else {
        logger_->warning("SensorManager", "No sensor profile configured - use MQTT command to configure");
    }
    
    initialized_ = true;
    logger_->info("SensorManager", "Sensor manager initialized successfully");
    return true;
}

bool SensorManager::initializeSensor() {
    // Cleanup any existing sensor instances
    cleanupSensors();
    
    // Initialize the required bus for this sensor type
    if (!initializeBusForSensor(currentProfile_)) {
        logger_->error("SensorManager", "Failed to initialize bus for sensor");
        return false;
    }
    
    switch (currentProfile_) {
        case SensorProfile::SLT5007:
            slt5007Sensor_ = new SLT5007(rs485Manager_, logger_);
            if (!slt5007Sensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize SLT5007 sensor");
                delete slt5007Sensor_;
                slt5007Sensor_ = nullptr;
                return false;
            }
            slt5007Sensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "SLT5007 sensor initialized and ready");
            return true;
            
        case SensorProfile::SHT31:
            sht31Sensor_ = new SHT31(logger_);
            if (!sht31Sensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize SHT31 sensor");
                delete sht31Sensor_;
                sht31Sensor_ = nullptr;
                return false;
            }
            sht31Sensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "SHT31 sensor initialized and ready");
            return true;
            
        case SensorProfile::CWTPSS:
            cwtpssSensor_ = new CWTPSS(rs485Manager_, logger_);
            if (!cwtpssSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize CWTPSS sensor");
                delete cwtpssSensor_;
                cwtpssSensor_ = nullptr;
                return false;
            }
            cwtpssSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "CWTPSS sensor initialized and ready");
            return true;
            
        case SensorProfile::LEAFTHSN:
            leafthsnSensor_ = new LEAFTHSN(rs485Manager_, logger_);
            if (!leafthsnSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize LEAFTHSN sensor");
                delete leafthsnSensor_;
                leafthsnSensor_ = nullptr;
                return false;
            }
            leafthsnSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "LEAFTHSN sensor initialized and ready");
            return true;
            
        case SensorProfile::CWTSOILTHS:
            cwtsoilthsSensor_ = new CWTSoilTHS(rs485Manager_, logger_);
            if (!cwtsoilthsSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize CWTSoilTHS sensor");
                delete cwtsoilthsSensor_;
                cwtsoilthsSensor_ = nullptr;
                return false;
            }
            cwtsoilthsSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "CWTSoilTHS sensor initialized and ready");
            return true;
            
        case SensorProfile::TEROS12:
            teros12Sensor_ = new TEROS12(sdi12Manager_, logger_, SDI12_SENSOR_ADDRESS);
            if (!teros12Sensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize TEROS12 sensor");
                delete teros12Sensor_;
                teros12Sensor_ = nullptr;
                return false;
            }
            teros12Sensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "TEROS12 sensor initialized and ready");
            return true;
            
        case SensorProfile::EZOPH:
            ezophSensor_ = new EZOph(logger_);
            if (!ezophSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize EZO pH sensor");
                delete ezophSensor_;
                ezophSensor_ = nullptr;
                return false;
            }
            ezophSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "EZO pH sensor initialized and ready");
            return true;
            
        case SensorProfile::EZOEC:
            ezoecSensor_ = new EZOec(logger_);
            if (!ezoecSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize EZO EC sensor");
                delete ezoecSensor_;
                ezoecSensor_ = nullptr;
                return false;
            }
            ezoecSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "EZO EC sensor initialized and ready");
            return true;
            
        case SensorProfile::DS18B20:
            ds18b20Sensor_ = new DS18B20(oneWireManager_, logger_);
            if (!ds18b20Sensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize DS18B20 sensor");
                delete ds18b20Sensor_;
                ds18b20Sensor_ = nullptr;
                return false;
            }
            ds18b20Sensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "DS18B20 sensor initialized and ready");
            return true;
            
        case SensorProfile::CWTTHXXS:
            cwtthxxsSensor_ = new CWTTHXXS(rs485Manager_, logger_);
            if (!cwtthxxsSensor_->initialize()) {
                logger_->error("SensorManager", "Failed to initialize CWTTHXXS sensor");
                delete cwtthxxsSensor_;
                cwtthxxsSensor_ = nullptr;
                return false;
            }
            cwtthxxsSensor_->setMQTTManager(mqttManager_);
            logger_->info("SensorManager", "CWTTHXXS sensor initialized and ready");
            return true;
            
        case SensorProfile::NONE:
        default:
            logger_->warning("SensorManager", "No sensor profile selected or unknown profile");
            return true; // Not an error, just no sensor configured
    }
}

bool SensorManager::readAndPublish() {
    if (!initialized_) {
        logger_->error("SensorManager", "Sensor manager not initialized");
        return false;
    }

    // Skip reading if paused (e.g., during OTA update)
    if (paused_) {
        return false;
    }

    if (!mqttManager_->isConnected()) {
        // Don't log warning - this is normal during disconnections
        return false;
    }

    // Check if any sensor is configured
    if (currentProfile_ == SensorProfile::NONE) {
        // Silently return false - no sensor configured is not an error to log repeatedly
        return false;
    }

    // Check if sensor instance is actually available before attempting read
    if (!isSensorAvailable()) {
        // Log once per session that sensor is not available, then use backoff
        if (consecutiveErrors_ == 0) {
            logger_->error("SensorManager", "Sensor profile configured but sensor instance not available - sensor may have failed initialization");
        }
        handleSensorError();
        return false;
    }

    bool success = false;

    // Read from the active sensor - simplified since we already validated availability
    switch (currentProfile_) {
        case SensorProfile::SLT5007:
            success = slt5007Sensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::SHT31:
            success = sht31Sensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::CWTPSS:
            success = cwtpssSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::LEAFTHSN:
            success = leafthsnSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::CWTSOILTHS:
            success = cwtsoilthsSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::TEROS12:
            success = teros12Sensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::EZOPH:
            success = ezophSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::EZOEC:
            success = ezoecSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::DS18B20:
            success = ds18b20Sensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        case SensorProfile::CWTTHXXS:
            success = cwtthxxsSensor_->readAndPublishData(getSensorDataTopic());
            break;
            
        default:
            logger_->error("SensorManager", "Unknown sensor profile: " + String(static_cast<int>(currentProfile_)));
            success = false;
            break;
    }

    // Handle success/error for backoff mechanism
    if (success) {
        handleSensorSuccess();
    } else {
        handleSensorError();
    }

    return success;
}

bool SensorManager::isReadingDue() {
    unsigned long currentTime = millis();
    
    // Check if normal reading interval has passed
    bool intervalPassed = (currentTime - lastReading_) >= readingInterval_;
    
    // If we have consecutive errors, check backoff period
    if (consecutiveErrors_ >= MAX_CONSECUTIVE_ERRORS) {
        if (errorBackoffInterval_ > 0 && (currentTime - lastErrorTime_) < errorBackoffInterval_) {
            // Still in backoff period
            return false;
        }
        // Backoff period has passed, allow retry
        logger_->info("SensorManager", "Sensor error backoff period ended, retrying after " + 
                      String(consecutiveErrors_) + " consecutive errors");
    }
    
    return intervalPassed;
}

void SensorManager::updateLastReading() {
    lastReading_ = millis();
}

uint32_t SensorManager::getReadingInterval() const {
    return readingInterval_;
}

String SensorManager::getSensorDataTopic() const {
    return config_->getSensorTopicData();
}

bool SensorManager::configureSensor(SensorProfile profile) {
    logger_->info("SensorManager", "Configuring sensor profile: " + String(static_cast<int>(profile)));
    
    // Set sensor-specific defaults BEFORE updating configuration
    String sensorName;
    uint32_t defaultInterval;
    
    switch (profile) {
        case SensorProfile::SLT5007:
            sensorName = "SLT5007";
            defaultInterval = 180000; // 3 minutes
            break;
        case SensorProfile::SHT31:
            sensorName = "SHT31";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::CWTPSS:
            sensorName = "CWTPSS";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::LEAFTHSN:
            sensorName = "LEAFTHSN";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::CWTSOILTHS:
            sensorName = "CWTSoilTHS";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::TEROS12:
            sensorName = "TEROS12";
            defaultInterval = 180000; // 3 minutes (soil sensors typically slower)
            break;
        case SensorProfile::EZOPH:
            sensorName = "EZOph";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::EZOEC:
            sensorName = "EZOec";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::DS18B20:
            sensorName = "DS18B20";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::CWTTHXXS:
            sensorName = "CWTTHXXS";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::NONE:
        default:
            sensorName = "None";
            defaultInterval = 60000; // 1 minute fallback
            break;
    }
    
    // Test sensor initialization BEFORE saving configuration
    if (profile != SensorProfile::NONE && initialized_) {
        // Temporarily store current config in case we need to rollback
        SensorProfile oldProfile = currentProfile_;
        
        // Try to initialize the new sensor
        currentProfile_ = profile;
        if (!initializeSensor()) {
            logger_->error("SensorManager", "Failed to initialize new sensor profile - keeping old configuration");
            // Rollback to previous profile
            currentProfile_ = oldProfile;
            initializeSensor(); // Restore previous sensor if possible
            return false;
        }
    }
    
    // Only update and save configuration if sensor initialization succeeded
    currentProfile_ = profile;
    config_->setSensorProfile(profile);
    config_->setSensorName(sensorName);
    config_->setSensorReadingInterval(defaultInterval);
    readingInterval_ = defaultInterval;
    
    // Reset error state when configuring new sensor
    resetErrorState();
    
    // Save configuration persistently
    if (!config_->save()) {
        logger_->error("SensorManager", "Failed to save sensor configuration");
        return false;
    }
    
    logger_->info("SensorManager", "Sensor profile configured successfully: " + sensorName + 
                 " (interval: " + String(defaultInterval) + "ms)");
    return true;
}

SensorProfile SensorManager::getCurrentProfile() const {
    return currentProfile_;
}

bool SensorManager::hasSensorConfigured() const {
    return currentProfile_ != SensorProfile::NONE;
}

bool SensorManager::isSensorAvailable() const {
    if (currentProfile_ == SensorProfile::NONE) {
        return false;
    }
    
    switch (currentProfile_) {
        case SensorProfile::SLT5007:
            return slt5007Sensor_ != nullptr;
        case SensorProfile::SHT31:
            return sht31Sensor_ != nullptr;
        case SensorProfile::CWTPSS:
            return cwtpssSensor_ != nullptr;
        case SensorProfile::LEAFTHSN:
            return leafthsnSensor_ != nullptr;
        case SensorProfile::CWTSOILTHS:
            return cwtsoilthsSensor_ != nullptr;
        case SensorProfile::TEROS12:
            return teros12Sensor_ != nullptr;
        case SensorProfile::EZOPH:
            return ezophSensor_ != nullptr;
        case SensorProfile::EZOEC:
            return ezoecSensor_ != nullptr;
        case SensorProfile::DS18B20:
            return ds18b20Sensor_ != nullptr;
        case SensorProfile::CWTTHXXS:
            return cwtthxxsSensor_ != nullptr;
        default:
            return false;
    }
}

void SensorManager::cleanupSensors() {
    if (slt5007Sensor_) {
        delete slt5007Sensor_;
        slt5007Sensor_ = nullptr;
    }
    if (sht31Sensor_) {
        delete sht31Sensor_;
        sht31Sensor_ = nullptr;
    }
    if (cwtpssSensor_) {
        delete cwtpssSensor_;
        cwtpssSensor_ = nullptr;
    }
    if (leafthsnSensor_) {
        delete leafthsnSensor_;
        leafthsnSensor_ = nullptr;
    }
    if (cwtsoilthsSensor_) {
        delete cwtsoilthsSensor_;
        cwtsoilthsSensor_ = nullptr;
    }
    if (teros12Sensor_) {
        delete teros12Sensor_;
        teros12Sensor_ = nullptr;
    }
    if (ezophSensor_) {
        delete ezophSensor_;
        ezophSensor_ = nullptr;
    }
    if (ezoecSensor_) {
        delete ezoecSensor_;
        ezoecSensor_ = nullptr;
    }
    if (ds18b20Sensor_) {
        delete ds18b20Sensor_;
        ds18b20Sensor_ = nullptr;
    }
    if (cwtthxxsSensor_) {
        delete cwtthxxsSensor_;
        cwtthxxsSensor_ = nullptr;
    }
}

bool SensorManager::setReadingInterval(uint32_t intervalMs) {
    if (intervalMs < 1000) {
        logger_->warning("SensorManager", "Interval too short (min 1000ms), rejecting");
        return false;
    }
    
    if (intervalMs > 3600000) {
        logger_->warning("SensorManager", "Interval too long (max 1 hour), rejecting");
        return false;
    }
    
    readingInterval_ = intervalMs;
    config_->setSensorReadingInterval(intervalMs);
    config_->save();
    
    logger_->info("SensorManager", "Reading interval updated to " + String(intervalMs) + "ms (" + 
                  String(intervalMs / 1000) + "s)");
    
    return true;
}

void SensorManager::resetErrorState() {
    if (consecutiveErrors_ > 0) {
        logger_->info("SensorManager", "Resetting sensor error state (was: " + 
                      String(consecutiveErrors_) + " consecutive errors)");
    }
    consecutiveErrors_ = 0;
    lastErrorTime_ = 0;
    errorBackoffInterval_ = 0;
}

void SensorManager::handleSensorError() {
    consecutiveErrors_++;
    lastErrorTime_ = millis();
    
    // Calculate exponential backoff interval
    if (consecutiveErrors_ >= MAX_CONSECUTIVE_ERRORS) {
        // Calculate backoff: BASE_BACKOFF * 2^(errors - MAX_ERRORS)
        uint32_t backoffMultiplier = 1 << (consecutiveErrors_ - MAX_CONSECUTIVE_ERRORS);
        errorBackoffInterval_ = min(BASE_BACKOFF_INTERVAL * backoffMultiplier, MAX_BACKOFF_INTERVAL);
        
        logger_->warning("SensorManager", "Sensor failed " + String(consecutiveErrors_) + 
                         " consecutive times. Backing off for " + String(errorBackoffInterval_ / 1000) + 
                         " seconds before next attempt (will be included in next heartbeat)");
    } else {
        logger_->warning("SensorManager", "Sensor error count: " + String(consecutiveErrors_) + 
                         "/" + String(MAX_CONSECUTIVE_ERRORS));
    }
}

void SensorManager::handleSensorSuccess() {
    if (consecutiveErrors_ > 0) {
        logger_->info("SensorManager", "Sensor recovered after " + String(consecutiveErrors_) + 
                      " consecutive errors (will be included in next heartbeat)");
        resetErrorState();
    }
}

String SensorManager::getSensorTypeName() const {
    switch (currentProfile_) {
        case SensorProfile::SLT5007:    return "SLT5007";
        case SensorProfile::SHT31:      return "SHT31";
        case SensorProfile::CWTPSS:     return "CWTPSS";
        case SensorProfile::LEAFTHSN:   return "LEAFTHSN";
        case SensorProfile::CWTSOILTHS: return "CWTSoilTHS";
        case SensorProfile::TEROS12:    return "TEROS12";
        case SensorProfile::EZOPH:      return "EZOph";
        case SensorProfile::EZOEC:      return "EZOec";
        case SensorProfile::DS18B20:    return "DS18B20";
        case SensorProfile::CWTTHXXS:   return "CWTTHXXS";
        case SensorProfile::NONE:
        default:                        return "None";
    }
}

uint8_t SensorManager::getConsecutiveErrors() const {
    return consecutiveErrors_;
}

uint32_t SensorManager::getBackoffInterval() const {
    return errorBackoffInterval_;
}

bool SensorManager::getSensorHealthStatus() const {
    // Sensor is considered unhealthy if:
    // 1. No sensor is configured, OR
    // 2. Sensor has consecutive errors >= threshold
    if (currentProfile_ == SensorProfile::NONE) {
        return false; // No sensor configured
    }
    
    return consecutiveErrors_ < MAX_CONSECUTIVE_ERRORS;
}


void SensorManager::pause() {
    paused_ = true;
    logger_->info("SensorManager", "Sensor operations paused");
}

void SensorManager::resume() {
    paused_ = false;
    logger_->info("SensorManager", "Sensor operations resumed");
}

EZOph* SensorManager::getEZOphSensor() const {
    if (currentProfile_ == SensorProfile::EZOPH) {
        return ezophSensor_;
    }
    return nullptr;
}

bool SensorManager::initializeBusForSensor(SensorProfile profile) {
    // Determine which bus this sensor needs
    bool needsRS485 = false;
    bool needsSDI12 = false;
    bool needsOneWire = false;
    
    switch (profile) {
        case SensorProfile::SLT5007:
        case SensorProfile::CWTPSS:
        case SensorProfile::LEAFTHSN:
        case SensorProfile::CWTSOILTHS:
        case SensorProfile::CWTTHXXS:
            needsRS485 = true;
            break;
            
        case SensorProfile::TEROS12:
            needsSDI12 = true;
            break;
            
        case SensorProfile::DS18B20:
            needsOneWire = true;
            break;
            
        case SensorProfile::SHT31:
        case SensorProfile::EZOPH:
        case SensorProfile::EZOEC:
            // I2C sensors - no special bus initialization needed
            // I2C is initialized by the sensor itself
            break;
            
        case SensorProfile::NONE:
        default:
            // No bus needed
            return true;
    }
    
    // Shutdown any previously initialized bus that we don't need
    if (needsRS485 && sdi12Initialized_) {
        logger_->info("SensorManager", "Shutting down SDI-12 bus (switching to RS485)");
        sdi12Manager_->shutdown();
        sdi12Initialized_ = false;
    }
    
    if (needsRS485 && oneWireInitialized_) {
        logger_->info("SensorManager", "Shutting down OneWire bus (switching to RS485)");
        oneWireManager_->shutdown();
        oneWireInitialized_ = false;
    }
    
    if (needsSDI12 && rs485Initialized_) {
        logger_->info("SensorManager", "Shutting down RS485 bus (switching to SDI-12)");
        rs485Manager_->shutdown();
        rs485Initialized_ = false;
    }
    
    if (needsSDI12 && oneWireInitialized_) {
        logger_->info("SensorManager", "Shutting down OneWire bus (switching to SDI-12)");
        oneWireManager_->shutdown();
        oneWireInitialized_ = false;
    }
    
    if (needsOneWire && rs485Initialized_) {
        logger_->info("SensorManager", "Shutting down RS485 bus (switching to OneWire)");
        rs485Manager_->shutdown();
        rs485Initialized_ = false;
    }
    
    if (needsOneWire && sdi12Initialized_) {
        logger_->info("SensorManager", "Shutting down SDI-12 bus (switching to OneWire)");
        sdi12Manager_->shutdown();
        sdi12Initialized_ = false;
    }
    
    // Initialize required bus if not already initialized
    if (needsRS485 && !rs485Initialized_) {
        logger_->info("SensorManager", "Initializing RS485 bus for sensor");
        if (!rs485Manager_->initialize(RS485_DE_RE_PIN, 500, 9600)) {
            logger_->error("SensorManager", "Failed to initialize RS485 bus");
            return false;
        }
        rs485Initialized_ = true;
        logger_->info("SensorManager", "RS485 bus initialized successfully");
    }
    
    if (needsSDI12 && !sdi12Initialized_) {
        logger_->info("SensorManager", "Initializing SDI-12 bus for sensor");
        if (!sdi12Manager_->initialize(SDI12_DATA_PIN)) {
            logger_->error("SensorManager", "Failed to initialize SDI-12 bus");
            return false;
        }
        sdi12Initialized_ = true;
        logger_->info("SensorManager", "SDI-12 bus initialized successfully");
    }
    
    if (needsOneWire && !oneWireInitialized_) {
        logger_->info("SensorManager", "Initializing OneWire bus for sensor");
        if (!oneWireManager_->initialize(ONEWIRE_DATA_PIN)) {
            logger_->error("SensorManager", "Failed to initialize OneWire bus");
            return false;
        }
        oneWireInitialized_ = true;
        logger_->info("SensorManager", "OneWire bus initialized successfully");
    }
    
    return true;
}

void SensorManager::cleanupBusManagers() {
    if (rs485Initialized_) {
        logger_->info("SensorManager", "Shutting down RS485 bus");
        rs485Manager_->shutdown();
        rs485Initialized_ = false;
    }
    
    if (sdi12Initialized_) {
        logger_->info("SensorManager", "Shutting down SDI-12 bus");
        sdi12Manager_->shutdown();
        sdi12Initialized_ = false;
    }
    
    if (oneWireInitialized_) {
        logger_->info("SensorManager", "Shutting down OneWire bus");
        oneWireManager_->shutdown();
        oneWireInitialized_ = false;
    }
}

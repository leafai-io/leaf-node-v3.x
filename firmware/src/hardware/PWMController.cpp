#include "PWMController.h"
#include "config.h"

// Logger helper macros - use Serial if logger is nullptr
#define LOG_INFO(tag, msg) if (logger_) logger_->info(tag, msg); else Serial.println("[" + String(tag) + "] " + msg)
#define LOG_ERROR(tag, msg) if (logger_) logger_->error(tag, msg); else Serial.println("[" + String(tag) + "] ERROR: " + msg)
#define LOG_WARNING(tag, msg) if (logger_) logger_->warning(tag, msg); else Serial.println("[" + String(tag) + "] WARNING: " + msg)
#define LOG_DEBUG(tag, msg) if (logger_) logger_->debug(tag, msg); else Serial.println("[" + String(tag) + "] DEBUG: " + msg)

PWMController::PWMController(Logger* logger) 
    : logger_(logger)
    , pin_(2)
    , channel_(3)
    , frequency_(5000)
    , resolution_(8)
    , maxValue_(255)
    , currentValue_(0)
    , initialized_(false)
    , ledUpdateCallback_(nullptr)
    , lastCommandType_("")
    , timerActive_(false)
    , timerEndMillis_(0) {
}

PWMController::~PWMController() {
    if (initialized_) {
        turnOff();  // Safety: Turn off PWM on destruction
        ledcDetachPin(pin_);
    }
}

bool PWMController::initialize(uint8_t pin, uint8_t channel, uint32_t frequency, uint8_t resolution) {
    if (initialized_) {
        LOG_INFO("PWMController", "Already initialized");
        return true;
    }
    
    pin_ = pin;
    channel_ = channel;
    frequency_ = frequency;
    resolution_ = resolution;
    maxValue_ = (1 << resolution_) - 1;  // 2^resolution - 1
    
    LOG_INFO("PWMController", "Initializing PWM on GPIO" + String(pin_) + 
             " (Channel " + String(channel_) + ", " + String(frequency_) + "Hz, " + 
             String(resolution_) + "-bit)");
    
    // Setup LEDC channel
    ledcSetup(channel_, frequency_, resolution_);
    
    // Attach pin to channel
    ledcAttachPin(pin_, channel_);
    
    // Set to 0 as safe default
    writePWM(0);
    
    initialized_ = true;
    
    LOG_INFO("PWMController", "PWM Controller initialized successfully (0-" + String(maxValue_) + ")");
    
    return true;
}

bool PWMController::setValue(uint16_t value) {
    if (!initialized_) {
        LOG_ERROR("PWMController", "Not initialized");
        return false;
    }
    
    value = clampValue(value);
    
    if (writePWM(value)) {
        currentValue_ = value;
        lastCommandType_ = "value";
        
        LOG_INFO("PWMController", "Set PWM value: " + String(value) + 
                 " (" + String(getCurrentPercent(), 1) + "%)");
        
        // Trigger LED update callback if set
        if (ledUpdateCallback_) {
            ledUpdateCallback_();
        }
        
        return true;
    }
    
    return false;
}

bool PWMController::setPercent(float percent) {
    if (!initialized_) {
        LOG_ERROR("PWMController", "Not initialized");
        return false;
    }
    
    // Clamp percentage to 0-100
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    
    // Convert percentage to value
    uint16_t value = (uint16_t)((percent / 100.0f) * maxValue_);
    
    if (writePWM(value)) {
        currentValue_ = value;
        lastCommandType_ = "percent";
        
        LOG_INFO("PWMController", "Set PWM percent: " + String(percent, 1) + 
                 "% (value: " + String(value) + ")");
        
        // Trigger LED update callback if set
        if (ledUpdateCallback_) {
            ledUpdateCallback_();
        }
        
        return true;
    }
    
    return false;
}

bool PWMController::setVoltage(float voltage) {
    if (!initialized_) {
        LOG_ERROR("PWMController", "Not initialized");
        return false;
    }
    
    // Determine max voltage based on pin configuration
    float maxVoltage = 3.3f;  // Default ESP32 logic level
    #ifdef PWM_IO2_MAX_VOLTAGE
    if (pin_ == PWM_IO2_PIN) {
        maxVoltage = PWM_IO2_MAX_VOLTAGE;
    }
    #endif
    #ifdef PWM_MOSFET_MAX_VOLTAGE
    if (pin_ == PWM_MOSFET_PIN) {
        maxVoltage = PWM_MOSFET_MAX_VOLTAGE;
    }
    #endif
    
    // Clamp voltage to valid range
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > maxVoltage) voltage = maxVoltage;
    
    // Convert voltage to value
    uint16_t value = (uint16_t)((voltage / maxVoltage) * maxValue_);
    
    if (writePWM(value)) {
        currentValue_ = value;
        lastCommandType_ = "voltage";
        
        LOG_INFO("PWMController", "Set PWM voltage: " + String(voltage, 2) + 
                 "V (value: " + String(value) + ")");
        
        // Trigger LED update callback if set
        if (ledUpdateCallback_) {
            ledUpdateCallback_();
        }
        
        return true;
    }
    
    return false;
}

bool PWMController::turnOff() {
    if (!initialized_) {
        LOG_ERROR("PWMController", "Not initialized");
        return false;
    }
    
    if (writePWM(0)) {
        currentValue_ = 0;
        lastCommandType_ = "off";
        
        // Cancel any active timer
        timerActive_ = false;
        timerEndMillis_ = 0;
        
        LOG_INFO("PWMController", "PWM turned off");
        
        // Trigger LED update callback if set
        if (ledUpdateCallback_) {
            ledUpdateCallback_();
        }
        
        return true;
    }
    
    return false;
}

bool PWMController::setValueWithDuration(uint16_t value, uint32_t durationMillis) {
    if (!setValue(value)) {
        return false;
    }
    
    // Setup timer if duration is specified
    if (durationMillis > 0) {
        timerActive_ = true;
        timerEndMillis_ = millis() + durationMillis;
        LOG_INFO("PWMController", "Timer set for " + String(durationMillis) + "ms");
    } else {
        timerActive_ = false;
        timerEndMillis_ = 0;
    }
    
    return true;
}

bool PWMController::setPercentWithDuration(float percent, uint32_t durationMillis) {
    if (!setPercent(percent)) {
        return false;
    }
    
    // Setup timer if duration is specified
    if (durationMillis > 0) {
        timerActive_ = true;
        timerEndMillis_ = millis() + durationMillis;
        LOG_INFO("PWMController", "Timer set for " + String(durationMillis) + "ms");
    } else {
        timerActive_ = false;
        timerEndMillis_ = 0;
    }
    
    return true;
}

bool PWMController::setVoltageWithDuration(float voltage, uint32_t durationMillis) {
    if (!setVoltage(voltage)) {
        return false;
    }
    
    // Setup timer if duration is specified
    if (durationMillis > 0) {
        timerActive_ = true;
        timerEndMillis_ = millis() + durationMillis;
        LOG_INFO("PWMController", "Timer set for " + String(durationMillis) + "ms");
    } else {
        timerActive_ = false;
        timerEndMillis_ = 0;
    }
    
    return true;
}

uint32_t PWMController::getRemainingMillis() const {
    if (!timerActive_) {
        return 0;
    }
    
    uint32_t currentMillis = millis();
    if (currentMillis >= timerEndMillis_) {
        return 0;
    }
    
    return timerEndMillis_ - currentMillis;
}

bool PWMController::checkTimer() {
    if (!timerActive_) {
        return false;
    }
    
    uint32_t currentMillis = millis();
    if (currentMillis >= timerEndMillis_) {
        timerActive_ = false;
        timerEndMillis_ = 0;
        
        LOG_INFO("PWMController", "Timer expired, turning PWM off");
        turnOff();
        
        return true;  // Timer expired
    }
    
    return false;  // Timer still running
}

float PWMController::getCurrentPercent() const {
    if (maxValue_ == 0) return 0.0f;
    return ((float)currentValue_ / (float)maxValue_) * 100.0f;
}

float PWMController::getCurrentVoltage() const {
    if (maxValue_ == 0) return 0.0f;
    
    // Determine max voltage based on pin configuration
    float maxVoltage = 3.3f;  // Default ESP32 logic level
    #ifdef PWM_IO2_MAX_VOLTAGE
    if (pin_ == PWM_IO2_PIN) {
        maxVoltage = PWM_IO2_MAX_VOLTAGE;
    }
    #endif
    #ifdef PWM_MOSFET_MAX_VOLTAGE
    if (pin_ == PWM_MOSFET_PIN) {
        maxVoltage = PWM_MOSFET_MAX_VOLTAGE;
    }
    #endif
    
    return ((float)currentValue_ / (float)maxValue_) * maxVoltage;
}

uint16_t PWMController::clampValue(uint16_t value) {
    if (value > maxValue_) {
        LOG_WARNING("PWMController", "Value " + String(value) + " exceeds max " + 
                   String(maxValue_) + ", clamping");
        return maxValue_;
    }
    return value;
}

bool PWMController::writePWM(uint16_t value) {
    ledcWrite(channel_, value);
    LOG_DEBUG("PWMController", "Wrote PWM value " + String(value) + " to channel " + String(channel_));
    return true;
}

bool PWMController::handleCommand(const String& command, JsonVariantConst params) {
    if (!initialized_) {
        LOG_ERROR("PWMController", "Not initialized");
        return false;
    }
    
    LOG_DEBUG("PWMController", "Handling command: " + command);
    
    // Extract optional duration parameter
    uint32_t durationMillis = 0;
    if (params.containsKey("duration_ms")) {
        durationMillis = params["duration_ms"].as<uint32_t>();
    }
    
    if (command == "set_value") {
        if (!params.containsKey("value")) {
            LOG_ERROR("PWMController", "Missing 'value' parameter");
            return false;
        }
        uint16_t value = params["value"].as<uint16_t>();
        return durationMillis > 0 ? setValueWithDuration(value, durationMillis) : setValue(value);
    }
    else if (command == "set_percent") {
        // Accept both "percent" and "value" parameter names for flexibility
        float percent = 0.0f;
        if (params.containsKey("percent")) {
            percent = params["percent"].as<float>();
        } else if (params.containsKey("value")) {
            percent = params["value"].as<float>();
        } else {
            LOG_ERROR("PWMController", "Missing 'percent' or 'value' parameter");
            return false;
        }
        return durationMillis > 0 ? setPercentWithDuration(percent, durationMillis) : setPercent(percent);
    }
    else if (command == "set_voltage") {
        // Accept both "voltage" and "value" parameter names for flexibility
        float voltage = 0.0f;
        if (params.containsKey("voltage")) {
            voltage = params["voltage"].as<float>();
        } else if (params.containsKey("value")) {
            voltage = params["value"].as<float>();
        } else {
            LOG_ERROR("PWMController", "Missing 'voltage' or 'value' parameter");
            return false;
        }
        return durationMillis > 0 ? setVoltageWithDuration(voltage, durationMillis) : setVoltage(voltage);
    }
    else if (command == "off") {
        return turnOff();
    }
    else if (command == "read" || command == "status") {
        // Read is handled by caller getting the current value
        return true;
    }
    else {
        LOG_ERROR("PWMController", "Unknown command: " + command);
        return false;
    }
}

void PWMController::getStatus(JsonDocument& doc) {
    doc["value"] = currentValue_;
    doc["percent"] = getCurrentPercent();
    doc["voltage"] = getCurrentVoltage();
    doc["max_value"] = maxValue_;
    doc["pin"] = pin_;
    doc["channel"] = channel_;
    doc["frequency"] = frequency_;
    doc["resolution"] = resolution_;
    doc["last_command"] = lastCommandType_;
    doc["initialized"] = initialized_;
    doc["timer_active"] = timerActive_;
    if (timerActive_) {
        doc["timer_remaining_ms"] = getRemainingMillis();
    }
}

void PWMController::setLEDUpdateCallback(void (*callback)()) {
    ledUpdateCallback_ = callback;
}

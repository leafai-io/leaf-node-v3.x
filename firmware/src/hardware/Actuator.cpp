#include "Actuator.h"
#include "../system/SystemManager.h"
#include "../runtime/RuntimeConfig.h"
#include <esp32-hal-timer.h>

// Static member initialization
hw_timer_t* Actuator::hardwareTimer_ = nullptr;
Actuator* Actuator::instance_ = nullptr;

Actuator::Actuator() 
    : initialized_(false)
    , systemManager_(nullptr)
    , timerExpiredCallback_(nullptr)
    , timerStartedCallback_(nullptr)
    , config_(nullptr) {
    instance_ = this;  // Set singleton instance for ISR access
}

Actuator::~Actuator() {
    if (initialized_) {
        // Safety: Turn off relay on destruction
        // Note: MOSFET is controlled by PWM Controller, not here
        turnOff(Type::RELAY);
        
        // Stop and cleanup hardware timer
        if (hardwareTimer_ != nullptr) {
            timerEnd(hardwareTimer_);
            hardwareTimer_ = nullptr;
        }
    }
    instance_ = nullptr;
}

Actuator::Type Actuator::stringToType(const String& typeStr) {
    String lowerType = typeStr;
    lowerType.toLowerCase();
    
    if (lowerType == "relay") {
        return Type::RELAY;
    }
    return Type::MOSFET; // Default
}

String Actuator::typeToString(Type type) {
    switch (type) {
        case Type::RELAY:  return "relay";
        case Type::MOSFET: return "mosfet";
        default:           return "unknown";
    }
}

bool Actuator::initialize(uint8_t mosfetPin, uint8_t relayPin) {
    if (initialized_) {
        return true;  // Already initialized
    }

    // Store MOSFET pin reference (but DO NOT initialize as OUTPUT)
    // MOSFET is now controlled exclusively via PWM Controller
    mosfet_.pin = mosfetPin;
    mosfet_.timerExpiredFlag = false;
    mosfet_.state = false;
    mosfet_.timerActive = false;
    
    Serial.printf("[Actuator] ⚠️  MOSFET pin %d NOT initialized (controlled by PWM Controller)\n", mosfet_.pin);
    
    // Initialize Relay
    relay_.pin = relayPin;
    relay_.timerExpiredFlag = false;
    pinMode(relay_.pin, OUTPUT);
    digitalWrite(relay_.pin, LOW);
    relay_.state = false;
    relay_.timerActive = false;
    
    // Setup hardware timer for ±100µs precision on millisecond timers
    // Timer 0, prescaler 80 (80MHz / 80 = 1MHz = 1µs per tick)
    hardwareTimer_ = timerBegin(0, 80, true);
    if (hardwareTimer_ == nullptr) {
        Serial.println("[Actuator] ❌ Failed to initialize hardware timer!");
        return false;
    }
    
    // Attach ISR to timer - fires every 100µs to check timers
    timerAttachInterrupt(hardwareTimer_, &Actuator::onHardwareTimerISR, true);
    
    // Configure timer to fire every 100µs (100 ticks at 1MHz)
    timerAlarmWrite(hardwareTimer_, 100, true);  // 100µs interval, auto-reload
    
    // Start the timer immediately
    timerAlarmEnable(hardwareTimer_);
    
    initialized_ = true;
    
    Serial.printf("[Actuator] ✅ Initialized Relay on pin %d (OFF)\n", relay_.pin);
    Serial.println("[Actuator] Hardware timer running (100us precision)");
    
    return true;
}

bool Actuator::turnOnMillis(Type type, uint32_t durationMillis) {
    if (!initialized_) {
        Serial.println("[Actuator] Error: Not initialized");
        return false;
    }

    ActuatorInstance& instance = getInstance(type);
    
    // Set physical state
    if (!setPhysicalState(type, true)) {
        return false;
    }
    
    // Handle timer
    if (durationMillis > 0) {
        instance.timerActive = true;
        instance.timerExpiredFlag = false;  // Reset flag
        instance.timerEndMillis = millis() + durationMillis;
        
        Serial.printf("[Actuator] %s turned ON with %ums timer (100us precision)\n", 
                     typeToString(type).c_str(), durationMillis);
        
        // Notify that timer started (for schedule pause) - callback receives milliseconds!
        if (timerStartedCallback_) {
            timerStartedCallback_(type, durationMillis);
        }
    } else {
        instance.timerActive = false;
        Serial.printf("[Actuator] %s turned ON\n", typeToString(type).c_str());
    }
    
    // Save state to persistent storage
    saveStates();
    
    return true;
}

bool Actuator::turnOff(Type type) {
    if (!initialized_) {
        Serial.println("[Actuator] Error: Not initialized");
        return false;
    }

    ActuatorInstance& instance = getInstance(type);
    
    // Cancel any active timer
    if (instance.timerActive) {
        instance.timerActive = false;
        Serial.printf("[Actuator] %s timer cancelled\n", typeToString(type).c_str());
    }
    
    // Set physical state
    if (!setPhysicalState(type, false)) {
        return false;
    }
    
    Serial.printf("[Actuator] %s turned OFF\n", typeToString(type).c_str());
    
    // Save state to persistent storage
    saveStates();
    
    return true;
}

bool Actuator::toggleMillis(Type type, uint32_t durationMillis) {
    if (!initialized_) {
        Serial.println("[Actuator] Error: Not initialized");
        return false;
    }

    ActuatorInstance& instance = getInstance(type);
    bool newState = !instance.state;
    
    if (newState) {
        return turnOnMillis(type, durationMillis);
    } else {
        return turnOff(type);
    }
}

bool Actuator::cancelTimer(Type type) {
    if (!initialized_) {
        Serial.println("[Actuator] Error: Not initialized");
        return false;
    }

    ActuatorInstance& instance = getInstance(type);
    
    if (!instance.timerActive) {
        Serial.printf("[Actuator] %s has no active timer\n", typeToString(type).c_str());
        return false;
    }
    
    instance.timerActive = false;
    Serial.printf("[Actuator] %s timer cancelled\n", typeToString(type).c_str());
    
    return true;
}

void Actuator::update() {
    if (!initialized_) {
        return;
    }

    // ===== MOSFET timer DISABLED =====
    // MOSFET is now controlled by PWM Controller, not Actuator
    // Timer functionality for MOSFET is not used
    
    // ===== Check Relay timer =====
    if (relay_.timerActive && relay_.timerExpiredFlag) {
        relay_.timerExpiredFlag = false;  // Reset flag
        Serial.printf("[Actuator] ⏰ RELAY timer expired (ISR-triggered)\n");
        
        relay_.timerActive = false;
        bool oldState = relay_.state;
        // Note: GPIO already turned OFF by ISR for precision
        
        // Update LED: OFF if both actuators are OFF, otherwise keep ON
        if (systemManager_) {
            bool newAnyCombinedState = mosfet_.state || relay_.state;
            systemManager_->setActuatorLED(newAnyCombinedState);
        }
        
        // Call callback if set
        if (timerExpiredCallback_) {
            timerExpiredCallback_(Type::RELAY, oldState);
        }
    }
}

bool Actuator::getState(Type type) const {
    return getInstance(type).state;
}

bool Actuator::isTimerActive(Type type) const {
    return getInstance(type).timerActive;
}

uint32_t Actuator::getRemainingMillis(Type type) const {
    const ActuatorInstance& instance = getInstance(type);
    
    if (!instance.timerActive) {
        return 0;
    }
    
    unsigned long currentMillis = millis();
    if (currentMillis >= instance.timerEndMillis) {
        return 0;
    }
    return instance.timerEndMillis - currentMillis;
}

bool Actuator::setPhysicalState(Type type, bool state) {
    // MOSFET is now controlled exclusively by PWM Controller
    if (type == Type::MOSFET) {
        Serial.println("[Actuator] ⚠️  ERROR: MOSFET is controlled by PWM Controller, not Actuator");
        Serial.println("[Actuator] ℹ️  Use PWM commands instead: pwm_set_percent with type=mosfet");
        return false;
    }
    
    ActuatorInstance& instance = getInstance(type);
    
    // Store previous combined state before changing this actuator
    bool previousAnyCombinedState = mosfet_.state || relay_.state;
    
    // Set the pin state
    digitalWrite(instance.pin, state ? HIGH : LOW);
    instance.state = state;
    
    // Calculate new combined state
    bool newAnyCombinedState = mosfet_.state || relay_.state;
    
    // Update LED with special blink logic
    if (systemManager_) {
        bool shouldBlink = false;
        String blinkReason = "";
        
        // Case 1: Adding a second actuator (both now ON)
        if (state && previousAnyCombinedState && newAnyCombinedState && 
            mosfet_.state && relay_.state) {
            shouldBlink = true;
            blinkReason = "both actuators now active";
        }
        
        // Case 2: Removing one actuator (one goes OFF, one stays ON)
        else if (!state && previousAnyCombinedState && newAnyCombinedState &&
                 (mosfet_.state ^ relay_.state)) {  // XOR: exactly one is ON
            shouldBlink = true;
            String remainingType = mosfet_.state ? "mosfet" : "relay";
            blinkReason = "one actuator turned off, " + remainingType + " remains active";
        }
        
        if (shouldBlink) {
            // Blink: OFF for 100ms, then back ON
            systemManager_->setRGBLED((uint8_t)0, (uint8_t)0, (uint8_t)0);  // OFF
            delay(100);                           // Short blink
            systemManager_->setActuatorLED(true); // Back ON
            Serial.printf("[Actuator] LED blinked - %s\n", blinkReason.c_str());
        } else {
            // Normal LED control - ON if any actuator is ON
            systemManager_->setActuatorLED(newAnyCombinedState);
        }
    }
    
    return true;
}

Actuator::ActuatorInstance& Actuator::getInstance(Type type) {
    return (type == Type::RELAY) ? relay_ : mosfet_;
}

const Actuator::ActuatorInstance& Actuator::getInstance(Type type) const {
    return (type == Type::RELAY) ? relay_ : mosfet_;
}
void Actuator::saveStates() {
    if (!config_) {
        return;
    }
    
    // Save current states to config
    config_->setMosfetState(mosfet_.state);
    config_->setRelayState(relay_.state);
    config_->save();
}

void Actuator::restoreStates() {
    if (!config_ || !initialized_) {
        return;
    }
    
    // Safety: Always start with relay OFF on boot
    // Note: MOSFET is controlled by PWM Controller
    turnOff(Type::RELAY);
    
    Serial.println("[Actuator] Boot safety: Relay initialized to OFF state");
    Serial.println("[Actuator] Note: MOSFET is controlled by PWM Controller");
}

// ==================== Hardware Timer ISR ====================

void IRAM_ATTR Actuator::onHardwareTimerISR() {
    if (instance_ == nullptr) return;
    
    uint32_t nowMillis = millis();
    
    // ===== MOSFET timer DISABLED =====
    // MOSFET is now controlled by PWM Controller, not Actuator
    // Timer functionality for MOSFET is not used
    
    // ===== Check RELAY timer =====
    if (instance_->relay_.timerActive) {
        if (nowMillis >= instance_->relay_.timerEndMillis) {
            // CRITICAL: Turn off GPIO immediately in ISR for precision
            digitalWrite(instance_->relay_.pin, LOW);
            instance_->relay_.state = false;
            instance_->relay_.timerExpiredFlag = true;
            // Note: timerActive cleared in update() after callback
        }
    }
}
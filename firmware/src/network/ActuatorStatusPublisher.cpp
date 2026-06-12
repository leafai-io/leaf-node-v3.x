#include "ActuatorStatusPublisher.h"
#include "../hardware/MCP4725.h"
#include "../hardware/PWMController.h"
#include "config.h"

ActuatorStatusPublisher::ActuatorStatusPublisher(RuntimeConfig* config, MQTTManager* mqttManager, 
                                                 Logger* logger, Actuator* actuator, MCP4725* mcp4725,
                                                 PWMController* pwmController, PWMController* pwmControllerMOSFET)
    : config_(config), mqttManager_(mqttManager), logger_(logger), actuator_(actuator), mcp4725_(mcp4725),
      pwmController_(pwmController), pwmControllerMOSFET_(pwmControllerMOSFET) {
}

ActuatorStatusPublisher::~ActuatorStatusPublisher() {
}

bool ActuatorStatusPublisher::publishStatus(const String& lastCommand, const String& lastTarget, 
                                           const String& lastTimestamp, const String& actuatorType,
                                           bool isRamping, float rampTargetValue, 
                                           uint32_t rampDurationSeconds, uint32_t rampElapsedSeconds) {
    if (!mqttManager_) {
        if (logger_) {
            logger_->error("ActuatorStatusPublisher", "MQTT manager not available");
        }
        return false;
    }
    
    if (!actuator_) {
        if (logger_) {
            logger_->error("ActuatorStatusPublisher", "Actuator not available");
        }
        return false;
    }
    
    String topic = getStatusTopic();
    String payload = buildStatusPayload(lastCommand, lastTarget, lastTimestamp, actuatorType,
                                       isRamping, rampTargetValue, rampDurationSeconds, rampElapsedSeconds);
    
    if (topic.isEmpty() || payload.isEmpty()) {
        if (logger_) {
            logger_->error("ActuatorStatusPublisher", "Empty topic or payload");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("ActuatorStatusPublisher", "Publishing actuator status to: " + topic);
        logger_->debug("ActuatorStatusPublisher", "Payload: " + payload);
    }
    
    bool result = mqttManager_->publish(topic, payload, false);
    
    if (result) {
        if (logger_) {
            logger_->info("ActuatorStatusPublisher", "Actuator status published successfully");
        }
    } else {
        if (logger_) {
            logger_->error("ActuatorStatusPublisher", "Failed to publish actuator status");
        }
    }
    
    return result;
}

String ActuatorStatusPublisher::getStatusTopic() const {
    if (!config_ || config_->getSerialNumber().isEmpty()) {
        if (logger_) {
            logger_->error("ActuatorStatusPublisher", "Serial number not configured");
        }
        return "";
    }
    
    // Topic format: lai/devices/{serial}/status
    return "lai/devices/" + config_->getSerialNumber() + "/status";
}

String ActuatorStatusPublisher::buildStatusPayload(const String& lastCommand, const String& lastTarget, 
                                                   const String& lastTimestamp, const String& actuatorType,
                                                   bool isRamping, float rampTargetValue, 
                                                   uint32_t rampDurationSeconds, uint32_t rampElapsedSeconds) {
    DynamicJsonDocument doc(1024);
    
    // Device section
    JsonObject device = doc.createNestedObject("device");
    device["serial"] = config_ ? config_->getSerialNumber() : "UNKNOWN";
    device["name"] = "Leaf Node";
    device["firmware"] = FIRMWARE_VERSION;
    device["uptime"] = millis() / 1000;  // Convert to seconds
    
    // Network section
    JsonObject network = doc.createNestedObject("network");
    network["wifi_ssid"] = WiFi.SSID();
    network["ip_address"] = WiFi.localIP().toString();
    network["rssi"] = WiFi.RSSI();
    network["mac_address"] = WiFi.macAddress();
    
    // Only include the specific actuator type that changed, or both if not specified
    bool includeMosfet = actuatorType.isEmpty() || actuatorType.equalsIgnoreCase("mosfet");
    bool includeRelay = actuatorType.isEmpty() || actuatorType.equalsIgnoreCase("relay");
    
    // MOSFET section (only if relevant)
    if (includeMosfet) {
        JsonObject mosfet = doc.createNestedObject("mosfet");
        mosfet["state"] = actuator_ ? actuator_->getState(Actuator::Type::MOSFET) : false;
        if (actuator_ && actuator_->isTimerActive(Actuator::Type::MOSFET)) {
            mosfet["timer_active"] = true;
            uint32_t remainingMillis = actuator_->getRemainingMillis(Actuator::Type::MOSFET);
            
            mosfet["timer_remaining_ms"] = remainingMillis;
        } else {
            mosfet["timer_active"] = false;
        }
    }
    
    // Relay section (only if relevant)
    if (includeRelay) {
        JsonObject relay = doc.createNestedObject("relay");
        relay["state"] = actuator_ ? actuator_->getState(Actuator::Type::RELAY) : false;
        if (actuator_ && actuator_->isTimerActive(Actuator::Type::RELAY)) {
            relay["timer_active"] = true;
            uint32_t remainingMillis = actuator_->getRemainingMillis(Actuator::Type::RELAY);
            
            relay["timer_remaining_ms"] = remainingMillis;
        } else {
            relay["timer_active"] = false;
        }
    }
    
    // DAC section (if available and DAC-related actuatorType OR general status)
    bool isDACStatus = actuatorType.equalsIgnoreCase("dac") || 
                       actuatorType.startsWith("dac_") ||
                       isRamping ||
                       actuatorType.isEmpty(); // Include DAC in general status
    if (mcp4725_ && isDACStatus) {
        JsonObject dac = doc.createNestedObject("dac");
        dac["value"] = mcp4725_->getCurrentValue();
        dac["voltage"] = mcp4725_->getCurrentVoltage();
        dac["percent"] = mcp4725_->getCurrentPercent();
        dac["last_command_type"] = mcp4725_->getLastCommandType();
        
        // Add ramping info if active
        if (isRamping) {
            dac["ramping"] = true;
            dac["ramp_duration_seconds"] = rampDurationSeconds;
            dac["ramp_elapsed_seconds"] = rampElapsedSeconds;
            
            // Add target value based on last command type
            String commandType = mcp4725_->getLastCommandType();
            if (commandType == "voltage") {
                dac["ramp_target_voltage"] = rampTargetValue;
            } else if (commandType == "percent") {
                dac["ramp_target_percent"] = rampTargetValue;
            } else if (commandType == "value") {
                dac["ramp_target_value"] = (uint16_t)rampTargetValue;
            }
        }
    }
    
    // PWM section (if available and PWM-related actuatorType OR general status)
    bool isPWMStatus = actuatorType.equalsIgnoreCase("pwm") || 
                       actuatorType.startsWith("pwm_") ||
                       actuatorType.isEmpty(); // Include PWM in general status
    
    // Check both PWM controllers and publish whichever is initialized
    PWMController* activePWM = nullptr;
    if (pwmController_ && pwmController_->isInitialized()) {
        activePWM = pwmController_;
    } else if (pwmControllerMOSFET_ && pwmControllerMOSFET_->isInitialized()) {
        activePWM = pwmControllerMOSFET_;
    }
    
    if (activePWM && isPWMStatus) {
        JsonObject pwm = doc.createNestedObject("pwm");
        pwm["value"] = activePWM->getCurrentValue();
        pwm["voltage"] = activePWM->getCurrentVoltage();
        pwm["percent"] = activePWM->getCurrentPercent();
        pwm["last_command_type"] = activePWM->getLastCommandType();
    }
    
    // System section
    JsonObject system = doc.createNestedObject("system");
    system["free_heap"] = ESP.getFreeHeap();
    
    // Last command section (if provided)
    if (!lastCommand.isEmpty()) {
        JsonObject lastCmd = doc.createNestedObject("last_command");
        lastCmd["command"] = lastCommand;
        lastCmd["target"] = lastTarget;
        lastCmd["timestamp"] = lastTimestamp.isEmpty() ? formatTimestamp(millis()) : lastTimestamp;
        if (!actuatorType.isEmpty()) {
            lastCmd["type"] = actuatorType;
        }
    }
    
    // Current timestamp
    doc["timestamp"] = formatTimestamp(millis());
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    return jsonString;
}

String ActuatorStatusPublisher::formatTimestamp(unsigned long millis) {
    unsigned long totalSeconds = millis / 1000;
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long seconds = totalSeconds % 60;
    
    char buffer[9];  // HH:MM:SS + null terminator
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    
    return String(buffer);
}

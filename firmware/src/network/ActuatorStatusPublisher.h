#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "../runtime/RuntimeConfig.h"
#include "../network/MQTTManager.h"
#include "../diagnostics/Logger.h"
#include "../hardware/Actuator.h"

// Forward declarations
class MCP4725;
class PWMController;

/**
 * @brief MQTT Actuator Status Publisher
 * 
 * This class handles publishing actuator status to MQTT topics
 * in the standardized format matching the Leaf Smart Plug structure.
 */
class ActuatorStatusPublisher {
public:
    /**
     * @brief Constructor
     * @param config Configuration instance
     * @param mqttManager MQTT manager instance
     * @param logger Logger instance
     * @param actuator Actuator instance
     * @param mcp4725 MCP4725 DAC instance (optional)
     * @param pwmController PWM Controller IO2 instance (optional)
     * @param pwmControllerMOSFET PWM Controller MOSFET instance (optional)
     */
    ActuatorStatusPublisher(RuntimeConfig* config, MQTTManager* mqttManager, Logger* logger, Actuator* actuator, MCP4725* mcp4725 = nullptr, PWMController* pwmController = nullptr, PWMController* pwmControllerMOSFET = nullptr);
    
    /**
     * @brief Destructor
     */
    ~ActuatorStatusPublisher();

    /**
     * @brief Publish actuator status to MQTT
     * @param lastCommand Last command executed (e.g., "set")
     * @param lastTarget Last command target (e.g., "actuator")
     * @param lastTimestamp Last command timestamp
     * @param actuatorType Type of actuator ("mosfet" or "relay")
     * @param isRamping Is DAC currently ramping (optional)
     * @param rampTargetValue Target value for ramping (optional)
     * @param rampDurationSeconds Total ramping duration in seconds (optional)
     * @param rampElapsedSeconds Elapsed ramping time in seconds (optional)
     * @return true if published successfully
     */
    bool publishStatus(const String& lastCommand = "", const String& lastTarget = "", 
                      const String& lastTimestamp = "", const String& actuatorType = "",
                      bool isRamping = false, float rampTargetValue = 0.0f, 
                      uint32_t rampDurationSeconds = 0, uint32_t rampElapsedSeconds = 0);

    /**
     * @brief Get the MQTT topic for actuator status
     * @return MQTT topic string (lai/devices/{serial}/status)
     */
    String getStatusTopic() const;

private:
    RuntimeConfig* config_;
    MQTTManager* mqttManager_;
    Logger* logger_;
    Actuator* actuator_;
    MCP4725* mcp4725_;
    PWMController* pwmController_;         // PWM Controller for IO2
    PWMController* pwmControllerMOSFET_;   // PWM Controller for MOSFET
    
    /**
     * @brief Build the complete status payload JSON
     * @param lastCommand Last command executed
     * @param lastTarget Last command target
     * @param lastTimestamp Last command timestamp
     * @param actuatorType Type of actuator ("mosfet" or "relay")
     * @param isRamping Is DAC currently ramping
     * @param rampTargetValue Target value for ramping
     * @param rampDurationSeconds Total ramping duration in seconds
     * @param rampElapsedSeconds Elapsed ramping time in seconds
     * @return JSON string payload
     */
    String buildStatusPayload(const String& lastCommand, const String& lastTarget, 
                             const String& lastTimestamp, const String& actuatorType,
                             bool isRamping, float rampTargetValue, 
                             uint32_t rampDurationSeconds, uint32_t rampElapsedSeconds);
    
    /**
     * @brief Format timestamp from milliseconds to HH:MM:SS
     * @param millis Milliseconds since boot
     * @return Formatted time string
     */
    String formatTimestamp(unsigned long millis);
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include "../LeafNodeTypes.h"

/**
 * @brief Command target types for modular routing
 */
enum class CommandTarget {
    SENSOR,    // Sensor related commands
    ACTUATOR,  // Actuator control commands
    SYSTEM     // System level commands
};

/**
 * @brief Command execution result
 */
struct CommandResult {
    bool success;
    String message;
    DynamicJsonDocument responseData;
    
    CommandResult(bool s = false, const String& msg = "") 
        : success(s), message(msg), responseData(1024) {}  // Increased from 256 for schedule responses
};

/**
 * @brief Command handler function type
 */
using CommandFunction = std::function<CommandResult(const JsonDocument&)>;

/**
 * @brief MQTT Command Handler - Modular and extensible command system
 * 
 * This class handles incoming MQTT commands with a modular architecture
 * that allows easy extension for new command types and targets.
 */
class CommandHandler {
public:
    CommandHandler();
    ~CommandHandler();

    /**
     * @brief Initialize the command handler
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Process an incoming MQTT command message
     * @param topic The MQTT topic the command was received on
     * @param payload The JSON command payload
     * @return true if command was processed successfully
     */
    bool processCommand(const String& topic, const String& payload);

    /**
     * @brief Register a command handler for a specific target and command
     * @param target The command target (sensor, actuator, system)
     * @param command The command name
     * @param handler The function to handle this command
     */
    void registerCommand(CommandTarget target, const String& command, CommandFunction handler);

    /**
     * @brief Set callback for sending command responses
     * @param callback Function to call when sending responses
     */
    void setResponseCallback(std::function<bool(const String&, const String&)> callback) {
        responseCallback_ = callback;
    }

    /**
     * @brief Set logger for command processing
     */
    void setLogger(class Logger* logger) { logger_ = logger; }

    /**
     * @brief Set system manager for LED control
     */
    void setSystemManager(class SystemManager* systemManager) { systemManager_ = systemManager; }

    /**
     * @brief Set OTA manager for firmware update commands
     */
    void setOTAManager(class OTAManager* otaManager) { otaManager_ = otaManager; }

    /**
     * @brief Set sensor manager for sensor configuration commands
     */
    void setSensorManager(class SensorManager* sensorManager) { sensorManager_ = sensorManager; }

    /**
     * @brief Set runtime config for device configuration commands
     */
    void setRuntimeConfig(class RuntimeConfig* config) { config_ = config; }

    /**
     * @brief Set actuator for actuator control commands
     */
    void setActuator(class Actuator* actuator) { actuator_ = actuator; }

    /**
     * @brief Set MCP4725 DAC for analog output control commands
     */
    void setMCP4725(class MCP4725* mcp4725) { mcp4725_ = mcp4725; }

    /**
     * @brief Set PWM Controller (IO2) for PWM output control commands
     */
    void setPWMController(class PWMController* pwmController) { pwmController_ = pwmController; }

    /**
     * @brief Set PWM Controller (MOSFET) for PWM output control commands
     */
    void setPWMControllerMOSFET(class PWMController* pwmControllerMOSFET) { pwmControllerMOSFET_ = pwmControllerMOSFET; }

    /**
     * @brief Set actuator status publisher for status updates
     */
    void setActuatorStatusPublisher(class ActuatorStatusPublisher* publisher) { actuatorStatusPublisher_ = publisher; }
    
    /**
     * @brief Set schedule manager for time-based scheduling commands
     */
    void setScheduleManager(class ScheduleManager* scheduleManager) { scheduleManager_ = scheduleManager; }

private:
    bool initialized_;
    class Logger* logger_;
    class SystemManager* systemManager_;
    class OTAManager* otaManager_;
    class SensorManager* sensorManager_;
    class RuntimeConfig* config_;
    class Actuator* actuator_;
    class MCP4725* mcp4725_;
    class PWMController* pwmController_;
    class PWMController* pwmControllerMOSFET_;
    class ActuatorStatusPublisher* actuatorStatusPublisher_;
    class ScheduleManager* scheduleManager_;
    
    // Command registry - maps "target:command" to handler function
    std::map<String, CommandFunction> commandRegistry_;
    
    // Response callback for sending responses back via MQTT
    std::function<bool(const String&, const String&)> responseCallback_;
    
    // Helper methods
    String targetToString(CommandTarget target) const;
    CommandTarget stringToTarget(const String& targetStr) const;
    String buildCommandKey(CommandTarget target, const String& command) const;
    bool validateCommandFormat(const JsonDocument& doc) const;
    void sendResponse(const String& originalTopic, const JsonDocument& originalCommand, 
                     const CommandResult& result);
    String buildResponseTopic(const String& originalTopic) const;
    DynamicJsonDocument buildResponsePayload(const JsonDocument& originalCommand, 
                                            const CommandResult& result) const;
    
    // Built-in system commands
    void registerSystemCommands();
    void registerSensorCommands();
    void registerActuatorCommands();
    void registerDACCommands();
    void registerPWMCommands();
    void registerScheduleCommands();
    CommandResult handleSystemLedOn(const JsonDocument& params);
    CommandResult handleSystemLedOff(const JsonDocument& params);
    CommandResult handleSystemLedBlink(const JsonDocument& params);
    CommandResult handleSystemInfo(const JsonDocument& params);
    CommandResult handleSystemReset(const JsonDocument& params);
    CommandResult handleSystemReboot(const JsonDocument& params);
    CommandResult handleSystemUnregister(const JsonDocument& params);
    CommandResult handleFirmwareUpdate(const JsonDocument& params);
    CommandResult handleSystemSetTimezone(const JsonDocument& params);
    
    // Built-in sensor commands  
    CommandResult handleSensorConfigure(const JsonDocument& params);
    CommandResult handleSensorStatus(const JsonDocument& params);
    CommandResult handleSensorRead(const JsonDocument& params);
    CommandResult handleSensorSetInterval(const JsonDocument& params);
    CommandResult handleSensorEzoph(const JsonDocument& params);
    CommandResult handleSensorEzophCalibrate(const JsonDocument& params);
    CommandResult handleSensorEzophClearCalibration(const JsonDocument& params);
    
    // Built-in actuator commands
    CommandResult handleActuatorOn(const JsonDocument& params);
    CommandResult handleActuatorOff(const JsonDocument& params);
    CommandResult handleActuatorToggle(const JsonDocument& params);
    CommandResult handleActuatorCancelTimer(const JsonDocument& params);
    CommandResult handleActuatorStatus(const JsonDocument& params);
    void publishActuatorStatus(const JsonDocument& params, const String& typeStr);
    
    // Built-in DAC commands
    CommandResult handleDACSetValue(const JsonDocument& params);
    CommandResult handleDACSetVoltage(const JsonDocument& params);
    CommandResult handleDACSetPercent(const JsonDocument& params);
    CommandResult handleDACPowerDown(const JsonDocument& params);
    CommandResult handleDACReset(const JsonDocument& params);
    CommandResult handleDACRead(const JsonDocument& params);
    CommandResult handleDACStatus(const JsonDocument& params);
    
    // Built-in PWM commands
    CommandResult handlePWMSetValue(const JsonDocument& params);
    CommandResult handlePWMSetPercent(const JsonDocument& params);
    CommandResult handlePWMSetVoltage(const JsonDocument& params);
    CommandResult handlePWMOff(const JsonDocument& params);
    CommandResult handlePWMRead(const JsonDocument& params);
    CommandResult handlePWMStatus(const JsonDocument& params);
    
    // Built-in schedule commands
    CommandResult handleScheduleSet(const JsonDocument& params);
    CommandResult handleScheduleClear(const JsonDocument& params);
    CommandResult handleScheduleGet(const JsonDocument& params);
};

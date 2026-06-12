#include "CommandHandler.h"
#include "../diagnostics/Logger.h"
#include "../system/SystemManager.h"
#include "../../include/system/ScheduleManager.h"
#include "../sensors/SensorManager.h"
#include "../hardware/sensors/EZOph.h"
#include "../runtime/RuntimeConfig.h"
#include "../hardware/Actuator.h"
#include "../hardware/MCP4725.h"
#include "../hardware/PWMController.h"
#include "../network/ActuatorStatusPublisher.h"
#include "../../include/network/OTAManager.h"
#include "config.h"
#include <ArduinoJson.h>

CommandHandler::CommandHandler()
    : initialized_(false)
    , logger_(nullptr)
    , systemManager_(nullptr)
    , otaManager_(nullptr)
    , sensorManager_(nullptr)
    , config_(nullptr)
    , actuator_(nullptr)
    , mcp4725_(nullptr)
    , pwmController_(nullptr)
    , pwmControllerMOSFET_(nullptr)
    , actuatorStatusPublisher_(nullptr)
    , scheduleManager_(nullptr) {
}

CommandHandler::~CommandHandler() {
    commandRegistry_.clear();
}

bool CommandHandler::initialize() {
    if (initialized_) {
        return true;
    }
    
    if (logger_) {
        logger_->info("CommandHandler", "Initializing command handler...");
    }
    
    // Register built-in system commands
    registerSystemCommands();
    
    // Register built-in sensor commands
    registerSensorCommands();
    
    // Register built-in actuator commands
    registerActuatorCommands();
    
    // Register built-in DAC commands
    registerDACCommands();
    
    // Register built-in PWM commands
    registerPWMCommands();
    
    // Register built-in schedule commands
    registerScheduleCommands();
    
    initialized_ = true;
    
    if (logger_) {
        logger_->info("CommandHandler", "Command handler initialized with " + 
                     String(commandRegistry_.size()) + " commands");
    }
    
    return true;
}

bool CommandHandler::processCommand(const String& topic, const String& payload) {
    if (!initialized_) {
        if (logger_) {
            logger_->error("CommandHandler", "Handler not initialized");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("CommandHandler", "Processing command from topic: " + topic);
        logger_->debug("CommandHandler", "Payload: " + payload);
    }
    
    // Parse JSON payload (use larger buffer for OTA commands with signed URLs)
    DynamicJsonDocument doc(JSON_BUFFER_SIZE_LARGE);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        if (logger_) {
            logger_->error("CommandHandler", "JSON parse error: " + String(error.c_str()));
        }
        return false;
    }
    
    if (logger_) {
        String debugInfo = "Parsed JSON fields: ";
        if (doc.containsKey("target")) debugInfo += "target=" + doc["target"].as<String>() + " ";
        if (doc.containsKey("command")) debugInfo += "command=" + doc["command"].as<String>() + " ";
        if (doc.containsKey("parameters")) debugInfo += "parameters=present ";
        if (doc.containsKey("timestamp")) debugInfo += "timestamp=" + doc["timestamp"].as<String>() + " ";
        if (doc.containsKey("source")) debugInfo += "source=" + doc["source"].as<String>() + " ";
        logger_->debug("CommandHandler", debugInfo);
    }
    
    // Validate command format
    if (!validateCommandFormat(doc)) {
        if (logger_) {
            logger_->error("CommandHandler", "Invalid command format");
        }
        return false;
    }
    
    // Extract command details
    String targetStr = doc["target"].as<String>();
    String command = doc["command"].as<String>();
    
    // Convert target string to enum
    CommandTarget target = stringToTarget(targetStr);
    
    // Build command key for lookup
    String commandKey = buildCommandKey(target, command);
    
    if (logger_) {
        logger_->info("CommandHandler", "Executing command: " + commandKey);
    }
    
    // Find and execute command handler
    auto it = commandRegistry_.find(commandKey);
    if (it == commandRegistry_.end()) {
        if (logger_) {
            logger_->warning("CommandHandler", "Unknown command: " + commandKey);
        }
        
        // Send error response
        CommandResult result(false, "Unknown command: " + commandKey);
        sendResponse(topic, doc, result);
        return false;
    }
    
    // Execute command
    CommandResult result = it->second(doc);
    
    // Log result
    if (logger_) {
        if (result.success) {
            logger_->info("CommandHandler", "Command executed successfully: " + result.message);
        } else {
            logger_->warning("CommandHandler", "Command failed: " + result.message);
        }
    }
    
    // Send response
    sendResponse(topic, doc, result);
    
    return result.success;
}

void CommandHandler::registerCommand(CommandTarget target, const String& command, CommandFunction handler) {
    String commandKey = buildCommandKey(target, command);
    commandRegistry_[commandKey] = handler;
    
    if (logger_) {
        logger_->debug("CommandHandler", "Registered command: " + commandKey);
    }
}

String CommandHandler::targetToString(CommandTarget target) const {
    switch (target) {
        case CommandTarget::SENSOR:   return "sensor";
        case CommandTarget::ACTUATOR: return "actuator";
        case CommandTarget::SYSTEM:   return "system";
        default:                      return "unknown";
    }
}

CommandTarget CommandHandler::stringToTarget(const String& targetStr) const {
    if (targetStr == "sensor")   return CommandTarget::SENSOR;
    if (targetStr == "actuator") return CommandTarget::ACTUATOR;
    if (targetStr == "system")   return CommandTarget::SYSTEM;
    return CommandTarget::SYSTEM; // Default fallback
}

String CommandHandler::buildCommandKey(CommandTarget target, const String& command) const {
    return targetToString(target) + ":" + command;
}

bool CommandHandler::validateCommandFormat(const JsonDocument& doc) const {
    // Check required fields
    if (!doc.containsKey("target")) {
        if (logger_) logger_->error("CommandHandler", "Missing 'target' field");
        return false;
    }
    if (!doc.containsKey("command")) {
        if (logger_) logger_->error("CommandHandler", "Missing 'command' field");
        return false;
    }
    if (!doc.containsKey("parameters")) {
        if (logger_) logger_->error("CommandHandler", "Missing 'parameters' field");
        return false;
    }
    if (!doc.containsKey("timestamp")) {
        if (logger_) logger_->error("CommandHandler", "Missing 'timestamp' field");
        return false;
    }
    if (!doc.containsKey("source")) {
        if (logger_) logger_->error("CommandHandler", "Missing 'source' field");
        return false;
    }
    
    // Validate field types
    if (!doc["target"].is<String>()) {
        if (logger_) logger_->error("CommandHandler", "Field 'target' is not a string");
        return false;
    }
    if (!doc["command"].is<String>()) {
        if (logger_) logger_->error("CommandHandler", "Field 'command' is not a string");
        return false;
    }
    // Parameters field should be present (can be empty object)
    if (!doc.containsKey("parameters")) {
        if (logger_) {
            logger_->error("CommandHandler", "Field 'parameters' is missing");
        }
        return false;
    }
    if (!doc["timestamp"].is<String>()) {
        if (logger_) logger_->error("CommandHandler", "Field 'timestamp' is not a string");
        return false;
    }
    if (!doc["source"].is<String>()) {
        if (logger_) logger_->error("CommandHandler", "Field 'source' is not a string");
        return false;
    }
    
    return true;
}

void CommandHandler::sendResponse(const String& originalTopic, const JsonDocument& originalCommand, 
                                 const CommandResult& result) {
    if (!responseCallback_) {
        if (logger_) {
            logger_->warning("CommandHandler", "No response callback set, cannot send response");
        }
        return;
    }
    
    // Build response topic
    String responseTopic = buildResponseTopic(originalTopic);
    
    // Build response payload
    DynamicJsonDocument responsePayload = buildResponsePayload(originalCommand, result);
    
    // Serialize response
    String responseJson;
    serializeJson(responsePayload, responseJson);
    
    // DEBUG: Print response info
    Serial.println("[DEBUG sendResponse] Topic: " + responseTopic + 
                   " Size: " + String(responseJson.length()) + " bytes");
    Serial.println("[DEBUG sendResponse] Payload: " + responseJson);
    
    // Send response
    if (responseCallback_(responseTopic, responseJson)) {
        if (logger_) {
            logger_->debug("CommandHandler", "Response sent to: " + responseTopic);
        }
    } else {
        if (logger_) {
            logger_->error("CommandHandler", "Failed to send response to: " + responseTopic);
        }
    }
}

String CommandHandler::buildResponseTopic(const String& originalTopic) const {
    // Convert command topic to status topic
    // lai/devices/{serial}/command -> lai/devices/{serial}/status
    String responseTopic = originalTopic;
    responseTopic.replace("/command", "/status");
    return responseTopic;
}

DynamicJsonDocument CommandHandler::buildResponsePayload(const JsonDocument& originalCommand, 
                                                       const CommandResult& result) const {
    DynamicJsonDocument response(JSON_BUFFER_SIZE_LARGE);  // Use larger buffer for responses
    
    // Copy original command context
    response["original_command"] = originalCommand["command"];
    response["original_target"] = originalCommand["target"];
    response["original_timestamp"] = originalCommand["timestamp"];
    response["original_source"] = originalCommand["source"];
    
    // Add response details
    response["success"] = result.success;
    response["message"] = result.message;
    response["timestamp"] = String(millis()); // Response timestamp
    response["response_source"] = "leaf_node";
    
    // Add response data if available
    if (!result.responseData.isNull() && result.responseData.size() > 0) {
        response["data"] = result.responseData;
    }
    
    return response;
}

void CommandHandler::registerSystemCommands() {
    // Register built-in system commands
    registerCommand(CommandTarget::SYSTEM, "led_on", 
        [this](const JsonDocument& params) { return handleSystemLedOn(params); });
    
    registerCommand(CommandTarget::SYSTEM, "led_off", 
        [this](const JsonDocument& params) { return handleSystemLedOff(params); });
    
    registerCommand(CommandTarget::SYSTEM, "led_blink", 
        [this](const JsonDocument& params) { return handleSystemLedBlink(params); });
    
    registerCommand(CommandTarget::SYSTEM, "info", 
        [this](const JsonDocument& params) { return handleSystemInfo(params); });
    
    registerCommand(CommandTarget::SYSTEM, "reset", 
        [this](const JsonDocument& params) { return handleSystemReset(params); });
    
    registerCommand(CommandTarget::SYSTEM, "reboot", 
        [this](const JsonDocument& params) { return handleSystemReboot(params); });
    
    registerCommand(CommandTarget::SYSTEM, "firmware_update", 
        [this](const JsonDocument& params) { return handleFirmwareUpdate(params); });
    
    registerCommand(CommandTarget::SYSTEM, "unregister", 
        [this](const JsonDocument& params) { return handleSystemUnregister(params); });
    
    registerCommand(CommandTarget::SYSTEM, "set_timezone", 
        [this](const JsonDocument& params) { return handleSystemSetTimezone(params); });
}

void CommandHandler::registerSensorCommands() {
    // Register built-in sensor commands
    registerCommand(CommandTarget::SENSOR, "configure", 
        [this](const JsonDocument& params) { return handleSensorConfigure(params); });
    
    registerCommand(CommandTarget::SENSOR, "status", 
        [this](const JsonDocument& params) { return handleSensorStatus(params); });
    
    registerCommand(CommandTarget::SENSOR, "read", 
        [this](const JsonDocument& params) { return handleSensorRead(params); });
    
    registerCommand(CommandTarget::SENSOR, "set_interval", 
        [this](const JsonDocument& params) { return handleSensorSetInterval(params); });
    
    registerCommand(CommandTarget::SENSOR, "ezoph", 
        [this](const JsonDocument& params) { return handleSensorEzoph(params); });
    
    registerCommand(CommandTarget::SENSOR, "ezoph_calibrate", 
        [this](const JsonDocument& params) { return handleSensorEzophCalibrate(params); });
    
    registerCommand(CommandTarget::SENSOR, "ezoph_clear_calibration", 
        [this](const JsonDocument& params) { return handleSensorEzophClearCalibration(params); });
}

void CommandHandler::registerActuatorCommands() {
    // Register built-in actuator commands
    registerCommand(CommandTarget::ACTUATOR, "on", 
        [this](const JsonDocument& params) { return handleActuatorOn(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "off", 
        [this](const JsonDocument& params) { return handleActuatorOff(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "toggle", 
        [this](const JsonDocument& params) { return handleActuatorToggle(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "cancel_timer", 
        [this](const JsonDocument& params) { return handleActuatorCancelTimer(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "status", 
        [this](const JsonDocument& params) { return handleActuatorStatus(params); });
}

CommandResult CommandHandler::handleSystemLedOn(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sys_led_on");
    }
    
    if (!systemManager_) {
        CommandResult result(false, "System manager not available");
        return result;
    }
    
    // Get brightness parameter if provided, default to full brightness
    uint8_t brightness = 255;
    if (params["parameters"]["brightness"].is<int>()) {
        int brightnessValue = params["parameters"]["brightness"];
        if (brightnessValue >= 0 && brightnessValue <= 255) {
            brightness = brightnessValue;
        }
    }
    
    // Turn on green LED (success color) with specified brightness
    systemManager_->setRGBLED(0, brightness, 0);
    
    CommandResult result(true, "System LED turned on (green)");
    result.responseData["led_state"] = "on";
    result.responseData["color"] = "green";
    result.responseData["brightness"] = brightness;
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handleSystemLedOff(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sys_led_off");
    }
    
    if (!systemManager_) {
        CommandResult result(false, "System manager not available");
        return result;
    }
    
    // Turn off all LEDs
    systemManager_->setRGBLED((uint8_t)0, (uint8_t)0, (uint8_t)0);
    
    CommandResult result(true, "System LED turned off");
    result.responseData["led_state"] = "off";
    result.responseData["color"] = "none";
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handleSystemLedBlink(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sys_led_blink");
    }
    
    if (!systemManager_) {
        CommandResult result(false, "System manager not available");
        return result;
    }
    
    // Store current LED state before blinking
    uint8_t originalRed, originalGreen, originalBlue;
    systemManager_->getCurrentRGBLED(originalRed, originalGreen, originalBlue);
    
    // Parse parameters
    int count = 3; // Default blink count
    int delay_ms = 500; // Default delay in milliseconds
    uint8_t red = 0, green = 0, blue = 0; // Default: LED off, colors must be explicitly set
    
    // Check if any color is specified, if not use default green
    bool colorSpecified = params["parameters"]["red"].is<int>() || 
                         params["parameters"]["green"].is<int>() || 
                         params["parameters"]["blue"].is<int>();
    
    if (!colorSpecified) {
        // No color specified, use default green
        green = 255;
    }
    
    // Access parameters directly using nested JSON access
    if (params["parameters"]["count"].is<int>()) {
        int countValue = params["parameters"]["count"];
        if (countValue > 0 && countValue <= 50) {
            count = countValue;
        }
    }
    
    if (params["parameters"]["delay"].is<int>()) {
        int delayValue = params["parameters"]["delay"];
        if (delayValue >= 50 && delayValue <= 5000) {
            delay_ms = delayValue;
        }
    }
    
    // Optional color parameters
    if (params["parameters"]["red"].is<int>()) {
        int redValue = params["parameters"]["red"];
        if (redValue >= 0 && redValue <= 255) {
            red = redValue;
        }
    }
    if (params["parameters"]["green"].is<int>()) {
        int greenValue = params["parameters"]["green"];
        if (greenValue >= 0 && greenValue <= 255) {
            green = greenValue;
        }
    }
    if (params["parameters"]["blue"].is<int>()) {
        int blueValue = params["parameters"]["blue"];
        if (blueValue >= 0 && blueValue <= 255) {
            blue = blueValue;
        }
    }
    
    if (logger_) {
        logger_->info("Command", "Blinking LED " + String(count) + " times with " + String(delay_ms) + "ms delay");
    }
    
    // Perform blink sequence
    for (int i = 0; i < count; i++) {
        // Turn on LED with specified color
        systemManager_->setRGBLED(red, green, blue);
        delay(delay_ms);
        
        // Turn off LED
        systemManager_->setRGBLED((uint8_t)0, (uint8_t)0, (uint8_t)0);
        if (i < count - 1) { // Don't delay after last blink
            delay(delay_ms);
        }
    }
    
    // Restore original LED state
    systemManager_->setRGBLED(originalRed, originalGreen, originalBlue);
    
    CommandResult result(true, "LED blink sequence completed");
    result.responseData["led_state"] = "blinked";
    result.responseData["count"] = count;
    result.responseData["delay_ms"] = delay_ms;
    result.responseData["color"] = "rgb(" + String(red) + "," + String(green) + "," + String(blue) + ")";
    result.responseData["original_state"] = "rgb(" + String(originalRed) + "," + String(originalGreen) + "," + String(originalBlue) + ")";
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handleSystemInfo(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing system info");
    }
    
    CommandResult result(true, "System information retrieved");
    
    // Add system information to response
    result.responseData["firmware_version"] = FIRMWARE_VERSION;
    result.responseData["uptime_ms"] = String(millis());
    result.responseData["free_heap"] = String(ESP.getFreeHeap());
    result.responseData["chip_model"] = ESP.getChipModel();
    result.responseData["chip_revision"] = String(ESP.getChipRevision());
    result.responseData["cpu_freq_mhz"] = String(ESP.getCpuFreqMHz());
    
    return result;
}

CommandResult CommandHandler::handleSystemReset(const JsonDocument& params) {
    if (logger_) {
        logger_->warning("Command", "System reset requested!");
    }
    
    CommandResult result(true, "System reset initiated");
    result.responseData["reset_in_ms"] = "5000";
    
    // Schedule reset after response is sent
    // Note: In real implementation, you'd want to delay this properly
    
    return result;
}

CommandResult CommandHandler::handleSystemReboot(const JsonDocument& params) {
    if (logger_) {
        logger_->warning("Command", "System reboot requested - device will restart in 2 seconds");
    }
    
    // Get optional delay parameter (in milliseconds), default to 2000ms
    int delay_ms = 2000;
    if (params["parameters"]["delay"].is<int>()) {
        int delayValue = params["parameters"]["delay"];
        if (delayValue >= 0 && delayValue <= 10000) { // Max 10 seconds
            delay_ms = delayValue;
        }
    }
    
    CommandResult result(true, "Device reboot initiated");
    result.responseData["reboot_in_ms"] = delay_ms;
    result.responseData["timestamp"] = String(millis());
    
    // Schedule reboot after delay to allow MQTT response to be sent
    delay(delay_ms);
    
    if (logger_) {
        logger_->warning("Command", "Rebooting now...");
    }
    
    ESP.restart();
    
    return result;
}

CommandResult CommandHandler::handleSystemUnregister(const JsonDocument& params) {
    if (logger_) {
        logger_->warning("Command", "Device unregister requested!");
    }
    
    if (!config_) {
        CommandResult result(false, "Configuration manager not available");
        return result;
    }
    
    // Check if device is currently registered
    if (!config_->isDeviceSetup()) {
        CommandResult result(false, "Device is not registered");
        result.responseData["setup_complete"] = false;
        return result;
    }
    
    if (logger_) {
        logger_->info("Command", "Resetting registration status (setup_complete = false)");
    }
    
    // Reset the setup complete flag
    config_->setDeviceSetup(false);
    
    if (!config_->save()) {
        CommandResult result(false, "Failed to save configuration");
        return result;
    }
    
    if (logger_) {
        logger_->info("Command", "Registration reset successful. Rebooting in 3 seconds...");
    }
    
    CommandResult result(true, "Device unregistered successfully");
    result.responseData["setup_complete"] = false;
    result.responseData["action"] = "rebooting";
    result.responseData["reboot_in_ms"] = 3000;
    
    // Schedule reboot after delay to allow MQTT response to be sent
    // Using a simple delay approach - the response will be sent before the restart
    delay(3000);
    ESP.restart();
    
    return result;
}

CommandResult CommandHandler::handleFirmwareUpdate(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Firmware update requested");
    }
    
    if (!otaManager_) {
        CommandResult result(false, "OTA manager not available");
        return result;
    }
    
    // Extract required parameters - access directly without intermediate assignment
    if (!params["parameters"]["firmware_version"].is<String>() || 
        !params["parameters"]["download_url"].is<String>() || 
        !params["parameters"]["file_size"].is<unsigned int>()) {
        CommandResult result(false, "Missing required parameters: firmware_version, download_url, file_size");
        return result;
    }
    
    String firmwareVersion = params["parameters"]["firmware_version"].as<String>();
    String downloadUrl = params["parameters"]["download_url"].as<String>();
    uint32_t fileSize = params["parameters"]["file_size"].as<unsigned int>();
    
    // Extract optional parameters
    bool forceUpdate = params["parameters"]["force_update"].as<bool>(); // defaults to false
    uint32_t timeoutMinutes = params["parameters"]["timeout_minutes"].is<unsigned int>() ? 
                              params["parameters"]["timeout_minutes"].as<unsigned int>() : 30;
    
    if (logger_) {
        logger_->info("Command", "Starting firmware update:");
        logger_->info("Command", "  Version: " + firmwareVersion);
        logger_->info("Command", "  URL: " + downloadUrl);
        logger_->info("Command", "  Size: " + String(fileSize) + " bytes");
        logger_->info("Command", "  Force: " + String(forceUpdate ? "yes" : "no"));
        logger_->info("Command", "  Timeout: " + String(timeoutMinutes) + " minutes");
    }
    
    // Start the update
    bool updateStarted = otaManager_->startUpdate(firmwareVersion, downloadUrl, fileSize, 
                                                  forceUpdate, timeoutMinutes);
    
    CommandResult result(updateStarted, updateStarted ? "Firmware update started" : "Failed to start firmware update");
    
    if (updateStarted) {
        result.responseData["firmware_version"] = firmwareVersion;
        result.responseData["download_url"] = downloadUrl;
        result.responseData["file_size"] = fileSize;
        result.responseData["force_update"] = forceUpdate;
        result.responseData["timeout_minutes"] = timeoutMinutes;
        result.responseData["status"] = "started";
        result.responseData["estimated_duration"] = String(timeoutMinutes) + " minutes max";
    } else {
        // Get error details from OTA manager
        auto progress = otaManager_->getProgress();
        if (!progress.errorMessage.isEmpty()) {
            result.message = progress.errorMessage;
            result.responseData["error"] = progress.errorMessage;
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleSensorConfigure(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor configure command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    // Extract sensor type parameter from parameters object
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("sensor_type")) {
        return CommandResult(false, "Missing required parameter: sensor_type");
    }

    String sensorType = params["parameters"]["sensor_type"].as<String>();
    sensorType.toUpperCase();
    
    SensorProfile profile;
    if (sensorType == "SLT5007") {
        profile = SensorProfile::SLT5007;
    } else if (sensorType == "SHT31") {
        profile = SensorProfile::SHT31;
    } else if (sensorType == "CWTPSS") {
        profile = SensorProfile::CWTPSS;
    } else if (sensorType == "LEAFTHSN") {
        profile = SensorProfile::LEAFTHSN;
    } else if (sensorType == "CWTSOILTHS") {
        profile = SensorProfile::CWTSOILTHS;
    } else if (sensorType == "TEROS12") {
        profile = SensorProfile::TEROS12;
    } else if (sensorType == "EZOPH") {
        profile = SensorProfile::EZOPH;
    } else if (sensorType == "EZOEC") {
        profile = SensorProfile::EZOEC;
    } else if (sensorType == "DS18B20") {
        profile = SensorProfile::DS18B20;
    } else if (sensorType == "CWTTHXXS") {
        profile = SensorProfile::CWTTHXXS;
    } else if (sensorType == "NONE") {
        profile = SensorProfile::NONE;
    } else {
        return CommandResult(false, "Unknown sensor type: " + sensorType + ". Supported: SLT5007, SHT31, CWTPSS, LEAFTHSN, CWTSOILTHS, TEROS12, EZOPH, EZOEC, DS18B20, CWTTHXXS, NONE");
    }

    // Configure the sensor
    bool success = sensorManager_->configureSensor(profile);
    
    CommandResult result(success, success ? "Sensor configured successfully" : "Failed to configure sensor");
    
    if (success) {
        result.responseData["sensor_type"] = sensorType;
        result.responseData["sensor_profile"] = static_cast<int>(profile);
        result.responseData["configured"] = true;
        result.responseData["requires_restart"] = false; // Dynamic configuration
        
        if (logger_) {
            logger_->info("Command", "Sensor configured to: " + sensorType);
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleSensorStatus(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor status command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    SensorProfile currentProfile = sensorManager_->getCurrentProfile();
    bool hasSensor = sensorManager_->hasSensorConfigured();
    uint32_t readingInterval = sensorManager_->getReadingInterval();
    
    CommandResult result(true, "Sensor status retrieved");
    
    result.responseData["has_sensor"] = hasSensor;
    result.responseData["sensor_profile"] = static_cast<int>(currentProfile);
    result.responseData["reading_interval_ms"] = readingInterval;
    result.responseData["reading_interval_min"] = readingInterval / 60000.0;
    
    String profileName;
    switch (currentProfile) {
        case SensorProfile::SLT5007:    profileName = "SLT5007"; break;
        case SensorProfile::SHT31:      profileName = "SHT31"; break;
        case SensorProfile::CWTPSS:     profileName = "CWTPSS"; break;
        case SensorProfile::LEAFTHSN:   profileName = "LEAFTHSN"; break;
        case SensorProfile::CWTSOILTHS: profileName = "CWTSoilTHS"; break;
        case SensorProfile::TEROS12:    profileName = "TEROS12"; break;
        case SensorProfile::EZOPH:      profileName = "EZOph"; break;
        case SensorProfile::EZOEC:      profileName = "EZOec"; break;
        case SensorProfile::DS18B20:    profileName = "DS18B20"; break;
        case SensorProfile::CWTTHXXS:   profileName = "CWTTHXXS"; break;
        case SensorProfile::NONE:       profileName = "None"; break;
        default:                        profileName = "Unknown"; break;
    }
    
    result.responseData["sensor_name"] = profileName;
    result.responseData["status"] = hasSensor ? "configured" : "not_configured";
    
    return result;
}

CommandResult CommandHandler::handleSensorRead(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor read command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    if (!sensorManager_->hasSensorConfigured()) {
        return CommandResult(false, "No sensor configured. Use 'sensor configure' command first");
    }

    // Force a sensor reading
    bool success = sensorManager_->readAndPublish();
    
    CommandResult result(success, success ? "Sensor reading completed and published" : "Sensor reading failed");
    
    if (success) {
        result.responseData["reading_triggered"] = true;
        result.responseData["published_to_mqtt"] = true;
        sensorManager_->updateLastReading(); // Update timestamp
    } else {
        result.responseData["reading_triggered"] = false;
        result.responseData["error"] = "Check MQTT connection and sensor hardware";
    }
    
    return result;
}

CommandResult CommandHandler::handleSensorSetInterval(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor set_interval command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    // Extract interval_ms parameter from parameters object
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("interval_ms")) {
        return CommandResult(false, "Missing required parameter: interval_ms (in milliseconds)");
    }

    uint32_t intervalMs = params["parameters"]["interval_ms"].as<unsigned int>();
    
    // Validate interval range (1 second to 1 hour)
    if (intervalMs < 1000) {
        return CommandResult(false, "Interval too short. Minimum: 1000ms (1 second)");
    }
    
    if (intervalMs > 3600000) {
        return CommandResult(false, "Interval too long. Maximum: 3600000ms (1 hour)");
    }

    // Set the new interval
    bool success = sensorManager_->setReadingInterval(intervalMs);
    
    CommandResult result(success, success ? "Sensor reading interval updated" : "Failed to update interval");
    
    if (success) {
        result.responseData["interval_ms"] = intervalMs;
        result.responseData["interval_seconds"] = intervalMs / 1000;
        result.responseData["interval_minutes"] = intervalMs / 60000.0;
        result.responseData["saved_to_config"] = true;
        
        if (logger_) {
            logger_->info("Command", "Sensor interval set to: " + String(intervalMs) + "ms");
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleSensorEzoph(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor ezoph command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    // Check if EZO pH sensor is configured
    EZOph* ezophSensor = sensorManager_->getEZOphSensor();
    if (!ezophSensor) {
        return CommandResult(false, "EZO pH sensor not configured. Use 'sensor configure' with type 'EZOPH' first");
    }

    // Extract i2c_command parameter from parameters object
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("i2c_command")) {
        return CommandResult(false, "Missing required parameter: i2c_command (e.g., 'Cal,?', 'Cal,mid,7.00', 'T,25.5', 'i')");
    }

    String i2cCommand = params["parameters"]["i2c_command"].as<String>();
    
    // Validate command is not empty
    if (i2cCommand.length() == 0) {
        return CommandResult(false, "i2c_command cannot be empty");
    }

    if (logger_) {
        logger_->info("Command", "Sending I2C command to EZO pH: " + i2cCommand);
    }

    // Send command and get response
    String response;
    EZOph::ResponseCode responseCode;
    bool success = ezophSensor->sendCommandAndGetResponse(i2cCommand, response, responseCode);
    
    // Build result
    CommandResult result(success, success ? "EZO pH command executed successfully" : "EZO pH command failed");
    
    // Add command details to response
    result.responseData["i2c_command"] = i2cCommand;
    result.responseData["sensor_response"] = response;
    result.responseData["response_code"] = static_cast<uint8_t>(responseCode);
    
    // Add human-readable response code description
    switch (responseCode) {
        case EZOph::ResponseCode::SUCCESS:
            result.responseData["response_description"] = "Success";
            break;
        case EZOph::ResponseCode::FAILED:
            result.responseData["response_description"] = "Failed";
            break;
        case EZOph::ResponseCode::PENDING:
            result.responseData["response_description"] = "Pending";
            break;
        case EZOph::ResponseCode::NO_DATA:
            result.responseData["response_description"] = "No Data";
            break;
        default:
            result.responseData["response_description"] = "Unknown";
            break;
    }
    
    if (logger_) {
        logger_->info("Command", "EZO pH response [" + result.responseData["response_description"].as<String>() + "]: " + response);
    }
    
    return result;
}

CommandResult CommandHandler::handleSensorEzophCalibrate(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor ezoph auto-calibration command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    // Check if EZO pH sensor is configured
    EZOph* ezophSensor = sensorManager_->getEZOphSensor();
    if (!ezophSensor) {
        return CommandResult(false, "EZO pH sensor not configured. Use 'sensor configure' with type 'EZOPH' first");
    }

    // Extract and validate parameters
    if (!params.containsKey("parameters")) {
        return CommandResult(false, "Missing parameters object");
    }

    JsonVariantConst parameters = params["parameters"];
    
    // Required parameter: calibrationPoint
    if (!parameters.containsKey("calibrationPoint")) {
        return CommandResult(false, "Missing required parameter: calibrationPoint (e.g., 4.0, 7.0, or 10.0)");
    }
    float calibrationPoint = parameters["calibrationPoint"].as<float>();
    
    // Optional parameters with defaults
    float temperature = parameters.containsKey("temp") ? parameters["temp"].as<float>() : 25.0;
    float stabilityThreshold = parameters.containsKey("stabilityThreshold") ? parameters["stabilityThreshold"].as<float>() : 0.03;
    float measureDuration = parameters.containsKey("measureDuration") ? parameters["measureDuration"].as<float>() : 1.5;
    uint16_t timeout = parameters.containsKey("timeout") ? parameters["timeout"].as<uint16_t>() : 180;

    // Validate parameters
    if (calibrationPoint < 0.0 || calibrationPoint > 14.0) {
        return CommandResult(false, "Invalid calibrationPoint: must be between 0.0 and 14.0");
    }
    if (stabilityThreshold <= 0.0 || stabilityThreshold > 1.0) {
        return CommandResult(false, "Invalid stabilityThreshold: must be between 0.0 and 1.0");
    }
    if (measureDuration < 0.5 || measureDuration > 10.0) {
        return CommandResult(false, "Invalid measureDuration: must be between 0.5 and 10.0 seconds");
    }
    if (timeout < 10 || timeout > 600) {
        return CommandResult(false, "Invalid timeout: must be between 10 and 600 seconds");
    }

    // Determine calibration type based on pH point
    String calType;
    String calCommand;
    if (calibrationPoint >= 3.0 && calibrationPoint <= 5.0) {
        calType = "low";
        calCommand = "Cal,low," + String(calibrationPoint, 2);
    } else if (calibrationPoint >= 6.0 && calibrationPoint <= 8.0) {
        calType = "mid";
        calCommand = "Cal,mid," + String(calibrationPoint, 2);
    } else if (calibrationPoint >= 9.0 && calibrationPoint <= 11.0) {
        calType = "high";
        calCommand = "Cal,high," + String(calibrationPoint, 2);
    } else {
        return CommandResult(false, "Invalid calibration point. Use 3.0-5.0 (low), 6.0-8.0 (mid), or 9.0-11.0 (high)");
    }

    if (logger_) {
        logger_->info("Command", "Starting auto-calibration: point=" + String(calibrationPoint, 2) + 
                     ", type=" + calType + ", temp=" + String(temperature, 1) + "°C, " +
                     "threshold=" + String(stabilityThreshold, 3) + ", timeout=" + String(timeout) + "s");
    }

    // Build status topic for progress updates using RuntimeConfig
    String statusTopic = config_ ? config_->getMQTTTopicStatus() : "lai/devices/unknown/status";
    
    if (logger_) {
        logger_->info("Command", "Progress updates will be sent to: " + statusTopic);
    }

    // Helper lambda for publishing progress updates
    auto publishProgress = [&](const String& state, float currentPH, uint8_t stabilityPercent, 
                               uint16_t readingsCount, uint16_t elapsedSeconds) {
        if (!responseCallback_) return;
        
        DynamicJsonDocument progressDoc(512);
        progressDoc["type"] = "calibration_progress";
        progressDoc["state"] = state;
        
        JsonObject data = progressDoc.createNestedObject("data");
        data["current_pH"] = round(currentPH * 100.0) / 100.0;
        data["stability_percent"] = stabilityPercent;
        data["readings_count"] = readingsCount;
        data["elapsed_seconds"] = elapsedSeconds;
        data["calibration_point"] = calibrationPoint;
        data["calibration_type"] = calType;
        
        String progressJson;
        serializeJson(progressDoc, progressJson);
        responseCallback_(statusTopic, progressJson);
        
        if (logger_) {
            logger_->debug("Command", "Progress: " + state + " pH=" + String(currentPH, 2) + 
                          " stability=" + String(stabilityPercent) + "%");
        }
    };

    // Pause automatic sensor readings to avoid I2C conflicts
    bool wasPaused = false;
    if (sensorManager_) {
        sensorManager_->pause();
        wasPaused = true;
        if (logger_) {
            logger_->info("Command", "Paused automatic sensor readings during calibration");
        }
    }

    // Publish starting state
    publishProgress("starting", 0.0, 0, 0, 0);

    // Set temperature compensation
    String tempResponse;
    EZOph::ResponseCode tempResponseCode;
    String tempCommand = "T," + String(temperature, 1);
    if (!ezophSensor->sendCommandAndGetResponse(tempCommand, tempResponse, tempResponseCode)) {
        if (wasPaused) sensorManager_->resume();
        return CommandResult(false, "Failed to set temperature compensation");
    }
    
    if (logger_) {
        logger_->info("Command", "Temperature set to " + String(temperature, 1) + "°C");
    }

    delay(500); // Let sensor process temperature setting

    // Stabilization loop
    const uint8_t STABILITY_BUFFER_SIZE = 5;
    float phReadings[STABILITY_BUFFER_SIZE];
    uint8_t bufferIndex = 0;
    uint16_t readingCount = 0;
    bool bufferFilled = false;
    unsigned long startTime = millis();
    unsigned long lastProgressUpdate = 0;
    const unsigned long PROGRESS_UPDATE_INTERVAL = 5000; // 5 seconds
    uint32_t measureDurationMs = (uint32_t)(measureDuration * 1000.0);
    
    bool stabilized = false;
    float finalPH = 0.0;
    uint16_t elapsedSeconds = 0;

    while ((millis() - startTime) < (timeout * 1000UL)) {
        // Read pH
        String phResponse;
        EZOph::ResponseCode phResponseCode;
        if (!ezophSensor->sendCommandAndGetResponse("R", phResponse, phResponseCode)) {
            if (logger_) {
                logger_->warning("Command", "Failed to read pH, retrying...");
            }
            delay(1000);
            continue;
        }

        if (phResponseCode != EZOph::ResponseCode::SUCCESS) {
            if (logger_) {
                logger_->warning("Command", "pH reading not ready, retrying...");
            }
            delay(1000);
            continue;
        }

        // Parse pH value
        float currentPH = phResponse.toFloat();
        if (currentPH < 0.0 || currentPH > 14.0) {
            if (logger_) {
                logger_->warning("Command", "Invalid pH value: " + String(currentPH));
            }
            delay(1000);
            continue;
        }

        // Store reading in circular buffer
        phReadings[bufferIndex] = currentPH;
        bufferIndex = (bufferIndex + 1) % STABILITY_BUFFER_SIZE;
        readingCount++;
        
        if (readingCount >= STABILITY_BUFFER_SIZE) {
            bufferFilled = true;
        }

        // Calculate stability if buffer is filled
        uint8_t stabilityPercent = 0;
        if (bufferFilled) {
            // Find min and max in buffer
            float minPH = phReadings[0];
            float maxPH = phReadings[0];
            for (uint8_t i = 1; i < STABILITY_BUFFER_SIZE; i++) {
                if (phReadings[i] < minPH) minPH = phReadings[i];
                if (phReadings[i] > maxPH) maxPH = phReadings[i];
            }
            float range = maxPH - minPH;

            // Calculate stability percentage
            if (range <= stabilityThreshold) {
                stabilityPercent = 100;
                stabilized = true;
                finalPH = currentPH;
            } else {
                // Linear mapping: threshold=100%, 5*threshold=0%
                float normalizedRange = range / stabilityThreshold;
                stabilityPercent = (uint8_t)max(0.0, min(100.0, (1.0 - (normalizedRange - 1.0) / 4.0) * 100.0));
            }

            if (logger_) {
                logger_->debug("Command", "Stability check: range=" + String(range, 3) + 
                              " threshold=" + String(stabilityThreshold, 3) + 
                              " stability=" + String(stabilityPercent) + "%");
            }
        }

        // Publish progress update every 5 seconds
        elapsedSeconds = (millis() - startTime) / 1000;
        if ((millis() - lastProgressUpdate) >= PROGRESS_UPDATE_INTERVAL) {
            publishProgress("stabilizing", currentPH, stabilityPercent, readingCount, elapsedSeconds);
            lastProgressUpdate = millis();
        }

        // Check if stabilized
        if (stabilized) {
            if (logger_) {
                logger_->info("Command", "Sensor stabilized at pH " + String(finalPH, 2) + 
                             " after " + String(elapsedSeconds) + " seconds");
            }
            break;
        }

        // Wait before next measurement
        delay(measureDurationMs);
    }

    // Check if we stabilized
    if (!stabilized) {
        if (wasPaused) sensorManager_->resume();
        publishProgress("timeout", phReadings[bufferIndex > 0 ? bufferIndex - 1 : STABILITY_BUFFER_SIZE - 1], 
                       0, readingCount, elapsedSeconds);
        
        CommandResult result(false, "Calibration failed - sensor did not stabilize within timeout");
        result.responseData["timeout_seconds"] = timeout;
        result.responseData["elapsed_seconds"] = elapsedSeconds;
        result.responseData["readings_taken"] = readingCount;
        result.responseData["final_stability_percent"] = 0;
        return result;
    }

    // Publish calibrating state
    publishProgress("calibrating", finalPH, 100, readingCount, elapsedSeconds);

    // Send calibration command
    String calResponse;
    EZOph::ResponseCode calResponseCode;
    if (!ezophSensor->sendCommandAndGetResponse(calCommand, calResponse, calResponseCode)) {
        if (wasPaused) sensorManager_->resume();
        return CommandResult(false, "Failed to send calibration command");
    }

    if (calResponseCode != EZOph::ResponseCode::SUCCESS) {
        if (wasPaused) sensorManager_->resume();
        CommandResult result(false, "Calibration command failed");
        result.responseData["calibration_command"] = calCommand;
        result.responseData["sensor_response"] = calResponse;
        result.responseData["response_code"] = static_cast<uint8_t>(calResponseCode);
        return result;
    }

    if (logger_) {
        logger_->info("Command", "Calibration command sent successfully: " + calCommand);
    }

    // Resume automatic readings
    if (wasPaused) {
        sensorManager_->resume();
        if (logger_) {
            logger_->info("Command", "Resumed automatic sensor readings");
        }
    }

    // Publish completed state
    publishProgress("completed", finalPH, 100, readingCount, elapsedSeconds);

    // Build success result
    CommandResult result(true, "pH calibration completed successfully");
    result.responseData["calibration_point"] = calibrationPoint;
    result.responseData["calibration_type"] = calType;
    result.responseData["final_pH"] = round(finalPH * 100.0) / 100.0;
    result.responseData["time_to_stabilize_seconds"] = elapsedSeconds;
    result.responseData["total_readings"] = readingCount;
    result.responseData["temperature"] = temperature;
    result.responseData["calibration_command"] = calCommand;
    result.responseData["sensor_response"] = calResponse;

    if (logger_) {
        logger_->info("Command", "Auto-calibration completed successfully in " + 
                     String(elapsedSeconds) + " seconds");
    }

    return result;
}

CommandResult CommandHandler::handleSensorEzophClearCalibration(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing sensor ezoph_clear_calibration command");
    }

    if (!sensorManager_) {
        return CommandResult(false, "Sensor manager not available");
    }

    // Check if EZO pH sensor is configured
    EZOph* ezophSensor = sensorManager_->getEZOphSensor();
    if (!ezophSensor) {
        return CommandResult(false, "EZO pH sensor not configured. Use 'sensor configure' with type 'EZOPH' first");
    }

    if (logger_) {
        logger_->info("Command", "Clearing all pH calibrations...");
    }

    // Send Cal,clear command
    String clearResponse;
    EZOph::ResponseCode clearResponseCode;
    if (!ezophSensor->sendCommandAndGetResponse("Cal,clear", clearResponse, clearResponseCode)) {
        return CommandResult(false, "Failed to send Cal,clear command");
    }

    if (clearResponseCode != EZOph::ResponseCode::SUCCESS) {
        CommandResult result(false, "Cal,clear command failed");
        result.responseData["sensor_response"] = clearResponse;
        result.responseData["response_code"] = static_cast<uint8_t>(clearResponseCode);
        return result;
    }

    if (logger_) {
        logger_->info("Command", "Cal,clear sent successfully, verifying...");
    }

    // Wait for sensor to process the clear command
    delay(300);

    // Verify with Cal,? command
    String verifyResponse;
    EZOph::ResponseCode verifyResponseCode;
    if (!ezophSensor->sendCommandAndGetResponse("Cal,?", verifyResponse, verifyResponseCode)) {
        CommandResult result(false, "Calibration cleared but verification failed");
        result.responseData["clear_response"] = clearResponse;
        result.responseData["verification_failed"] = true;
        return result;
    }

    if (verifyResponseCode != EZOph::ResponseCode::SUCCESS) {
        CommandResult result(false, "Calibration cleared but verification response invalid");
        result.responseData["clear_response"] = clearResponse;
        result.responseData["verify_response"] = verifyResponse;
        result.responseData["verify_response_code"] = static_cast<uint8_t>(verifyResponseCode);
        return result;
    }

    // Parse verification response - expect "?Cal,0"
    verifyResponse.trim();
    bool verifiedCleared = (verifyResponse == "?Cal,0");
    
    if (logger_) {
        logger_->info("Command", "Verification response: " + verifyResponse + 
                     " (cleared=" + String(verifiedCleared ? "yes" : "no") + ")");
    }

    // Build result
    CommandResult result(verifiedCleared, 
                        verifiedCleared ? "All pH calibrations cleared successfully" 
                                       : "Calibration clear command sent but sensor still reports calibrations");
    result.responseData["clear_response"] = clearResponse;
    result.responseData["verify_response"] = verifyResponse;
    result.responseData["verified_cleared"] = verifiedCleared;
    
    // Parse calibration count from response
    if (verifyResponse.startsWith("?Cal,")) {
        String countStr = verifyResponse.substring(5);
        int calCount = countStr.toInt();
        result.responseData["remaining_calibrations"] = calCount;
    }

    return result;
}

CommandResult CommandHandler::handleActuatorOn(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing actuator ON command");
    }

    if (!actuator_) {
        return CommandResult(false, "Actuator not available");
    }

    // Extract required parameters
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("type")) {
        return CommandResult(false, "Missing required parameter: type (mosfet/relay)");
    }

    // Extract and validate type parameter
    String typeStr = params["parameters"]["type"].as<String>();
    Actuator::Type type = Actuator::stringToType(typeStr);
    
    // Extract optional duration parameter in milliseconds
    uint32_t durationMillis = 0;
    bool hasDuration = false;
    
    if (params["parameters"].containsKey("duration_ms")) {
        if (params["parameters"]["duration_ms"].is<unsigned int>()) {
            durationMillis = params["parameters"]["duration_ms"].as<unsigned int>();
            if (durationMillis > 86400000) { // Max 24 hours in milliseconds
                return CommandResult(false, "Duration too long. Maximum: 86400000 milliseconds (24 hours)");
            }
            hasDuration = true;
        }
    }

    // Turn on the actuator
    bool success = actuator_->turnOnMillis(type, durationMillis);
    
    CommandResult result(success, success ? 
        Actuator::typeToString(type) + " turned ON" + (hasDuration ? " with timer" : "") : 
        "Failed to turn ON " + Actuator::typeToString(type));
    
    if (success) {
        result.responseData["type"] = Actuator::typeToString(type);
        result.responseData["state"] = true;
        result.responseData["timer_active"] = hasDuration;
        if (hasDuration) {
            uint32_t remainingMillis = actuator_->getRemainingMillis(type);
            
            result.responseData["duration_ms"] = durationMillis;
            result.responseData["timer_remaining_ms"] = remainingMillis;
        }
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            String timerInfo = "";
            if (hasDuration) {
                timerInfo = " with " + String(durationMillis) + "ms timer";
            }
            logger_->info("Command", Actuator::typeToString(type) + " turned ON" + timerInfo);
        }
        
        // Publish actuator status to MQTT
        publishActuatorStatus(params, Actuator::typeToString(type));
    }
    
    return result;
}

CommandResult CommandHandler::handleActuatorOff(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing actuator OFF command");
    }

    if (!actuator_) {
        return CommandResult(false, "Actuator not available");
    }

    // Extract required parameters
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("type")) {
        return CommandResult(false, "Missing required parameter: type (mosfet/relay)");
    }

    // Extract and validate type parameter
    String typeStr = params["parameters"]["type"].as<String>();
    Actuator::Type type = Actuator::stringToType(typeStr);

    // Turn off the actuator
    bool success = actuator_->turnOff(type);
    
    CommandResult result(success, success ? 
        Actuator::typeToString(type) + " turned OFF" : 
        "Failed to turn OFF " + Actuator::typeToString(type));
    
    if (success) {
        result.responseData["type"] = Actuator::typeToString(type);
        result.responseData["state"] = false;
        result.responseData["timer_active"] = false;
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            logger_->info("Command", Actuator::typeToString(type) + " turned OFF");
        }
        
        // Publish actuator status to MQTT
        publishActuatorStatus(params, Actuator::typeToString(type));
    }
    
    return result;
}

CommandResult CommandHandler::handleActuatorToggle(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing actuator TOGGLE command");
    }

    if (!actuator_) {
        return CommandResult(false, "Actuator not available");
    }

    // Extract required parameters
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("type")) {
        return CommandResult(false, "Missing required parameter: type (mosfet/relay)");
    }

    // Extract and validate type parameter
    String typeStr = params["parameters"]["type"].as<String>();
    Actuator::Type type = Actuator::stringToType(typeStr);
    
    // Get current state before toggle
    bool oldState = actuator_->getState(type);
    
    // Extract optional duration parameter in milliseconds
    uint32_t durationMillis = 0;
    bool hasDuration = false;
    
    if (params["parameters"].containsKey("duration_ms")) {
        if (params["parameters"]["duration_ms"].is<unsigned int>()) {
            durationMillis = params["parameters"]["duration_ms"].as<unsigned int>();
            if (durationMillis > 86400000) { // Max 24 hours
                return CommandResult(false, "Duration too long. Maximum: 86400000 milliseconds (24 hours)");
            }
            hasDuration = true;
        }
    }

    // Toggle the actuator
    bool success = actuator_->toggleMillis(type, durationMillis);
    bool newState = actuator_->getState(type);
    
    CommandResult result(success, success ? 
        Actuator::typeToString(type) + " toggled to " + String(newState ? "ON" : "OFF") : 
        "Failed to toggle " + Actuator::typeToString(type));
    
    if (success) {
        result.responseData["type"] = Actuator::typeToString(type);
        result.responseData["state"] = newState;
        result.responseData["previous_state"] = oldState;
        result.responseData["timer_active"] = hasDuration && newState;
        if (hasDuration && newState) {
            uint32_t remainingMillis = actuator_->getRemainingMillis(type);
            
            result.responseData["duration_ms"] = durationMillis;
            result.responseData["timer_remaining_ms"] = remainingMillis;
        }
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            logger_->info("Command", Actuator::typeToString(type) + " toggled from " + 
                         String(oldState ? "ON" : "OFF") + " to " + String(newState ? "ON" : "OFF"));
        }
        
        // Publish actuator status to MQTT
        publishActuatorStatus(params, Actuator::typeToString(type));
    }
    
    return result;
}

CommandResult CommandHandler::handleActuatorCancelTimer(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing actuator CANCEL_TIMER command");
    }

    if (!actuator_) {
        return CommandResult(false, "Actuator not available");
    }

    // Extract required parameters
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("type")) {
        return CommandResult(false, "Missing required parameter: type (mosfet/relay)");
    }

    // Extract and validate type parameter
    String typeStr = params["parameters"]["type"].as<String>();
    Actuator::Type type = Actuator::stringToType(typeStr);

    // Cancel the timer
    bool success = actuator_->cancelTimer(type);
    
    CommandResult result(success, success ? 
        Actuator::typeToString(type) + " timer cancelled" : 
        Actuator::typeToString(type) + " has no active timer");
    
    result.responseData["type"] = Actuator::typeToString(type);
    result.responseData["timer_active"] = false;
    result.responseData["timer_cancelled"] = success;
    result.responseData["state"] = actuator_->getState(type) ? "on" : "off";
    result.responseData["timestamp"] = String(millis());
    
    if (logger_) {
        logger_->info("Command", Actuator::typeToString(type) + " timer " + 
                     (success ? "cancelled" : "was not active"));
    }
    
    // Publish actuator status to MQTT if timer was cancelled
    if (success) {
        publishActuatorStatus(params, Actuator::typeToString(type));
    }
    
    return result;
}

CommandResult CommandHandler::handleActuatorStatus(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing actuator status command");
    }

    if (!actuator_) {
        return CommandResult(false, "Actuator not available");
    }

    // Get status for both actuators
    CommandResult result(true, "Actuator status retrieved");
    
    // MOSFET status
    result.responseData["mosfet"]["state"] = actuator_->getState(Actuator::Type::MOSFET);
    result.responseData["mosfet"]["timer_active"] = actuator_->isTimerActive(Actuator::Type::MOSFET);
    if (actuator_->isTimerActive(Actuator::Type::MOSFET)) {
        uint32_t remainingMillis = actuator_->getRemainingMillis(Actuator::Type::MOSFET);
        
        result.responseData["mosfet"]["timer_remaining_ms"] = remainingMillis;
    }
    
    // Relay status
    result.responseData["relay"]["state"] = actuator_->getState(Actuator::Type::RELAY);
    result.responseData["relay"]["timer_active"] = actuator_->isTimerActive(Actuator::Type::RELAY);
    if (actuator_->isTimerActive(Actuator::Type::RELAY)) {
        uint32_t remainingMillis = actuator_->getRemainingMillis(Actuator::Type::RELAY);
        
        result.responseData["relay"]["timer_remaining_ms"] = remainingMillis;
    }
    
    // DAC status (if available)
    if (mcp4725_) {
        result.responseData["dac"]["value"] = mcp4725_->getCurrentValue();
        result.responseData["dac"]["voltage"] = serialized(String(mcp4725_->getCurrentVoltage(), 2));
        result.responseData["dac"]["percent"] = serialized(String(mcp4725_->getCurrentPercent(), 1));
        result.responseData["dac"]["last_command_type"] = mcp4725_->getLastCommandType();
    }
    
    // PWM status (if available)
    if (pwmController_ && pwmController_->isInitialized()) {
        result.responseData["pwm"]["value"] = pwmController_->getCurrentValue();
        result.responseData["pwm"]["voltage"] = serialized(String(pwmController_->getCurrentVoltage(), 2));
        result.responseData["pwm"]["percent"] = serialized(String(pwmController_->getCurrentPercent(), 1));
        result.responseData["pwm"]["last_command_type"] = pwmController_->getLastCommandType();
    } else if (pwmControllerMOSFET_ && pwmControllerMOSFET_->isInitialized()) {
        result.responseData["pwm"]["value"] = pwmControllerMOSFET_->getCurrentValue();
        result.responseData["pwm"]["voltage"] = serialized(String(pwmControllerMOSFET_->getCurrentVoltage(), 2));
        result.responseData["pwm"]["percent"] = serialized(String(pwmControllerMOSFET_->getCurrentPercent(), 1));
        result.responseData["pwm"]["last_command_type"] = pwmControllerMOSFET_->getLastCommandType();
    }
    
    // Schedule information (if available)
    if (scheduleManager_ && scheduleManager_->isScheduleActive()) {
        auto schedule = scheduleManager_->getSchedule();
        result.responseData["schedule"]["active"] = true;
        result.responseData["schedule"]["actuator_type"] = schedule.actuatorType;
        result.responseData["schedule"]["on_at"] = schedule.onAt;
        result.responseData["schedule"]["off_at"] = schedule.offAt;
        result.responseData["schedule"]["valid_days"] = schedule.validDays;
        result.responseData["schedule"]["start_date"] = scheduleManager_->getStartDate();
        
        if (schedule.validDays > 0) {
            result.responseData["schedule"]["expires_on"] = scheduleManager_->getExpiresOn();
            result.responseData["schedule"]["days_remaining"] = scheduleManager_->getDaysRemaining();
        }
        
        result.responseData["schedule"]["next_action"] = scheduleManager_->getNextAction();
        result.responseData["schedule"]["next_action_time"] = scheduleManager_->getNextActionTime();
        result.responseData["schedule"]["timezone"] = schedule.timezone;
        
        // DAC-spezifische Schedule-Parameter
        if (schedule.actuatorType.startsWith("dac_")) {
            result.responseData["schedule"]["on_value"] = serialized(String(schedule.onValue, 2));
            result.responseData["schedule"]["off_value"] = serialized(String(schedule.offValue, 2));
            result.responseData["schedule"]["ramp_seconds"] = schedule.rampSeconds;
        }
    } else {
        result.responseData["schedule"]["active"] = false;
    }
    
    result.responseData["initialized"] = actuator_->isInitialized();
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

void CommandHandler::publishActuatorStatus(const JsonDocument& params, const String& typeStr) {
    if (actuatorStatusPublisher_) {
        String command = params["command"].as<String>();
        String target = params["target"].as<String>();
        
        // Format timestamp from milliseconds
        unsigned long ms = millis();
        unsigned long totalSeconds = ms / 1000;
        unsigned long hours = (totalSeconds / 3600) % 24;
        unsigned long minutes = (totalSeconds / 60) % 60;
        unsigned long seconds = totalSeconds % 60;
        char timestampBuf[9];
        snprintf(timestampBuf, sizeof(timestampBuf), "%02lu:%02lu:%02lu", hours, minutes, seconds);
        
        actuatorStatusPublisher_->publishStatus(command, target, String(timestampBuf), typeStr);
    } else {
        if (logger_) {
            logger_->warning("Command", "ActuatorStatusPublisher not available, status not published");
        }
    }
}

void CommandHandler::registerDACCommands() {
    // Register built-in DAC commands (target: actuator, command: dac_*)
    registerCommand(CommandTarget::ACTUATOR, "dac_set_value", 
        [this](const JsonDocument& params) { return handleDACSetValue(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_set_voltage", 
        [this](const JsonDocument& params) { return handleDACSetVoltage(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_set_percent", 
        [this](const JsonDocument& params) { return handleDACSetPercent(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_power_down", 
        [this](const JsonDocument& params) { return handleDACPowerDown(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_reset", 
        [this](const JsonDocument& params) { return handleDACReset(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_read", 
        [this](const JsonDocument& params) { return handleDACRead(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "dac_status", 
        [this](const JsonDocument& params) { return handleDACStatus(params); });
}

CommandResult CommandHandler::handleDACSetValue(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC set_value command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    // Execute command directly
    bool success = mcp4725_->handleCommand("set_value", params["parameters"]);
    
    CommandResult result(success, success ? 
        "DAC value set to " + String(params["parameters"]["value"].as<uint16_t>()) : 
        "Failed to set DAC value");
    
    if (success) {
        result.responseData["value"] = mcp4725_->readValue();
        result.responseData["voltage"] = mcp4725_->getCurrentVoltage();
        result.responseData["percent"] = mcp4725_->getCurrentPercent();
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_value", "actuator", "", "dac");
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleDACSetVoltage(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC set_voltage command");
    }

    if (!mcp4725_) {
        if (logger_) {
            logger_->error("Command", "MCP4725 pointer is NULL!");
        }
        return CommandResult(false, "MCP4725 DAC not available");
    }
    
    if (logger_) {
        logger_->info("Command", "MCP4725 pointer is valid, calling handleCommand...");
    }

    // Execute command directly - isAvailable() check happens inside handleCommand
    bool success = mcp4725_->handleCommand("set_voltage", params["parameters"]);
    
    if (logger_) {
        logger_->info("Command", "handleCommand returned: " + String(success ? "true" : "false"));
    }
    
    CommandResult result(success, success ? 
        "DAC voltage set to " + String(params["parameters"]["voltage"].as<float>(), 3) + "V" : 
        "Failed to set DAC voltage");
    
    if (success) {
        result.responseData["value"] = mcp4725_->readValue();
        result.responseData["voltage"] = mcp4725_->getCurrentVoltage();
        result.responseData["percent"] = mcp4725_->getCurrentPercent();
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_voltage", "actuator", "", "dac");
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleDACSetPercent(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC set_percent command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    bool success = mcp4725_->handleCommand("set_percent", params["parameters"]);
    
    CommandResult result(success, success ? 
        "DAC set to " + String(params["parameters"]["percent"].as<float>(), 1) + "%" : 
        "Failed to set DAC percent");
    
    if (success) {
        result.responseData["value"] = mcp4725_->readValue();
        result.responseData["voltage"] = mcp4725_->getCurrentVoltage();
        result.responseData["percent"] = mcp4725_->getCurrentPercent();
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_percent", "actuator", "", "dac");
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleDACPowerDown(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC power_down command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    bool success = mcp4725_->handleCommand("power_down", params["parameters"]);
    
    CommandResult result(success, success ? 
        "DAC powered down" : 
        "Failed to power down DAC");
    
    if (success) {
        result.responseData["state"] = "powered_down";
        result.responseData["timestamp"] = String(millis());
    }
    
    return result;
}

CommandResult CommandHandler::handleDACReset(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC reset command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    bool success = mcp4725_->handleCommand("reset", params["parameters"]);
    
    CommandResult result(success, success ? 
        "DAC reset to 0V" : 
        "Failed to reset DAC");
    
    if (success) {
        result.responseData["value"] = 0;
        result.responseData["voltage"] = 0.0;
        result.responseData["percent"] = 0.0;
        result.responseData["timestamp"] = String(millis());
    }
    
    return result;
}

CommandResult CommandHandler::handleDACRead(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC read command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    uint16_t value = mcp4725_->readValue();
    
    if (value == 0xFFFF) {
        return CommandResult(false, "Failed to read DAC value");
    }
    
    CommandResult result(true, "DAC value read successfully");
    result.responseData["value"] = value;
    result.responseData["voltage"] = mcp4725_->getCurrentVoltage();
    result.responseData["percent"] = mcp4725_->getCurrentPercent();
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handleDACStatus(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing DAC status command");
    }

    if (!mcp4725_) {
        return CommandResult(false, "MCP4725 DAC not available");
    }

    bool available = mcp4725_->isAvailable();
    
    CommandResult result(true, "DAC status retrieved");
    result.responseData["available"] = available;
    result.responseData["initialized"] = available;
    
    if (available) {
        uint16_t dacValue, eepromValue;
        MCP4725::PowerDownMode powerDown;
        
        if (mcp4725_->readSettings(dacValue, eepromValue, powerDown)) {
            result.responseData["dac_value"] = dacValue;
            result.responseData["eeprom_value"] = eepromValue;
            result.responseData["power_down_mode"] = (int)powerDown;
            result.responseData["voltage"] = mcp4725_->getCurrentVoltage();
            result.responseData["percent"] = mcp4725_->getCurrentPercent();
        }
    }
    
    result.responseData["status"] = mcp4725_->getStatus();
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

// ============================================================================
// PWM COMMANDS
// ============================================================================

void CommandHandler::registerPWMCommands() {
    // Register built-in PWM commands (target: actuator, command: pwm_*)
    registerCommand(CommandTarget::ACTUATOR, "pwm_set_value", 
        [this](const JsonDocument& params) { return handlePWMSetValue(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "pwm_set_percent", 
        [this](const JsonDocument& params) { return handlePWMSetPercent(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "pwm_set_voltage", 
        [this](const JsonDocument& params) { return handlePWMSetVoltage(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "pwm_off", 
        [this](const JsonDocument& params) { return handlePWMOff(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "pwm_read", 
        [this](const JsonDocument& params) { return handlePWMRead(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "pwm_status", 
        [this](const JsonDocument& params) { return handlePWMStatus(params); });
}

CommandResult CommandHandler::handlePWMSetValue(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM set_value command");
    }

    // Extract type parameter (default: "io2")
    String type = "io2";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    // Select PWM Controller based on type
    PWMController* controller = nullptr;
    String typeLabel = "";
    
    if (type == "mosfet" || type == "gpio36") {
        controller = pwmControllerMOSFET_;
        typeLabel = "MOSFET";
    } else {
        controller = pwmController_;
        typeLabel = "IO2";
    }

    if (!controller) {
        return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
    }

    // Execute command directly
    bool success = controller->handleCommand("set_value", params["parameters"]);
    
    CommandResult result(success, success ? 
        "PWM (" + typeLabel + ") value set to " + String(controller->getCurrentValue()) : 
        "Failed to set PWM (" + typeLabel + ") value");
    
    if (success) {
        result.responseData["type"] = type;
        result.responseData["value"] = controller->getCurrentValue();
        result.responseData["percent"] = controller->getCurrentPercent();
        result.responseData["timer_active"] = controller->isTimerActive();
        if (controller->isTimerActive()) {
            result.responseData["timer_remaining_ms"] = controller->getRemainingMillis();
        }
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_value", "actuator", "", "pwm_" + type);
        }
    }
    
    return result;
}

CommandResult CommandHandler::handlePWMSetPercent(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM set_percent command");
    }

    // Extract type parameter (default: "io2")
    String type = "io2";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    // Select PWM Controller based on type
    PWMController* controller = nullptr;
    String typeLabel = "";
    
    if (type == "mosfet" || type == "gpio36") {
        controller = pwmControllerMOSFET_;
        typeLabel = "MOSFET";
    } else {
        controller = pwmController_;
        typeLabel = "IO2";
    }

    if (!controller) {
        return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
    }

    bool success = controller->handleCommand("set_percent", params["parameters"]);
    
    CommandResult result(success, success ? 
        "PWM (" + typeLabel + ") set to " + String(controller->getCurrentPercent(), 1) + "%" : 
        "Failed to set PWM (" + typeLabel + ") percent");
    
    if (success) {
        result.responseData["type"] = type;
        result.responseData["value"] = controller->getCurrentValue();
        result.responseData["percent"] = controller->getCurrentPercent();
        result.responseData["timer_active"] = controller->isTimerActive();
        if (controller->isTimerActive()) {
            result.responseData["timer_remaining_ms"] = controller->getRemainingMillis();
        }
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_percent", "actuator", "", "pwm_" + type);
        }
    }
    
    return result;
}

CommandResult CommandHandler::handlePWMSetVoltage(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM set_voltage command");
    }

    // Extract type parameter (default: "io2")
    String type = "io2";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    // Select PWM Controller based on type
    PWMController* controller = nullptr;
    String typeLabel = "";
    
    if (type == "mosfet" || type == "gpio36") {
        controller = pwmControllerMOSFET_;
        typeLabel = "MOSFET";
    } else {
        controller = pwmController_;
        typeLabel = "IO2";
    }

    if (!controller) {
        return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
    }

    bool success = controller->handleCommand("set_voltage", params["parameters"]);
    
    CommandResult result(success, success ? 
        "PWM (" + typeLabel + ") set to " + String(controller->getCurrentVoltage(), 2) + "V" : 
        "Failed to set PWM (" + typeLabel + ") voltage");
    
    if (success) {
        result.responseData["type"] = type;
        result.responseData["value"] = controller->getCurrentValue();
        result.responseData["percent"] = controller->getCurrentPercent();
        result.responseData["voltage"] = controller->getCurrentVoltage();
        result.responseData["timer_active"] = controller->isTimerActive();
        if (controller->isTimerActive()) {
            result.responseData["timer_remaining_ms"] = controller->getRemainingMillis();
        }
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("set_voltage", "actuator", "", "pwm_" + type);
        }
    }
    
    return result;
}

CommandResult CommandHandler::handlePWMOff(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM off command");
    }

    // Extract type parameter (default: "io2")
    String type = "io2";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    // Select PWM Controller based on type
    PWMController* controller = nullptr;
    String typeLabel = "";
    
    if (type == "mosfet" || type == "gpio36") {
        controller = pwmControllerMOSFET_;
        typeLabel = "MOSFET";
    } else {
        controller = pwmController_;
        typeLabel = "IO2";
    }

    if (!controller) {
        return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
    }

    bool success = controller->handleCommand("off", params["parameters"]);
    
    CommandResult result(success, success ? 
        "PWM (" + typeLabel + ") turned off" : 
        "Failed to turn off PWM (" + typeLabel + ")");
    
    if (success) {
        result.responseData["type"] = type;
        result.responseData["value"] = 0;
        result.responseData["percent"] = 0.0;
        result.responseData["timestamp"] = String(millis());
        
        // Publish actuator status to MQTT
        if (actuatorStatusPublisher_) {
            actuatorStatusPublisher_->publishStatus("off", "actuator", "", "pwm_" + type);
        }
    }
    
    return result;
}

CommandResult CommandHandler::handlePWMRead(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM read command");
    }

    // Extract type parameter (default: "io2")
    String type = "io2";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    // Select PWM Controller based on type
    PWMController* controller = nullptr;
    String typeLabel = "";
    
    if (type == "mosfet" || type == "gpio36") {
        controller = pwmControllerMOSFET_;
        typeLabel = "MOSFET";
    } else {
        controller = pwmController_;
        typeLabel = "IO2";
    }

    if (!controller) {
        return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
    }

    uint16_t value = controller->getCurrentValue();
    
    CommandResult result(true, "PWM (" + typeLabel + ") value read successfully");
    result.responseData["type"] = type;
    result.responseData["value"] = value;
    result.responseData["percent"] = controller->getCurrentPercent();
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handlePWMStatus(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing PWM status command");
    }

    // Extract type parameter (default: "io2" - or return status for both if not specified)
    String type = "";
    if (params["parameters"].containsKey("type")) {
        type = params["parameters"]["type"].as<String>();
        type.toLowerCase();
    }

    CommandResult result(true, "PWM status retrieved");
    
    // If no type specified, return status for both controllers
    if (type.isEmpty() || type == "all") {
        // IO2 Status
        if (pwmController_) {
            DynamicJsonDocument statusDoc(512);
            pwmController_->getStatus(statusDoc);
            
            result.responseData["io2"]["value"] = statusDoc["value"];
            result.responseData["io2"]["percent"] = statusDoc["percent"];
            result.responseData["io2"]["voltage"] = statusDoc["voltage"];
            result.responseData["io2"]["max_value"] = statusDoc["max_value"];
            result.responseData["io2"]["pin"] = statusDoc["pin"];
            result.responseData["io2"]["channel"] = statusDoc["channel"];
            result.responseData["io2"]["frequency"] = statusDoc["frequency"];
            result.responseData["io2"]["resolution"] = statusDoc["resolution"];
            result.responseData["io2"]["last_command"] = statusDoc["last_command"].as<String>();
            result.responseData["io2"]["initialized"] = statusDoc["initialized"];
        } else {
            result.responseData["io2"]["initialized"] = false;
        }
        
        // MOSFET Status
        if (pwmControllerMOSFET_) {
            DynamicJsonDocument statusDoc(512);
            pwmControllerMOSFET_->getStatus(statusDoc);
            
            result.responseData["mosfet"]["value"] = statusDoc["value"];
            result.responseData["mosfet"]["percent"] = statusDoc["percent"];
            result.responseData["mosfet"]["voltage"] = statusDoc["voltage"];
            result.responseData["mosfet"]["max_value"] = statusDoc["max_value"];
            result.responseData["mosfet"]["pin"] = statusDoc["pin"];
            result.responseData["mosfet"]["channel"] = statusDoc["channel"];
            result.responseData["mosfet"]["frequency"] = statusDoc["frequency"];
            result.responseData["mosfet"]["resolution"] = statusDoc["resolution"];
            result.responseData["mosfet"]["last_command"] = statusDoc["last_command"].as<String>();
            result.responseData["mosfet"]["initialized"] = statusDoc["initialized"];
        } else {
            result.responseData["mosfet"]["initialized"] = false;
        }
    } else {
        // Single controller status
        PWMController* controller = nullptr;
        String typeLabel = "";
        
        if (type == "mosfet" || type == "gpio36") {
            controller = pwmControllerMOSFET_;
            typeLabel = "MOSFET";
        } else {
            controller = pwmController_;
            typeLabel = "IO2";
        }

        if (!controller) {
            return CommandResult(false, "PWM Controller (" + typeLabel + ") not available");
        }
        
        DynamicJsonDocument statusDoc(512);
        controller->getStatus(statusDoc);
        
        result.responseData["type"] = type;
        result.responseData["value"] = statusDoc["value"];
        result.responseData["percent"] = statusDoc["percent"];
        result.responseData["voltage"] = statusDoc["voltage"];
        result.responseData["max_value"] = statusDoc["max_value"];
        result.responseData["pin"] = statusDoc["pin"];
        result.responseData["channel"] = statusDoc["channel"];
        result.responseData["frequency"] = statusDoc["frequency"];
        result.responseData["resolution"] = statusDoc["resolution"];
        result.responseData["last_command"] = statusDoc["last_command"].as<String>();
        result.responseData["initialized"] = statusDoc["initialized"];
    }
    
    result.responseData["timestamp"] = String(millis());
    
    return result;
}

CommandResult CommandHandler::handleSystemSetTimezone(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing set_timezone command");
    }

    if (!scheduleManager_) {
        return CommandResult(false, "Schedule manager not available");
    }

    // Extract timezone parameter
    if (!params.containsKey("parameters") || !params["parameters"].containsKey("timezone")) {
        return CommandResult(false, "Missing required parameter: timezone (seconds offset)");
    }

    int32_t timezone = params["parameters"]["timezone"].as<int32_t>();
    
    // Validate timezone range (-43200 to +43200 = UTC-12 to UTC+12)
    if (timezone < -43200 || timezone > 43200) {
        return CommandResult(false, "Invalid timezone offset. Must be between -43200 and +43200 seconds");
    }

    // Set timezone
    bool success = scheduleManager_->setTimezone(timezone);
    
    CommandResult result(success, success ? "Timezone set successfully" : "Failed to set timezone");
    
    if (success) {
        result.responseData["timezone"] = timezone;
        result.responseData["local_time"] = scheduleManager_->getLocalTime();
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            logger_->info("Command", "Timezone set to: " + String(timezone) + " seconds");
        }
    }
    
    return result;
}

void CommandHandler::registerScheduleCommands() {
    // Register schedule commands under actuator target
    registerCommand(CommandTarget::ACTUATOR, "set_schedule", 
        [this](const JsonDocument& params) { return handleScheduleSet(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "clear_schedule", 
        [this](const JsonDocument& params) { return handleScheduleClear(params); });
    
    registerCommand(CommandTarget::ACTUATOR, "get_schedule", 
        [this](const JsonDocument& params) { return handleScheduleGet(params); });
}

CommandResult CommandHandler::handleScheduleSet(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing set_schedule command");
    }

    if (!scheduleManager_) {
        return CommandResult(false, "Schedule manager not available");
    }

    // Extract required parameters
    if (!params.containsKey("parameters")) {
        return CommandResult(false, "Missing parameters object");
    }

    JsonVariantConst parametersVar = params["parameters"];
    
    if (!parametersVar.containsKey("type") || !parametersVar.containsKey("on_at") || 
        !parametersVar.containsKey("off_at") || !parametersVar.containsKey("valid_days") || 
        !parametersVar.containsKey("timezone")) {
        return CommandResult(false, "Missing required parameters: type, on_at, off_at, valid_days, timezone");
    }

    String type = parametersVar["type"].as<String>();
    String onAt = parametersVar["on_at"].as<String>();
    String offAt = parametersVar["off_at"].as<String>();
    uint16_t validDays = parametersVar["valid_days"].as<uint16_t>();
    int32_t timezone = parametersVar["timezone"].as<int32_t>();
    
    bool success = false;
    
    // DAC Schedule types
    if (type == "dac_voltage" || type == "dac_percent" || type == "dac_value") {
        // Alle DAC-Modi verwenden on_value/off_value Parameter
        if (!parametersVar.containsKey("on_value") || !parametersVar.containsKey("off_value")) {
            return CommandResult(false, "DAC schedule requires on_value and off_value parameters");
        }
        
        float onValue = parametersVar["on_value"].as<float>();
        float offValue = parametersVar["off_value"].as<float>();
        uint32_t rampSeconds = parametersVar.containsKey("ramp_seconds") ? 
                               parametersVar["ramp_seconds"].as<uint32_t>() : 0;
        
        success = scheduleManager_->setDACSchedule(type, onAt, offAt, onValue, offValue,
                                                   rampSeconds, validDays, timezone);
    }
    // MOSFET/Relay Schedule (existing logic)
    else if (type == "mosfet" || type == "relay") {
        success = scheduleManager_->setSchedule(type, onAt, offAt, validDays, timezone);
    }
    else {
        return CommandResult(false, "Invalid type. Must be 'mosfet', 'relay', 'dac_voltage', 'dac_percent', or 'dac_value'");
    }
    
    CommandResult result(success, success ? "Schedule set successfully" : "Failed to set schedule");
    
    if (success) {
        // Build detailed response
        auto schedule = scheduleManager_->getSchedule();
        
        result.responseData["schedule"]["active"] = schedule.active;
        result.responseData["schedule"]["on_at"] = schedule.onAt;
        result.responseData["schedule"]["off_at"] = schedule.offAt;
        result.responseData["schedule"]["valid_days"] = schedule.validDays;
        result.responseData["schedule"]["start_date"] = scheduleManager_->getStartDate();
        
        if (schedule.validDays > 0) {
            result.responseData["schedule"]["expires_on"] = scheduleManager_->getExpiresOn();
            result.responseData["schedule"]["days_remaining"] = scheduleManager_->getDaysRemaining();
        } else {
            result.responseData["schedule"]["expires_on"] = "unlimited";
            result.responseData["schedule"]["days_remaining"] = 0;
        }
        
        result.responseData["schedule"]["next_action"] = scheduleManager_->getNextAction();
        result.responseData["schedule"]["next_action_at"] = scheduleManager_->getNextActionTime();
        result.responseData["schedule"]["actuator_type"] = type;
        
        // DAC-spezifische Parameter hinzufügen (einheitlich für alle Modi)
        if (type == "dac_voltage" || type == "dac_percent" || type == "dac_value") {
            Serial.println("[DEBUG set_schedule] onValue=" + String(schedule.onValue, 2) + 
                          " offValue=" + String(schedule.offValue, 2) + 
                          " rampSeconds=" + String(schedule.rampSeconds));
            result.responseData["schedule"]["on_value"] = schedule.onValue;
            result.responseData["schedule"]["off_value"] = schedule.offValue;
            result.responseData["schedule"]["ramp_seconds"] = schedule.rampSeconds;
        }
        
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            logger_->info("Command", "Schedule set: " + type + " " + onAt + "-" + offAt);
        }
        
        // Mutex-Logik: Wenn Timer/manuelles Command aktiv ist, pausiere Schedule
        if (type == "mosfet" || type == "relay") {
            if (actuator_) {
                Actuator::Type actType = (type == "mosfet") ? Actuator::Type::MOSFET : Actuator::Type::RELAY;
                if (actuator_->isTimerActive(actType)) {
                    if (logger_) {
                        logger_->info("Command", "Timer is active, schedule will pause until timer finishes");
                    }
                    scheduleManager_->pauseSchedule();
                }
            }
        }
        // DAC schedules: paused wenn manuelle DAC-Befehle aktiv (wird später implementiert)
    }
    
    return result;
}

CommandResult CommandHandler::handleScheduleClear(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing clear_schedule command");
    }

    if (!scheduleManager_) {
        return CommandResult(false, "Schedule manager not available");
    }

    // Check if schedule is active
    if (!scheduleManager_->isScheduleActive() && !scheduleManager_->getSchedule().active) {
        if (logger_) {
            logger_->info("Command", "No active schedule to clear");
        }
        
        CommandResult result(true, "No active schedule");
        result.responseData["message"] = "Schedule already cleared";
        result.responseData["timestamp"] = String(millis());
        return result;
    }

    // Clear schedule (turnOffActuator = true by default)
    bool success = scheduleManager_->clearSchedule(true);
    
    CommandResult result(success, success ? "Schedule cleared successfully" : "Failed to clear schedule");
    
    if (success) {
        result.responseData["message"] = "Schedule cleared";
        result.responseData["timestamp"] = String(millis());
        
        if (logger_) {
            logger_->info("Command", "Schedule cleared");
        }
    }
    
    return result;
}

CommandResult CommandHandler::handleScheduleGet(const JsonDocument& params) {
    if (logger_) {
        logger_->info("Command", "Executing get_schedule command");
    }

    if (!scheduleManager_) {
        return CommandResult(false, "Schedule manager not available");
    }

    auto schedule = scheduleManager_->getSchedule();
    
    // DEBUG: Print actuatorType
    Serial.println("[DEBUG handleScheduleGet] actuatorType: '" + schedule.actuatorType + 
                   "' length: " + String(schedule.actuatorType.length()) +
                   " active: " + String(schedule.active ? "true" : "false"));
    
    CommandResult result(true, "Schedule information retrieved");
    
    // Schedule data
    result.responseData["schedule"]["active"] = schedule.active;
    result.responseData["schedule"]["on_at"] = schedule.active ? schedule.onAt : "";
    result.responseData["schedule"]["off_at"] = schedule.active ? schedule.offAt : "";
    result.responseData["schedule"]["valid_days"] = schedule.active ? schedule.validDays : 0;
    result.responseData["schedule"]["start_date"] = schedule.active ? scheduleManager_->getStartDate() : "";
    
    if (schedule.active && schedule.validDays > 0) {
        result.responseData["schedule"]["expires_on"] = scheduleManager_->getExpiresOn();
        result.responseData["schedule"]["days_remaining"] = scheduleManager_->getDaysRemaining();
    } else {
        result.responseData["schedule"]["expires_on"] = schedule.active ? "unlimited" : "";
        result.responseData["schedule"]["days_remaining"] = 0;
    }
    
    result.responseData["schedule"]["next_action"] = schedule.active ? scheduleManager_->getNextAction() : "";
    result.responseData["schedule"]["next_action_at"] = schedule.active ? scheduleManager_->getNextActionTime() : "";
    result.responseData["schedule"]["expired"] = schedule.active ? scheduleManager_->isExpired() : false;
    result.responseData["schedule"]["paused"] = schedule.active ? scheduleManager_->isPaused() : false;
    
    if (schedule.active) {
        // Copy actuatorType to avoid String reference issues
        String actuatorType = schedule.actuatorType;
        Serial.println("[DEBUG get_schedule] actuatorType='" + actuatorType + 
                      "' onValue=" + String(schedule.onValue, 2) + 
                      " offValue=" + String(schedule.offValue, 2) + 
                      " rampSeconds=" + String(schedule.rampSeconds));
        result.responseData["schedule"]["actuator_type"] = actuatorType;
        
        // DAC-spezifische Parameter hinzufügen (einheitlich für alle Modi)
        if (schedule.actuatorType == "dac_voltage" || 
            schedule.actuatorType == "dac_percent" || 
            schedule.actuatorType == "dac_value") {
            result.responseData["schedule"]["on_value"] = schedule.onValue;
            result.responseData["schedule"]["off_value"] = schedule.offValue;
            result.responseData["schedule"]["ramp_seconds"] = schedule.rampSeconds;
        }
    }
    
    // Time data
    result.responseData["time"]["synced"] = scheduleManager_->isTimeSynced();
    result.responseData["time"]["timezone"] = scheduleManager_->getTimezone();
    result.responseData["time"]["local_time"] = scheduleManager_->getLocalTime();
    
    result.responseData["timestamp"] = String(millis());
    
    return result;
}
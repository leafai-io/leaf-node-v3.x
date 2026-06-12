#pragma once

#include <ArduinoJson.h>
#include "../LeafNodeTypes.h"

/**
 * @brief Runtime Configuration Manager
 * 
 * Manages device-specific runtime settings stored in NVS (Non-Volatile Storage).
 * Handles loading, saving, and validating persistent configuration data like
 * WiFi credentials, MQTT settings, and device-specific parameters.
 * 
 * Note: Compile-time constants are defined in /config.h
 */
class RuntimeConfig {
public:
    RuntimeConfig();
    ~RuntimeConfig();

    /**
     * @brief Load configuration from persistent storage
     * @return true if configuration was loaded successfully
     */
    bool load();

    /**
     * @brief Save current configuration to persistent storage
     * @return true if configuration was saved successfully
     */
    bool save();

    /**
     * @brief Reset configuration to factory defaults
     */
    void resetToDefaults();

    /**
     * @brief Validate current configuration
     * @return true if configuration is valid
     */
    bool validate() const;

    /**
     * @brief Get configuration as JSON document
     * @return Reference to the JSON document
     */
    const DynamicJsonDocument& getJson() const { return configDoc_; }

    /**
     * @brief Set configuration from JSON document
     * @param json JSON document containing configuration
     * @return true if configuration was set successfully
     */
    bool setFromJson(const DynamicJsonDocument& json);

    // Configuration getters
    String getDeviceName() const;
    String getSerialNumber() const;
    String getWiFiSSID() const;
    String getWiFiPassword() const;
    String getBLEKey() const;
    LogLevel getLogLevel() const;
    uint32_t getHeartbeatInterval() const;
    bool isDebugMode() const;
    bool hasWiFiCredentials() const;
    bool isWiFiAutoConnect() const;
    
    /**
     * @brief Check if device is in factory mode (no configuration set)
     * @return true if device has no configuration and needs factory setup
     */
    bool isFactoryMode() const;
    
    /**
     * @brief Check if a valid sensor configuration is set (including NONE)
     * @return true if sensor profile is configured (NONE is also valid)
     */
    bool hasValidSensorConfiguration() const;

    // MQTT configuration getters
    String getMQTTServer() const;
    int getMQTTPort() const;
    String getMQTTUsername() const;
    String getMQTTPassword() const;
    String getMQTTClientId() const;
    bool hasMQTTCredentials() const;
    bool isMQTTAutoConnect() const;

    // Configuration setters
    void setDeviceName(const String& name);
    void setSerialNumber(const String& serialNumber);
    void setWiFiCredentials(const String& ssid, const String& password);
    void setWiFiSSID(const String& ssid);
    void setWiFiPassword(const String& password);
    void setWiFiAutoConnect(bool autoConnect);
    void setBLEKey(const String& bleKey);
    void setLogLevel(LogLevel level);
    void setHeartbeatInterval(uint32_t interval);
    void setDebugMode(bool enabled);
    void resetWiFiCredentials(); // Reset WiFi settings

    // MQTT configuration setters
    void setMQTTCredentials(const String& server, int port, const String& username, const String& password);
    void setMQTTServer(const String& server);
    void setMQTTPort(int port);
    void setMQTTUsername(const String& username);
    void setMQTTPassword(const String& password);
    void setMQTTClientId(const String& clientId);
    void setMQTTAutoConnect(bool autoConnect);
    void resetMQTTCredentials(); // Reset MQTT settings
    
    // Sensor configuration getters
    SensorProfile getSensorProfile() const;
    String getSensorName() const;
    uint32_t getSensorReadingInterval() const;
    bool hasSensorConfiguration() const;
    
    // Sensor configuration setters
    void setSensorProfile(SensorProfile profile);
    void setSensorName(const String& name);
    void setSensorReadingInterval(uint32_t interval);
    void resetSensorConfiguration(); // Reset sensor settings
    
    // Device setup status
    bool isDeviceSetup() const;
    void setDeviceSetup(bool setup);
    void resetDeviceSetup(); // Reset setup status to false
    
    // UART Chain Position
    uint8_t getChainPosition() const;
    void setChainPosition(uint8_t position);
    bool hasChainPosition() const;
    
    // Setup timeout methods
    void startSetupTimeout();
    bool isSetupTimeoutExpired() const;
    void cancelSetupTimeout();
    bool isInSetupMode() const;
    void resetToSetupMode();
    
    // MQTT Topic helpers - construct topics using serial number
    String getMQTTTopicRegister() const;
    String getMQTTTopicRegistrationAck() const;
    String getMQTTTopicStatus() const;
    String getMQTTTopicHeartbeat() const;
    String getMQTTTopicCommand() const;
    String getMQTTTopicCommandResponse() const;
    String getMQTTTopicCommands() const;  // Legacy
    String getSensorTopicData() const;
    
    // Schedule configuration getters
    bool getScheduleActive() const;
    String getScheduleActuatorType() const;
    String getScheduleOnAt() const;
    String getScheduleOffAt() const;
    uint16_t getScheduleValidDays() const;
    time_t getScheduleStartTime() const;
    
    // Schedule configuration setters
    void setScheduleActive(bool active);
    void setScheduleActuatorType(const String& type);
    void setScheduleOnAt(const String& onAt);
    void setScheduleOffAt(const String& offAt);
    void setScheduleValidDays(uint16_t days);
    void setScheduleStartTime(time_t startTime);
    
    // DAC Schedule configuration getters
    float getScheduleOnValue() const;
    float getScheduleOffValue() const;
    uint32_t getScheduleRampSeconds() const;
    uint16_t getLastDACValue() const;
    
    // DAC Schedule configuration setters
    void setScheduleOnValue(float value);
    void setScheduleOffValue(float value);
    void setScheduleRampSeconds(uint32_t seconds);
    void setLastDACValue(uint16_t value);
    
    // Timezone configuration
    int32_t getTimezone() const;
    void setTimezone(int32_t timezone);
    
    // Actuator state persistence (for power-loss recovery)
    bool getMosfetState() const;
    bool getRelayState() const;
    void setMosfetState(bool state);
    void setRelayState(bool state);

private:
    DynamicJsonDocument configDoc_;
    bool loaded_;
    bool dirty_;
    
    // Setup timeout management
    unsigned long setupStartTime_;
    bool setupTimeoutActive_;
    static const unsigned long SETUP_TIMEOUT_MS = 30000; // 30 seconds

    void setDefaults();
    bool loadFromPreferences();
    bool saveToPreferences();
    bool loadSerialNumberFromFactory();
    String generateDefaultDeviceName() const;
    
    // Helper methods for sensor profile conversion
    String sensorProfileToString(SensorProfile profile) const;
    SensorProfile stringToSensorProfile(const String& profileStr) const;
};

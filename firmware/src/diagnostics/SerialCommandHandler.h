#pragma once

#include <Arduino.h>
#include "../runtime/RuntimeConfig.h"
#include "Logger.h"

// Forward declaration
class LeafNode;

/**
 * @brief Serial Command Handler for Development/Debug Mode
 * 
 * Provides interactive serial commands for testing and debugging.
 * Only active when NOT in PRODUCTION_MODE.
 * 
 * Available Commands:
 *   help       - Show all available commands
 *   status     - Display device status
 *   reset      - Reset configuration to factory defaults
 *   reboot     - Reboot the device
 *   wifi       - Show WiFi configuration
 *   mqtt       - Show MQTT configuration
 *   serial     - Show serial number
 *   resetmqtt  - Reset only MQTT credentials
 *   resetwifi  - Reset only WiFi credentials
 *   setmqtt    - Configure MQTT server interactively
 *   setwifi    - Configure WiFi credentials interactively
 *   mockack    - Simulate MQTT registration ACK (skip 30s timeout)
 *   led        - Show LED status
 *   ledtest    - Test LED with different colors
 *   ledon      - Turn LED on (white)
 *   ledoff     - Turn LED off
 *   ledred     - Set LED to red
 *   ledgreen   - Set LED to green
 *   ledblue    - Set LED to blue
 *   i2cscan    - Scan I2C bus for connected devices
 *   factory    - Show factory configuration menu (Factory Mode only)
 *   setserial  - Set device serial number (Factory Mode only)
 *   setsensor  - Set sensor profile (Factory Mode only, usage: setsensor or setsensor <NAME>)
 */
class SerialCommandHandler {
public:
    /**
     * @brief Constructor
     * @param config Runtime configuration instance
     * @param logger Logger instance
     * @param leafNode LeafNode instance for registration ACK simulation
     */
    SerialCommandHandler(RuntimeConfig* config, Logger* logger, LeafNode* leafNode = nullptr);
    
    /**
     * @brief Check for and process serial input
     * Call this regularly in the main loop
     */
    void update();
    
    /**
     * @brief Enable or disable command handler
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
    /**
     * @brief Check if handler is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return enabled_; }
    
    /**
     * @brief Enable or disable factory mode
     * @param factoryMode true for factory mode (enables setserial, setsensor commands)
     */
    void setFactoryMode(bool factoryMode) { factoryMode_ = factoryMode; }
    
    /**
     * @brief Check if in factory mode
     * @return true if in factory mode
     */
    bool isFactoryMode() const { return factoryMode_; }

private:
    RuntimeConfig* config_;
    Logger* logger_;
    LeafNode* leafNode_;
    String inputBuffer_;
    bool enabled_;
    bool factoryMode_;
    bool resetConfirmPending_;
    bool mqttConfigPending_;
    int mqttConfigStep_;
    String mqttServer_;
    int mqttPort_;
    String mqttUsername_;
    String mqttPassword_;
    bool wifiConfigPending_;
    int wifiConfigStep_;
    String wifiSSID_;
    String wifiPassword_;
    bool sensorConfigPending_;
    int sensorConfigStep_;
    bool serialConfigPending_;
    
    // Command handlers
    void handleCommand(const String& command);
    void showHelp();
    void showStatus();
    void resetConfig();
    void resetMQTTConfig();
    void resetWiFiConfig();
    void setMQTTConfig();
    void setWiFiConfig();
    void mockRegistrationAck();
    void unregisterDevice();
    void handleMQTTConfigInput(const String& input);
    void handleWiFiConfigInput(const String& input);
    void rebootDevice();
    void showWiFiConfig();
    void showMQTTConfig();
    void showSerialNumber();
    
    // Factory Mode commands
    void showFactoryMenu();
    void setSerialNumber();
    void setSensorProfile(const String& sensorName = "");
    void handleSerialConfigInput(const String& input);
    void handleSensorConfigInput(const String& input);
    
    // Helper function to map sensor name to profile
    bool mapSensorNameToProfile(const String& name, SensorProfile& profile, String& sensorName, uint32_t& interval);
    
    // LED test commands
    void showLEDStatus();
    void testLED();
    void setLEDOn();
    void setLEDOff();
    void setLEDRed();
    void setLEDGreen();
    void setLEDBlue();
    
    // I2C commands
    void scanI2CBus();
    
    // NVS diagnostic commands
    void showNVSData();
    
    // Helper methods
    void printSeparator();
    void printPrompt();
};

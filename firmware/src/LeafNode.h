#pragma once

#include <Arduino.h>
#include "runtime/RuntimeConfig.h"
#include "system/SystemManager.h"
#include "core/TaskManager.h"
#include "core/CommandHandler.h"
#include "core/SetupManager.h"
#include "diagnostics/Logger.h"
#include "diagnostics/SerialCommandHandler.h"
#include "network/WiFiManager.h"
#include "network/BLEConfigManager.h"
#include "network/MQTTManager.h"
#include "network/WebServerManager.h"
#include "network/OTAManager.h"
#include "hardware/StatusLED.h"
#include "hardware/RS485Manager.h"
#include "hardware/SDI12Manager.h"
#include "hardware/OneWireManager.h"
#include "hardware/Actuator.h"
#include "sensors/SensorManager.h"
#include "network/ActuatorStatusPublisher.h"
#include "system/ScheduleManager.h"

/**
 * @brief Main LeafNode class - Central coordinator for the entire system
 * 
 * This class acts as the main orchestrator for the LeafNode firmware,
 * managing initialization, configuration, and coordinating between
 * different system components.
 */
class LeafNode {
public:
    LeafNode();
    ~LeafNode();

    /**
     * @brief Initialize the LeafNode system
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Main update loop - should be called from Arduino loop()
     */
    void update();

    /**
     * @brief Get system status
     * @return Current system status
     */
    SystemStatus getSystemStatus() const;

    /**
     * @brief Get the temporarily stored user ID from BLE configuration
     * @return User ID string, empty if not set
     */
    String getTemporaryUserId() const;

    /**
     * @brief Clear the temporarily stored user ID
     */
    void clearTemporaryUserId();

    /**
     * @brief Reset the system to factory defaults
     */
    void factoryReset();

    /**
     * @brief Publish device registration to MQTT (initial setup)
     * @return true if registration was sent successfully
     */
    bool publishDeviceRegistration();

    /**
     * @brief Publish heartbeat to MQTT
     * @return true if heartbeat was sent successfully
     */
    bool publishHeartbeat();

    /**
     * @brief Get configuration instance (for testing/debugging)
     * @return Pointer to configuration instance
     */
    RuntimeConfig* getConfig() const { return config_; }
    
    /**
     * @brief Get status LED instance (for testing/debugging)
     * @return Pointer to status LED instance
     */
    StatusLED* getStatusLED() const { return statusLED_; }
    
    /**
     * @brief Simulate registration acknowledgment for testing
     * @param payload Mock registration acknowledgment payload
     */
    void simulateRegistrationAck(const String& payload);
    
    /**
     * @brief Get RS485 manager instance
     * @return Pointer to RS485 manager instance
     */
    RS485Manager* getRS485Manager() const { return rs485Manager_; }

private:
    // Core components
    RuntimeConfig* config_;
    SystemManager* systemManager_;
    TaskManager* taskManager_;
    CommandHandler* commandHandler_;
    SetupManager* setupManager_;
    Logger* logger_;
    SerialCommandHandler* serialCommandHandler_;
    
    // Network components
    WiFiManager* wifiManager_;
    BLEConfigManager* bleConfigManager_;
    MQTTManager* mqttManager_;
    OTAManager* otaManager_;
    StatusLED* statusLED_;
    
    // Hardware components
    RS485Manager* rs485Manager_;
    SDI12Manager* sdi12Manager_;
    OneWireManager* oneWireManager_;
    SensorManager* sensorManager_;
    class Actuator* actuator_;
    class ActuatorStatusPublisher* actuatorStatusPublisher_;
    class ScheduleManager* scheduleManager_;
    
    // State
    bool initialized_;
    bool factoryMode_;
    bool configuredViaUART_; // Flag to indicate config was received via UART chain
    unsigned long lastHeartbeat_;
    unsigned long lastSensorReading_;
    NetworkStatus networkStatus_;
    String temporaryUserId_; // Temporarily store user ID from BLE
    unsigned long registrationRetryTime_; // Time for next registration retry
    
    // Registration state management
    bool registrationActive_;
    unsigned long registrationRetryInterval_;
    unsigned long lastRegistrationAttempt_;
    bool registrationAckReceived_;
    
    // WiFi reconnection state management
    unsigned long lastWiFiReconnectAttempt_;
    unsigned long wifiReconnectInterval_;
    bool wifiReconnectionEnabled_;
    
    // Static instance for LED callback
    static LeafNode* instance_;
    
    // Static callback for DAC LED update
    static void dacLEDUpdateCallback();
    
    // Private methods
    void setupLogging();
    void printStartupInfo();
    bool validateConfiguration();
    bool initializeFactoryMode();
    bool initializeNetwork();
    void startNetworkConnection();
    void startBLEConfigMode();
    void onWiFiConnected();
    void onWiFiCredentialsReceived(const String& ssid, const String& password, const String& userId);
    void onFactoryResetRequested();
    void checkInitialDeviceSetup(); // Check and handle initial device setup
    void handleMQTTMessage(const String& topic, const String& payload); // Handle incoming MQTT messages
    void handleRegistrationAck(const String& payload); // Handle registration acknowledgment
    void handleCommandMessage(const String& topic, const String& payload); // Handle command messages
    void handleRegistrationRetry(); // Handle periodic registration retries
    void handleSensorReading(); // Handle periodic sensor readings
    void handleWiFiReconnection(); // Handle periodic WiFi reconnection attempts
    
    // MQTT methods
    bool initializeMQTT();
    void connectMQTT();
    void onMQTTConnected();
    void onMQTTDisconnected();
    void publishUserRegistration(const String& userId);
};

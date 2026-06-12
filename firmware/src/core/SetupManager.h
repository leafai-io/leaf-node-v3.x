#pragma once

#include <Arduino.h>
#include "../runtime/RuntimeConfig.h"
#include "../network/WiFiManager.h"
#include "../network/MQTTManager.h"
#include "../network/BLEConfigManager.h"
#include "../diagnostics/Logger.h"

/**
 * @brief Setup Manager - Handles device setup flow with timeout
 * 
 * Manages the initial device setup process including:
 * - BLE configuration mode
 * - WiFi connection
 * - MQTT registration
 * - 30-second timeout with automatic reset to BLE mode
 */
class SetupManager {
public:
    enum class SetupState {
        WAITING_FOR_CONFIG,    // BLE active, waiting for WiFi credentials
        CONNECTING_WIFI,       // Attempting WiFi connection
        WAITING_FOR_ACK,       // Connected, waiting for MQTT ACK
        SETUP_COMPLETE,        // All done
        SETUP_FAILED           // Timeout or error
    };

    SetupManager(RuntimeConfig& config, WiFiManager& wifi, 
                MQTTManager& mqtt, BLEConfigManager& ble, Logger& logger);
    
    /**
     * @brief Initialize setup manager
     */
    void begin();
    
    /**
     * @brief Update setup state machine - call from main loop
     */
    void update();
    
    /**
     * @brief Get current setup state
     */
    SetupState getState() const { return state_; }
    
    /**
     * @brief Check if setup is complete
     */
    bool isSetupComplete() const { return state_ == SetupState::SETUP_COMPLETE; }
    
    /**
     * @brief Check if device is in setup mode (BLE active)
     */
    bool isInSetupMode() const;
    
    // Event callbacks - call these from LeafNode when events occur
    void onWiFiConfigReceived();
    void onWiFiConnected();
    void onMQTTConnected();
    void onRegistrationAck();
    void onRegistrationFailed();
    
    /**
     * @brief Force reset to setup mode
     */
    void forceResetToSetupMode();

private:
    RuntimeConfig& config_;
    WiFiManager& wifi_;
    MQTTManager& mqtt_;
    BLEConfigManager& ble_;
    Logger& logger_;
    
    SetupState state_;
    unsigned long stateStartTime_;
    unsigned long lastLogTime_;
    
    static const unsigned long WIFI_CONNECTION_TIMEOUT_MS = 30000;  // 30s for WiFi
    static const unsigned long LOG_INTERVAL_MS = 5000;             // Log every 5s
    
    void setState(SetupState newState);
    void handleTimeout();
    void resetToInitialState();
    void logStateProgress();
};

#include "SetupManager.h"

SetupManager::SetupManager(RuntimeConfig& config, WiFiManager& wifi, 
                          MQTTManager& mqtt, BLEConfigManager& ble, Logger& logger)
    : config_(config), wifi_(wifi), mqtt_(mqtt), ble_(ble), logger_(logger)
    , state_(SetupState::WAITING_FOR_CONFIG), stateStartTime_(0), lastLogTime_(0) {
}

void SetupManager::begin() {
    logger_.info("SetupManager", "Initializing setup manager");
    
    if (config_.isDeviceSetup() && config_.hasWiFiCredentials()) {
        logger_.info("SetupManager", "Device already configured, skipping setup");
        setState(SetupState::SETUP_COMPLETE);
    } else {
        logger_.info("SetupManager", "Starting fresh setup - activating BLE");
        setState(SetupState::WAITING_FOR_CONFIG);
    }
}

void SetupManager::update() {
    // Log progress periodically
    if (millis() - lastLogTime_ > LOG_INTERVAL_MS) {
        logStateProgress();
        lastLogTime_ = millis();
    }
    
    switch (state_) {
        case SetupState::WAITING_FOR_CONFIG:
            // BLE should be active, waiting for credentials
            if (config_.hasWiFiCredentials()) {
                logger_.info("SetupManager", "WiFi credentials received, attempting connection");
                setState(SetupState::CONNECTING_WIFI);
            }
            break;
            
        case SetupState::CONNECTING_WIFI:
            if (wifi_.isConnected()) {
                logger_.info("SetupManager", "WiFi connected, starting MQTT setup timeout");
                setState(SetupState::WAITING_FOR_ACK);
                config_.startSetupTimeout(); // Start 30s timeout for MQTT registration
            } else {
                // Check WiFi connection timeout
                if (millis() - stateStartTime_ > WIFI_CONNECTION_TIMEOUT_MS) {
                    logger_.warning("SetupManager", "WiFi connection timeout, resetting to setup mode");
                    handleTimeout();
                }
            }
            break;
            
        case SetupState::WAITING_FOR_ACK:
            // Check for setup timeout (30s for MQTT registration ACK)
            if (config_.isSetupTimeoutExpired()) {
                logger_.warning("SetupManager", "MQTT registration timeout (30s), resetting to setup mode");
                handleTimeout();
            }
            break;
            
        case SetupState::SETUP_COMPLETE:
            // Nothing to do - setup is complete
            break;
            
        case SetupState::SETUP_FAILED:
            // Reset after a delay
            if (millis() - stateStartTime_ > 5000) { // 5s delay
                logger_.info("SetupManager", "Resetting to initial setup state after failure");
                resetToInitialState();
            }
            break;
    }
}

void SetupManager::onWiFiConfigReceived() {
    if (state_ == SetupState::WAITING_FOR_CONFIG) {
        logger_.info("SetupManager", "WiFi configuration received via BLE");
        // State will change to CONNECTING_WIFI in next update() cycle
    }
}

void SetupManager::onWiFiConnected() {
    if (state_ == SetupState::CONNECTING_WIFI) {
        logger_.info("SetupManager", "WiFi connection established");
        // State change handled in update() method
    }
}

void SetupManager::onMQTTConnected() {
    if (state_ == SetupState::WAITING_FOR_ACK) {
        logger_.info("SetupManager", "MQTT connected, waiting for registration ACK");
        // Continue waiting for registration ACK - timeout is still active
    }
}

void SetupManager::onRegistrationAck() {
    logger_.info("SetupManager", "Registration ACK received - setup complete!");
    config_.cancelSetupTimeout();
    setState(SetupState::SETUP_COMPLETE);
}

void SetupManager::onRegistrationFailed() {
    if (state_ == SetupState::WAITING_FOR_ACK) {
        logger_.warning("SetupManager", "Registration failed - will retry or timeout");
        // Let the timeout handle this case
    }
}

bool SetupManager::isInSetupMode() const {
    return state_ == SetupState::WAITING_FOR_CONFIG || 
           state_ == SetupState::CONNECTING_WIFI ||
           state_ == SetupState::SETUP_FAILED;
}

void SetupManager::forceResetToSetupMode() {
    logger_.warning("SetupManager", "Forcing reset to setup mode");
    handleTimeout();
}

void SetupManager::setState(SetupState newState) {
    if (state_ != newState) {
        SetupState oldState = state_;
        state_ = newState;
        stateStartTime_ = millis();
        
        logger_.info("SetupManager", "State change: " + String(static_cast<int>(oldState)) + 
                    " -> " + String(static_cast<int>(newState)));
        
        // Handle state entry actions
        switch (newState) {
            case SetupState::WAITING_FOR_CONFIG:
                // Activate BLE for configuration
                ble_.start();
                logger_.info("SetupManager", "BLE advertising started");
                break;
                
            case SetupState::CONNECTING_WIFI:
                // WiFi connection will be handled by WiFiManager
                break;
                
            case SetupState::WAITING_FOR_ACK:
                // MQTT connection and registration handled by MQTTManager
                break;
                
            case SetupState::SETUP_COMPLETE:
                // Deactivate BLE to save power
                ble_.stop();
                logger_.info("SetupManager", "BLE advertising stopped - setup complete");
                break;
                
            case SetupState::SETUP_FAILED:
                logger_.error("SetupManager", "Setup failed - will reset shortly");
                break;
        }
    }
}

void SetupManager::handleTimeout() {
    logger_.warning("SetupManager", "Setup timeout occurred - resetting to BLE setup mode");
    setState(SetupState::SETUP_FAILED);
    resetToInitialState();
}

void SetupManager::resetToInitialState() {
    logger_.info("SetupManager", "Resetting to initial BLE setup state");
    
    // Reset ONLY WiFi credentials (keep MQTT configuration!)
    // This allows the customer to keep their MQTT settings and only
    // reconfigure WiFi if there's a connectivity issue
    config_.resetWiFiCredentials();
    config_.resetDeviceSetup(); // Reset setup status only
    config_.save(); // Save the changes
    
    // Disconnect WiFi
    wifi_.disconnect();
    
    // Disconnect MQTT (but keep credentials)
    mqtt_.disconnect();
    
    // Restart BLE advertising
    setState(SetupState::WAITING_FOR_CONFIG);
}

void SetupManager::logStateProgress() {
    unsigned long elapsedTime = millis() - stateStartTime_;
    
    switch (state_) {
        case SetupState::WAITING_FOR_CONFIG:
            logger_.info("SetupManager", "Waiting for BLE configuration... (" + 
                        String(elapsedTime / 1000) + "s)");
            break;
            
        case SetupState::CONNECTING_WIFI:
            logger_.info("SetupManager", "Connecting to WiFi... (" + 
                        String(elapsedTime / 1000) + "s/" + 
                        String(WIFI_CONNECTION_TIMEOUT_MS / 1000) + "s)");
            break;
            
        case SetupState::WAITING_FOR_ACK:
            logger_.info("SetupManager", "Waiting for MQTT registration ACK... (" + 
                        String(elapsedTime / 1000) + "s/30s)");
            break;
            
        case SetupState::SETUP_COMPLETE:
            // Don't spam logs when complete
            break;
            
        case SetupState::SETUP_FAILED:
            logger_.warning("SetupManager", "Setup failed, resetting in " + 
                           String(5 - (elapsedTime / 1000)) + "s");
            break;
    }
}

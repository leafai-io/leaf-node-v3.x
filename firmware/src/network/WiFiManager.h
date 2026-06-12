#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "../LeafNodeTypes.h"

/**
 * @brief WiFi connection manager
 * 
 * Handles WiFi connection, reconnection, and status monitoring.
 * Supports multiple retry attempts and connection validation.
 */
class WiFiManager {
public:
    WiFiManager();
    ~WiFiManager();

    /**
     * @brief Initialize WiFi manager
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Connect to WiFi with credentials
     * @param ssid WiFi network SSID
     * @param password WiFi network password
     * @param maxRetries Maximum number of connection attempts
     * @return true if connection was successful
     */
    bool connect(const String& ssid, const String& password, uint8_t maxRetries = WIFI_MAX_RETRY);

    /**
     * @brief Connect using stored credentials
     * @return true if connection was successful
     */
    bool connect();

    /**
     * @brief Disconnect from WiFi
     */
    void disconnect();

    /**
     * @brief Check if WiFi is connected
     * @return true if connected
     */
    bool isConnected() const;
    
    /**
     * @brief Set callback for WiFi connection event
     * @param callback Function to call when WiFi connects
     */
    void setOnConnectedCallback(std::function<void()> callback);

    /**
     * @brief Scan for available WiFi networks
     * @return String containing scan results in format "SSID,RSSI,encrypted|SSID2,RSSI2,encrypted2"
     */
    String scanNetworks();

    /**
     * @brief Get current network status
     * @return Current network status
     */
    NetworkStatus getStatus() const { return status_; }

    /**
     * @brief Get current IP address
     * @return IP address as string, empty if not connected
     */
    String getIPAddress() const;

    /**
     * @brief Get WiFi signal strength (RSSI)
     * @return RSSI in dBm, 0 if not connected
     */
    int32_t getRSSI() const;

    /**
     * @brief Set WiFi credentials for connection
     * @param ssid WiFi network SSID
     * @param password WiFi network password
     */
    void setCredentials(const String& ssid, const String& password);

    /**
     * @brief Get current SSID
     * @return Current SSID, empty if not set
     */
    String getSSID() const { return ssid_; }

    /**
     * @brief Check if credentials are configured
     * @return true if SSID and password are set
     */
    bool hasCredentials() const;

    /**
     * @brief Update WiFi status (should be called regularly)
     */
    void update();

    /**
     * @brief Get connection statistics
     * @return Statistics as JSON string
     */
    String getConnectionStats() const;

private:
    NetworkStatus status_;
    String ssid_;
    String password_;
    uint32_t lastConnectionAttempt_;
    uint32_t connectionStartTime_;
    uint8_t currentRetryCount_;
    uint8_t maxRetries_;
    
    // Statistics
    uint32_t totalConnections_;
    uint32_t failedConnections_;
    uint32_t lastConnectedTime_;
    uint32_t totalConnectedTime_;
    
    // Callback for connection events
    std::function<void()> onConnectedCallback_;
    
    bool attemptConnection();
    void updateConnectionStats();
    void setStatus(NetworkStatus newStatus);
};

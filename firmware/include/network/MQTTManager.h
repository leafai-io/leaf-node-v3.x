#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <functional>
#include "../../src/LeafNodeTypes.h"

/**
 * @brief MQTT connection manager for LeafNode
 * 
 * Handles MQTT connection, publishing, subscribing, and message handling.
 * Supports automatic reconnection and connection monitoring.
 */
class MQTTManager {
public:
    // Callback function types
    typedef std::function<void(const String& topic, const String& payload)> MessageCallback;
    typedef std::function<void()> ConnectedCallback;
    typedef std::function<void()> DisconnectedCallback;

    MQTTManager();
    ~MQTTManager();

    /**
     * @brief Initialize MQTT manager
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Configure MQTT connection parameters
     * @param server MQTT broker IP address or hostname
     * @param port MQTT broker port
     * @param username MQTT username
     * @param password MQTT password
     * @param clientId MQTT client ID (uses serial number if empty)
     */
    void configure(const String& server, int port, const String& username, 
                  const String& password, const String& clientId = "");

    /**
     * @brief Connect to MQTT broker
     * @return true if connection was successful
     */
    bool connect();

    /**
     * @brief Disconnect from MQTT broker
     */
    void disconnect();

    /**
     * @brief Check if MQTT is connected
     * @return true if connected
     */
    bool isConnected() const;

    /**
     * @brief Update MQTT client (should be called regularly)
     */
    void update();

    /**
     * @brief Publish message to topic
     * @param topic MQTT topic
     * @param payload Message payload
     * @param retained Whether message should be retained
     * @return true if message was published
     */
    bool publish(const String& topic, const String& payload, bool retained = false);

    /**
     * @brief Subscribe to topic
     * @param topic MQTT topic to subscribe to
     * @return true if subscription was successful
     */
    bool subscribe(const String& topic);

    /**
     * @brief Subscribe to all device-specific topics
     * @param serialNumber Device serial number
     */
    void subscribeToDeviceTopics(const String& serialNumber);

    /**
     * @brief Unsubscribe from topic
     * @param topic MQTT topic to unsubscribe from
     * @return true if unsubscription was successful
     */
    bool unsubscribe(const String& topic);

    /**
     * @brief Set callback for incoming messages
     * @param callback Function to call when message is received
     */
    void setMessageCallback(MessageCallback callback) { messageCallback_ = callback; }

    /**
     * @brief Set callback for connection events
     * @param callback Function to call when connected
     */
    void setConnectedCallback(ConnectedCallback callback) { connectedCallback_ = callback; }

    /**
     * @brief Set callback for disconnection events
     * @param callback Function to call when disconnected
     */
    void setDisconnectedCallback(DisconnectedCallback callback) { disconnectedCallback_ = callback; }

    /**
     * @brief Get connection statistics as JSON
     * @return JSON string with connection stats
     */
    String getConnectionStats() const;

    /**
     * @brief Check if configuration is valid
     * @return true if MQTT is properly configured
     */
    bool hasValidConfiguration() const;

    /**
     * @brief Get current connection status
     * @return MQTT connection status
     */
    MQTTStatus getStatus() const { return status_; }

    /**
     * @brief Register device with MQTT broker using structured format
     * @param serialNumber Device serial number
     * @param userId User identifier
     * @return true if registration message was sent
     */
    bool registerDevice(const String& serialNumber, const String& userId);

private:
    WiFiClient wifiClient_;
    PubSubClient mqttClient_;
    
    // Configuration
    String server_;
    int port_;
    String username_;
    String password_;
    String clientId_;
    
    // State
    MQTTStatus status_;
    bool initialized_;
    unsigned long lastConnectionAttempt_;
    unsigned long lastKeepAlive_;
    int connectionRetries_;
    
    // Statistics
    unsigned long totalConnections_;
    unsigned long totalDisconnections_;
    unsigned long totalPublishes_;
    unsigned long totalReceives_;
    unsigned long lastConnectedTime_;
    unsigned long totalConnectedTime_;
    
    // Resilient reconnect tracking
    uint8_t consecutiveFailures_;
    uint32_t lastReconnectAttempt_;
    uint32_t reconnectInterval_;
    bool offlineMode_;
    uint32_t offlineModeStartTime_;
    uint8_t offlineModeLogCounter_;
    
    // Callbacks
    MessageCallback messageCallback_;
    ConnectedCallback connectedCallback_;
    DisconnectedCallback disconnectedCallback_;
    
    // Private methods
    void setStatus(MQTTStatus newStatus);
    bool attemptConnection();
    void updateConnectionStats();
    void cleanupConnection();
    uint32_t calculateReconnectInterval();
    void logReconnectAttempt();
    static void staticMessageCallback(char* topic, byte* payload, unsigned int length);
    void handleMessage(const String& topic, const String& payload);
    
    // Static instance for callback
    static MQTTManager* instance_;
};

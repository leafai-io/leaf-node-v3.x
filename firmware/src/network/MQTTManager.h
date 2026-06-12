#pragma once

#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "../LeafNodeTypes.h"

class MQTTManager {
public:
    MQTTManager();
    ~MQTTManager();
    
    bool initialize();
    void configure(const String& server, int port, const String& username, const String& password, const String& clientId = "");
    void update();
    
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    bool publish(const String& topic, const String& payload, bool retained = false);
    bool subscribe(const String& topic);
    void subscribeToDeviceTopics(const String& serialNumber);
    bool unsubscribe(const String& topic);
    
    // Auto-registration
    bool registerDevice(const String& serialNumber, const String& userId);
    
    // Callback setters
    void setConnectedCallback(std::function<void()> callback) { connectedCallback_ = callback; }
    void setDisconnectedCallback(std::function<void()> callback) { disconnectedCallback_ = callback; }
    void setMessageCallback(std::function<void(const String&, const String&)> callback) { messageCallback_ = callback; }
    
    // Stats and status
    String getConnectionStats() const;
    MQTTStatus getStatus() const { return status_; }
    
private:
    WiFiClient wifiClient_;
    PubSubClient mqttClient_;
    
    // Configuration
    String server_;
    int port_;
    String username_;
    String password_;
    String clientId_;
    
    // Status and timing
    MQTTStatus status_;
    bool initialized_;
    unsigned long lastConnectionAttempt_;
    unsigned long lastKeepAlive_;
    
    // Statistics
    unsigned int connectionRetries_;
    unsigned int totalConnections_;
    unsigned int totalDisconnections_;
    unsigned int totalPublishes_;
    unsigned int totalReceives_;
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
    std::function<void()> connectedCallback_;
    std::function<void()> disconnectedCallback_;
    std::function<void(const String&, const String&)> messageCallback_;
    
    // Helper methods
    bool hasValidConfiguration() const;
    void setStatus(MQTTStatus newStatus);
    bool attemptConnection();
    void updateConnectionStats();
    void cleanupConnection();
    uint32_t calculateReconnectInterval();
    void logReconnectAttempt();
    void handleMessage(const String& topic, const String& payload);
    
    // Static callback for MQTT
    static void staticMessageCallback(char* topic, byte* payload, unsigned int length);
    static MQTTManager* instance_;
};

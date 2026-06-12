#include "MQTTManager.h"
#include <ArduinoJson.h>
#include "config.h"

// Static instance for callback
MQTTManager* MQTTManager::instance_ = nullptr;

MQTTManager::MQTTManager()
    : mqttClient_(wifiClient_)
    , server_("")
    , port_(1883)
    , username_("")
    , password_("")
    , clientId_("")
    , status_(MQTTStatus::DISCONNECTED)
    , initialized_(false)
    , lastConnectionAttempt_(0)
    , lastKeepAlive_(0)
    , connectionRetries_(0)
    , totalConnections_(0)
    , totalDisconnections_(0)
    , totalPublishes_(0)
    , totalReceives_(0)
    , lastConnectedTime_(0)
    , totalConnectedTime_(0)
    , consecutiveFailures_(0)
    , lastReconnectAttempt_(0)
    , reconnectInterval_(5000)
    , offlineMode_(false)
    , offlineModeStartTime_(0)
    , offlineModeLogCounter_(0) {
    
    // Set static instance for callback
    instance_ = this;
}

MQTTManager::~MQTTManager() {
    disconnect();
    instance_ = nullptr;
}

bool MQTTManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Set MQTT callback
    mqttClient_.setCallback(staticMessageCallback);
    
    // Set buffer size for larger messages
    mqttClient_.setBufferSize(1024);
    
    initialized_ = true;
    setStatus(MQTTStatus::DISCONNECTED);
    
    return true;
}

void MQTTManager::configure(const String& server, int port, const String& username, 
                           const String& password, const String& clientId) {
    server_ = server;
    port_ = port;
    username_ = username;
    password_ = password;
    clientId_ = clientId.length() > 0 ? clientId : ("LeafNode_" + String(random(1000, 9999)));
    
    // Configure MQTT client
    mqttClient_.setServer(server_.c_str(), port_);
}

bool MQTTManager::connect() {
    if (!initialized_ || !hasValidConfiguration()) {
        return false;
    }
    
    if (isConnected()) {
        return true;
    }
    
    // Check if WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        setStatus(MQTTStatus::FAILED);
        return false;
    }
    
    setStatus(MQTTStatus::CONNECTING);
    
    // Attempt connection with retry logic
    const int maxRetries = 3;
    connectionRetries_ = 0;
    
    while (connectionRetries_ < maxRetries && !isConnected()) {
        if (attemptConnection()) {
            setStatus(MQTTStatus::CONNECTED);
            totalConnections_++;
            lastConnectedTime_ = millis();
            
            // Call connected callback
            if (connectedCallback_) {
                connectedCallback_();
            }
            
            return true;
        }
        
        connectionRetries_++;
        if (connectionRetries_ < maxRetries) {
            delay(2000); // Wait 2 seconds between retries
        }
    }
    
    setStatus(MQTTStatus::FAILED);
    return false;
}

void MQTTManager::disconnect() {
    if (isConnected()) {
        updateConnectionStats();
        totalDisconnections_++;
        
        // Call disconnected callback
        if (disconnectedCallback_) {
            disconnectedCallback_();
        }
    }
    
    mqttClient_.disconnect();
    setStatus(MQTTStatus::DISCONNECTED);
}

bool MQTTManager::isConnected() const {
    return const_cast<PubSubClient&>(mqttClient_).connected() && status_ == MQTTStatus::CONNECTED;
}

void MQTTManager::update() {
    if (!initialized_) {
        return;
    }
    
    // Handle MQTT loop
    if (mqttClient_.connected()) {
        mqttClient_.loop();
        
        // Send keep-alive if needed
        unsigned long now = millis();
        if (now - lastKeepAlive_ > 30000) { // Every 30 seconds
            lastKeepAlive_ = now;
        }
    } else {
        // Handle disconnection
        if (status_ == MQTTStatus::CONNECTED) {
            setStatus(MQTTStatus::DISCONNECTED);
            updateConnectionStats();
            totalDisconnections_++;
            
            if (disconnectedCallback_) {
                disconnectedCallback_();
            }
        }
        
        // Auto-reconnect with exponential backoff
        if (hasValidConfiguration() && WiFi.status() == WL_CONNECTED) {
            unsigned long now = millis();
            if (now - lastReconnectAttempt_ >= reconnectInterval_) {
                lastReconnectAttempt_ = now;
                setStatus(MQTTStatus::RECONNECTING);
                
                // Cleanup before reconnect attempt
                cleanupConnection();
                
                // Log reconnect attempt
                logReconnectAttempt();
                
                if (attemptConnection()) {
                    // Success! Reset all failure tracking
                    if (offlineMode_) {
                        unsigned long offlineTime = (millis() - offlineModeStartTime_) / 1000;
                        Serial.printf("[MQTTManager] ✅ CONNECTION RESTORED after %lus offline!\n", offlineTime);
                    }
                    
                    consecutiveFailures_ = 0;
                    reconnectInterval_ = 5000;
                    offlineMode_ = false;
                    offlineModeLogCounter_ = 0;
                    
                    setStatus(MQTTStatus::CONNECTED);
                    totalConnections_++;
                    lastConnectedTime_ = millis();
                    
                    if (connectedCallback_) {
                        connectedCallback_();
                    }
                } else {
                    // Connection failed - increment failure counter and calculate new interval
                    consecutiveFailures_++;
                    reconnectInterval_ = calculateReconnectInterval();
                }
            }
        }
    }
}

bool MQTTManager::publish(const String& topic, const String& payload, bool retained) {
    if (!isConnected()) {
        return false;
    }
    
    bool result = mqttClient_.publish(topic.c_str(), payload.c_str(), retained);
    if (result) {
        totalPublishes_++;
    }
    
    return result;
}

bool MQTTManager::subscribe(const String& topic) {
    if (!isConnected()) {
        return false;
    }
    
    return mqttClient_.subscribe(topic.c_str());
}

void MQTTManager::subscribeToDeviceTopics(const String& serialNumber) {
    if (!isConnected()) {
        return;
    }
    
    // Subscribe to device topics using config.h defines
    String commandsTopic = String(MQTT_TOPIC_PREFIX) + serialNumber + String(MQTT_TOPIC_COMMANDS);
    String registrationAckTopic = String(MQTT_TOPIC_PREFIX) + serialNumber + String(MQTT_TOPIC_REGISTER_ACK);
    String nodeCommandTopic = String(MQTT_TOPIC_PREFIX) + serialNumber + String(MQTT_TOPIC_COMMAND);
    
    subscribe(commandsTopic);
    subscribe(registrationAckTopic);
    subscribe(nodeCommandTopic);
}

bool MQTTManager::unsubscribe(const String& topic) {
    if (!isConnected()) {
        return false;
    }
    
    return mqttClient_.unsubscribe(topic.c_str());
}

String MQTTManager::getConnectionStats() const {
    DynamicJsonDocument doc(512);
    
    doc["status"] = static_cast<int>(status_);
    doc["server"] = server_;
    doc["port"] = port_;
    doc["client_id"] = clientId_;
    doc["connected"] = isConnected();
    doc["total_connections"] = totalConnections_;
    doc["total_disconnections"] = totalDisconnections_;
    doc["total_publishes"] = totalPublishes_;
    doc["total_receives"] = totalReceives_;
    doc["connection_retries"] = connectionRetries_;
    doc["consecutive_failures"] = consecutiveFailures_;
    doc["offline_mode"] = offlineMode_;
    doc["reconnect_interval"] = reconnectInterval_;
    
    if (offlineMode_ && offlineModeStartTime_ > 0) {
        doc["offline_time"] = (millis() - offlineModeStartTime_) / 1000;
    }
    
    if (isConnected()) {
        doc["connected_time"] = millis() - lastConnectedTime_;
    }
    doc["total_connected_time"] = totalConnectedTime_;
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool MQTTManager::hasValidConfiguration() const {
    return server_.length() > 0 && port_ > 0 && 
           username_.length() > 0 && password_.length() > 0 &&
           clientId_.length() > 0;
}

void MQTTManager::setStatus(MQTTStatus newStatus) {
    if (status_ != newStatus) {
        status_ = newStatus;
    }
}

bool MQTTManager::attemptConnection() {
    // Attempt MQTT connection
    bool connected = false;
    
    if (username_.length() > 0 && password_.length() > 0) {
        connected = mqttClient_.connect(clientId_.c_str(), username_.c_str(), password_.c_str());
    } else {
        connected = mqttClient_.connect(clientId_.c_str());
    }
    
    // Log error details on failure (only in non-offline mode)
    if (!connected && !offlineMode_) {
        int state = mqttClient_.state();
        const char* errorMsg = "Unknown error";
        
        switch(state) {
            case -4: errorMsg = "Connection timeout"; break;
            case -3: errorMsg = "Connection lost"; break;
            case -2: errorMsg = "Connect failed (broker unreachable)"; break;
            case -1: errorMsg = "Disconnected"; break;
            case 1: errorMsg = "Bad protocol version"; break;
            case 2: errorMsg = "Client ID rejected"; break;
            case 3: errorMsg = "Server unavailable"; break;
            case 4: errorMsg = "Bad credentials"; break;
            case 5: errorMsg = "Not authorized"; break;
        }
        
        Serial.printf("[MQTTManager] Connection failed: %s (code: %d)\n", errorMsg, state);
    }
    
    return connected;
}

void MQTTManager::updateConnectionStats() {
    if (lastConnectedTime_ > 0) {
        totalConnectedTime_ += millis() - lastConnectedTime_;
    }
}

void MQTTManager::cleanupConnection() {
    // Explicit socket cleanup before reconnect attempt
    // This prevents "Connection reset by peer" errors
    
    if (mqttClient_.connected()) {
        mqttClient_.disconnect();
    }
    
    // Stop underlying WiFi client to clean up TCP socket
    wifiClient_.stop();
    
    // Give OS time to clean up socket resources
    delay(100);
}

uint32_t MQTTManager::calculateReconnectInterval() {
    // Phase 1: Normal retry (attempts 1-5) - 5 seconds
    if (consecutiveFailures_ <= 5) {
        return 5000;
    }
    
    // Phase 2: Exponential backoff (attempts 6-15)
    if (consecutiveFailures_ <= 15) {
        uint32_t exponent = consecutiveFailures_ - 5;
        uint32_t interval = 5000 * (1 << exponent); // 5000 * 2^exponent
        
        // Cap at 5 minutes
        if (interval > 300000) {
            interval = 300000;
        }
        
        return interval;
    }
    
    // Phase 3: Offline mode (attempts 16+) - 5 minutes fixed
    if (!offlineMode_) {
        offlineMode_ = true;
        offlineModeStartTime_ = millis();
        offlineModeLogCounter_ = 0;
        
        Serial.println("[MQTTManager] ⚠️  Entering OFFLINE MODE after 15 failed attempts");
        Serial.println("[MQTTManager]    Device continues operating without MQTT");
        Serial.println("[MQTTManager]    Reconnect attempts: every 300 seconds");
    }
    
    return 300000; // 5 minutes
}

void MQTTManager::logReconnectAttempt() {
    // Phase 1 & 2: Log every attempt
    if (consecutiveFailures_ <= 15) {
        Serial.printf("[MQTTManager] Reconnect attempt #%d (server: %s, interval: %lums)\n",
                     consecutiveFailures_ + 1, server_.c_str(), reconnectInterval_);
    }
    // Phase 3: Log only every 10th attempt to reduce spam
    else if (offlineMode_) {
        offlineModeLogCounter_++;
        if (offlineModeLogCounter_ >= 10) {
            offlineModeLogCounter_ = 0;
            uint32_t offlineTime = (millis() - offlineModeStartTime_) / 1000;
            Serial.printf("[MQTTManager] 📴 OFFLINE MODE: %lus | Attempts: %d | Next retry in 300s\n",
                         offlineTime, consecutiveFailures_ + 1);
        }
    }
}

void MQTTManager::staticMessageCallback(char* topic, byte* payload, unsigned int length) {
    if (instance_) {
        // Convert payload to String
        String payloadStr = "";
        for (unsigned int i = 0; i < length; i++) {
            payloadStr += (char)payload[i];
        }
        
        instance_->handleMessage(String(topic), payloadStr);
    }
}

void MQTTManager::handleMessage(const String& topic, const String& payload) {
    totalReceives_++;
    
    if (messageCallback_) {
        messageCallback_(topic, payload);
    }
}

bool MQTTManager::registerDevice(const String& serialNumber, const String& userId) {
    if (!isConnected()) {
        return false;
    }
    
    // Create registration topic using config.h defines
    String topic = String(MQTT_TOPIC_PREFIX) + serialNumber + String(MQTT_TOPIC_REGISTER);
    
    // Create registration payload with structured format
    DynamicJsonDocument doc(512);
    
    // System information
    doc["system"]["firmware_version"] = FIRMWARE_VERSION;
    doc["system"]["timestamp"] = millis();
    
    // Registration data
    doc["serial_number"] = serialNumber;
    doc["user_id"] = userId;
    doc["status"] = "registering";
    
    String payload;
    serializeJson(doc, payload);
    
    return publish(topic, payload, false);
}

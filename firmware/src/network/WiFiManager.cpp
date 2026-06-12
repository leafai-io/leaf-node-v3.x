#include "WiFiManager.h"
#include <ArduinoJson.h>

WiFiManager::WiFiManager() 
    : status_(NetworkStatus::DISCONNECTED)
    , ssid_("")
    , password_("")
    , lastConnectionAttempt_(0)
    , connectionStartTime_(0)
    , currentRetryCount_(0)
    , maxRetries_(WIFI_MAX_RETRY)
    , totalConnections_(0)
    , failedConnections_(0)
    , lastConnectedTime_(0)
    , totalConnectedTime_(0) {
}

WiFiManager::~WiFiManager() {
    disconnect();
}

bool WiFiManager::initialize() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // We handle reconnection manually
    setStatus(NetworkStatus::DISCONNECTED);
    return true;
}

bool WiFiManager::connect(const String& ssid, const String& password, uint8_t maxRetries) {
    ssid_ = ssid;
    password_ = password;
    maxRetries_ = maxRetries;
    currentRetryCount_ = 0;
    
    return connect();
}

bool WiFiManager::connect() {
    if (!hasCredentials()) {
        return false;
    }
    
    currentRetryCount_ = 0;
    setStatus(NetworkStatus::WIFI_CONNECTING);
    
    while (currentRetryCount_ < maxRetries_) {
        if (attemptConnection()) {
            setStatus(NetworkStatus::WIFI_CONNECTED);
            updateConnectionStats();
            totalConnections_++;
            lastConnectedTime_ = millis();
            
            // Trigger connection callback (for NTP sync etc.)
            if (onConnectedCallback_) {
                onConnectedCallback_();
            }
            
            return true;
        }
        
        currentRetryCount_++;
        if (currentRetryCount_ < maxRetries_) {
            delay(WIFI_RETRY_DELAY);
        }
    }
    
    setStatus(NetworkStatus::WIFI_FAILED);
    failedConnections_++;
    return false;
}

void WiFiManager::disconnect() {
    if (status_ == NetworkStatus::WIFI_CONNECTED) {
        updateConnectionStats();
    }
    
    WiFi.disconnect(true);
    setStatus(NetworkStatus::DISCONNECTED);
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED && status_ == NetworkStatus::WIFI_CONNECTED;
}

String WiFiManager::getIPAddress() const {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

int32_t WiFiManager::getRSSI() const {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return 0;
}

String WiFiManager::scanNetworks() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks();
    String result = "";
    
    if (n == 0) {
        result = "No networks found";
    } else {
        for (int i = 0; i < n; ++i) {
            if (i > 0) result += "|";
            result += WiFi.SSID(i);
            result += ",";
            result += WiFi.RSSI(i);
            result += ",";
            result += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "0" : "1";
        }
    }
    
    WiFi.scanDelete();
    return result;
}

void WiFiManager::setCredentials(const String& ssid, const String& password) {
    ssid_ = ssid;
    password_ = password;
}

bool WiFiManager::hasCredentials() const {
    return !ssid_.isEmpty() && !password_.isEmpty();
}

void WiFiManager::update() {
    // Check if we lost connection
    if (status_ == NetworkStatus::WIFI_CONNECTED && WiFi.status() != WL_CONNECTED) {
        setStatus(NetworkStatus::DISCONNECTED);
        updateConnectionStats();
    }
}

String WiFiManager::getConnectionStats() const {
    DynamicJsonDocument doc(512);
    
    doc["status"] = static_cast<int>(status_);
    doc["ssid"] = ssid_;
    doc["ip"] = getIPAddress();
    doc["rssi"] = getRSSI();
    doc["total_connections"] = totalConnections_;
    doc["failed_connections"] = failedConnections_;
    doc["current_retry"] = currentRetryCount_;
    doc["max_retries"] = maxRetries_;
    
    if (isConnected()) {
        doc["connected_time"] = millis() - lastConnectedTime_;
    }
    
    doc["total_connected_time"] = totalConnectedTime_;
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool WiFiManager::attemptConnection() {
    connectionStartTime_ = millis();
    
    WiFi.begin(ssid_.c_str(), password_.c_str());
    
    // Wait for connection with timeout
    uint32_t timeout = NETWORK_TIMEOUT;
    uint32_t startTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
        delay(100);
    }
    
    return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::updateConnectionStats() {
    if (lastConnectedTime_ > 0) {
        totalConnectedTime_ += millis() - lastConnectedTime_;
    }
}

void WiFiManager::setStatus(NetworkStatus newStatus) {
    if (status_ != newStatus) {
        status_ = newStatus;
    }
}
void WiFiManager::setOnConnectedCallback(std::function<void()> callback) {
    onConnectedCallback_ = callback;
}
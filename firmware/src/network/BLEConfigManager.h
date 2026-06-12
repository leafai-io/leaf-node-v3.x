#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <functional>
#include "../LeafNodeTypes.h"

// BLE Service and Characteristic UUIDs
#define BLE_SERVICE_UUID        "7EF7CD05-F598-4DFC-B5BC-D1CC1CE141DC"
#define BLE_CHAR_SN_UUID        "35601389-4683-43FA-A783-5E452E5023AB"
#define BLE_CHAR_KEY_UUID       "30D03AFC-659A-4352-AA2A-8891C16ADE8F"
#define BLE_CHAR_WIFI_UUID      "FBA432C3-1771-4AB8-9E4C-3237392F07AF"
#define BLE_CHAR_SCAN_UUID      "B8A5E5C2-4D78-4F91-A2B3-7E8F1C9D2A4B"

/**
 * @brief BLE Configuration Manager
 * 
 * Provides BLE-based WiFi configuration service for the LeafNode.
 * Allows mobile apps to configure WiFi credentials and scan networks.
 */
class BLEConfigManager {
public:
    // Callback function types
    typedef std::function<void(const String& ssid, const String& password, const String& userId)> WiFiConfigCallback;
    typedef std::function<String()> NetworkScanCallback;
    typedef std::function<String()> SerialNumberCallback;
    typedef std::function<String()> BLEKeyCallback;

    BLEConfigManager();
    ~BLEConfigManager();

    /**
     * @brief Initialize BLE configuration service
     * @param deviceName BLE device name for advertising
     * @return true if initialization was successful
     */
    bool initialize(const String& deviceName = "LeafNode Setup");

    /**
     * @brief Start BLE configuration mode
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop BLE configuration mode
     */
    void stop();

    /**
     * @brief Check if BLE is active
     * @return true if BLE is running
     */
    bool isActive() const { return active_; }

    /**
     * @brief Handle BLE events (should be called regularly when active)
     */
    void handleEvents();

    /**
     * @brief Set callback for WiFi configuration
     * @param callback Function to call when WiFi config is received (now includes user_id)
     */
    void setWiFiConfigCallback(WiFiConfigCallback callback) { wifiConfigCallback_ = callback; }

    /**
     * @brief Set callback for network scanning
     * @param callback Function to call when network scan is requested
     */
    void setNetworkScanCallback(NetworkScanCallback callback) { networkScanCallback_ = callback; }

    /**
     * @brief Set callback for serial number retrieval
     * @param callback Function to call when serial number is requested
     */
    void setSerialNumberCallback(SerialNumberCallback callback) { serialNumberCallback_ = callback; }

    /**
     * @brief Set callback for BLE key retrieval
     * @param callback Function to call when BLE key is requested
     */
    void setBLEKeyCallback(BLEKeyCallback callback) { bleKeyCallback_ = callback; }

    /**
     * @brief Send status update to connected client
     * @param status Status message to send
     */
    void sendStatus(const String& status);

    /**
     * @brief Get the temporarily stored user ID from last WiFi configuration
     * @return User ID string, empty if not set
     */
    String getTemporaryUserId() const { return temporaryUserId_; }

    /**
     * @brief Clear the temporarily stored user ID
     */
    void clearTemporaryUserId() { temporaryUserId_ = ""; }

    /**
     * @brief Get number of connected BLE clients
     * @return Number of connected clients
     */
    uint32_t getConnectedClients() const;

private:
    BLEServer* pServer_;
    BLEService* pService_;
    BLECharacteristic* pCharSN_;
    BLECharacteristic* pCharKey_;
    BLECharacteristic* pCharWiFi_;
    BLECharacteristic* pCharScan_;
    
    bool initialized_;
    bool active_;
    String deviceName_;
    
    // Callback functions
    WiFiConfigCallback wifiConfigCallback_;
    NetworkScanCallback networkScanCallback_;
    SerialNumberCallback serialNumberCallback_;
    BLEKeyCallback bleKeyCallback_;
    
    // Event flags
    bool scanRequested_;
    bool wifiConfigRequested_;
    String pendingSSID_;
    String pendingPassword_;
    String pendingUserId_;
    
    // Temporary storage for user ID
    String temporaryUserId_;
    
    void setupCharacteristics();
    void startAdvertising();
    bool validateBLEKey(const String& providedKey);
    
    friend class WiFiCharacteristicCallback;
    friend class ScanCharacteristicCallback;
};

/**
 * @brief Callback handler for WiFi configuration characteristic
 */
class WiFiCharacteristicCallback : public BLECharacteristicCallbacks {
public:
    WiFiCharacteristicCallback(BLEConfigManager* manager) : manager_(manager) {}
    void onWrite(BLECharacteristic *pCharacteristic) override;

private:
    BLEConfigManager* manager_;
};

/**
 * @brief Callback handler for network scan characteristic
 */
class ScanCharacteristicCallback : public BLECharacteristicCallbacks {
public:
    ScanCharacteristicCallback(BLEConfigManager* manager) : manager_(manager) {}
    void onWrite(BLECharacteristic *pCharacteristic) override;

private:
    BLEConfigManager* manager_;
};

#include "BLEConfigManager.h"

BLEConfigManager::BLEConfigManager() 
    : pServer_(nullptr)
    , pService_(nullptr)
    , pCharSN_(nullptr)
    , pCharKey_(nullptr)
    , pCharWiFi_(nullptr)
    , pCharScan_(nullptr)
    , initialized_(false)
    , active_(false)
    , deviceName_("LeafNode Setup")
    , scanRequested_(false)
    , wifiConfigRequested_(false)
    , temporaryUserId_("") {
}

BLEConfigManager::~BLEConfigManager() {
    stop();
}

bool BLEConfigManager::initialize(const String& deviceName) {
    if (initialized_) {
        return true;
    }
    
    deviceName_ = deviceName;
    
    BLEDevice::init(deviceName_.c_str());
    pServer_ = BLEDevice::createServer();
    
    if (!pServer_) {
        return false;
    }
    
    pService_ = pServer_->createService(BLE_SERVICE_UUID);
    if (!pService_) {
        return false;
    }
    
    setupCharacteristics();
    initialized_ = true;
    
    return true;
}

bool BLEConfigManager::start() {
    if (!initialized_ || active_) {
        return false;
    }
    
    // Update characteristics with current values
    if (serialNumberCallback_) {
        pCharSN_->setValue(serialNumberCallback_().c_str());
    }
    
    if (bleKeyCallback_) {
        pCharKey_->setValue(bleKeyCallback_().c_str());
    }
    
    pCharWiFi_->setValue("Ready");
    pCharScan_->setValue("Ready for scan");
    
    pService_->start();
    startAdvertising();
    
    active_ = true;
    return true;
}

void BLEConfigManager::stop() {
    if (!active_) {
        return;
    }
    
    BLEDevice::getAdvertising()->stop();
    
    if (pService_) {
        pService_->stop();
    }
    
    active_ = false;
}

void BLEConfigManager::handleEvents() {
    if (!active_) {
        return;
    }
    
    // Handle network scan request
    if (scanRequested_) {
        scanRequested_ = false;
        
        if (networkScanCallback_) {
            String networks = networkScanCallback_();
            pCharScan_->setValue(networks.c_str());
            pCharScan_->notify();
        } else {
            pCharScan_->setValue("ERROR:Scan not available");
            pCharScan_->notify();
        }
    }
    
    // Handle WiFi configuration request
    if (wifiConfigRequested_) {
        wifiConfigRequested_ = false;
        
        Serial.println("[BLE] handleEvents: About to call wifiConfigCallback");
        Serial.println("[BLE] handleEvents: pendingSSID=[" + pendingSSID_ + "]");
        Serial.println("[BLE] handleEvents: pendingPassword=[" + pendingPassword_ + "]");
        Serial.println("[BLE] handleEvents: pendingUserId=[" + pendingUserId_ + "]");
        
        if (wifiConfigCallback_) {
            // Store user ID temporarily and pass it to callback
            temporaryUserId_ = pendingUserId_;
            wifiConfigCallback_(pendingSSID_, pendingPassword_, pendingUserId_);
        }
        
        pendingSSID_ = "";
        pendingPassword_ = "";
        pendingUserId_ = "";
    }
}

void BLEConfigManager::sendStatus(const String& status) {
    if (active_ && pCharWiFi_) {
        pCharWiFi_->setValue(status.c_str());
        pCharWiFi_->notify();
    }
}

uint32_t BLEConfigManager::getConnectedClients() const {
    if (active_ && pServer_) {
        return pServer_->getConnectedCount();
    }
    return 0;
}

void BLEConfigManager::setupCharacteristics() {
    // Serial Number characteristic (read-only)
    pCharSN_ = pService_->createCharacteristic(
        BLE_CHAR_SN_UUID, 
        BLECharacteristic::PROPERTY_READ
    );
    pCharSN_->setValue("UNKNOWN");
    
    // BLE Key characteristic (read-only)
    pCharKey_ = pService_->createCharacteristic(
        BLE_CHAR_KEY_UUID, 
        BLECharacteristic::PROPERTY_READ
    );
    pCharKey_->setValue("DEFAULT_KEY");
    
    // WiFi configuration characteristic (read/write/notify)
    pCharWiFi_ = pService_->createCharacteristic(
        BLE_CHAR_WIFI_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharWiFi_->setCallbacks(new WiFiCharacteristicCallback(this));
    pCharWiFi_->addDescriptor(new BLE2902());
    pCharWiFi_->setValue("Ready");
    
    // Network scan characteristic (read/write/notify)
    pCharScan_ = pService_->createCharacteristic(
        BLE_CHAR_SCAN_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharScan_->setCallbacks(new ScanCharacteristicCallback(this));
    pCharScan_->addDescriptor(new BLE2902());
    pCharScan_->setValue("Ready for scan");
}

void BLEConfigManager::startAdvertising() {
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    pAdvertising->start();
}

bool BLEConfigManager::validateBLEKey(const String& providedKey) {
    if (!bleKeyCallback_) {
        return false;
    }
    
    String validKey = bleKeyCallback_();
    return providedKey == validKey;
}

// WiFi Characteristic Callback Implementation
void WiFiCharacteristicCallback::onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    String data = String(value.c_str());
    
    // Expected format: BLE_KEY|SSID|PASSWORD|USER_ID
    int firstSep = data.indexOf('|');
    int secondSep = data.indexOf('|', firstSep + 1);
    int thirdSep = data.indexOf('|', secondSep + 1);
    
    if (firstSep == -1 || secondSep == -1 || thirdSep == -1) {
        pCharacteristic->setValue("ERROR:Invalid format - Expected: KEY|SSID|PASSWORD|USER_ID");
        pCharacteristic->notify();
        return;
    }
    
    String providedKey = data.substring(0, firstSep);
    String ssid = data.substring(firstSep + 1, secondSep);
    String password = data.substring(secondSep + 1, thirdSep);
    String userId = data.substring(thirdSep + 1);
    
    // DEBUG: Log what we received
    Serial.println("[BLE] Raw BLE data: [" + data + "]");
    Serial.println("[BLE] Parsed Key: [" + providedKey + "]");
    Serial.println("[BLE] Parsed SSID: [" + ssid + "]");
    Serial.println("[BLE] Parsed Password: [" + password + "]");
    Serial.println("[BLE] Parsed UserId: [" + userId + "]");
    Serial.println("[BLE] Password length: " + String(password.length()));
    
    // Validate user ID format (basic UUID validation)
    if (userId.length() != 36 || userId.charAt(8) != '-' || userId.charAt(13) != '-' || 
        userId.charAt(18) != '-' || userId.charAt(23) != '-') {
        pCharacteristic->setValue("ERROR:Invalid USER_ID format - Expected UUID format");
        pCharacteristic->notify();
        return;
    }
    
    if (!manager_->validateBLEKey(providedKey)) {
        pCharacteristic->setValue("ERROR:Wrong key");
        pCharacteristic->notify();
        return;
    }
    
    // Valid key and format - proceed with WiFi configuration
    pCharacteristic->setValue("STATUS:Processing WiFi configuration with User ID");
    pCharacteristic->notify();
    
    manager_->pendingSSID_ = ssid;
    manager_->pendingPassword_ = password;
    manager_->pendingUserId_ = userId;
    manager_->wifiConfigRequested_ = true;
}

// Scan Characteristic Callback Implementation
void ScanCharacteristicCallback::onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    String data = String(value.c_str());
    
    if (!manager_->validateBLEKey(data)) {
        pCharacteristic->setValue("ERROR:Wrong key");
        pCharacteristic->notify();
        return;
    }
    
    // Valid key - proceed with network scan
    pCharacteristic->setValue("SCANNING");
    pCharacteristic->notify();
    manager_->scanRequested_ = true;
}

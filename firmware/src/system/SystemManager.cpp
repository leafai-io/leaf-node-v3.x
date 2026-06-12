#include "SystemManager.h"
#include "../hardware/StatusLED.h"
#include "../hardware/MCP4725.h"
#include "../hardware/PWMController.h"
#include "../diagnostics/Logger.h"
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include "config.h"

SystemManager::SystemManager() 
    : status_(SystemStatus::BOOTING)
    , initialized_(false)
    , watchdogEnabled_(true)
    , lastHeartbeat_(0)
    , bootTime_(0)
    , statusLED_(nullptr)
    , mcp4725_(nullptr)
    , pwmController_(nullptr)
    , pwmControllerMOSFET_(nullptr)
    , actuatorActive_(false) {
}

SystemManager::~SystemManager() {
    if (watchdogEnabled_) {
        esp_task_wdt_deinit();
    }
    
    // Clean up MCP4725
    if (mcp4725_) {
        delete mcp4725_;
        mcp4725_ = nullptr;
    }
    
    // Clean up PWM Controller (IO2)
    if (pwmController_) {
        delete pwmController_;
        pwmController_ = nullptr;
    }
    
    // Clean up PWM Controller (MOSFET)
    if (pwmControllerMOSFET_) {
        delete pwmControllerMOSFET_;
        pwmControllerMOSFET_ = nullptr;
    }
}

bool SystemManager::initialize() {
    bootTime_ = millis();
    status_ = SystemStatus::INITIALIZING;
    
    // Initialize hardware
    initializeHardware();
    
    // Setup pins
    setupPins();
    
    // Initialize UART Chain Manager
    Serial.println("[SystemManager] Initializing UART Chain Manager...");
    if (chainManager_.initialize()) {
        Serial.println("[SystemManager] UART Chain Manager initialized successfully");
    } else {
        Serial.println("[SystemManager] WARNING: UART Chain Manager initialization failed");
    }
    
    // Initialize MCP4725 DAC
    Serial.println("[SystemManager] Initializing MCP4725 DAC...");
    if (initializeMCP4725()) {
        Serial.println("[SystemManager] MCP4725 DAC initialized successfully");
    } else {
        Serial.println("[SystemManager] WARNING: MCP4725 DAC not available");
    }
    
    // Initialize PWM Controller (IO2)
    Serial.println("[SystemManager] Initializing PWM Controller (IO2)...");
    if (initializePWMController()) {
        Serial.println("[SystemManager] PWM Controller (IO2) initialized successfully");
    } else {
        Serial.println("[SystemManager] WARNING: PWM Controller (IO2) initialization failed");
    }
    
    // Initialize PWM Controller (MOSFET)
    Serial.println("[SystemManager] Initializing PWM Controller (MOSFET)...");
    if (initializePWMControllerMOSFET()) {
        Serial.println("[SystemManager] PWM Controller (MOSFET) initialized successfully");
    } else {
        Serial.println("[SystemManager] WARNING: PWM Controller (MOSFET) initialization failed");
    }
    
    // Initialize memory tracking
    minFreeHeap_ = ESP.getFreeHeap();
    
    // Enable watchdog
    setWatchdogEnabled(true);
    
    initialized_ = true;
    return true;
}

void SystemManager::setStatusLED(StatusLED* led) {
    statusLED_ = led;
}

void SystemManager::update() {
    // Update memory statistics
    updateMemoryStats();
    
    // Check watchdog
    if (watchdogEnabled_) {
        checkWatchdog();
    }
}

void SystemManager::setStatus(SystemStatus status) {
    if (status_ != status) {
        status_ = status;
        
        // Update LED status based on system status
        if (statusLED_) {
            switch (status) {
                case SystemStatus::BOOTING:
                    statusLED_->setStatus(LEDStatus::BOOTING);
                    break;
                case SystemStatus::INITIALIZING:
                    statusLED_->setStatus(LEDStatus::BLE_CONFIG); // Blue during setup
                    break;
                case SystemStatus::RUNNING:
                    // Will be handled by startSuccessFade() call from LeafNode
                    break;
                case SystemStatus::ERROR:
                    statusLED_->setStatus(LEDStatus::ERROR);
                    break;
                default:
                    break;
            }
        }
    }
}

uint32_t SystemManager::getUptime() const {
    return millis() - bootTime_;
}

size_t SystemManager::getFreeHeap() const {
    return ESP.getFreeHeap();
}

size_t SystemManager::getMinFreeHeap() const {
    return minFreeHeap_;
}

void SystemManager::restart() {
    ESP.restart();
}

void SystemManager::setWatchdogEnabled(bool enabled) {
    if (enabled && !watchdogEnabled_) {
        esp_task_wdt_init(WATCHDOG_TIMEOUT / 1000, true);
        esp_task_wdt_add(NULL);
        feedWatchdog();
    } else if (!enabled && watchdogEnabled_) {
        esp_task_wdt_delete(NULL);
        esp_task_wdt_deinit();
    }
    watchdogEnabled_ = enabled;
}

void SystemManager::feedWatchdog() {
    if (watchdogEnabled_) {
        esp_task_wdt_reset();
        lastWatchdogFeed_ = millis();
    }
}

String SystemManager::getSystemInfo() const {
    StaticJsonDocument<512> doc;
    
    // System information block
    doc["system"]["firmware_version"] = FIRMWARE_VERSION;
    doc["system"]["uptime"] = getUptime();
    doc["system"]["timestamp"] = millis();
    
    // Hardware information
    doc["hardware"]["free_heap"] = getFreeHeap();
    doc["hardware"]["min_free_heap"] = getMinFreeHeap();
    doc["hardware"]["core_id"] = xPortGetCoreID();
    doc["hardware"]["chip_model"] = ESP.getChipModel();
    doc["hardware"]["chip_revision"] = ESP.getChipRevision();
    doc["hardware"]["cpu_freq"] = ESP.getCpuFreqMHz();
    doc["hardware"]["flash_size"] = ESP.getFlashChipSize();
    
    // Configuration
    doc["config"]["watchdog_enabled"] = watchdogEnabled_;
    
    String result;
    serializeJson(doc, result);
    return result;
}

void SystemManager::setRGBLED(uint8_t red, uint8_t green, uint8_t blue) {
    // Manual MQTT control - Now handled by StatusLED for both V2 and V3
    if (statusLED_) {
        statusLED_->setRGB(red, green, blue);
    }
}

void SystemManager::getCurrentRGBLED(uint8_t& red, uint8_t& green, uint8_t& blue) const {
    if (statusLED_) {
        statusLED_->getCurrentRGB(red, green, blue);
    } else {
        red = 0;
        green = 0;
        blue = 0;
    }
}

void SystemManager::setRGBLED(bool red, bool green, bool blue) {
    // Manual MQTT control
    if (statusLED_) {
        statusLED_->setRGB(red ? 255 : 0, green ? 255 : 0, blue ? 255 : 0);
    }
}

void SystemManager::startSuccessFade() {
    if (statusLED_) {
        statusLED_->setStatus(LEDStatus::SUCCESS_FADE);
    }
}

void SystemManager::stopSuccessFade() {
    if (statusLED_) {
        statusLED_->setAutoMode(true); // Re-enable automatic control
    }
}

void SystemManager::setNetworkLED(NetworkStatus networkStatus) {
    // Map network status to LED status
    if (!statusLED_) return;
    
    // Priority: Actuator state overrides network status
    if (actuatorActive_) {
        return; // Keep actuator LED active
    }
    
    switch (networkStatus) {
        case NetworkStatus::DISCONNECTED:
            statusLED_->setStatus(LEDStatus::BLE_CONFIG); // Blue for setup
            break;
        case NetworkStatus::WIFI_CONNECTING:
            statusLED_->setStatus(LEDStatus::WIFI_CONNECTING);
            break;
        case NetworkStatus::WIFI_CONNECTED:
            // Don't change - let success fade handle this
            break;
        case NetworkStatus::WIFI_FAILED:
            statusLED_->setStatus(LEDStatus::WIFI_FAILED);
            break;
        case NetworkStatus::WIFI_RECONNECTING:
            statusLED_->setStatus(LEDStatus::WIFI_RECONNECTING);
            break;
        case NetworkStatus::BLE_CONFIG:
        case NetworkStatus::BLE_CONFIG_MODE:
        case NetworkStatus::BLE_ACTIVE:
            statusLED_->setStatus(LEDStatus::BLE_CONFIG);
            break;
        default:
            break;
    }
}

void SystemManager::setActuatorLED(bool active) {
    actuatorActive_ = active;
    updateActuatorDACLED();
}

void SystemManager::updateActuatorDACLED() {
    if (!statusLED_) return;
    
    // Check if actuator is active
    bool actuatorOn = actuatorActive_;
    
    // Check if DAC is active (value > 0)
    bool dacOn = false;
    if (mcp4725_) {
        dacOn = (mcp4725_->getCurrentValue() > 0);
    }
    
    // Check if PWM IO2 is active (value > 0)
    bool pwmIO2On = false;
    if (pwmController_) {
        pwmIO2On = (pwmController_->getCurrentValue() > 0);
    }
    
    // Check if PWM MOSFET is active (value > 0)
    bool pwmMOSFETOn = false;
    if (pwmControllerMOSFET_) {
        pwmMOSFETOn = (pwmControllerMOSFET_->getCurrentValue() > 0);
    }
    
    if (actuatorOn || dacOn || pwmIO2On || pwmMOSFETOn) {
        // Actuator, DAC, or PWM is ON - show yellow/orange (priority over network status)
        statusLED_->setRGB(255, 140, 0); // Orange color
    } else {
        // All are OFF - restore automatic LED control
        statusLED_->setAutoMode(true);
    }
}

void SystemManager::initializeHardware() {
    // Basic hardware initialization
    // ESP32-S3 specific initialization can be added here
}

void SystemManager::setupPins() {
    // RGB LED pins will be setup by StatusLED class
    // This function kept for compatibility
}

void SystemManager::updateMemoryStats() {
    size_t currentFree = ESP.getFreeHeap();
    if (currentFree < minFreeHeap_) {
        minFreeHeap_ = currentFree;
    }
}

void SystemManager::checkWatchdog() {
    uint32_t now = millis();
    if (now - lastWatchdogFeed_ > (WATCHDOG_TIMEOUT - 5000)) {
        // Auto-feed watchdog if it's getting close to timeout
        feedWatchdog();
    }
}

void SystemManager::updatePWMTimers() {
    // Check PWM IO2 timer
    if (pwmController_ && pwmController_->isTimerActive()) {
        pwmController_->checkTimer();
    }
    
    // Check PWM MOSFET timer
    if (pwmControllerMOSFET_ && pwmControllerMOSFET_->isTimerActive()) {
        pwmControllerMOSFET_->checkTimer();
    }
}

void SystemManager::updateStatusLED() {
    // Status LED is now handled by StatusLED class automatically
    // This function kept for compatibility but does nothing
}

bool SystemManager::initializeMCP4725() {
    // Note: Logger is nullptr - MCP4725 will use Serial.println as fallback
    // This avoids issues with static logger initialization
    
    // Create MCP4725 instance with 3.3V supply and 3.06x amplifier gain (LM358)
    // This allows output voltage range of 0-10.1V
    mcp4725_ = new MCP4725(nullptr, 3.3, 3.06);
    
    if (!mcp4725_) {
        Serial.println("[SystemManager] Failed to allocate MCP4725 instance");
        return false;
    }
    
    // Initialize DAC with default I2C address
    if (!mcp4725_->initialize(MCP4725_I2C_ADDRESS)) {
        Serial.println("[SystemManager] Failed to initialize MCP4725 at address 0x" + String(MCP4725_I2C_ADDRESS, HEX));
        delete mcp4725_;
        mcp4725_ = nullptr;
        return false;
    }
    
    // Note: MQTT manager will be set later by LeafNode after MQTT initialization
    Serial.println("[SystemManager] MCP4725 DAC initialized at address 0x" + String(MCP4725_I2C_ADDRESS, HEX));
    Serial.println("[SystemManager] Output voltage range: 0-10.1V (via 3.06x amplifier)");
    
    return true;
}

bool SystemManager::initializePWMController() {
    // Create PWM Controller instance
    pwmController_ = new PWMController(nullptr);
    
    if (!pwmController_) {
        Serial.println("[SystemManager] Failed to allocate PWM Controller instance");
        return false;
    }
    
    // Initialize PWM Controller with configured parameters
    if (!pwmController_->initialize(PWM_IO2_PIN, PWM_IO2_CHANNEL, PWM_IO2_FREQUENCY, PWM_IO2_RESOLUTION)) {
        Serial.println("[SystemManager] Failed to initialize PWM Controller on GPIO" + String(PWM_IO2_PIN));
        delete pwmController_;
        pwmController_ = nullptr;
        return false;
    }
    
    uint16_t maxValue = (1 << PWM_IO2_RESOLUTION) - 1;  // 2^resolution - 1
    Serial.println("[SystemManager] PWM Controller initialized on GPIO" + String(PWM_IO2_PIN) + 
                   " (" + String(PWM_IO2_FREQUENCY) + "Hz, " + String(PWM_IO2_RESOLUTION) + "-bit, 0-" + String(maxValue) + ")");
    
    return true;
}

bool SystemManager::initializePWMControllerMOSFET() {
    // Create PWM Controller instance for MOSFET
    pwmControllerMOSFET_ = new PWMController(nullptr);
    
    if (!pwmControllerMOSFET_) {
        Serial.println("[SystemManager] Failed to allocate PWM Controller MOSFET instance");
        return false;
    }
    
    // Initialize PWM Controller with configured parameters for MOSFET
    if (!pwmControllerMOSFET_->initialize(PWM_MOSFET_PIN, PWM_MOSFET_CHANNEL, PWM_MOSFET_FREQUENCY, PWM_MOSFET_RESOLUTION)) {
        Serial.println("[SystemManager] Failed to initialize PWM Controller MOSFET on GPIO" + String(PWM_MOSFET_PIN));
        delete pwmControllerMOSFET_;
        pwmControllerMOSFET_ = nullptr;
        return false;
    }
    
    uint16_t maxValue = (1 << PWM_MOSFET_RESOLUTION) - 1;  // 2^resolution - 1
    Serial.println("[SystemManager] PWM Controller MOSFET initialized on GPIO" + String(PWM_MOSFET_PIN) + 
                   " (" + String(PWM_MOSFET_FREQUENCY) + "Hz, " + String(PWM_MOSFET_RESOLUTION) + "-bit, 0-" + String(maxValue) + ")");
    
    return true;
}

#include "RuntimeConfig.h"
#include <Preferences.h>
#include "../LeafNodeTypes.h"
#include "config.h"

// Forward declaration helper functions for LogLevel conversion
String logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:   return "TRACE";
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:               return "INFO";
    }
}

LogLevel stringToLogLevel(const String& levelStr) {
    String upper = levelStr;
    upper.toUpperCase();
    
    if (upper == "TRACE")   return LogLevel::TRACE;
    if (upper == "DEBUG")   return LogLevel::DEBUG;
    if (upper == "INFO")    return LogLevel::INFO;
    if (upper == "WARNING" || upper == "WARN") return LogLevel::WARNING;
    if (upper == "ERROR")   return LogLevel::ERROR;
    if (upper == "FATAL")   return LogLevel::FATAL;
    
    return LogLevel::INFO; // Default
}

// Helper functions for SensorProfile conversion
String RuntimeConfig::sensorProfileToString(SensorProfile profile) const {
    switch (profile) {
        case SensorProfile::NONE:       return "NONE";
        case SensorProfile::SLT5007:    return "SLT5007";
        case SensorProfile::SHT31:      return "SHT31";
        case SensorProfile::CWTPSS:     return "CWTPSS";
        case SensorProfile::LEAFTHSN:   return "LEAFTHSN";
        case SensorProfile::CWTSOILTHS: return "CWTSOILTHS";
        case SensorProfile::TEROS12:    return "TEROS12";
        case SensorProfile::EZOPH:      return "EZOPH";
        case SensorProfile::EZOEC:      return "EZOEC";
        case SensorProfile::DS18B20:    return "DS18B20";
        case SensorProfile::CWTTHXXS:   return "CWTTHXXS";
        default:                        return "UNKNOWN";
    }
}

SensorProfile RuntimeConfig::stringToSensorProfile(const String& profileStr) const {
    String upper = profileStr;
    upper.toUpperCase();
    
    if (upper == "NONE")       return SensorProfile::NONE;
    if (upper == "SLT5007")    return SensorProfile::SLT5007;
    if (upper == "SHT31")      return SensorProfile::SHT31;
    if (upper == "CWTPSS")     return SensorProfile::CWTPSS;
    if (upper == "LEAFTHSN")   return SensorProfile::LEAFTHSN;
    if (upper == "CWTSOILTHS") return SensorProfile::CWTSOILTHS;
    if (upper == "TEROS12")    return SensorProfile::TEROS12;
    if (upper == "EZOPH")      return SensorProfile::EZOPH;
    if (upper == "EZOEC")      return SensorProfile::EZOEC;
    if (upper == "DS18B20")    return SensorProfile::DS18B20;
    if (upper == "CWTTHXXS")   return SensorProfile::CWTTHXXS;
    
    return SensorProfile::NONE; // Default
}

RuntimeConfig::RuntimeConfig() : configDoc_(2048), loaded_(false), dirty_(false), 
                                 setupStartTime_(0), setupTimeoutActive_(false) {
    setDefaults();
}

RuntimeConfig::~RuntimeConfig() {
    // Don't auto-save in destructor to avoid unnecessary flash writes
    // Config is explicitly saved after actual changes via command handlers
    if (dirty_) {
        Serial.println("[WARNING] RuntimeConfig destructed with unsaved changes (dirty flag set)");
    }
}

bool RuntimeConfig::load() {
    if (!loadFromPreferences()) {
        setDefaults();
        loadSerialNumberFromFactory(); // Load serial number from factory partition
        return false;
    }
    
    // Always try to load serial number from factory, even if config exists
    loadSerialNumberFromFactory();
    
    // Always update firmware version to current version after loading from NVS
    // This ensures the version in config matches the actual running firmware
    configDoc_["device"]["version"] = FIRMWARE_VERSION;
    
    loaded_ = true;
    dirty_ = false;
    return validate();
}

bool RuntimeConfig::save() {
    if (!saveToPreferences()) {
        return false;
    }
    
    dirty_ = false;
    return true;
}

void RuntimeConfig::resetToDefaults() {
    setDefaults();
    dirty_ = true;
}

bool RuntimeConfig::validate() const {
    // Validate device name
    String deviceName = getDeviceName();
    if (deviceName.length() == 0 || deviceName.length() > MAX_DEVICE_NAME_LENGTH) {
        return false;
    }
    
    // Validate serial number
    String serialNumber = getSerialNumber();
    if (serialNumber.length() == 0 || serialNumber.length() > MAX_SERIAL_NUMBER_LENGTH) {
        return false;
    }
    
    // Validate heartbeat interval
    uint32_t interval = getHeartbeatInterval();
    if (interval < 1000 || interval > 300000) { // 1 second to 5 minutes
        return false;
    }
    
    return true;
}

bool RuntimeConfig::setFromJson(const DynamicJsonDocument& json) {
    configDoc_ = json;
    dirty_ = true;
    return validate();
}

String RuntimeConfig::getDeviceName() const {
    return configDoc_["device"]["name"] | generateDefaultDeviceName();
}

String RuntimeConfig::getSerialNumber() const {
    return configDoc_["device"]["serial_number"] | "UNKNOWN";
}

String RuntimeConfig::getWiFiSSID() const {
    return configDoc_["wifi"]["ssid"] | "";
}

String RuntimeConfig::getWiFiPassword() const {
    return configDoc_["wifi"]["password"] | "";
}

String RuntimeConfig::getBLEKey() const {
    return configDoc_["ble"]["key"] | "DEFAULT_KEY";
}

LogLevel RuntimeConfig::getLogLevel() const {
    String levelStr = configDoc_["logging"]["level"] | "INFO";
    return stringToLogLevel(levelStr);
}

uint32_t RuntimeConfig::getHeartbeatInterval() const {
    return configDoc_["system"]["heartbeat_interval"] | HEARTBEAT_INTERVAL;
}

bool RuntimeConfig::isDebugMode() const {
    return configDoc_["system"]["debug_mode"] | false;
}

bool RuntimeConfig::hasWiFiCredentials() const {
    String ssid = getWiFiSSID();
    String password = getWiFiPassword();
    return !ssid.isEmpty() && !password.isEmpty();
}

bool RuntimeConfig::hasValidSensorConfiguration() const {
    // Fresh device (not loaded from storage) needs Factory Mode configuration
    // Even if defaults set sensor to NONE, user must explicitly configure it
    if (!loaded_) {
        return false;
    }
    
    // Configuration was loaded from storage - check if sensor profile is set
    // NONE is a valid configuration (for devices without sensors)
    if (!configDoc_["sensor"]["profile"].is<String>()) {
        return false;
    }
    String profile = configDoc_["sensor"]["profile"] | "";
    return !profile.isEmpty();
}

bool RuntimeConfig::isFactoryMode() const {
    // Factory Mode wenn die essentiellen Komponenten fehlen:
    // - Serial Number (sollte aus Factory Partition kommen)
    // - MQTT Configuration (Server-Verbindung)
    // - Sensor Configuration (welcher Sensor ist angeschlossen, NONE ist erlaubt)
    // WiFi ist OPTIONAL - kann später über BLE konfiguriert werden
    
    bool hasSerial = !getSerialNumber().isEmpty() && getSerialNumber() != "UNKNOWN";
    bool hasValidSensor = hasValidSensorConfiguration();
    
    // Factory Mode wenn Serial ODER MQTT ODER Sensor fehlt
    return !hasSerial || !hasMQTTCredentials() || !hasValidSensor;
}

void RuntimeConfig::setDeviceName(const String& name) {
    configDoc_["device"]["name"] = name;
    dirty_ = true;
}

void RuntimeConfig::setSerialNumber(const String& serialNumber) {
    configDoc_["device"]["serial_number"] = serialNumber;
    dirty_ = true;
}

void RuntimeConfig::setWiFiCredentials(const String& ssid, const String& password) {
    // Create temporary copies to ensure proper memory allocation
    String tempSsid = ssid;
    String tempPassword = password;
    configDoc_["wifi"]["ssid"] = tempSsid;
    configDoc_["wifi"]["password"] = tempPassword;
    dirty_ = true;
}

void RuntimeConfig::setBLEKey(const String& bleKey) {
    configDoc_["ble"]["key"] = bleKey;
    dirty_ = true;
}

void RuntimeConfig::setLogLevel(LogLevel level) {
    configDoc_["logging"]["level"] = logLevelToString(level);
    dirty_ = true;
}

void RuntimeConfig::setHeartbeatInterval(uint32_t interval) {
    configDoc_["system"]["heartbeat_interval"] = interval;
    dirty_ = true;
}

void RuntimeConfig::setDebugMode(bool enabled) {
    configDoc_["system"]["debug_mode"] = enabled;
    dirty_ = true;
}

void RuntimeConfig::setDefaults() {
    configDoc_.clear();
    
    // Device configuration
    configDoc_["device"]["name"] = generateDefaultDeviceName();
    configDoc_["device"]["type"] = "leaf-node";
    configDoc_["device"]["version"] = FIRMWARE_VERSION;
    configDoc_["device"]["setup_complete"] = false; // Device needs initial setup
    
    // System configuration
    configDoc_["system"]["heartbeat_interval"] = HEARTBEAT_INTERVAL;
    configDoc_["system"]["debug_mode"] = false;
    configDoc_["system"]["auto_update"] = true;
    
    // Logging configuration
    configDoc_["logging"]["level"] = "INFO";
    configDoc_["logging"]["enable_serial"] = true;
    configDoc_["logging"]["enable_timestamp"] = true;
    
    // WiFi configuration (empty by default)
    configDoc_["wifi"]["ssid"] = "";
    configDoc_["wifi"]["password"] = "";
    configDoc_["wifi"]["auto_connect"] = true;
    
    // BLE configuration
    configDoc_["ble"]["key"] = "LeafNode2025";
    configDoc_["ble"]["device_name"] = getDeviceName();
    configDoc_["ble"]["enabled"] = true;
    
    // MQTT configuration (empty by default, set by factory firmware)
    configDoc_["mqtt"]["server"] = "";
    configDoc_["mqtt"]["port"] = 1883;
    configDoc_["mqtt"]["username"] = "";
    configDoc_["mqtt"]["password"] = "";
    configDoc_["mqtt"]["client_id"] = "";
    configDoc_["mqtt"]["auto_connect"] = true;
    configDoc_["mqtt"]["enabled"] = true;
    
    // Sensor configuration (no sensor configured by default - user must select in Factory Mode)
    configDoc_["sensor"]["profile"] = "NONE";
    configDoc_["sensor"]["name"] = "None";
    configDoc_["sensor"]["reading_interval"] = 60000; // 1 minute default fallback
    configDoc_["sensor"]["enabled"] = true;
    
    // For testing: Set test MQTT configuration (only in Development Mode)
    #ifndef PRODUCTION_MODE
    // Removed: Test config should not be set by default
    // User should configure via Factory Mode or set explicitly
    #endif
    
    dirty_ = true;
}

bool RuntimeConfig::loadFromPreferences() {
    Preferences prefs;
    if (!prefs.begin("leafnode", true)) { // read-only
        return false;
    }
    
    String configJson = prefs.getString("config", "");
    prefs.end();
    
    if (configJson.isEmpty()) {
        return false;
    }
    
    DeserializationError error = deserializeJson(configDoc_, configJson);
    return error == DeserializationError::Ok;
}

bool RuntimeConfig::saveToPreferences() {
    String configJson;
    serializeJson(configDoc_, configJson);
    
    Serial.println("[DEBUG] Config JSON size: " + String(configJson.length()) + " bytes");
    Serial.println("[DEBUG] Config JSON: " + configJson);
    
    Preferences prefs;
    if (!prefs.begin("leafnode", false)) { // read-write
        Serial.println("[ERROR] Failed to open NVS namespace 'leafnode'");
        return false;
    }
    
    bool success = prefs.putString("config", configJson);
    if (!success) {
        Serial.println("[ERROR] Failed to save config to NVS");
    } else {
        Serial.println("[DEBUG] Config saved successfully to NVS");
    }
    prefs.end();
    
    return success;
}

bool RuntimeConfig::loadSerialNumberFromFactory() {
    Preferences factoryPrefs;
    
    // Try to open factory partition (read-only)
    if (!factoryPrefs.begin("factory", true)) {
        // No factory partition found, use fallback with SN- prefix
        String macSerial = String((uint32_t)ESP.getEfuseMac(), HEX);
        macSerial.toUpperCase();
        configDoc_["device"]["serial_number"] = "SN-" + macSerial;
        return false;
    }
    
    String serialNumber = factoryPrefs.getString("serial_number", "");
    factoryPrefs.end();
    
    if (serialNumber.isEmpty()) {
        // No serial number in factory partition, use fallback with SN- prefix
        String macSerial = String((uint32_t)ESP.getEfuseMac(), HEX);
        macSerial.toUpperCase();
        configDoc_["device"]["serial_number"] = "SN-" + macSerial;
        return false;
    }
    
    // Validate serial number format (basic check)
    if (serialNumber.length() > MAX_SERIAL_NUMBER_LENGTH) {
        configDoc_["device"]["serial_number"] = "SN-INVALID";
        return false;
    }
    
    // Set the serial number from factory
    configDoc_["device"]["serial_number"] = serialNumber;
    return true;
}

String RuntimeConfig::generateDefaultDeviceName() const {
    uint64_t chipId = ESP.getEfuseMac();
    return "LeafNode-" + String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX);
}

bool RuntimeConfig::isWiFiAutoConnect() const {
    return configDoc_["wifi"]["auto_connect"] | true;
}

void RuntimeConfig::setWiFiSSID(const String& ssid) {
    // Create a temporary copy to ensure proper memory allocation
    String temp = ssid;
    configDoc_["wifi"]["ssid"] = temp;
    dirty_ = true;
}

void RuntimeConfig::setWiFiPassword(const String& password) {
    // Create a temporary copy to ensure proper memory allocation
    String temp = password;
    configDoc_["wifi"]["password"] = temp;
    dirty_ = true;
}

void RuntimeConfig::setWiFiAutoConnect(bool autoConnect) {
    configDoc_["wifi"]["auto_connect"] = autoConnect;
    dirty_ = true;
}

void RuntimeConfig::resetWiFiCredentials() {
    // Clear WiFi credentials in memory
    configDoc_["wifi"]["ssid"] = "";
    configDoc_["wifi"]["password"] = "";
    configDoc_["wifi"]["auto_connect"] = false;
    dirty_ = true;
    
    // Also clear from persistent storage immediately
    Preferences prefs;
    if (prefs.begin("leafnode", false)) { // read-write mode
        prefs.remove("config"); // Remove entire config to force reload
        prefs.end();
    }
}

// MQTT configuration getters
String RuntimeConfig::getMQTTServer() const {
    return configDoc_["mqtt"]["server"] | "";
}

int RuntimeConfig::getMQTTPort() const {
    return configDoc_["mqtt"]["port"] | 1883;
}

String RuntimeConfig::getMQTTUsername() const {
    return configDoc_["mqtt"]["username"] | "";
}

String RuntimeConfig::getMQTTPassword() const {
    return configDoc_["mqtt"]["password"] | "";
}

String RuntimeConfig::getMQTTClientId() const {
    return configDoc_["mqtt"]["client_id"] | "";
}

bool RuntimeConfig::hasMQTTCredentials() const {
    String server = getMQTTServer();
    String username = getMQTTUsername();
    String password = getMQTTPassword();
    return !server.isEmpty() && !username.isEmpty() && !password.isEmpty();
}

bool RuntimeConfig::isMQTTAutoConnect() const {
    return configDoc_["mqtt"]["auto_connect"] | true;
}

// MQTT configuration setters
void RuntimeConfig::setMQTTCredentials(const String& server, int port, const String& username, const String& password) {
    configDoc_["mqtt"]["server"] = server;
    configDoc_["mqtt"]["port"] = port;
    configDoc_["mqtt"]["username"] = username;
    configDoc_["mqtt"]["password"] = password;
    dirty_ = true;
}

void RuntimeConfig::setMQTTServer(const String& server) {
    configDoc_["mqtt"]["server"] = server;
    dirty_ = true;
}

void RuntimeConfig::setMQTTPort(int port) {
    configDoc_["mqtt"]["port"] = port;
    dirty_ = true;
}

void RuntimeConfig::setMQTTUsername(const String& username) {
    configDoc_["mqtt"]["username"] = username;
    dirty_ = true;
}

void RuntimeConfig::setMQTTPassword(const String& password) {
    configDoc_["mqtt"]["password"] = password;
    dirty_ = true;
}

void RuntimeConfig::setMQTTClientId(const String& clientId) {
    configDoc_["mqtt"]["client_id"] = clientId;
    dirty_ = true;
}

void RuntimeConfig::setMQTTAutoConnect(bool autoConnect) {
    configDoc_["mqtt"]["auto_connect"] = autoConnect;
    dirty_ = true;
}

void RuntimeConfig::resetMQTTCredentials() {
    // Clear MQTT credentials in memory
    configDoc_["mqtt"]["server"] = "";
    configDoc_["mqtt"]["port"] = 1883;
    configDoc_["mqtt"]["username"] = "";
    configDoc_["mqtt"]["password"] = "";
    configDoc_["mqtt"]["client_id"] = "";
    configDoc_["mqtt"]["auto_connect"] = false;
    dirty_ = true;
}

// Device setup status methods
bool RuntimeConfig::isDeviceSetup() const {
    return configDoc_["device"]["setup_complete"] | false;
}

void RuntimeConfig::setDeviceSetup(bool setup) {
    configDoc_["device"]["setup_complete"] = setup;
    dirty_ = true;
}

void RuntimeConfig::resetDeviceSetup() {
    configDoc_["device"]["setup_complete"] = false;
    // Note: DO NOT clear MQTT password here - we want to keep MQTT config
    // when timeout occurs, only WiFi should be reset to allow reconfiguration
    dirty_ = true;
}

// Setup timeout methods

// Setup timeout methods
void RuntimeConfig::startSetupTimeout() {
    setupStartTime_ = millis();
    setupTimeoutActive_ = true;
    setDeviceSetup(false); // Mark as not setup
}

bool RuntimeConfig::isSetupTimeoutExpired() const {
    if (!setupTimeoutActive_) {
        return false;
    }
    
    return (millis() - setupStartTime_) > SETUP_TIMEOUT_MS;
}

void RuntimeConfig::cancelSetupTimeout() {
    setupTimeoutActive_ = false;
    setDeviceSetup(true); // Mark as setup complete
}

bool RuntimeConfig::isInSetupMode() const {
    return setupTimeoutActive_ || !isDeviceSetup();
}

void RuntimeConfig::resetToSetupMode() {
    // Reset WiFi credentials
    resetWiFiCredentials();
    
    // Reset MQTT credentials (optional - keep for reconnection)
    // resetMQTTCredentials();
    
    // Reset device setup status
    resetDeviceSetup();
    
    // Cancel any active timeout
    setupTimeoutActive_ = false;
    
    // Save changes
    save();
}

// MQTT Topic helpers - construct topics using serial number from config.h
String RuntimeConfig::getMQTTTopicRegister() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_REGISTER;
    return topic;
}

String RuntimeConfig::getMQTTTopicRegistrationAck() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_REGISTER_ACK;
    return topic;
}

String RuntimeConfig::getMQTTTopicStatus() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_STATUS;
    return topic;
}

String RuntimeConfig::getMQTTTopicHeartbeat() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_HEARTBEAT;
    return topic;
}

String RuntimeConfig::getMQTTTopicCommand() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_COMMAND;
    return topic;
}

String RuntimeConfig::getMQTTTopicCommandResponse() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_COMMAND_RESPONSE;
    return topic;
}

String RuntimeConfig::getMQTTTopicCommands() const {
    String topic = MQTT_TOPIC_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_TOPIC_COMMANDS;
    return topic;
}

String RuntimeConfig::getSensorTopicData() const {
    String topic = MQTT_SENSOR_PREFIX;
    topic += getSerialNumber();
    topic += MQTT_SENSOR_DATA;
    return topic;
}

// Sensor configuration getters
SensorProfile RuntimeConfig::getSensorProfile() const {
    if (configDoc_.containsKey("sensor") && configDoc_["sensor"].containsKey("profile")) {
        return stringToSensorProfile(configDoc_["sensor"]["profile"].as<String>());
    }
    return SensorProfile::NONE;
}

String RuntimeConfig::getSensorName() const {
    if (configDoc_.containsKey("sensor") && configDoc_["sensor"].containsKey("name")) {
        return configDoc_["sensor"]["name"].as<String>();
    }
    return "None";
}

uint32_t RuntimeConfig::getSensorReadingInterval() const {
    if (configDoc_.containsKey("sensor") && configDoc_["sensor"].containsKey("reading_interval")) {
        return configDoc_["sensor"]["reading_interval"].as<uint32_t>();
    }
    return 60000; // Default: 60 seconds
}

bool RuntimeConfig::hasSensorConfiguration() const {
    return getSensorProfile() != SensorProfile::NONE;
}

// Sensor configuration setters
void RuntimeConfig::setSensorProfile(SensorProfile profile) {
    if (!configDoc_.containsKey("sensor")) {
        configDoc_.createNestedObject("sensor");
    }
    configDoc_["sensor"]["profile"] = sensorProfileToString(profile);
    dirty_ = true;
}

void RuntimeConfig::setSensorName(const String& name) {
    if (!configDoc_.containsKey("sensor")) {
        configDoc_.createNestedObject("sensor");
    }
    configDoc_["sensor"]["name"] = name;
    dirty_ = true;
}

void RuntimeConfig::setSensorReadingInterval(uint32_t interval) {
    if (!configDoc_.containsKey("sensor")) {
        configDoc_.createNestedObject("sensor");
    }
    configDoc_["sensor"]["reading_interval"] = interval;
    dirty_ = true;
}

void RuntimeConfig::resetSensorConfiguration() {
    if (configDoc_.containsKey("sensor")) {
        configDoc_.remove("sensor");
        dirty_ = true;
    }
}

// UART Chain Position methods
uint8_t RuntimeConfig::getChainPosition() const {
    if (configDoc_.containsKey("chain") && configDoc_["chain"].containsKey("position")) {
        return configDoc_["chain"]["position"].as<uint8_t>();
    }
    return 0; // 0 = unknown/not discovered
}

void RuntimeConfig::setChainPosition(uint8_t position) {
    if (!configDoc_.containsKey("chain")) {
        configDoc_.createNestedObject("chain");
    }
    configDoc_["chain"]["position"] = position;
    dirty_ = true;
}

bool RuntimeConfig::hasChainPosition() const {
    return getChainPosition() > 0;
}
// Schedule configuration getters
bool RuntimeConfig::getScheduleActive() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("active")) {
        return configDoc_["schedule"]["active"].as<bool>();
    }
    return false;
}

String RuntimeConfig::getScheduleActuatorType() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("actuator_type")) {
        return configDoc_["schedule"]["actuator_type"].as<String>();
    }
    return "";
}

String RuntimeConfig::getScheduleOnAt() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("on_at")) {
        return configDoc_["schedule"]["on_at"].as<String>();
    }
    return "";
}

String RuntimeConfig::getScheduleOffAt() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("off_at")) {
        return configDoc_["schedule"]["off_at"].as<String>();
    }
    return "";
}

uint16_t RuntimeConfig::getScheduleValidDays() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("valid_days")) {
        return configDoc_["schedule"]["valid_days"].as<uint16_t>();
    }
    return 0;
}

time_t RuntimeConfig::getScheduleStartTime() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("start_time")) {
        return configDoc_["schedule"]["start_time"].as<time_t>();
    }
    return 0;
}

// Schedule configuration setters
void RuntimeConfig::setScheduleActive(bool active) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["active"] = active;
    dirty_ = true;
}

void RuntimeConfig::setScheduleActuatorType(const String& type) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["actuator_type"] = type;
    dirty_ = true;
}

void RuntimeConfig::setScheduleOnAt(const String& onAt) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["on_at"] = onAt;
    dirty_ = true;
}

void RuntimeConfig::setScheduleOffAt(const String& offAt) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["off_at"] = offAt;
    dirty_ = true;
}

void RuntimeConfig::setScheduleValidDays(uint16_t days) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["valid_days"] = days;
    dirty_ = true;
}

void RuntimeConfig::setScheduleStartTime(time_t startTime) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["start_time"] = (uint32_t)startTime;
    dirty_ = true;
}

// Timezone configuration
int32_t RuntimeConfig::getTimezone() const {
    if (configDoc_.containsKey("time") && configDoc_["time"].containsKey("timezone")) {
        return configDoc_["time"]["timezone"].as<int32_t>();
    }
    return 0; // UTC default
}

void RuntimeConfig::setTimezone(int32_t timezone) {
    if (!configDoc_.containsKey("time")) {
        configDoc_.createNestedObject("time");
    }
    configDoc_["time"]["timezone"] = timezone;
    dirty_ = true;
}

// Actuator state persistence (for power-loss recovery)
bool RuntimeConfig::getMosfetState() const {
    if (configDoc_.containsKey("actuator") && configDoc_["actuator"].containsKey("mosfet_state")) {
        return configDoc_["actuator"]["mosfet_state"].as<bool>();
    }
    return false; // Default OFF
}

bool RuntimeConfig::getRelayState() const {
    if (configDoc_.containsKey("actuator") && configDoc_["actuator"].containsKey("relay_state")) {
        return configDoc_["actuator"]["relay_state"].as<bool>();
    }
    return false; // Default OFF
}

void RuntimeConfig::setMosfetState(bool state) {
    if (!configDoc_.containsKey("actuator")) {
        configDoc_.createNestedObject("actuator");
    }
    configDoc_["actuator"]["mosfet_state"] = state;
    dirty_ = true;
}
// DAC Schedule configuration getters
// DAC Schedule configuration getters
float RuntimeConfig::getScheduleOnValue() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("on_value")) {
        return configDoc_["schedule"]["on_value"].as<float>();
    }
    return 0.0f;
}

float RuntimeConfig::getScheduleOffValue() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("off_value")) {
        return configDoc_["schedule"]["off_value"].as<float>();
    }
    return 0.0f;
}

uint32_t RuntimeConfig::getScheduleRampSeconds() const {
    if (configDoc_.containsKey("schedule") && configDoc_["schedule"].containsKey("ramp_seconds")) {
        return configDoc_["schedule"]["ramp_seconds"].as<uint32_t>();
    }
    return 0; // Default: no ramping
}

uint16_t RuntimeConfig::getLastDACValue() const {
    if (configDoc_.containsKey("actuator") && configDoc_["actuator"].containsKey("last_dac_value")) {
        return configDoc_["actuator"]["last_dac_value"].as<uint16_t>();
    }
    return 0;
}

// DAC Schedule configuration setters
void RuntimeConfig::setScheduleOnValue(float value) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["on_value"] = value;
    dirty_ = true;
}

void RuntimeConfig::setScheduleOffValue(float value) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["off_value"] = value;
    dirty_ = true;
}

void RuntimeConfig::setScheduleRampSeconds(uint32_t seconds) {
    if (!configDoc_.containsKey("schedule")) {
        configDoc_.createNestedObject("schedule");
    }
    configDoc_["schedule"]["ramp_seconds"] = seconds;
    dirty_ = true;
}

void RuntimeConfig::setLastDACValue(uint16_t value) {
    if (!configDoc_.containsKey("actuator")) {
        configDoc_.createNestedObject("actuator");
    }
    configDoc_["actuator"]["last_dac_value"] = value;
    dirty_ = true;
}

void RuntimeConfig::setRelayState(bool state) {
    if (!configDoc_.containsKey("actuator")) {
        configDoc_.createNestedObject("actuator");
    }
    configDoc_["actuator"]["relay_state"] = state;
    dirty_ = true;
}
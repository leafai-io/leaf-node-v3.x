#include "WebServerManager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"

WebServerManager::WebServerManager(RuntimeConfig* config, Logger* logger)
    : config_(config)
    , logger_(logger)
    , server_(nullptr)
    , running_(false) {
}

WebServerManager::~WebServerManager() {
    stop();
    if (server_) {
        delete server_;
    }
}

bool WebServerManager::initialize() {
    if (!config_ || !logger_) {
        return false;
    }
    
    logger_->info("WebServer", "Initializing Factory Mode Web Server");
    
    // Create web server instance
    server_ = new WebServer(FACTORY_WEBSERVER_PORT);
    
    if (!server_) {
        logger_->error("WebServer", "Failed to create server instance");
        return false;
    }
    
    // Register endpoint handlers
    server_->on("/api/config/wifi", HTTP_POST, [this]() { handleConfigWiFi(); });
    server_->on("/api/config/mqtt", HTTP_POST, [this]() { handleConfigMQTT(); });
    server_->on("/api/config/serial", HTTP_POST, [this]() { handleConfigSerial(); });
    server_->on("/api/config/sensor", HTTP_POST, [this]() { handleConfigSensor(); });
    server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_->on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
    server_->onNotFound([this]() { handleNotFound(); });
    
    logger_->info("WebServer", "Endpoints registered");
    return true;
}

bool WebServerManager::start() {
    if (!server_) {
        logger_->error("WebServer", "Server not initialized");
        return false;
    }
    
    if (running_) {
        logger_->warning("WebServer", "Server already running");
        return true;
    }
    
    server_->begin();
    running_ = true;
    
    logger_->info("WebServer", "Server started on port " + String(FACTORY_WEBSERVER_PORT));
    logger_->info("WebServer", "Access at: http://192.168.4.1");
    return true;
}

void WebServerManager::stop() {
    if (server_ && running_) {
        server_->stop();
        running_ = false;
        logger_->info("WebServer", "Server stopped");
    }
}

void WebServerManager::handleClient() {
    if (server_ && running_) {
        server_->handleClient();
    }
}

void WebServerManager::handleConfigWiFi() {
    logger_->info("WebServer", "POST /api/config/wifi");
    
    if (!validateJsonBody()) {
        return;
    }
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, server_->arg("plain"));
    
    if (error) {
        sendError(400, "Invalid JSON format");
        return;
    }
    
    // Validate required fields
    if (!doc.containsKey("ssid") || !doc.containsKey("password")) {
        sendError(400, "Missing required fields: ssid, password");
        return;
    }
    
    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();
    
    // Validate lengths
    if (ssid.length() == 0 || ssid.length() > MAX_SSID_LENGTH) {
        sendError(400, "Invalid SSID length");
        return;
    }
    
    if (password.length() > MAX_PASSWORD_LENGTH) {
        sendError(400, "Invalid password length");
        return;
    }
    
    // Save WiFi credentials
    config_->setWiFiCredentials(ssid, password);
    config_->setWiFiAutoConnect(true);
    
    if (!config_->save()) {
        sendError(500, "Failed to save configuration");
        return;
    }
    
    logger_->info("WebServer", "WiFi credentials saved: " + ssid);
    sendJsonResponse(200, true, "WiFi credentials saved", "");
}

void WebServerManager::handleConfigMQTT() {
    logger_->info("WebServer", "POST /api/config/mqtt");
    
    if (!validateJsonBody()) {
        return;
    }
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, server_->arg("plain"));
    
    if (error) {
        sendError(400, "Invalid JSON format");
        return;
    }
    
    // Validate required fields
    if (!doc.containsKey("server") || !doc.containsKey("port")) {
        sendError(400, "Missing required fields: server, port");
        return;
    }
    
    String server = doc["server"].as<String>();
    int port = doc["port"].as<int>();
    String username = doc["username"] | "";
    String password = doc["password"] | "";
    
    // Validate
    if (server.length() == 0 || server.length() > MAX_MQTT_SERVER_LENGTH) {
        sendError(400, "Invalid server length");
        return;
    }
    
    if (port <= 0 || port > 65535) {
        sendError(400, "Invalid port number");
        return;
    }
    
    // Save MQTT configuration
    config_->setMQTTCredentials(server, port, username, password);
    config_->setMQTTAutoConnect(true);
    
    if (!config_->save()) {
        sendError(500, "Failed to save configuration");
        return;
    }
    
    logger_->info("WebServer", "MQTT configuration saved: " + server + ":" + String(port));
    sendJsonResponse(200, true, "MQTT configuration saved", "");
}

void WebServerManager::handleConfigSerial() {
    logger_->info("WebServer", "POST /api/config/serial");
    
    if (!validateJsonBody()) {
        return;
    }
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server_->arg("plain"));
    
    if (error) {
        sendError(400, "Invalid JSON format");
        return;
    }
    
    // Validate required field
    if (!doc.containsKey("serial_number")) {
        sendError(400, "Missing required field: serial_number");
        return;
    }
    
    String serialNumber = doc["serial_number"].as<String>();
    
    // Validate length
    if (serialNumber.length() == 0 || serialNumber.length() > MAX_SERIAL_NUMBER_LENGTH) {
        sendError(400, "Invalid serial number length");
        return;
    }
    
    // Save to NVS factory partition
    Preferences factoryPrefs;
    if (!factoryPrefs.begin("factory", false)) { // Read-write
        sendError(500, "Failed to open factory storage");
        return;
    }
    
    factoryPrefs.putString("serial_number", serialNumber);
    factoryPrefs.end();
    
    // Also update runtime config
    config_->setSerialNumber(serialNumber);
    
    if (!config_->save()) {
        sendError(500, "Failed to save configuration");
        return;
    }
    
    logger_->info("WebServer", "Serial number saved: " + serialNumber);
    sendJsonResponse(200, true, "Serial number updated", "");
}

void WebServerManager::handleConfigSensor() {
    logger_->info("WebServer", "POST /api/config/sensor");
    
    if (!validateJsonBody()) {
        return;
    }
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server_->arg("plain"));
    
    if (error) {
        sendError(400, "Invalid JSON format");
        return;
    }
    
    // Validate required field
    if (!doc.containsKey("sensor_type")) {
        sendError(400, "Missing required field: sensor_type");
        return;
    }
    
    String sensorType = doc["sensor_type"].as<String>();
    sensorType.toUpperCase();
    
    // Validate sensor type
    SensorProfile profile;
    if (sensorType == "SLT5007") {
        profile = SensorProfile::SLT5007;
    } else if (sensorType == "SHT31") {
        profile = SensorProfile::SHT31;
    } else if (sensorType == "CWTPSS") {
        profile = SensorProfile::CWTPSS;
    } else if (sensorType == "LEAFTHSN") {
        profile = SensorProfile::LEAFTHSN;
    } else if (sensorType == "CWTSOILTHS") {
        profile = SensorProfile::CWTSOILTHS;
    } else if (sensorType == "TEROS12") {
        profile = SensorProfile::TEROS12;
    } else if (sensorType == "EZOPH") {
        profile = SensorProfile::EZOPH;
    } else if (sensorType == "DS18B20") {
        profile = SensorProfile::DS18B20;
    } else if (sensorType == "NONE") {
        profile = SensorProfile::NONE;
    } else {
        sendError(400, "Invalid sensor type. Supported: SLT5007, SHT31, CWTPSS, LEAFTHSN, CWTSOILTHS, TEROS12, EZOPH, DS18B20, NONE");
        return;
    }
    
    // Save sensor configuration
    config_->setSensorProfile(profile);
    
    // Set sensor-specific defaults
    String sensorName;
    uint32_t defaultInterval;
    
    switch (profile) {
        case SensorProfile::SLT5007:
            sensorName = "SLT5007";
            defaultInterval = 180000; // 3 minutes
            break;
        case SensorProfile::SHT31:
            sensorName = "SHT31";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::CWTPSS:
            sensorName = "CWTPSS";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::LEAFTHSN:
            sensorName = "LEAFTHSN";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::CWTSOILTHS:
            sensorName = "CWTSoilTHS";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::TEROS12:
            sensorName = "TEROS12";
            defaultInterval = 180000; // 3 minutes
            break;
        case SensorProfile::EZOPH:
            sensorName = "EZOph";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::DS18B20:
            sensorName = "DS18B20";
            defaultInterval = 60000; // 1 minute
            break;
        case SensorProfile::NONE:
        default:
            sensorName = "None";
            defaultInterval = 60000; // 1 minute fallback
            break;
    }
    
    config_->setSensorProfile(profile);
    config_->setSensorName(sensorName);
    config_->setSensorReadingInterval(defaultInterval);
    
    if (!config_->save()) {
        sendError(500, "Failed to save sensor configuration");
        return;
    }
    
    logger_->info("WebServer", "Sensor configuration saved: " + sensorType + 
                 " (interval: " + String(defaultInterval) + "ms)");
    
    // Build response with configuration details
    DynamicJsonDocument responseData(256);
    responseData["sensor_type"] = sensorType;
    responseData["sensor_name"] = sensorName;
    responseData["reading_interval_ms"] = defaultInterval;
    responseData["reading_interval_min"] = defaultInterval / 60000.0;
    responseData["profile_id"] = static_cast<int>(profile);
    
    String responseJson;
    serializeJson(responseData, responseJson);
    
    sendJsonResponse(200, true, "Sensor configuration saved", responseJson);
}

void WebServerManager::handleStatus() {
    logger_->debug("WebServer", "GET /api/status");
    
    DynamicJsonDocument doc(1024);
    
    // Device info
    doc["device"]["name"] = config_->getDeviceName();
    doc["device"]["serial_number"] = config_->getSerialNumber();
    doc["device"]["firmware_version"] = FIRMWARE_VERSION;
    doc["device"]["manufacturer"] = MANUFACTURER;
    
    // System info
    doc["system"]["free_heap"] = ESP.getFreeHeap();
    doc["system"]["uptime"] = millis() / 1000;
    doc["system"]["chip_model"] = ESP.getChipModel();
    
    // Configuration status
    doc["config"]["has_wifi"] = config_->hasWiFiCredentials();
    doc["config"]["has_mqtt"] = config_->hasMQTTCredentials();
    doc["config"]["has_sensor"] = config_->hasSensorConfiguration();
    doc["config"]["factory_mode"] = config_->isFactoryMode();
    
    // WiFi info (if configured)
    if (config_->hasWiFiCredentials()) {
        doc["wifi"]["ssid"] = config_->getWiFiSSID();
        // Don't send password for security
    }
    
    // MQTT info (if configured)
    if (config_->hasMQTTCredentials()) {
        doc["mqtt"]["server"] = config_->getMQTTServer();
        doc["mqtt"]["port"] = config_->getMQTTPort();
        doc["mqtt"]["username"] = config_->getMQTTUsername();
        // Don't send password for security
    }
    
    // Sensor info (if configured)
    if (config_->hasSensorConfiguration()) {
        SensorProfile profile = config_->getSensorProfile();
        String profileName;
        
        switch (profile) {
            case SensorProfile::SLT5007:    profileName = "SLT5007"; break;
            case SensorProfile::SHT31:      profileName = "SHT31"; break;
            case SensorProfile::CWTPSS:     profileName = "CWTPSS"; break;
            case SensorProfile::LEAFTHSN:   profileName = "LEAFTHSN"; break;
            case SensorProfile::CWTSOILTHS: profileName = "CWTSoilTHS"; break;
            case SensorProfile::TEROS12:    profileName = "TEROS12"; break;
            case SensorProfile::EZOPH:      profileName = "EZOph"; break;
            case SensorProfile::NONE:       profileName = "None"; break;
            default:                        profileName = "Unknown"; break;
        }
        
        doc["sensor"]["type"] = profileName;
        doc["sensor"]["reading_interval_ms"] = config_->getSensorReadingInterval();
        doc["sensor"]["reading_interval_min"] = config_->getSensorReadingInterval() / 60000.0;
        doc["sensor"]["profile_id"] = static_cast<int>(profile);
    }
    
    String response;
    serializeJson(doc, response);
    
    server_->send(200, "application/json", response);
}

void WebServerManager::handleReboot() {
    logger_->info("WebServer", "POST /api/reboot - Rebooting device...");
    
    sendJsonResponse(200, true, "Device rebooting...", "");
    
    delay(1000); // Give time for response to be sent
    ESP.restart();
}

void WebServerManager::handleNotFound() {
    logger_->warning("WebServer", "404 Not Found: " + server_->uri());
    sendError(404, "Endpoint not found");
}

void WebServerManager::sendJsonResponse(int code, bool success, const String& message, const String& data) {
    DynamicJsonDocument doc(512);
    doc["success"] = success;
    doc["message"] = message;
    
    if (data.length() > 0) {
        doc["data"] = data;
    }
    
    String response;
    serializeJson(doc, response);
    
    server_->send(code, "application/json", response);
}

void WebServerManager::sendError(int code, const String& message) {
    logger_->error("WebServer", message);
    sendJsonResponse(code, false, message, "");
}

bool WebServerManager::validateJsonBody() {
    if (server_->method() != HTTP_POST) {
        sendError(405, "Method not allowed");
        return false;
    }
    
    if (!server_->hasArg("plain")) {
        sendError(400, "Missing request body");
        return false;
    }
    
    return true;
}

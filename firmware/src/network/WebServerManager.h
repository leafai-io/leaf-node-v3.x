#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "../runtime/RuntimeConfig.h"
#include "../diagnostics/Logger.h"

/**
 * @brief WebServer Manager for Factory Mode Configuration
 * 
 * Provides REST API endpoints for device configuration when in Factory Mode.
 * Endpoints:
 *   POST /api/config/wifi   - Set WiFi credentials
 *   POST /api/config/mqtt   - Set MQTT configuration
 *   POST /api/config/serial - Set serial number
 *   POST /api/config/sensor - Set sensor profile
 *   GET  /api/status        - Get device status
 *   POST /api/reboot        - Reboot device
 */
class WebServerManager {
public:
    /**
     * @brief Constructor
     * @param config Runtime configuration instance
     * @param logger Logger instance
     */
    WebServerManager(RuntimeConfig* config, Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~WebServerManager();
    
    /**
     * @brief Initialize web server
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Start web server
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop web server
     */
    void stop();
    
    /**
     * @brief Handle incoming web requests (call in loop)
     */
    void handleClient();
    
    /**
     * @brief Check if web server is running
     * @return true if running
     */
    bool isRunning() const { return running_; }

private:
    RuntimeConfig* config_;
    Logger* logger_;
    WebServer* server_;
    bool running_;
    
    // Endpoint handlers
    void handleConfigWiFi();
    void handleConfigMQTT();
    void handleConfigSerial();
    void handleConfigSensor();
    void handleStatus();
    void handleReboot();
    void handleNotFound();
    
    // Helper methods
    void sendJsonResponse(int code, bool success, const String& message, const String& data = "");
    void sendError(int code, const String& message);
    bool validateJsonBody();
};

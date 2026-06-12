#include "LeafNode.h"
#include "hardware/MCP4725.h"
#include "config.h"
#include <ArduinoJson.h>

// Static instance pointer for callback
LeafNode* LeafNode::instance_ = nullptr;

LeafNode::LeafNode() 
    : config_(nullptr)
    , systemManager_(nullptr)
    , taskManager_(nullptr)
    , commandHandler_(nullptr)
    , setupManager_(nullptr)
    , logger_(nullptr)
    , serialCommandHandler_(nullptr)
    , wifiManager_(nullptr)
    , bleConfigManager_(nullptr)
    , mqttManager_(nullptr)
    , otaManager_(nullptr)
    , statusLED_(nullptr)
    , rs485Manager_(nullptr)
    , sdi12Manager_(nullptr)
    , oneWireManager_(nullptr)
    , sensorManager_(nullptr)
    , actuator_(nullptr)
    , actuatorStatusPublisher_(nullptr)
    , scheduleManager_(nullptr)
    , initialized_(false)
    , factoryMode_(false)
    , configuredViaUART_(false)
    , lastHeartbeat_(0)
    , lastSensorReading_(0)
    , networkStatus_(NetworkStatus::DISCONNECTED)
    , temporaryUserId_("")
    , registrationRetryTime_(0)
    , registrationActive_(false)
    , registrationRetryInterval_(30000)
    , lastRegistrationAttempt_(0)
    , registrationAckReceived_(false)
    , lastWiFiReconnectAttempt_(0)
    , wifiReconnectInterval_(45000) // 45 seconds between WiFi reconnect attempts
    , wifiReconnectionEnabled_(false) {
    // Set static instance pointer for callback
    instance_ = this;
}

LeafNode::~LeafNode() {
    instance_ = nullptr;
    delete config_;
    delete systemManager_;
    delete taskManager_;
    delete commandHandler_;
    delete setupManager_;
    delete logger_;
    delete serialCommandHandler_;
    delete wifiManager_;
    delete bleConfigManager_;
    delete mqttManager_;
    delete otaManager_;
    delete rs485Manager_;
    delete sdi12Manager_;
    delete oneWireManager_;
    delete sensorManager_;
    delete actuator_;
    delete actuatorStatusPublisher_;
    delete scheduleManager_;
    // delete statusLED_;  // WS2812 disabled
}

bool LeafNode::initialize() {
    // Initialize Serial communication first
    Serial.begin(115200);
    delay(1000); // Give Serial time to initialize
    
    Serial.println();
    Serial.println("=== LeafNode Firmware Starting ===");
    
    // Create and initialize logger first
    logger_ = new Logger();
    if (!logger_->initialize(LogLevel::INFO, true)) {
        Serial.println("ERROR: Failed to initialize logger");
        return false;
    }
    
    logger_->info("LeafNode", "Starting initialization...");
    
    // Create and initialize configuration
    config_ = new RuntimeConfig();
    if (!config_->load()) {
        logger_->warning("LeafNode", "Failed to load configuration, using defaults");
        config_->resetToDefaults();
    }
    
    // Update logger level from configuration
    logger_->setLogLevel(config_->getLogLevel());
    
    // Check for Factory Mode
    factoryMode_ = config_->isFactoryMode();
    if (factoryMode_) {
        logger_->info("LeafNode", "⚙️  FACTORY MODE DETECTED");
        logger_->info("LeafNode", "Device has no configuration - starting factory setup");
    }
    
    // Create and initialize system manager
    systemManager_ = new SystemManager();
    if (!systemManager_->initialize()) {
        logger_->error("LeafNode", "Failed to initialize system manager");
        return false;
    }
    
    // Perform UART Chain Discovery (skip in Factory Mode)
    if (!factoryMode_) {
        logger_->info("LeafNode", "Starting UART Chain Discovery...");
        UARTChainManager* chainManager = systemManager_->getChainManager();
        UARTChainManager::DiscoveryStatus discoveryStatus = chainManager->discoverChainPosition(3000);  // 3 second timeout instead of 10
        
        if (discoveryStatus == UARTChainManager::DiscoveryStatus::COMPLETED) {
            uint8_t chainPos = chainManager->getChainPosition();
            logger_->info("LeafNode", "✓ Chain Discovery Complete - Position: " + String(chainPos));
            logger_->info("LeafNode", "  Left Neighbor:  " + String(chainManager->hasLeftNeighbor() ? "YES" : "NO"));
            logger_->info("LeafNode", "  Right Neighbor: " + String(chainManager->hasRightNeighbor() ? "YES" : "NO"));
            
            // Save chain position to runtime config
            config_->setChainPosition(chainPos);
            config_->save();
            logger_->info("LeafNode", "Chain position saved to configuration");
        } else {
            logger_->warning("LeafNode", "Chain Discovery failed or timed out - Status: " + chainManager->getStatusString());
            logger_->info("LeafNode", "Continuing without chain position...");
        }
    } else {
        logger_->info("LeafNode", "Skipping UART Chain Discovery in Factory Mode");
    }
    
    // Create and initialize task manager
    taskManager_ = new TaskManager();
    if (!taskManager_->initialize()) {
        logger_->error("LeafNode", "Failed to initialize task manager");
        return false;
    }
    
    // Create and initialize command handler
    commandHandler_ = new CommandHandler();
    commandHandler_->setLogger(logger_);
    commandHandler_->setSystemManager(systemManager_);
    commandHandler_->setRuntimeConfig(config_);
    if (!commandHandler_->initialize()) {
        logger_->error("LeafNode", "Failed to initialize command handler");
        return false;
    }
    
    // Set up logging with proper configuration
    setupLogging();
    
    // Initialize Serial Command Handler (always available for Factory Mode)
    serialCommandHandler_ = new SerialCommandHandler(config_, logger_, this);
    
    #ifndef PRODUCTION_MODE
    // Enable in Development Mode by default
    serialCommandHandler_->setEnabled(true);
    logger_->info("LeafNode", "📟 Serial Command Handler enabled (Development Mode)");
    logger_->info("LeafNode", "   Type 'help' for available commands");
    #endif
    
    // Validate configuration
    if (!validateConfiguration()) {
        logger_->error("LeafNode", "Configuration validation failed");
        return false;
    }
    
    // Print startup information
    printStartupInfo();
    
    // FACTORY MODE: Start AP + WebServer, skip normal initialization
    if (factoryMode_) {
        return initializeFactoryMode();
    }
    
    // NORMAL MODE: Initialize network components
    if (!initializeNetwork()) {
        logger_->error("LeafNode", "Failed to initialize network components");
        return false;
    }
    
    // Initialize MQTT
    if (!initializeMQTT()) {
        logger_->error("LeafNode", "Failed to initialize MQTT");
        return false;
    }
    
    // Add system heartbeat task
    taskManager_->addTask(
        "SystemHeartbeat",
        [this]() {
            logger_->debug("Heartbeat", "System alive - Uptime: " + 
                          String(systemManager_->getUptime() / 1000) + "s, Free heap: " + 
                          String(systemManager_->getFreeHeap()) + " bytes");
            systemManager_->feedWatchdog();
            lastHeartbeat_ = millis();
            
            // Publish MQTT heartbeat if connected
            publishHeartbeat();
        },
        config_->getHeartbeatInterval(),
        TaskPriority::NORMAL
    );
    
    // Note: Config is saved explicitly after changes via commands
    // No periodic auto-save to reduce flash wear
    
    // Create bus managers (lazy initialization - will be initialized on demand by sensor manager)
    rs485Manager_ = new RS485Manager(*logger_);
    logger_->info("Hardware", "RS485 manager created (will be initialized on demand)");
    
    sdi12Manager_ = new SDI12Manager(*logger_);
    logger_->info("Hardware", "SDI-12 manager created (will be initialized on demand)");
    
    oneWireManager_ = new OneWireManager(*logger_);
    logger_->info("Hardware", "OneWire manager created (will be initialized on demand)");
    
    // Create and initialize sensor manager
    // Note: Bus managers will be initialized automatically when a sensor is configured
    sensorManager_ = new SensorManager(config_, rs485Manager_, sdi12Manager_, oneWireManager_, mqttManager_, logger_);
    if (!sensorManager_->initialize()) {
        logger_->error("Hardware", "Failed to initialize sensor manager");
        return false;
    }
    logger_->info("Hardware", "Sensor manager initialized successfully");
    
    // Connect sensor manager to command handler
    commandHandler_->setSensorManager(sensorManager_);
    logger_->info("LeafNode", "Sensor manager connected to command handler");
    
    // Create and initialize actuator (MOSFET + Relay)
    actuator_ = new Actuator();
    if (!actuator_->initialize(MOSFET_PIN, RELAY_PIN)) {
        logger_->error("Hardware", "Failed to initialize actuators");
        return false;
    }
    logger_->info("Hardware", "Actuators initialized - MOSFET: pin " + String(MOSFET_PIN) + ", Relay: pin " + String(RELAY_PIN));
    
    // Connect actuator to system manager for LED control
    actuator_->setSystemManager(systemManager_);
    
    // Connect actuator to RuntimeConfig for state persistence
    actuator_->setRuntimeConfig(config_);
    
    // Set timer expired callback for MQTT notifications
    actuator_->setTimerExpiredCallback([this](Actuator::Type type, bool oldState) {
        if (mqttManager_ && mqttManager_->isConnected()) {
            // Publish timer expired event
            DynamicJsonDocument doc(256);
            doc["event"] = "timer_expired";
            doc["type"] = Actuator::typeToString(type);
            doc["previous_state"] = oldState ? "on" : "off";
            doc["current_state"] = "off";
            doc["timestamp"] = String(millis());
            
            String payload;
            serializeJson(doc, payload);
            
            String topic = String(MQTT_TOPIC_PREFIX) + config_->getSerialNumber() + "/actuator/event";
            mqttManager_->publish(topic, payload);
            
            logger_->info("Actuator", Actuator::typeToString(type) + " timer expired, turned OFF automatically");
            
            // Publish full status update to /status topic
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("timer_expired", "actuator", "", Actuator::typeToString(type));
            }
        }
        
        // Resume schedule when timer expires
        if (scheduleManager_ && scheduleManager_->isPaused()) {
            scheduleManager_->resumeSchedule();
            logger_->info("Schedule", "Schedule resumed after timer expired");
        }
    });
    
    // Set timer started callback to pause schedule
    actuator_->setTimerStartedCallback([this](Actuator::Type type, uint32_t duration) {
        if (scheduleManager_ && scheduleManager_->isScheduleActive()) {
            scheduleManager_->pauseSchedule();
            logger_->info("Schedule", "Schedule paused while timer is active");
        }
    });
    
    // Restore actuator states from persistent storage (power-loss recovery)
    actuator_->restoreStates();
    logger_->info("Hardware", "Actuator states restored from config");
    
    // Create actuator status publisher
    actuatorStatusPublisher_ = new ActuatorStatusPublisher(config_, mqttManager_, logger_, actuator_, systemManager_->getMCP4725());
    logger_->info("LeafNode", "Actuator status publisher initialized");
    
    // Create and initialize schedule manager
    scheduleManager_ = new ScheduleManager();
    if (!scheduleManager_->initialize(logger_, config_, actuator_, systemManager_->getMCP4725(), systemManager_->getPWMController(), systemManager_->getPWMControllerMOSFET(), actuatorStatusPublisher_)) {
        logger_->error("Schedule", "Failed to initialize schedule manager");
        return false;
    }
    logger_->info("Schedule", "Schedule manager initialized");
    
    // If WiFi is already connected, sync NTP time immediately
    if (wifiManager_ && wifiManager_->isConnected()) {
        scheduleManager_->syncTime();
        logger_->info("Schedule", "NTP time synced after schedule manager initialization");
    }
    
    // Connect actuator to command handler
    commandHandler_->setActuator(actuator_);
    commandHandler_->setActuatorStatusPublisher(actuatorStatusPublisher_);
    commandHandler_->setScheduleManager(scheduleManager_);
    logger_->info("LeafNode", "Actuator and schedule manager connected to command handler");
    
    // Connect MCP4725 DAC to command handler
    if (systemManager_->getMCP4725()) {
        commandHandler_->setMCP4725(systemManager_->getMCP4725());
        logger_->info("LeafNode", "MCP4725 DAC connected to command handler");
    } else {
        logger_->warning("LeafNode", "MCP4725 DAC not available");
    }
    
    // Connect PWM Controller (IO2) to command handler
    if (systemManager_->getPWMController()) {
        commandHandler_->setPWMController(systemManager_->getPWMController());
        logger_->info("LeafNode", "PWM Controller (IO2) connected to command handler");
    } else {
        logger_->warning("LeafNode", "PWM Controller (IO2) not available");
    }
    
    // Connect PWM Controller (MOSFET) to command handler
    if (systemManager_->getPWMControllerMOSFET()) {
        commandHandler_->setPWMControllerMOSFET(systemManager_->getPWMControllerMOSFET());
        logger_->info("LeafNode", "PWM Controller (MOSFET) connected to command handler");
    } else {
        logger_->warning("LeafNode", "PWM Controller (MOSFET) not available");
    }
    
    // Create and initialize setup manager
    setupManager_ = new SetupManager(*config_, *wifiManager_, *mqttManager_, *bleConfigManager_, *logger_);
    
    // Only start BLE setup if NOT configured via UART
    if (!configuredViaUART_) {
        setupManager_->begin();
    } else {
        logger_->info("LeafNode", "Config received via UART chain - skipping BLE setup");
        // Don't call begin() - SetupManager stays in default state and won't activate BLE
    }
    
    systemManager_->setStatus(SystemStatus::RUNNING);
    initialized_ = true;
    
    logger_->info("LeafNode", "Initialization completed successfully");
    
    // Note: LED behavior will be set after network initialization is complete
    // to avoid being overridden by BLE configuration status
    if (config_->isDeviceSetup()) {
        logger_->info("LeafNode", "Device setup complete - success indication will be shown after network init");
        // Mark registration as complete since device is already registered
        registrationAckReceived_ = true;
        registrationActive_ = false;
    } else {
        // Device needs setup - show blue LED for BLE configuration
        systemManager_->setNetworkLED(NetworkStatus::DISCONNECTED);
        logger_->info("LeafNode", "Device setup required - showing BLE configuration mode");
    }
    
    return true;
}

void LeafNode::update() {
    if (!initialized_) {
        return;
    }
    
    // HIGH PRIORITY: Check actuator timers FIRST (critical for microsecond precision)
    if (actuator_) {
        actuator_->update();
    }
    
    // Handle Serial Commands (always active for Factory Mode support)
    if (serialCommandHandler_) {
        serialCommandHandler_->update();
    }
    
    // FACTORY MODE: Only handle serial commands and LED
    if (factoryMode_) {
        if (systemManager_) {
            systemManager_->update();
            systemManager_->feedWatchdog();
        }
        // Update status LED in factory mode
        if (statusLED_) {
            statusLED_->update();
        }
        return;
    }
    
    // NORMAL MODE: Full update cycle
    // Update setup manager first
    if (setupManager_) {
        setupManager_->update();
    }
    
    // Update system manager
    systemManager_->update();
    
    // Check PWM timers (for duration-based PWM commands)
    systemManager_->updatePWMTimers();
    
    // Update network LED based on current network status
    systemManager_->setNetworkLED(networkStatus_);
    
    // Update task manager
    taskManager_->update();
    
    // HIGH PRIORITY: Check actuator timers again (for microsecond precision)
    if (actuator_) {
        actuator_->update();
    }
    
    // Update actuator (for timer management)
    if (actuator_) {
        actuator_->update();
    }
    
    // Update schedule manager (for time-based triggers)
    if (scheduleManager_) {
        scheduleManager_->update();
    }
    
    // HIGH PRIORITY: Check actuator timers again (for microsecond precision)
    if (actuator_) {
        actuator_->update();
    }
    
    // Update status LED
    if (statusLED_) {
        statusLED_->update();
    }
    
    // Update WiFi Manager (for reconnection handling)
    if (wifiManager_) {
        NetworkStatus prevWiFiStatus = wifiManager_->getStatus();
        wifiManager_->update();
        
        // Check if WiFi connection was lost
        if (prevWiFiStatus == NetworkStatus::WIFI_CONNECTED && 
            wifiManager_->getStatus() != NetworkStatus::WIFI_CONNECTED) {
            logger_->warning("Network", "WiFi connection lost, enabling reconnection attempts");
            networkStatus_ = NetworkStatus::WIFI_RECONNECTING; // Use specific status for reconnection
            wifiReconnectionEnabled_ = true;
            lastWiFiReconnectAttempt_ = millis(); // Reset timer for immediate first attempt
            
            // Start BLE if not already active for reconfiguration options
            if (!bleConfigManager_->isActive()) {
                logger_->info("Network", "Starting BLE configuration mode due to WiFi loss");
                startBLEConfigMode();
            }
        }
        
        // After WiFi reset, stop reconnection attempts if no credentials
        if (wifiReconnectionEnabled_ && 
            (!config_->isWiFiAutoConnect() || 
             config_->getWiFiSSID().length() == 0 || 
             config_->getWiFiPassword().length() == 0)) {
            logger_->info("Network", "WiFi credentials cleared, disabling reconnection attempts");
            wifiReconnectionEnabled_ = false;
        }
    }

    // Update MQTT
    if (mqttManager_) {
        mqttManager_->update();
    }
    
    // HIGH PRIORITY: Check actuator timers after MQTT (can be slow)
    if (actuator_) {
        actuator_->update();
    }

    // Update OTA Manager
    if (otaManager_) {
        otaManager_->update();
    }
    
    // HIGH PRIORITY: Check actuator timers after OTA (can be slow)
    if (actuator_) {
        actuator_->update();
    }

    // Handle BLE events
    if (bleConfigManager_ && bleConfigManager_->isActive()) {
        bleConfigManager_->handleEvents();
    }
    
    // Handle device registration retries
    handleRegistrationRetry();
    
    // Handle WiFi reconnection attempts
    handleWiFiReconnection();
    
    // Handle sensor readings (only if MQTT is connected, system is running, AND registration ACK received)
    if (systemManager_->getStatus() == SystemStatus::RUNNING && 
        mqttManager_ && mqttManager_->isConnected() && 
        registrationAckReceived_) {
        handleSensorReading();
    }
    
    // Check for critical errors (but don't spam recovery attempts)
    static unsigned long lastErrorRecovery = 0;
    if (systemManager_->getStatus() == SystemStatus::ERROR) {
        unsigned long currentTime = millis();
        if (currentTime - lastErrorRecovery >= 5000) { // Only try recovery every 5 seconds
            logger_->error("LeafNode", "System error detected, attempting recovery...");
            lastErrorRecovery = currentTime;
            
            // Try to reset system status to INITIALIZING
            systemManager_->setStatus(SystemStatus::INITIALIZING);
        }
    }
}

SystemStatus LeafNode::getSystemStatus() const {
    return systemManager_ ? systemManager_->getStatus() : SystemStatus::ERROR;
}

void LeafNode::factoryReset() {
    logger_->warning("LeafNode", "Factory reset initiated");
    
    systemManager_->setStatus(SystemStatus::FACTORY_RESET);
    
    // Reset configuration (including setup status)
    config_->resetToDefaults();
    config_->resetDeviceSetup(); // Explicitly reset setup status
    config_->save();
    
    logger_->info("LeafNode", "Factory reset completed, restarting...");
    delay(1000);
    
    // Restart system
    systemManager_->restart();
}

void LeafNode::setupLogging() {
    logger_->setTimestampEnabled(true);
    logger_->setColorEnabled(config_->isDebugMode());
}

void LeafNode::printStartupInfo() {
    logger_->info("System", "=== LeafNode Firmware ===");
    logger_->info("System", "Version: " + String(FIRMWARE_VERSION));
    logger_->info("System", "Build: " + String(LeafNodeConstants::BUILD_DATE));
    logger_->info("System", "Device: " + config_->getDeviceName());
    logger_->info("System", "Serial: " + config_->getSerialNumber());
    logger_->info("System", "Chip: ESP32-C3");
    logger_->info("System", "Free heap: " + String(systemManager_->getFreeHeap()) + " bytes");
    logger_->info("System", "=========================");
}

bool LeafNode::initializeFactoryMode() {
    logger_->info("Factory", "⚙️  Initializing Factory Mode...");
    
    // Initialize Status LED for Factory Mode (White - all LEDs on)
    statusLED_ = new StatusLED();
#ifdef USE_WS2812B_LED
    if (!statusLED_->initialize(LED_WS2812B_PIN, 255)) {
        logger_->error("Factory", "Failed to initialize status LED");
        return false;
    }
#endif
#ifdef USE_RGB_LED
    if (!statusLED_->initialize(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN, 255)) {
        logger_->error("Factory", "Failed to initialize status LED");
        return false;
    }
#endif
    statusLED_->setStatus(LEDStatus::FACTORY_MODE);
    logger_->info("Factory", "Status LED set to Factory Mode (White)");
    
    // Pass LED to SystemManager
    systemManager_->setStatusLED(statusLED_);
    
    // Enable Serial Command Handler with Factory Mode enabled
    logger_->info("Factory", "Activating Serial Command Handler...");
    serialCommandHandler_->setEnabled(true);
    serialCommandHandler_->setFactoryMode(true);
    
    // Print factory mode welcome message
    Serial.println();
    Serial.println("================================================================");
    Serial.println("🏭 FACTORY MODE ACTIVE");
    Serial.println("================================================================");
    Serial.println();
    Serial.println("Device is in Factory Configuration Mode.");
    Serial.println("Configuration can be done via serial console.");
    Serial.println();
    Serial.println("Type 'factory' to show the configuration menu.");
    Serial.println("Type 'help' to see all available commands.");
    Serial.println();
    Serial.println("================================================================");
    Serial.println();
    Serial.print("> ");
    
    logger_->info("Factory", "✅ Factory Mode ready (Serial Console)");
    
    systemManager_->setStatus(SystemStatus::RUNNING);
    initialized_ = true;
    
    return true;
}

bool LeafNode::validateConfiguration() {
    if (!config_->validate()) {
        logger_->error("LeafNode", "Configuration validation failed");
        return false;
    }
    
    logger_->debug("LeafNode", "Configuration validation passed");
    return true;
}

bool LeafNode::initializeNetwork() {
    logger_->info("Network", "Initializing network components...");
    
    // Create status LED
    statusLED_ = new StatusLED();
#ifdef USE_WS2812B_LED
    if (!statusLED_->initialize(LED_WS2812B_PIN, 255)) {
        logger_->error("Network", "Failed to initialize status LED");
        return false;
    }
#endif
#ifdef USE_RGB_LED
    if (!statusLED_->initialize(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN, 255)) {
        logger_->error("Network", "Failed to initialize status LED");
        return false;
    }
#endif
    logger_->info("Network", "Status LED initialized successfully");
    
    // Pass LED to SystemManager
    systemManager_->setStatusLED(statusLED_);
    
    // Create WiFi manager
    wifiManager_ = new WiFiManager();
    if (!wifiManager_->initialize()) {
        logger_->error("Network", "Failed to initialize WiFi manager");
        return false;
    }
    
    // Set WiFi connected callback for NTP sync
    wifiManager_->setOnConnectedCallback([this]() {
        if (scheduleManager_) {
            scheduleManager_->syncTime();
            logger_->info("Schedule", "NTP time synchronized on WiFi reconnect");
        }
    });
    
    // Create BLE config manager
    bleConfigManager_ = new BLEConfigManager();
    
    // Check chain position - only Node #1 activates BLE if no WiFi configured
    uint8_t chainPos = config_->getChainPosition();
    bool shouldActivateBLE = false;
    
    if (!config_->hasWiFiCredentials()) {
        if (chainPos == 1) {
            // Node #1: Activate BLE for configuration
            logger_->info("Network", "Chain Position 1 - Activating BLE config mode");
            shouldActivateBLE = true;
        } else if (chainPos > 1) {
            // Node #2+: Wait for config from chain
            logger_->info("Network", "Chain Position " + String(chainPos) + " - Waiting for config from Node #1");
            
            // Set LED to waiting orange via SystemManager
            if (systemManager_ && systemManager_->getStatusLED()) {
                systemManager_->getStatusLED()->setStatus(LEDStatus::WAITING_CHAIN_CONFIG);
                logger_->info("Network", "LED set to orange (waiting mode)");
            } else {
                logger_->warning("Network", "StatusLED not available yet");
            }
            
            // Wait for config from left neighbor (NO TIMEOUT - wait indefinitely!)
            String ssid, password, userId;
            
            UARTChainManager* chainManager = systemManager_->getChainManager();
            bool gotConfig = chainManager->waitForConfig(ssid, password, userId,
                                                         systemManager_->getStatusLED());
            
            if (gotConfig) {
                logger_->info("Network", "✓ Received config via UART chain!");
                
                // Mark that config was received via UART (not BLE)
                configuredViaUART_ = true;
                
                // Store user ID temporarily for registration (same as BLE flow)
                temporaryUserId_ = userId;
                logger_->info("Network", "User ID stored: " + userId);
                
                // Set LED to success briefly
                if (systemManager_ && systemManager_->getStatusLED()) {
                    systemManager_->getStatusLED()->setStatus(LEDStatus::CONNECTED);
                }
                
                // Save WiFi credentials
                config_->setWiFiCredentials(ssid, password);
                config_->setWiFiAutoConnect(true);
                
                // Save config
                if (!config_->save()) {
                    logger_->error("Network", "Failed to save WiFi credentials");
                    shouldActivateBLE = true;
                } else {
                    logger_->info("Network", "Config saved successfully");
                    
                    // Forward config to next node in chain (if we have right neighbor)
                    if (chainManager->hasRightNeighbor()) {
                        logger_->info("Network", "Forwarding config to next node...");
                        chainManager->sendConfigForward(ssid, password, userId);
                        logger_->info("Network", "✓ Config forwarded");
                    }
                    
                    // Notify setup manager (same as BLE flow)
                    if (setupManager_) {
                        setupManager_->onWiFiConfigReceived();
                    }
                    
                    // NOW CONNECT TO WiFi (EXACTLY LIKE BLE FLOW!)
                    logger_->info("Network", "Connecting to WiFi...");
                    networkStatus_ = NetworkStatus::WIFI_CONNECTING;
                    
                    if (wifiManager_->connect(ssid, password)) {
                        // WiFi connected - trigger the same flow as BLE
                        onWiFiConnected();
                        
                        // Skip BLE setup - we already have config via UART!
                        logger_->info("Network", "✓ Config via UART - BLE setup not needed");
                        
                        // Skip further network initialization since we're already connected
                        logger_->info("Network", "Network components initialized successfully");
                        return true;
                    } else {
                        logger_->error("Network", "Failed to connect with received credentials");
                        networkStatus_ = NetworkStatus::WIFI_FAILED;
                        shouldActivateBLE = true;
                    }
                }
            } else {
                // This should never happen since waitForConfig waits forever
                logger_->error("Network", "ERROR: Config wait failed unexpectedly");
                shouldActivateBLE = true;
            }
        } else {
            // Chain position unknown - activate BLE as fallback
            logger_->warning("Network", "Chain position unknown - activating BLE as fallback");
            shouldActivateBLE = true;
        }
    } else {
        // WiFi already configured - no need to START BLE, but still initialize it
        logger_->info("Network", "WiFi already configured - initializing BLE for fallback use");
        shouldActivateBLE = false;
    }
    
    // ALWAYS initialize BLE manager (even if we don't start it immediately)
    if (!bleConfigManager_->initialize(config_->getDeviceName())) {
        logger_->error("Network", "Failed to initialize BLE config manager");
        return false;
    }
    
    if (shouldActivateBLE) {
        // BLE was already initialized above, now start it
        logger_->info("Network", "Starting BLE configuration mode");
    } else {
        // BLE initialized but not started - ready for fallback use
        logger_->info("Network", "BLE config manager initialized but not activated");
    }
    
    // Set BLE callbacks (even if not activated yet)
    bleConfigManager_->setWiFiConfigCallback([this](const String& ssid, const String& password, const String& userId) {
        onWiFiCredentialsReceived(ssid, password, userId);
    });
    
    bleConfigManager_->setNetworkScanCallback([this]() -> String {
        if (wifiManager_) {
            logger_->info("Network", "Starting WiFi network scan...");
            if (statusLED_) statusLED_->setStatus(LEDStatus::BLE_SCANNING);
            systemManager_->setStatus(SystemStatus::INITIALIZING);
            String result = wifiManager_->scanNetworks();
            logger_->info("Network", "WiFi scan completed");
            if (statusLED_) statusLED_->setStatus(LEDStatus::BLE_CONFIG);
            systemManager_->setStatus(SystemStatus::INITIALIZING);
            return result;
        }
        return "ERROR:WiFi manager not available";
    });
    
    bleConfigManager_->setSerialNumberCallback([this]() -> String {
        return config_->getSerialNumber();
    });
    
    bleConfigManager_->setBLEKeyCallback([this]() -> String {
        return config_->getBLEKey();
    });
    
    // Start network connection sequence
    startNetworkConnection();
    
    logger_->info("Network", "Network components initialized successfully");
    return true;
}

void LeafNode::startNetworkConnection() {
    logger_->info("Network", "Starting network connection sequence...");
    
    // Set status to connecting
    networkStatus_ = NetworkStatus::CONNECTING;
    
    // Check if we have WiFi credentials
    String ssid = config_->getWiFiSSID();
    String password = config_->getWiFiPassword();
    
    bool hasWiFiCredentials = (ssid.length() > 0 && password.length() > 0 && config_->isWiFiAutoConnect());
    
    if (hasWiFiCredentials) {
        logger_->info("Network", "Attempting WiFi connection to: " + ssid);
        
        // Try to connect to WiFi with 3 retries
        int retries = 3;
        while (retries > 0 && !wifiManager_->connect(ssid, password)) {
            retries--;
            logger_->warning("Network", "WiFi connection failed, retries left: " + String(retries));
            if (retries > 0) {
                delay(2000);
            }
        }
        
        if (wifiManager_->isConnected()) {
            logger_->info("Network", "WiFi connected successfully on startup");
            onWiFiConnected();
            // BLE is initialized but not started - ready for fallback if needed
            return;
        } else {
            // WiFi failed, enable reconnection attempts and start BLE
            logger_->warning("Network", "WiFi connection failed, enabling continuous reconnection attempts");
            wifiReconnectionEnabled_ = true;
            lastWiFiReconnectAttempt_ = millis();
        }
    } else {
        logger_->info("Network", "No WiFi credentials configured");
    }
    
    // Start BLE configuration mode (either for initial setup or as fallback)
    logger_->info("Network", "Starting BLE configuration mode...");
    startBLEConfigMode();
}

void LeafNode::startBLEConfigMode() {
    // Check if BLE is already active
    if (bleConfigManager_->isActive()) {
        logger_->debug("Network", "BLE configuration mode already active");
        // Only change networkStatus if not already in WiFi reconnection mode
        if (networkStatus_ != NetworkStatus::WIFI_RECONNECTING) {
            networkStatus_ = NetworkStatus::BLE_CONFIG;
        }
        return;
    }
    
    // Only set network status to BLE config mode if not already reconnecting WiFi
    if (networkStatus_ != NetworkStatus::WIFI_RECONNECTING) {
        networkStatus_ = NetworkStatus::BLE_CONFIG;
    }
    
    if (!bleConfigManager_->start()) {
        logger_->error("Network", "Failed to start BLE advertising");
        if (statusLED_) statusLED_->setStatus(LEDStatus::ERROR);
        systemManager_->setStatus(SystemStatus::ERROR);
        networkStatus_ = NetworkStatus::ERROR;
        return;
    }
    
    // Only set LED to BLE_CONFIG if not already showing WiFi reconnection status
    if (statusLED_ && statusLED_->getStatus() != LEDStatus::WIFI_RECONNECTING) {
        statusLED_->setStatus(LEDStatus::BLE_CONFIG);
    }
    
    logger_->info("Network", "BLE configuration mode active");
    logger_->info("Network", "Device name: " + config_->getDeviceName());
    logger_->info("Network", "Service UUID: 7EF7CD05-F598-4DFC-B5BC-D1CC1CE141DC");
}

void LeafNode::onWiFiConnected() {
    logger_->info("Network", "WiFi connected successfully");
    logger_->info("Network", "IP Address: " + WiFi.localIP().toString());
    
    // Disable WiFi reconnection attempts when connected
    wifiReconnectionEnabled_ = false;
    
    // Notify setup manager
    if (setupManager_) {
        setupManager_->onWiFiConnected();
    }
    
    // Set status to WiFi connected
    systemManager_->setStatus(SystemStatus::RUNNING);
    networkStatus_ = NetworkStatus::WIFI_CONNECTED;
    
    // Show success indication if device is already configured and setup is complete
    if (config_->isDeviceSetup() && registrationAckReceived_) {
        systemManager_->startSuccessFade();
        logger_->info("Network", "Device fully configured - showing success indication");
    }
    
    // Send success message to BLE client with IP address (if BLE is active)
    if (bleConfigManager_->isActive()) {
        String successMessage = "SUCCESS:" + WiFi.localIP().toString();
        bleConfigManager_->sendStatus(successMessage);
        
        // Give the client time to receive the message
        delay(1000);
        
        // Stop BLE after successful WiFi reconnection
        bleConfigManager_->stop();
        logger_->info("Network", "BLE configuration stopped after successful WiFi reconnection");
    }
    
    // Connect to MQTT after WiFi is established
    connectMQTT();
    
    // Sync NTP time for schedule manager
    if (scheduleManager_) {
        scheduleManager_->syncTime();
        logger_->info("Schedule", "NTP time sync triggered");
    }
    
    // Setup check will be performed after MQTT connection in onMQTTConnected()
}

void LeafNode::onWiFiCredentialsReceived(const String& ssid, const String& password, const String& userId) {
    logger_->info("Network", "WiFi credentials received via BLE: " + ssid);
    logger_->info("Network", "User ID received: " + userId);
    
    // CRITICAL: Make local copies IMMEDIATELY because the source strings may be cleared
    String ssidCopy = ssid;
    String passwordCopy = password;
    String userIdCopy = userId;
    
    // Store user ID temporarily for registration
    temporaryUserId_ = userIdCopy;
    
    // Send status update to BLE client
    if (bleConfigManager_->isActive()) {
        bleConfigManager_->sendStatus("STATUS:Saving credentials and user ID");
    }
    
    // Save credentials to configuration using our stable copies
    config_->setWiFiSSID(ssidCopy);
    config_->setWiFiPassword(passwordCopy);
    config_->setWiFiAutoConnect(true);
    
    // DEBUG: Check if credentials are actually set
    logger_->info("Network", "SSID being saved: [" + ssidCopy + "]");
    logger_->info("Network", "Password being saved: [" + passwordCopy + "]");
    logger_->info("Network", "Config SSID after set: [" + config_->getWiFiSSID() + "]");
    logger_->info("Network", "Config Password after set: [" + config_->getWiFiPassword() + "]");
    
    if (!config_->save()) {
        logger_->error("Network", "Failed to save WiFi credentials");
        if (bleConfigManager_->isActive()) {
            bleConfigManager_->sendStatus("ERROR:Failed to save credentials");
        }
        return;
    }
    
    // Forward config to next node in chain (if we're Node #1 and have right neighbor)
    uint8_t chainPos = config_->getChainPosition();
    UARTChainManager* chainManager = systemManager_->getChainManager();
    
    if (chainPos == 1 && chainManager->hasRightNeighbor()) {
        logger_->info("Network", "Node #1 forwarding config to chain...");
        chainManager->sendConfigForward(ssidCopy, passwordCopy, userIdCopy);
        logger_->info("Network", "✓ Config forwarded to next node");
    }
    
    // Notify setup manager after successful save
    if (setupManager_) {
        setupManager_->onWiFiConfigReceived();
    }
    
    logger_->info("Network", "User ID temporarily stored for device registration");
    
    // Send connecting status
    if (bleConfigManager_->isActive()) {
        bleConfigManager_->sendStatus("STATUS:Connecting to WiFi");
    }
    
    // Try to connect
    networkStatus_ = NetworkStatus::WIFI_CONNECTING;
    
    if (wifiManager_->connect(ssid, password)) {
        onWiFiConnected();
    } else {
        logger_->error("Network", "Failed to connect with received credentials");
        networkStatus_ = NetworkStatus::WIFI_FAILED;
        
        // Send error to BLE client
        if (bleConfigManager_->isActive()) {
            bleConfigManager_->sendStatus("ERROR:Failed to connect to WiFi");
        }
        
        // Return to BLE config mode after a delay
        delay(3000);
        startBLEConfigMode();
    }
}

void LeafNode::onFactoryResetRequested() {
    logger_->warning("Network", "Factory reset requested via BLE");
    
    // Indicate factory reset in progress
    if (statusLED_) statusLED_->setStatus(LEDStatus::ERROR); // Use error pattern for factory reset
    systemManager_->setStatus(SystemStatus::FACTORY_RESET);
    
    // Perform factory reset
    factoryReset();
}

String LeafNode::getTemporaryUserId() const {
    return temporaryUserId_;
}

void LeafNode::clearTemporaryUserId() {
    temporaryUserId_ = "";
    logger_->debug("Network", "Temporary user ID cleared");
}

bool LeafNode::initializeMQTT() {
    logger_->info("MQTT", "Initializing MQTT manager...");
    
    mqttManager_ = new MQTTManager();
    if (!mqttManager_->initialize()) {
        logger_->error("MQTT", "Failed to initialize MQTT manager");
        return false;
    }
    
    // Configure MQTT with settings from config
    if (config_->hasMQTTCredentials()) {
        mqttManager_->configure(
            config_->getMQTTServer(),
            config_->getMQTTPort(),
            config_->getMQTTUsername(),
            config_->getMQTTPassword(),
            config_->getMQTTClientId()
        );
        logger_->info("MQTT", "MQTT configured with server: " + config_->getMQTTServer());
    } else {
        logger_->warning("MQTT", "No MQTT credentials configured");
    }
    
    // Set MQTT callbacks
    mqttManager_->setConnectedCallback([this]() {
        onMQTTConnected();
    });
    
    mqttManager_->setDisconnectedCallback([this]() {
        onMQTTDisconnected();
    });
    
    mqttManager_->setMessageCallback([this](const String& topic, const String& payload) {
        logger_->debug("MQTT", "Message received on topic: " + topic + " -> " + payload);
        handleMQTTMessage(topic, payload);
    });
    
    // Configure command handler with MQTT response callback
    commandHandler_->setResponseCallback([this](const String& topic, const String& payload) {
        if (mqttManager_ && mqttManager_->isConnected()) {
            return mqttManager_->publish(topic, payload, false);
        }
        return false;
    });
    
    logger_->info("MQTT", "MQTT manager initialized successfully");
    
    // Initialize OTA Manager
    logger_->info("OTA", "Initializing OTA manager...");
    otaManager_ = new OTAManager();
    otaManager_->setLogger(logger_);
    otaManager_->setSystemManager(systemManager_);
    otaManager_->setTaskManager(taskManager_);
    otaManager_->setSensorManager(sensorManager_);
    
    if (!otaManager_->initialize()) {
        logger_->error("OTA", "Failed to initialize OTA manager");
        return false;
    }
    
    // Set OTA callbacks
    otaManager_->setProgressCallback([this](const OTAProgress& progress) {
        // Publish OTA progress to MQTT
        if (mqttManager_ && mqttManager_->isConnected()) {
            String otaTopic = config_->getMQTTTopicStatus() + "/ota";
            
            DynamicJsonDocument progressDoc(256);
            progressDoc["status"] = static_cast<int>(progress.status);
            progressDoc["progress"] = progress.percentage;
            progressDoc["current_version"] = progress.currentVersion;
            progressDoc["target_version"] = progress.targetVersion;
            progressDoc["bytes_downloaded"] = progress.bytesDownloaded;
            progressDoc["total_bytes"] = progress.totalBytes;
            
            if (!progress.errorMessage.isEmpty()) {
                progressDoc["error"] = progress.errorMessage;
            }
            
            String progressPayload;
            serializeJson(progressDoc, progressPayload);
            mqttManager_->publish(otaTopic, progressPayload, false);
        }
    });
    
    otaManager_->setCompletionCallback([this](bool success, const String& message) {
        if (mqttManager_ && mqttManager_->isConnected()) {
            String otaTopic = config_->getMQTTTopicStatus() + "/ota";
            
            DynamicJsonDocument completionDoc(256);
            completionDoc["status"] = success ? "complete" : "failed";
            completionDoc["message"] = message;
            completionDoc["timestamp"] = millis();
            
            String completionPayload;
            serializeJson(completionDoc, completionPayload);
            mqttManager_->publish(otaTopic, completionPayload, true);
        }
        
        logger_->info("OTA", "Update " + String(success ? "completed" : "failed") + ": " + message);
    });
    
    // Set OTA manager in command handler
    commandHandler_->setOTAManager(otaManager_);
    
    logger_->info("OTA", "OTA manager initialized successfully");
    
    // Set MQTT manager for MCP4725 DAC (if available)
    if (systemManager_->getMCP4725()) {
        systemManager_->getMCP4725()->setMQTTManager(mqttManager_);
        
        // Set LED update callback
        systemManager_->getMCP4725()->setLEDUpdateCallback(LeafNode::dacLEDUpdateCallback);
        
        logger_->info("MQTT", "MCP4725 DAC connected to MQTT manager");
    }
    
    return true;
}

// Static callback for DAC LED update
void LeafNode::dacLEDUpdateCallback() {
    if (instance_ && instance_->systemManager_) {
        instance_->systemManager_->updateActuatorDACLED();
    }
}

void LeafNode::connectMQTT() {
    if (!mqttManager_ || !config_->hasMQTTCredentials()) {
        logger_->warning("MQTT", "Cannot connect - MQTT not configured");
        return;
    }
    
    if (!config_->isMQTTAutoConnect()) {
        logger_->info("MQTT", "MQTT auto-connect disabled");
        return;
    }
    
    logger_->info("MQTT", "Attempting MQTT connection...");
    
    if (mqttManager_->connect()) {
        logger_->info("MQTT", "MQTT connected successfully");
    } else {
        logger_->warning("MQTT", "MQTT connection failed - will retry automatically");
    }
}

void LeafNode::onMQTTConnected() {
    logger_->info("MQTT", "MQTT connection established");
    
    // Notify setup manager
    if (setupManager_) {
        setupManager_->onMQTTConnected();
    }
    
    // Subscribe to all device-specific topics
    mqttManager_->subscribeToDeviceTopics(config_->getSerialNumber());
    
    // Publish device online status
    String statusTopic = config_->getMQTTTopicStatus();
    DynamicJsonDocument doc(256);
    doc["status"] = "online";
    doc["timestamp"] = millis();
    doc["firmware"] = FIRMWARE_VERSION;
    
    String statusPayload;
    serializeJson(doc, statusPayload);
    mqttManager_->publish(statusTopic, statusPayload, true);
    
    logger_->info("MQTT", "Device status published");
    
    // Show success indication for fully configured device after MQTT connection
    if (config_->isDeviceSetup() && registrationAckReceived_) {
        systemManager_->startSuccessFade();
        logger_->info("MQTT", "Device fully operational - showing success indication");
    }
    
    // Check if device needs initial setup after MQTT is connected
    checkInitialDeviceSetup();
}

void LeafNode::onMQTTDisconnected() {
    logger_->warning("MQTT", "MQTT connection lost");
}

void LeafNode::publishUserRegistration(const String& userId) {
    if (!mqttManager_ || !mqttManager_->isConnected()) {
        logger_->error("MQTT", "Cannot publish user registration - MQTT not connected");
        return;
    }
    
    String topic = config_->getMQTTTopicRegister();
    String payload = "{\"user_id\":\"" + userId + "\",\"device_id\":\"" + config_->getSerialNumber() + 
                    "\",\"timestamp\":" + String(millis()) + ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
    
    logger_->info("MQTT", "Publishing user registration to topic: " + topic);
    logger_->info("MQTT", "Payload: " + payload);
    
    if (mqttManager_->publish(topic, payload, false)) {
        logger_->info("MQTT", "User registration published successfully");
    } else {
        logger_->error("MQTT", "Failed to publish user registration");
    }
}

void LeafNode::checkInitialDeviceSetup() {
    logger_->info("Setup", "Checking device setup status...");
    
    // Check if device is already set up
    if (config_->isDeviceSetup()) {
        logger_->info("Setup", "Device already set up - skipping registration");
        
        // Notify setup manager that setup is complete (no ACK needed for pre-registered devices)
        if (setupManager_) {
            setupManager_->onRegistrationAck();
        }
        
        return;
    }
    
    logger_->info("Setup", "Device not set up yet - checking for user_id");
    
    // Only proceed with registration if we have a user_id
    String userId = getTemporaryUserId();
    if (userId.isEmpty()) {
        logger_->info("Setup", "No user_id available yet - waiting for BLE configuration");
        logger_->info("Setup", "Device will register automatically when user_id is provided");
        return;
    }
    
    logger_->info("Setup", "User_id found: " + userId + " - starting registration process");
    
    // Start registration process
    registrationActive_ = true;
    registrationAckReceived_ = false;
    registrationRetryInterval_ = 30000; // Start with 30 seconds
    lastRegistrationAttempt_ = millis();
    
    // Perform initial device registration
    if (publishDeviceRegistration()) {
        logger_->info("Setup", "Device registration sent, waiting for server acknowledgment...");
        logger_->info("Setup", "Will retry with exponential backoff until ACK received");
    } else {
        logger_->warning("Setup", "Initial device registration failed - will retry");
    }
}

bool LeafNode::publishDeviceRegistration() {
    if (!mqttManager_ || !mqttManager_->isConnected()) {
        logger_->warning("Setup", "Cannot register device - MQTT not connected");
        return false;
    }
    
    // Only register if we have a user_id from BLE setup
    String userId = getTemporaryUserId();
    if (userId.isEmpty()) {
        logger_->info("Setup", "No user_id available - device registration skipped");
        logger_->info("Setup", "Device will register when user_id is provided via BLE");
        return false; // Not an error, just no user_id yet
    }
    
    String serialNumber = config_->getSerialNumber();
    String topic = config_->getMQTTTopicRegister();
    
    // Create simplified registration payload with user_id
    DynamicJsonDocument doc(256);
    doc["serial_number"] = serialNumber;
    doc["user_id"] = userId;
    
    String payload;
    serializeJson(doc, payload);
    
    logger_->info("Setup", "Registering device on topic: " + topic);
    logger_->info("Setup", "Registration payload: " + payload);
    
    bool success = mqttManager_->publish(topic, payload, false);
    
    if (success) {
        logger_->info("Setup", "Device registration published successfully");
        // Clear the temporary user_id after successful registration
        clearTemporaryUserId();
    } else {
        logger_->error("Setup", "Failed to publish device registration");
    }
    
    return success;
}

bool LeafNode::publishHeartbeat() {
    if (!mqttManager_ || !mqttManager_->isConnected()) {
        logger_->debug("Heartbeat", "Skipping heartbeat - MQTT not connected");
        return false;
    }
    
    String serialNumber = config_->getSerialNumber();
    String topic = config_->getMQTTTopicHeartbeat();
    
    // Create heartbeat payload with timestamp and RSSI
    DynamicJsonDocument doc(512);  // Increased size for sensor health info
    doc["timestamp"] = millis();
    doc["uptime"] = systemManager_->getUptime();
    doc["free_heap"] = systemManager_->getFreeHeap();
    doc["firmware"] = FIRMWARE_VERSION;
    
    // Add RSSI if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        doc["rssi"] = WiFi.RSSI();
    }
    
    // Add actuators (only if active)
    JsonObject actuators = doc.createNestedObject("actuators");
    bool hasActiveActuators = false;
    
    if (actuator_) {
        // Add MOSFET if ON
        if (actuator_->getState(Actuator::Type::MOSFET)) {
            actuators["mosfet"]["state"] = true;
            hasActiveActuators = true;
        }
        
        // Add RELAY if ON
        if (actuator_->getState(Actuator::Type::RELAY)) {
            actuators["relay"]["state"] = true;
            hasActiveActuators = true;
        }
    }
    
    // Add DAC if value > 0 (only the last command type)
    if (systemManager_->getMCP4725() && systemManager_->getMCP4725()->getCurrentValue() > 0) {
        String lastType = systemManager_->getMCP4725()->getLastCommandType();
        if (lastType == "voltage") {
            actuators["dac"]["voltage"] = systemManager_->getMCP4725()->getCurrentVoltage();
            hasActiveActuators = true;
        } else if (lastType == "percent") {
            actuators["dac"]["percent"] = systemManager_->getMCP4725()->getCurrentPercent();
            hasActiveActuators = true;
        } else if (lastType == "value") {
            actuators["dac"]["value"] = systemManager_->getMCP4725()->getCurrentValue();
            hasActiveActuators = true;
        }
    }
    
    // Remove actuators object if nothing is active
    if (!hasActiveActuators) {
        doc.remove("actuators");
    }
    
    // Add sensor health information
    JsonObject sensorHealth = doc.createNestedObject("sensor_health");
    bool sensorHealthy = sensorManager_->getSensorHealthStatus();
    sensorHealth["healthy"] = sensorHealthy;
    sensorHealth["sensor_type"] = sensorManager_->getSensorTypeName();
    sensorHealth["consecutive_errors"] = sensorManager_->getConsecutiveErrors();
    
    if (!sensorHealthy && sensorManager_->getCurrentProfile() != SensorProfile::NONE) {
        sensorHealth["error_details"] = "Sensor communication failed";
        if (sensorManager_->getConsecutiveErrors() >= 5) {
            sensorHealth["backoff_active"] = true;
            sensorHealth["backoff_interval_seconds"] = sensorManager_->getBackoffInterval() / 1000;
        }
    }
    
    String payload;
    serializeJson(doc, payload);
    
    bool success = mqttManager_->publish(topic, payload, false);
    
    if (success) {
        logger_->info("Heartbeat", "Published to " + topic);
        logger_->debug("Heartbeat", "Payload: " + payload);
    } else {
        logger_->warning("Heartbeat", "Failed to publish heartbeat");
    }
    
    return success;
}

void LeafNode::handleMQTTMessage(const String& topic, const String& payload) {
    String serialNumber = config_->getSerialNumber();
    
    // Handle registration acknowledgment
    String registrationAckTopic = config_->getMQTTTopicRegistrationAck();
    if (topic.equals(registrationAckTopic)) {
        handleRegistrationAck(payload);
        return;
    }
    
    // Handle device commands (old format for backward compatibility)
    String deviceCommandsTopic = config_->getMQTTTopicCommands();
    if (topic.equals(deviceCommandsTopic)) {
        logger_->info("MQTT", "Device command received: " + payload);
        handleCommandMessage(topic, payload);
        return;
    }
    
    // Handle node commands (new format)
    String nodeCommandsTopic = config_->getMQTTTopicCommand();
    if (topic.equals(nodeCommandsTopic)) {
        logger_->info("MQTT", "Node command received: " + payload);
        handleCommandMessage(topic, payload);
        return;
    }
    
    logger_->warning("MQTT", "Unknown topic: " + topic);
}

void LeafNode::handleRegistrationAck(const String& payload) {
    logger_->info("Setup", "Registration ACK received: " + payload);
    
    // Parse JSON payload
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        logger_->warning("Setup", "Failed to parse registration ACK: " + String(error.c_str()));
        if (setupManager_) {
            setupManager_->onRegistrationFailed();
        }
        return;
    }
    
    String serialNumber = doc["serial_number"];
    String status = doc["status"];
    
    // Validate serial number matches
    if (serialNumber != config_->getSerialNumber()) {
        logger_->warning("Setup", "Registration ACK serial number mismatch: " + serialNumber);
        if (setupManager_) {
            setupManager_->onRegistrationFailed();
        }
        return;
    }
    
    // Validate status
    if (status != "registered") {
        logger_->warning("Setup", "Unexpected registration ACK status: " + status);
        if (setupManager_) {
            setupManager_->onRegistrationFailed();
        }
        return;
    }
    
    // Notify setup manager of successful registration
    if (setupManager_) {
        setupManager_->onRegistrationAck();
    }
    
    logger_->info("Setup", "✅ Device registration confirmed by server!");
    
    // Mark device as setup complete immediately
    config_->setDeviceSetup(true);
    config_->save();
    
    // Clear temporary user ID as it's no longer needed
    clearTemporaryUserId();
    
    logger_->info("Setup", "Device setup completed and saved to configuration");
    
    // Start success fade animation now that setup is complete
    systemManager_->startSuccessFade();
    logger_->info("Setup", "Showing success indication after registration");
    
    // Stop registration retry
    registrationActive_ = false;
    registrationAckReceived_ = true;
}

void LeafNode::simulateRegistrationAck(const String& payload) {
    logger_->info("Test", "🧪 Simulating registration ACK: " + payload);
    handleRegistrationAck(payload);
}

void LeafNode::handleCommandMessage(const String& topic, const String& payload) {
    if (!commandHandler_) {
        logger_->error("LeafNode", "Command handler not initialized");
        return;
    }
    
    // Process command through command handler
    commandHandler_->processCommand(topic, payload);
}

void LeafNode::handleRegistrationRetry() {
    // Only retry if registration is active and we haven't received ACK yet
    if (!registrationActive_ || registrationAckReceived_) {
        return;
    }
    
    // Only retry if we have connectivity
    if (!mqttManager_ || !mqttManager_->isConnected()) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Check if it's time for next retry
    if (currentTime - lastRegistrationAttempt_ >= registrationRetryInterval_) {
        logger_->info("Setup", "Retrying device registration...");
        
        // Send registration again
        if (publishDeviceRegistration()) {
            // Exponential backoff: double interval, max 5 minutes
            registrationRetryInterval_ = min(registrationRetryInterval_ * 2, 300000UL);
            lastRegistrationAttempt_ = currentTime;
            
            logger_->info("Setup", "Next retry in " + String(registrationRetryInterval_ / 1000) + " seconds");
        } else {
            logger_->warning("Setup", "Registration failed, retrying soon");
        }
    }
}

void LeafNode::handleSensorReading() {
    if (!sensorManager_) {
        return;
    }
    
    // Check if it's time for next sensor reading
    if (sensorManager_->isReadingDue()) {
        logger_->debug("Sensor", "Performing sensor reading...");
        
        // Read sensor data and publish directly to MQTT
        if (sensorManager_->readAndPublish()) {
            logger_->info("Sensor", "Sensor data published successfully");
            sensorManager_->updateLastReading();
        } else {
            // Only log error if sensor is actually configured and should be working
            // The SensorManager handles its own error logging with backoff
            if (sensorManager_->hasSensorConfigured() && sensorManager_->isSensorAvailable()) {
                logger_->debug("Sensor", "Sensor reading failed - see SensorManager logs for details");
            }
        }
        
        lastSensorReading_ = millis();
    }
}

void LeafNode::handleWiFiReconnection() {
    // Only attempt reconnection if WiFi reconnection is enabled
    if (!wifiReconnectionEnabled_ || !wifiManager_) {
        return;
    }
    
    // Only try to reconnect if we're not already connected
    if (wifiManager_->isConnected()) {
        return;
    }
    
    // Check if we have WiFi credentials
    if (!config_ || !config_->isWiFiAutoConnect() || 
        config_->getWiFiSSID().length() == 0 || 
        config_->getWiFiPassword().length() == 0) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Check if it's time for next WiFi reconnect attempt
    if (currentTime - lastWiFiReconnectAttempt_ >= wifiReconnectInterval_) {
        logger_->info("Network", "Attempting WiFi reconnection to: " + config_->getWiFiSSID());
        
        // Try to reconnect
        if (wifiManager_->connect(config_->getWiFiSSID(), config_->getWiFiPassword(), 1)) {
            logger_->info("Network", "WiFi reconnection successful!");
            onWiFiConnected();
        } else {
            logger_->warning("Network", "WiFi reconnection failed, will retry in " + String(wifiReconnectInterval_ / 1000) + " seconds");
        }
        
        lastWiFiReconnectAttempt_ = currentTime;
    }
}

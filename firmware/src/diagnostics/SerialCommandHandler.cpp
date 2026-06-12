#include "SerialCommandHandler.h"
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include "config.h"
#include "../LeafNode.h"

SerialCommandHandler::SerialCommandHandler(RuntimeConfig* config, Logger* logger, LeafNode* leafNode)
    : config_(config)
    , logger_(logger)
    , leafNode_(leafNode)
    , inputBuffer_("")
    , enabled_(false)
    , factoryMode_(false)
    , resetConfirmPending_(false)
    , mqttConfigPending_(false)
    , mqttConfigStep_(0)
    , mqttPort_(1883)
    , wifiConfigPending_(false)
    , wifiConfigStep_(0)
    , sensorConfigPending_(false)
    , sensorConfigStep_(0)
    , serialConfigPending_(false) {
}

void SerialCommandHandler::update() {
    if (!enabled_) {
        return;
    }
    
    // Check for available serial data
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        // Handle newline/carriage return - execute command
        if (c == '\n' || c == '\r') {
            if (inputBuffer_.length() > 0) {
                Serial.println(); // Echo newline
                handleCommand(inputBuffer_);
                inputBuffer_ = "";
                printPrompt();
            }
        }
        // Handle backspace
        else if (c == '\b' || c == 127) {
            if (inputBuffer_.length() > 0) {
                inputBuffer_.remove(inputBuffer_.length() - 1);
                Serial.print("\b \b"); // Erase character on terminal
            }
        }
        // Add character to buffer
        else if (c >= 32 && c <= 126) { // Printable characters
            inputBuffer_ += c;
            Serial.print(c); // Echo character
        }
    }
}

void SerialCommandHandler::handleCommand(const String& command) {
    String cmd = command;
    cmd.trim();
    
    // Check if we're waiting for reset confirmation
    if (resetConfirmPending_) {
        if (cmd == "YES") {
            Serial.println();
            Serial.println("🔄 Resetting configuration...");
            Serial.println("⚠️  Deleting ALL data: WiFi, MQTT, Serial Number");
            
            Preferences prefs;
            
            // Clear leafnode namespace (WiFi, MQTT, device settings)
            prefs.begin("leafnode", false);
            prefs.clear();
            prefs.end();
            Serial.println("   ✓ leafnode cleared");
            
            // Clear factory namespace (Serial Number)
            prefs.begin("factory", false);
            prefs.clear();
            prefs.end();
            Serial.println("   ✓ factory cleared (Serial Number deleted)");
            
            Serial.println("✅ ALL configuration deleted");
            Serial.println("🏭 Device will enter Factory Mode on next boot");
            Serial.println("🔄 Rebooting in 2 seconds...");
            delay(2000);
            ESP.restart();
        } else {
            Serial.println();
            Serial.println("❌ Reset cancelled");
            printSeparator();
        }
        resetConfirmPending_ = false;
        return;
    }
    
    // Check if we're in MQTT configuration mode
    if (mqttConfigPending_) {
        handleMQTTConfigInput(cmd);
        return;
    }
    
    // Check if we're in WiFi configuration mode
    if (wifiConfigPending_) {
        handleWiFiConfigInput(cmd);
        return;
    }
    
    // Check if we're in sensor configuration mode
    if (sensorConfigPending_) {
        handleSensorConfigInput(cmd);
        return;
    }
    
    // Check if we're in serial number configuration mode
    if (serialConfigPending_) {
        handleSerialConfigInput(cmd);
        return;
    }
    
    // Normal command processing
    cmd.toLowerCase();
    
    // ========================================================================
    // CORE COMMANDS (always available)
    // ========================================================================
    if (cmd == "help" || cmd == "?") {
        showHelp();
    }
    else if (cmd == "status") {
        showStatus();
    }
    else if (cmd == "reboot") {
        rebootDevice();
    }
    
    // ========================================================================
    // FACTORY MODE COMMANDS (always available)
    // ========================================================================
    else if (cmd == "factory") {
        showFactoryMenu();
    }
    else if (cmd == "setserial") {
        setSerialNumber();
    }
    else if (cmd == "setsensor" || cmd.startsWith("setsensor ")) {
        // Extract parameter if provided (e.g., "setsensor SHT31")
        String param = "";
        int spacePos = cmd.indexOf(' ');
        if (spacePos > 0) {
            param = cmd.substring(spacePos + 1);
            param.trim();
        }
        setSensorProfile(param);
    }
    else if (cmd == "setwifi") {
        setWiFiConfig();
    }
    else if (cmd == "setmqtt") {
        setMQTTConfig();
    }
    
    // ========================================================================
    // DEVELOPMENT COMMANDS (only available in development mode)
    // ========================================================================
    #ifndef PRODUCTION_MODE
    else if (cmd == "reset") {
        resetConfig();
    }
    else if (cmd == "wifi") {
        showWiFiConfig();
    }
    else if (cmd == "mqtt") {
        showMQTTConfig();
    }
    else if (cmd == "serial") {
        showSerialNumber();
    }
    else if (cmd == "resetmqtt") {
        resetMQTTConfig();
    }
    else if (cmd == "resetwifi") {
        resetWiFiConfig();
    }
    else if (cmd == "mockack") {
        mockRegistrationAck();
    }
    else if (cmd == "unreg") {
        unregisterDevice();
    }
    else if (cmd == "led") {
        showLEDStatus();
    }
    else if (cmd == "ledtest") {
        testLED();
    }
    else if (cmd == "ledon") {
        setLEDOn();
    }
    else if (cmd == "ledoff") {
        setLEDOff();
    }
    else if (cmd == "ledred") {
        setLEDRed();
    }
    else if (cmd == "ledgreen") {
        setLEDGreen();
    }
    else if (cmd == "ledblue") {
        setLEDBlue();
    }
    else if (cmd == "i2cscan") {
        scanI2CBus();
    }
    else if (cmd == "nvs") {
        showNVSData();
    }
    #else
    // Production Mode - Block development commands
    else if (cmd == "reset" || cmd == "resetmqtt" || cmd == "resetwifi" || 
             cmd == "mockack" || cmd == "unreg" || cmd == "ledtest" || 
             cmd == "ledon" || cmd == "ledoff" || cmd == "ledred" || 
             cmd == "ledgreen" || cmd == "ledblue" || cmd == "i2cscan" ||
             cmd == "wifi" || cmd == "mqtt" || cmd == "serial" || cmd == "led" || cmd == "nvs") {
        Serial.println("❌ Command not available in Production Mode");
        Serial.println("   This is a development/debug command.");
        Serial.println("   Use Factory Mode commands instead (type 'factory')");
    }
    #endif
    
    else if (cmd.length() > 0) {
        Serial.println("❌ Unknown command: '" + cmd + "'");
        Serial.println("   Type 'help' for available commands");
    }
}

void SerialCommandHandler::showHelp() {
    printSeparator();
    
    #ifdef PRODUCTION_MODE
    Serial.println("📋 AVAILABLE COMMANDS (Production Mode)");
    #else
    Serial.println("📋 AVAILABLE COMMANDS (Development Mode)");
    #endif
    
    printSeparator();
    Serial.println();
    
    // Core Commands - Always available
    Serial.println("Core Commands:");
    Serial.println("  help     - Show this help message");
    Serial.println("  status   - Display device status");
    Serial.println("  reboot   - Reboot the device");
    Serial.println();
    
    // Factory Mode Commands - Always available
    Serial.println("Factory Configuration Commands:");
    Serial.println("  factory    - Show factory configuration menu");
    Serial.println("  setserial  - Set device serial number");
    Serial.println("  setsensor  - Set sensor profile (or: setsensor <NAME>)");
    Serial.println("  setwifi    - Configure WiFi credentials");
    Serial.println("  setmqtt    - Configure MQTT server");
    Serial.println();
    
    #ifndef PRODUCTION_MODE
    // Development Commands - Only in Development Mode
    Serial.println("Development/Debug Commands:");
    Serial.println("  reset      - Reset configuration to factory defaults");
    Serial.println("  wifi       - Show WiFi configuration");
    Serial.println("  mqtt       - Show MQTT configuration");
    Serial.println("  serial     - Show serial number");
    Serial.println("  resetmqtt  - Reset only MQTT credentials");
    Serial.println("  resetwifi  - Reset only WiFi credentials");
    Serial.println("  mockack    - Simulate MQTT registration ACK");
    Serial.println("  unreg      - Reset registration status");
    Serial.println();
    Serial.println("LED Test Commands:");
    Serial.println("  led        - Show LED status");
    Serial.println("  ledtest    - Test LED with color sequence (R->G->B->W)");
    Serial.println("  ledon      - Turn LED on (white)");
    Serial.println("  ledoff     - Turn LED off");
    Serial.println("  ledred     - Set LED to red");
    Serial.println("  ledgreen   - Set LED to green");
    Serial.println("  ledblue    - Set LED to blue");
    Serial.println();
    Serial.println("I2C Commands:");
    Serial.println("  i2cscan    - Scan I2C bus for connected devices");
    Serial.println();
    Serial.println("NVS Diagnostic Commands:");
    Serial.println("  nvs        - Show NVS stored configuration data");
    Serial.println();
    #else
    Serial.println("⚠️  Development commands are disabled in Production Mode.");
    Serial.println("   Use Factory Mode commands for configuration.");
    Serial.println();
    #endif
    
    printSeparator();
}

void SerialCommandHandler::showStatus() {
    printSeparator();
    Serial.println("📊 DEVICE STATUS");
    printSeparator();
    Serial.println();
    
    // Device info
    Serial.println("Device Information:");
    Serial.println("  Name:         " + config_->getDeviceName());
    Serial.println("  Serial:       " + config_->getSerialNumber());
    Serial.println("  Firmware:     " + String(FIRMWARE_VERSION));
    Serial.println("  Manufacturer: " + String(MANUFACTURER));
    Serial.println();
    
    // System info
    Serial.println("System Information:");
    Serial.println("  Free Heap:    " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("  Uptime:       " + String(millis() / 1000) + " seconds");
    Serial.println("  Chip Model:   " + String(ESP.getChipModel()));
    Serial.println("  CPU Freq:     " + String(ESP.getCpuFreqMHz()) + " MHz");
    Serial.println();
    
    // Configuration status
    Serial.println("Configuration Status:");
    Serial.println("  Has WiFi:     " + String(config_->hasWiFiCredentials() ? "✅ Yes" : "❌ No"));
    Serial.println("  Has MQTT:     " + String(config_->hasMQTTCredentials() ? "✅ Yes" : "❌ No"));
    Serial.println("  Has Sensor:   " + String(config_->hasSensorConfiguration() ? "✅ Yes" : "❌ No"));
    Serial.println("  Factory Mode: " + String(config_->isFactoryMode() ? "⚙️  Yes" : "❌ No"));
    Serial.println();
    
    // Sensor configuration
    if (config_->hasSensorConfiguration()) {
        Serial.println("Sensor Configuration:");
        Serial.println("  Type:         " + config_->getSensorName());
        Serial.println("  Interval:     " + String(config_->getSensorReadingInterval() / 1000) + " seconds");
        Serial.println();
    }
    
    // WiFi status
    Serial.println("WiFi Status:");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("  Status:       ✅ Connected");
        Serial.println("  SSID:         " + WiFi.SSID());
        Serial.println("  IP Address:   " + WiFi.localIP().toString());
        Serial.println("  RSSI:         " + String(WiFi.RSSI()) + " dBm");
    } else {
        Serial.println("  Status:       ❌ Disconnected");
    }
    Serial.println();
    
    printSeparator();
}

void SerialCommandHandler::resetConfig() {
    printSeparator();
    Serial.println("⚠️  RESET CONFIGURATION");
    printSeparator();
    Serial.println();
    Serial.println("This will delete ALL configuration and reboot the device.");
    Serial.println("The device will enter Factory Mode on next boot.");
    Serial.println();
    Serial.println("⚠️  Type 'YES' and press Enter to confirm:");
    
    // Set flag to wait for confirmation
    resetConfirmPending_ = true;
}

void SerialCommandHandler::rebootDevice() {
    printSeparator();
    Serial.println("🔄 REBOOTING DEVICE");
    printSeparator();
    Serial.println();
    Serial.println("Device will reboot in 2 seconds...");
    delay(2000);
    ESP.restart();
}

void SerialCommandHandler::showWiFiConfig() {
    printSeparator();
    Serial.println("📡 WIFI CONFIGURATION");
    printSeparator();
    Serial.println();
    
    if (config_->hasWiFiCredentials()) {
        Serial.println("  SSID:         " + config_->getWiFiSSID());
        Serial.println("  Password:     " + String(config_->getWiFiPassword().length() > 0 ? "****** (set)" : "(not set)"));
        Serial.println("  Auto Connect: " + String(config_->isWiFiAutoConnect() ? "Enabled" : "Disabled"));
    } else {
        Serial.println("  ❌ No WiFi credentials configured");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::showMQTTConfig() {
    printSeparator();
    Serial.println("📨 MQTT CONFIGURATION");
    printSeparator();
    Serial.println();
    
    if (config_->hasMQTTCredentials()) {
        Serial.println("  Server:       " + config_->getMQTTServer());
        Serial.println("  Port:         " + String(config_->getMQTTPort()));
        Serial.println("  Username:     " + config_->getMQTTUsername());
        Serial.println("  Password:     " + String(config_->getMQTTPassword().length() > 0 ? "****** (set)" : "(not set)"));
        Serial.println("  Client ID:    " + config_->getMQTTClientId());
        Serial.println("  Auto Connect: " + String(config_->isMQTTAutoConnect() ? "Enabled" : "Disabled"));
    } else {
        Serial.println("  ❌ No MQTT credentials configured");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::showSerialNumber() {
    printSeparator();
    Serial.println("🔢 SERIAL NUMBER");
    printSeparator();
    Serial.println();
    Serial.println("  Serial Number: " + config_->getSerialNumber());
    Serial.println();
    
    if (config_->getSerialNumber().startsWith("SN-")) {
        Serial.println("  ⚠️  This is a fallback serial number (based on MAC)");
        Serial.println("     Set a proper serial number via Factory Mode");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::resetMQTTConfig() {
    printSeparator();
    Serial.println("🔄 RESET MQTT CONFIGURATION");
    printSeparator();
    Serial.println();
    
    // Reset MQTT credentials using RuntimeConfig method
    config_->resetMQTTCredentials();
    
    // Save configuration
    if (config_->save()) {
        Serial.println("✅ MQTT configuration reset successfully!");
        Serial.println("   • MQTT Server: Cleared");
        Serial.println("   • MQTT Port: Reset to 1883");
        Serial.println("   • MQTT Username: Cleared");
        Serial.println("   • MQTT Password: Cleared");
        Serial.println("   • MQTT Client ID: Cleared");
        Serial.println("   • Auto Connect: Disabled");
        Serial.println();
        Serial.println("📱 Device will need MQTT reconfiguration via Factory Mode.");
    } else {
        Serial.println("❌ Failed to save MQTT configuration reset!");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::resetWiFiConfig() {
    printSeparator();
    Serial.println("🔄 RESET WIFI CONFIGURATION");
    printSeparator();
    Serial.println();
    
    // Show current WiFi status before reset
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("⚠️  Currently connected to: " + WiFi.SSID());
        Serial.println("   This will disconnect the device from WiFi!");
        Serial.println();
    }
    
    // Reset WiFi credentials using RuntimeConfig method
    config_->resetWiFiCredentials();
    
    // Disconnect from current WiFi
    WiFi.disconnect(true); // true = erase stored credentials
    
    // Save configuration
    if (config_->save()) {
        Serial.println("✅ WiFi configuration reset successfully!");
        Serial.println("   • WiFi SSID: Cleared");
        Serial.println("   • WiFi Password: Cleared");
        Serial.println("   • Auto Connect: Disabled");
        Serial.println("   • WiFi Status: Disconnected");
        Serial.println();
        Serial.println("📱 Device will need WiFi reconfiguration via Factory Mode or BLE.");
        Serial.println("🔄 Device will enter Factory Mode on next reboot if no other");
        Serial.println("   network configuration is available.");
    } else {
        Serial.println("❌ Failed to save WiFi configuration reset!");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::setMQTTConfig() {
    printSeparator();
    Serial.println("⚙️  CONFIGURE MQTT");
    printSeparator();
    Serial.println();
    Serial.println("📝 Please provide the following MQTT configuration:");
    Serial.println("   (Press Ctrl+C or type 'cancel' to abort)");
    Serial.println();
    
    // Initialize MQTT config mode
    mqttConfigPending_ = true;
    mqttConfigStep_ = 0;
    mqttServer_ = "";
    mqttPort_ = 1883;
    mqttUsername_ = "";
    mqttPassword_ = "";
    
    Serial.print("📡 MQTT Server (IP or hostname): ");
}

void SerialCommandHandler::setWiFiConfig() {
    printSeparator();
    Serial.println("📶 CONFIGURE WIFI");
    printSeparator();
    Serial.println();
    
    // Show current WiFi status
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("⚠️  Currently connected to: " + WiFi.SSID());
        Serial.println("   This will disconnect and reconfigure WiFi!");
        Serial.println();
    }
    
    Serial.println("📝 Please provide the following WiFi configuration:");
    Serial.println("   (Press Ctrl+C or type 'cancel' to abort)");
    Serial.println();
    
    // Initialize WiFi config mode
    wifiConfigPending_ = true;
    wifiConfigStep_ = 0;
    wifiSSID_ = "";
    wifiPassword_ = "";
    
    Serial.print("📶 WiFi SSID (Network Name): ");
}

void SerialCommandHandler::handleMQTTConfigInput(const String& input) {
    String value = input;
    value.trim();
    
    // Check for cancel
    if (value.equalsIgnoreCase("cancel") || value.equalsIgnoreCase("abort")) {
        Serial.println();
        Serial.println("❌ MQTT configuration cancelled");
        mqttConfigPending_ = false;
        printSeparator();
        return;
    }
    
    switch (mqttConfigStep_) {
        case 0: // MQTT Server
            if (value.length() == 0) {
                Serial.println("❌ MQTT Server cannot be empty. Please try again:");
                Serial.print("📡 MQTT Server (IP or hostname): ");
                return;
            }
            mqttServer_ = value;
            mqttConfigStep_++;
            Serial.print("🔌 MQTT Port (default 1883): ");
            break;
            
        case 1: // MQTT Port
            if (value.length() == 0) {
                mqttPort_ = 1883; // Use default
            } else {
                mqttPort_ = value.toInt();
                if (mqttPort_ <= 0 || mqttPort_ > 65535) {
                    Serial.println("❌ Invalid port. Please enter a number between 1-65535:");
                    Serial.print("🔌 MQTT Port (default 1883): ");
                    return;
                }
            }
            mqttConfigStep_++;
            Serial.print("👤 MQTT Username: ");
            break;
            
        case 2: // MQTT Username
            mqttUsername_ = value; // Can be empty
            mqttConfigStep_++;
            Serial.print("🔑 MQTT Password: ");
            break;
            
        case 3: // MQTT Password
            mqttPassword_ = value; // Can be empty
            
            // Show summary and save
            Serial.println();
            printSeparator();
            Serial.println("📋 MQTT CONFIGURATION SUMMARY:");
            printSeparator();
            Serial.println("  Server:   " + mqttServer_);
            Serial.println("  Port:     " + String(mqttPort_));
            Serial.println("  Username: " + (mqttUsername_.length() > 0 ? mqttUsername_ : String("(empty)")));
            Serial.println("  Password: " + String(mqttPassword_.length() > 0 ? "********" : "(empty)"));
            Serial.println();
            
            // Save configuration
            config_->setMQTTCredentials(mqttServer_, mqttPort_, mqttUsername_, mqttPassword_);
            config_->setMQTTAutoConnect(true); // Enable auto connect
            
            if (config_->save()) {
                Serial.println("✅ MQTT configuration saved successfully!");
                Serial.println("🔄 MQTT will attempt to connect automatically.");
            } else {
                Serial.println("❌ Failed to save MQTT configuration!");
            }
            
            mqttConfigPending_ = false;
            Serial.println();
            printSeparator();
            break;
            
        default:
            mqttConfigPending_ = false;
            break;
    }
}

void SerialCommandHandler::handleWiFiConfigInput(const String& input) {
    String value = input;
    value.trim();
    
    // Check for cancel
    if (value.equalsIgnoreCase("cancel") || value.equalsIgnoreCase("abort")) {
        Serial.println();
        Serial.println("❌ WiFi configuration cancelled");
        wifiConfigPending_ = false;
        printSeparator();
        return;
    }
    
    switch (wifiConfigStep_) {
        case 0: // WiFi SSID
            if (value.length() == 0) {
                Serial.println("❌ WiFi SSID cannot be empty. Please try again:");
                Serial.print("📶 WiFi SSID (Network Name): ");
                return;
            }
            if (value.length() > MAX_SSID_LENGTH) {
                Serial.println("❌ SSID too long (max " + String(MAX_SSID_LENGTH) + " chars). Please try again:");
                Serial.print("📶 WiFi SSID (Network Name): ");
                return;
            }
            wifiSSID_ = value;
            wifiConfigStep_++;
            Serial.print("🔐 WiFi Password (leave empty for open network): ");
            break;
            
        case 1: // WiFi Password
            if (value.length() > MAX_PASSWORD_LENGTH) {
                Serial.println("❌ Password too long (max " + String(MAX_PASSWORD_LENGTH) + " chars). Please try again:");
                Serial.print("🔐 WiFi Password (leave empty for open network): ");
                return;
            }
            wifiPassword_ = value; // Can be empty for open networks
            
            // Show summary and save
            Serial.println();
            printSeparator();
            Serial.println("📋 WIFI CONFIGURATION SUMMARY:");
            printSeparator();
            Serial.println("  SSID:     " + wifiSSID_);
            Serial.println("  Password: " + String(wifiPassword_.length() > 0 ? "********" : "(open network)"));
            Serial.println();
            
            // Disconnect current WiFi first
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("📱 Disconnecting from current network...");
                WiFi.disconnect(true);
                delay(1000);
            }
            
            // Save configuration
            config_->setWiFiCredentials(wifiSSID_, wifiPassword_);
            config_->setWiFiAutoConnect(true); // Enable auto connect
            
            if (config_->save()) {
                Serial.println("✅ WiFi configuration saved successfully!");
                Serial.println("🔄 Attempting to connect to new network...");
                
                // Try to connect
                WiFi.begin(wifiSSID_.c_str(), wifiPassword_.c_str());
                
                // Wait for connection (max 15 seconds)
                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                    delay(500);
                    Serial.print(".");
                    attempts++;
                }
                
                Serial.println();
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("✅ Successfully connected to: " + WiFi.SSID());
                    Serial.println("📡 IP Address: " + WiFi.localIP().toString());
                    Serial.println("📶 Signal Strength: " + String(WiFi.RSSI()) + " dBm");
                } else {
                    Serial.println("❌ Failed to connect to WiFi network!");
                    Serial.println("   Please check SSID and password, then try again.");
                    Serial.println("   Or use 'resetwifi' to clear and reconfigure.");
                }
            } else {
                Serial.println("❌ Failed to save WiFi configuration!");
            }
            
            wifiConfigPending_ = false;
            Serial.println();
            printSeparator();
            break;
            
        default:
            wifiConfigPending_ = false;
            break;
    }
}

void SerialCommandHandler::mockRegistrationAck() {
    printSeparator();
    Serial.println("🤖 SIMULATE REGISTRATION ACK");
    printSeparator();
    Serial.println();
    
    if (!leafNode_) {
        Serial.println("❌ Error: LeafNode instance not available for ACK simulation");
        Serial.println("   This command requires access to the LeafNode instance.");
        Serial.println();
        printSeparator();
        return;
    }
    
    // Check if device is already setup
    if (config_->isDeviceSetup()) {
        Serial.println("⚠️  Device is already marked as setup complete!");
        Serial.println("   Serial Number: " + config_->getSerialNumber());
        Serial.println("   Use 'reset' command to restart setup process if needed.");
        Serial.println();
        printSeparator();
        return;
    }
    
    Serial.println("📡 Simulating MQTT registration ACK...");
    Serial.println("   This will skip the 30-second timeout and mark setup as complete.");
    Serial.println();
    
    // Create mock registration ACK payload
    String serialNumber = config_->getSerialNumber();
    String mockPayload = "{\"serial_number\":\"" + serialNumber + "\",\"status\":\"registered\",\"timestamp\":" + String(millis()) + "}";
    
    Serial.println("📋 Mock ACK Payload:");
    Serial.println("   " + mockPayload);
    Serial.println();
    
    // Call the registration ACK handler directly
    Serial.println("🔄 Processing mock registration ACK...");
    leafNode_->simulateRegistrationAck(mockPayload);
    
    Serial.println("✅ Mock registration ACK processed!");
    Serial.println("   Device should now be marked as setup complete.");
    Serial.println("   The 30-second timeout should be cancelled.");
    Serial.println();
    
    printSeparator();
}

void SerialCommandHandler::unregisterDevice() {
    printSeparator();
    Serial.println("🔄 RESET REGISTRATION STATUS");
    printSeparator();
    Serial.println();
    
    // Check if device is currently registered
    if (!config_->isDeviceSetup()) {
        Serial.println("⚠️  Device is not registered yet!");
        Serial.println("   setup_complete is already false.");
        Serial.println();
        printSeparator();
        return;
    }
    
    Serial.println("📋 Current Status:");
    Serial.println("   Serial Number:  " + config_->getSerialNumber());
    Serial.println("   Setup Complete: Yes");
    Serial.println();
    
    Serial.println("🔄 Resetting registration status...");
    Serial.println("   Setting setup_complete = false");
    
    // Reset the setup complete flag
    config_->setDeviceSetup(false);
    
    if (!config_->save()) {
        Serial.println("❌ Error: Failed to save configuration!");
        Serial.println();
        printSeparator();
        return;
    }
    
    Serial.println("   ✓ Configuration saved");
    Serial.println();
    Serial.println("✅ Registration status reset successfully!");
    Serial.println("   Device will wait for registration on next boot.");
    Serial.println();
    Serial.println("🔄 Rebooting in 2 seconds...");
    
    printSeparator();
    delay(2000);
    ESP.restart();
}

void SerialCommandHandler::printSeparator() {
    Serial.println("================================================================");
}

void SerialCommandHandler::printPrompt() {
    Serial.print("> ");
}

void SerialCommandHandler::showLEDStatus() {
    printSeparator();
    Serial.println("💡 LED STATUS");
    printSeparator();
    Serial.println();
    
    if (leafNode_ && leafNode_->getStatusLED()) {
        auto led = leafNode_->getStatusLED();
        uint8_t r, g, b;
        led->getCurrentRGB(r, g, b);
        
        Serial.println("LED Information:");
        Serial.println("  Pin:        GPIO 48");
        Serial.println("  Type:       SK6812 (SK68XXMINI-HS)");
        Serial.println("  Color Mode: GRB");
        Serial.println("  Brightness: " + String(led->getBrightness()));
        Serial.println();
        Serial.println("Current Color:");
        Serial.println("  Red:   " + String(r));
        Serial.println("  Green: " + String(g));
        Serial.println("  Blue:  " + String(b));
        Serial.println();
        
        // Convert LEDStatus enum to string
        String statusStr = "Unknown";
        switch (led->getStatus()) {
            case LEDStatus::OFF: statusStr = "OFF"; break;
            case LEDStatus::FACTORY_MODE: statusStr = "FACTORY_MODE"; break;
            case LEDStatus::BOOTING: statusStr = "BOOTING"; break;
            case LEDStatus::BLE_CONFIG: statusStr = "BLE_CONFIG"; break;
            case LEDStatus::BLE_SCANNING: statusStr = "BLE_SCANNING"; break;
            case LEDStatus::CONNECTING: statusStr = "CONNECTING"; break;
            case LEDStatus::WIFI_CONNECTING: statusStr = "WIFI_CONNECTING"; break;
            case LEDStatus::WIFI_RECONNECTING: statusStr = "WIFI_RECONNECTING"; break;
            case LEDStatus::CONNECTED: statusStr = "CONNECTED"; break;
            case LEDStatus::WIFI_CONNECTED: statusStr = "WIFI_CONNECTED"; break;
            case LEDStatus::WIFI_FAILED: statusStr = "WIFI_FAILED"; break;
            case LEDStatus::ERROR: statusStr = "ERROR"; break;
            case LEDStatus::OTA_DOWNLOADING: statusStr = "OTA_DOWNLOADING"; break;
            case LEDStatus::OTA_INSTALLING: statusStr = "OTA_INSTALLING"; break;
            case LEDStatus::SUCCESS_FADE: statusStr = "SUCCESS_FADE"; break;
            case LEDStatus::WAITING_CHAIN_CONFIG: statusStr = "WAITING_CHAIN_CONFIG"; break;
        }
        Serial.println("Status: " + statusStr);
    } else {
        Serial.println("❌ LED not available");
    }
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::testLED() {
    printSeparator();
    Serial.println("🧪 LED TEST SEQUENCE");
    printSeparator();
    Serial.println();
    
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        printSeparator();
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false); // Disable automatic control
    
    Serial.println("Testing LED with color sequence...");
    Serial.println();
    
    Serial.println("🔴 RED (1 second)");
    led->setRGB(255, 0, 0);
    delay(1000);
    
    Serial.println("🟢 GREEN (1 second)");
    led->setRGB(0, 255, 0);
    delay(1000);
    
    Serial.println("🔵 BLUE (1 second)");
    led->setRGB(0, 0, 255);
    delay(1000);
    
    Serial.println("⚪ WHITE (1 second)");
    led->setRGB(255, 255, 255);
    delay(1000);
    
    Serial.println("⚫ OFF");
    led->setRGB(0, 0, 0);
    
    Serial.println();
    Serial.println("✅ LED test complete!");
    Serial.println("   If you didn't see any colors, check:");
    Serial.println("   - LED power supply (VCC to 5V or 3.3V)");
    Serial.println("   - LED ground (GND to GND)");
    Serial.println("   - LED data pin (DIN to GPIO 48)");
    Serial.println("   - Try different color order: RGB, GRB, BGR");
    Serial.println();
    
    led->setAutoMode(true); // Re-enable automatic control
    printSeparator();
}

void SerialCommandHandler::setLEDOn() {
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false);
    led->setRGB(255, 255, 255);
    Serial.println("✅ LED ON (white)");
}

void SerialCommandHandler::setLEDOff() {
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false);
    led->setRGB(0, 0, 0);
    Serial.println("✅ LED OFF");
}

void SerialCommandHandler::setLEDRed() {
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false);
    led->setRGB(255, 0, 0);
    Serial.println("✅ LED set to RED");
}

void SerialCommandHandler::setLEDGreen() {
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false);
    led->setRGB(0, 255, 0);
    Serial.println("✅ LED set to GREEN");
}

void SerialCommandHandler::setLEDBlue() {
    if (!leafNode_ || !leafNode_->getStatusLED()) {
        Serial.println("❌ LED not available");
        return;
    }
    
    auto led = leafNode_->getStatusLED();
    led->setAutoMode(false);
    led->setRGB(0, 0, 255);
    Serial.println("✅ LED set to BLUE");
}

void SerialCommandHandler::showNVSData() {
    printSeparator();
    Serial.println("💾 NVS STORED CONFIGURATION DATA");
    printSeparator();
    Serial.println();
    
    Preferences prefs;
    
    // ========================================================================
    // LEAFNODE NAMESPACE (Main Configuration)
    // ========================================================================
    Serial.println("📦 Namespace: 'leafnode'");
    printSeparator();
    
    if (!prefs.begin("leafnode", true)) { // read-only
        Serial.println("❌ Failed to open 'leafnode' namespace");
    } else {
        String configJson = prefs.getString("config", "");
        
        if (configJson.isEmpty()) {
            Serial.println("⚠️  No configuration data found in NVS");
        } else {
            Serial.println();
            Serial.println("Raw JSON Configuration:");
            Serial.println("Size: " + String(configJson.length()) + " bytes");
            Serial.println();
            
            #ifdef PRODUCTION_MODE
            // In production mode, mask sensitive data
            String maskedJson = configJson;
            
            // Mask WiFi password
            int wifiPassStart = maskedJson.indexOf("\"wifi_password\":\"");
            if (wifiPassStart != -1) {
                int passStart = wifiPassStart + 17; // Length of "wifi_password":"
                int passEnd = maskedJson.indexOf("\"", passStart);
                if (passEnd != -1) {
                    String replacement = "";
                    for (int i = 0; i < (passEnd - passStart); i++) replacement += "*";
                    maskedJson = maskedJson.substring(0, passStart) + replacement + maskedJson.substring(passEnd);
                }
            }
            
            // Mask MQTT password
            int mqttPassStart = maskedJson.indexOf("\"mqtt_password\":\"");
            if (mqttPassStart != -1) {
                int passStart = mqttPassStart + 17; // Length of "mqtt_password":"
                int passEnd = maskedJson.indexOf("\"", passStart);
                if (passEnd != -1) {
                    String replacement = "";
                    for (int i = 0; i < (passEnd - passStart); i++) replacement += "*";
                    maskedJson = maskedJson.substring(0, passStart) + replacement + maskedJson.substring(passEnd);
                }
            }
            
            Serial.println(maskedJson);
            Serial.println();
            Serial.println("⚠️  Passwords masked in Production Mode");
            #else
            // In development mode, show everything
            Serial.println(configJson);
            #endif
        }
        
        prefs.end();
    }
    
    Serial.println();
    printSeparator();
    
    // ========================================================================
    // FACTORY NAMESPACE (Serial Number)
    // ========================================================================
    Serial.println("📦 Namespace: 'factory'");
    printSeparator();
    
    if (!prefs.begin("factory", true)) { // read-only
        Serial.println("⚠️  No 'factory' namespace found (Serial Number not set)");
    } else {
        String serialNumber = prefs.getString("serial_number", "");
        
        Serial.println();
        if (serialNumber.isEmpty()) {
            Serial.println("⚠️  No serial number stored in factory namespace");
        } else {
            Serial.println("Serial Number: " + serialNumber);
        }
        
        prefs.end();
    }
    
    Serial.println();
    printSeparator();
    
    // ========================================================================
    // NVS STATISTICS
    // ========================================================================
    Serial.println("📊 NVS Statistics");
    printSeparator();
    Serial.println();
    
    // Get NVS stats for leafnode namespace
    if (prefs.begin("leafnode", true)) {
        size_t usedEntries = prefs.freeEntries();
        Serial.println("Leafnode namespace:");
        Serial.println("  Free entries: " + String(usedEntries));
        prefs.end();
    }
    
    // Get total NVS partition info
    Serial.println();
    Serial.println("Heap Memory:");
    Serial.println("  Free: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("  Min Free: " + String(ESP.getMinFreeHeap()) + " bytes");
    
    Serial.println();
    printSeparator();
}

void SerialCommandHandler::scanI2CBus() {
    printSeparator();
    Serial.println("🔍 I2C BUS SCANNER");
    printSeparator();
    Serial.println();
    Serial.println("Scanning I2C bus (addresses 0x01 to 0x7F)...");
    Serial.println();
    
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    delay(10);
    
    bool deviceFound = false;
    int deviceCount = 0;
    String foundAddresses = "";
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
            deviceFound = true;
            deviceCount++;
            
            if (foundAddresses.length() > 0) {
                foundAddresses += ", ";
            }
            
            foundAddresses += "0x";
            if (addr < 16) foundAddresses += "0";
            foundAddresses += String(addr, HEX);
            foundAddresses += " (" + String(addr) + ")";
        }
        
        delay(1);
    }
    
    Serial.println();
    printSeparator();
    
    if (deviceFound) {
        Serial.print("✅ Found ");
        Serial.print(deviceCount);
        Serial.print(" device");
        if (deviceCount > 1) Serial.print("s");
        Serial.println(" on I2C bus:");
        Serial.println();
        Serial.println("   " + foundAddresses);
    } else {
        Serial.println("❌ No I2C devices found!");
        Serial.println();
        Serial.println("Troubleshooting:");
        Serial.println("  • Wiring: SDA -> GPIO" + String(I2C_SDA_PIN) + ", SCL -> GPIO" + String(I2C_SCL_PIN));
        Serial.println("  • Power supply (3.3V or 5V)");
        Serial.println("  • Pull-up resistors (4.7kΩ)");
    }
    
    printSeparator();
}

// ============================================================================
// FACTORY MODE COMMANDS
// ============================================================================

void SerialCommandHandler::showFactoryMenu() {
    printSeparator();
    Serial.println("🏭 FACTORY CONFIGURATION MODE");
    printSeparator();
    Serial.println();
    
    // Configuration status
    Serial.println("Device Configuration Status:");
    Serial.println();
    
    // Serial Number
    String serialNumber = config_->getSerialNumber();
    bool hasSerial = !serialNumber.startsWith("SN-"); // Not a fallback MAC-based serial
    Serial.print("  Serial Number:  ");
    if (hasSerial) {
        Serial.print("✅ " + serialNumber);
    } else {
        Serial.print("❌ [NOT SET] (using fallback: " + serialNumber + ")");
    }
    Serial.println();
    
    // Sensor
    bool hasSensor = config_->hasSensorConfiguration();
    Serial.print("  Sensor Type:    ");
    if (hasSensor) {
        Serial.print("✅ " + config_->getSensorName());
    } else {
        Serial.print("❌ [NOT SET]");
    }
    Serial.println();
    
    // WiFi
    bool hasWiFi = config_->hasWiFiCredentials();
    Serial.print("  WiFi:           ");
    if (hasWiFi) {
        Serial.print("✅ " + config_->getWiFiSSID());
    } else {
        Serial.print("❌ [NOT SET]");
    }
    Serial.println();
    
    // MQTT
    bool hasMQTT = config_->hasMQTTCredentials();
    Serial.print("  MQTT:           ");
    if (hasMQTT) {
        Serial.print("✅ " + config_->getMQTTServer() + ":" + String(config_->getMQTTPort()));
    } else {
        Serial.print("❌ [NOT SET]");
    }
    Serial.println();
    
    Serial.println();
    printSeparator();
    Serial.println();
    
    // Available commands
    Serial.println("Available Factory Commands:");
    Serial.println();
    Serial.println("  setserial  - Configure device serial number");
    Serial.println("  setsensor  - Configure sensor type (or: setsensor <NAME>)");
    Serial.println("  setwifi    - Configure WiFi credentials");
    Serial.println("  setmqtt    - Configure MQTT server");
    Serial.println("  status     - Show detailed device status");
    Serial.println("  reboot     - Reboot device");
    Serial.println("  factory    - Show this menu again");
    Serial.println();
    printSeparator();
    Serial.println();
    
    // Check if ready to exit factory mode
    if (hasSerial && hasSensor && hasWiFi && hasMQTT) {
        Serial.println("✅ ALL CONFIGURATION COMPLETE!");
        Serial.println("   Device is ready to exit Factory Mode.");
        Serial.println("   Type 'reboot' to restart in normal mode.");
        Serial.println();
    } else {
        Serial.println("⚠️  Configuration incomplete. Please configure all settings.");
        Serial.println();
    }
}

void SerialCommandHandler::setSerialNumber() {
    if (!factoryMode_) {
        Serial.println("❌ Error: 'setserial' command only available in Factory Mode");
        return;
    }
    
    printSeparator();
    Serial.println("🔢 SET SERIAL NUMBER");
    printSeparator();
    Serial.println();
    
    // Show current serial number
    String currentSerial = config_->getSerialNumber();
    bool isFallback = currentSerial.startsWith("SN-");
    
    Serial.println("Current Serial Number:");
    Serial.println("  " + currentSerial);
    if (isFallback) {
        Serial.println("  ⚠️  This is a fallback serial number (based on MAC)");
    }
    Serial.println();
    
    Serial.println("📝 Please enter the new serial number:");
    Serial.println("   Format examples: LN-2025-001, LEAF-ABC123, SN123456");
    Serial.println("   (Type 'cancel' to abort)");
    Serial.println();
    
    // Enter serial config mode
    serialConfigPending_ = true;
    
    Serial.print("Serial Number: ");
}

void SerialCommandHandler::handleSerialConfigInput(const String& input) {
    String value = input;
    value.trim();
    
    // Check for cancel
    if (value.equalsIgnoreCase("cancel") || value.equalsIgnoreCase("abort")) {
        Serial.println();
        Serial.println("❌ Serial number configuration cancelled");
        serialConfigPending_ = false;
        printSeparator();
        return;
    }
    
    // Validate length
    if (value.length() == 0) {
        Serial.println();
        Serial.println("❌ Serial number cannot be empty. Please try again:");
        Serial.print("Serial Number: ");
        return;
    }
    
    if (value.length() > MAX_SERIAL_NUMBER_LENGTH) {
        Serial.println();
        Serial.println("❌ Serial number too long (max " + String(MAX_SERIAL_NUMBER_LENGTH) + " chars)");
        Serial.print("Serial Number: ");
        return;
    }
    
    // Save to NVS factory partition (same as WebServer did)
    Serial.println();
    Serial.println();
    Serial.println("💾 Saving serial number to factory storage...");
    
    Preferences factoryPrefs;
    if (!factoryPrefs.begin("factory", false)) { // Read-write
        Serial.println("❌ Failed to open factory storage");
        serialConfigPending_ = false;
        printSeparator();
        return;
    }
    
    factoryPrefs.putString("serial_number", value);
    factoryPrefs.end();
    
    // Also update runtime config
    config_->setSerialNumber(value);
    
    if (!config_->save()) {
        Serial.println("❌ Failed to save runtime configuration");
        serialConfigPending_ = false;
        printSeparator();
        return;
    }
    
    Serial.println("✅ Serial number saved successfully!");
    Serial.println("   Serial Number: " + value);
    Serial.println();
    
    serialConfigPending_ = false;
    printSeparator();
}

bool SerialCommandHandler::mapSensorNameToProfile(const String& name, SensorProfile& profile, String& sensorName, uint32_t& interval) {
    String upperName = name;
    upperName.toUpperCase();
    
    if (upperName == "SLT5007") {
        profile = SensorProfile::SLT5007;
        sensorName = "SLT5007";
        interval = 180000; // 3 minutes
        return true;
    }
    else if (upperName == "SHT31") {
        profile = SensorProfile::SHT31;
        sensorName = "SHT31";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "CWTPSS") {
        profile = SensorProfile::CWTPSS;
        sensorName = "CWTPSS";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "LEAFTHSN") {
        profile = SensorProfile::LEAFTHSN;
        sensorName = "LEAFTHSN";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "CWTSOILTHS") {
        profile = SensorProfile::CWTSOILTHS;
        sensorName = "CWTSoilTHS";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "TEROS12") {
        profile = SensorProfile::TEROS12;
        sensorName = "TEROS12";
        interval = 180000; // 3 minutes
        return true;
    }
    else if (upperName == "EZOPH") {
        profile = SensorProfile::EZOPH;
        sensorName = "EZOph";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "EZOEC") {
        profile = SensorProfile::EZOEC;
        sensorName = "EZOec";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "DS18B20") {
        profile = SensorProfile::DS18B20;
        sensorName = "DS18B20";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "CWTTHXXS") {
        profile = SensorProfile::CWTTHXXS;
        sensorName = "CWTTHXXS";
        interval = 60000; // 1 minute
        return true;
    }
    else if (upperName == "NONE") {
        profile = SensorProfile::NONE;
        sensorName = "None";
        interval = 60000; // 1 minute
        return true;
    }
    
    return false;
}

void SerialCommandHandler::setSensorProfile(const String& directSensorName) {
    if (!factoryMode_) {
        Serial.println("❌ Error: 'setsensor' command only available in Factory Mode");
        return;
    }
    
    // ========================================================================
    // DIRECT MODE: setsensor <sensor_name>
    // ========================================================================
    if (directSensorName.length() > 0) {
        SensorProfile profile;
        String sensorName;
        uint32_t interval;
        
        if (!mapSensorNameToProfile(directSensorName, profile, sensorName, interval)) {
            Serial.println("❌ Error: Unknown sensor type '" + directSensorName + "'");
            Serial.println();
            Serial.println("Valid sensor types:");
            Serial.println("  SLT5007, SHT31, CWTPSS, LEAFTHSN, CWTSOILTHS,");
            Serial.println("  TEROS12, EZOPH, EZOEC, DS18B20, CWTTHXXS, NONE");
            return;
        }
        
        // Save configuration directly
        config_->setSensorProfile(profile);
        config_->setSensorName(sensorName);
        config_->setSensorReadingInterval(interval);
        
        if (!config_->save()) {
            Serial.println("❌ Failed to save sensor configuration");
            return;
        }
        
        Serial.println("✅ Sensor configured: " + sensorName + " (" + String(interval / 1000) + "s interval)");
        return;
    }
    
    // ========================================================================
    // INTERACTIVE MODE: setsensor (with menu)
    // ========================================================================
    printSeparator();
    Serial.println("🌡️  SET SENSOR PROFILE");
    printSeparator();
    Serial.println();
    
    // Show current sensor
    if (config_->hasSensorConfiguration()) {
        Serial.println("Current Sensor:");
        Serial.println("  Type:     " + config_->getSensorName());
        Serial.println("  Interval: " + String(config_->getSensorReadingInterval() / 1000) + " seconds");
        Serial.println();
    }
    
    Serial.println("📋 Available Sensor Types:");
    Serial.println();
    Serial.println("  1) SLT5007     - RS485 Soil Moisture Sensor (3 min interval)");
    Serial.println("  2) SHT31       - I2C Temperature & Humidity (1 min interval)");
    Serial.println("  3) CWTPSS      - RS485 Pressure Sensor (1 min interval)");
    Serial.println("  4) LEAFTHSN    - RS485 Temp/Humidity/Soil (1 min interval)");
    Serial.println("  5) CWTSOILTHS  - RS485 Soil Sensor (1 min interval)");
    Serial.println("  6) TEROS12     - SDI-12 Soil Sensor (3 min interval)");
    Serial.println("  7) EZOPH       - I2C pH Sensor (1 min interval)");
    Serial.println("  8) EZOEC       - I2C EC Sensor (1 min interval)");
    Serial.println("  9) DS18B20     - OneWire Temperature Sensor (1 min interval)");
    Serial.println(" 10) CWTTHXXS    - RS485 Air Temp/Humidity (1 min interval)");
    Serial.println(" 11) NONE        - No sensor (testing only)");
    Serial.println();
    Serial.println("   (Type 'cancel' to abort, or enter sensor name directly)");
    Serial.println();
    
    // Enter sensor config mode
    sensorConfigPending_ = true;
    sensorConfigStep_ = 0;
    
    Serial.print("Select sensor (1-11 or name): ");
}

void SerialCommandHandler::handleSensorConfigInput(const String& input) {
    String value = input;
    value.trim();
    
    // Check for cancel
    if (value.equalsIgnoreCase("cancel") || value.equalsIgnoreCase("abort")) {
        Serial.println();
        Serial.println("❌ Sensor configuration cancelled");
        sensorConfigPending_ = false;
        printSeparator();
        return;
    }
    
    // Map selection to sensor profile
    SensorProfile profile;
    String sensorName;
    uint32_t defaultInterval;
    
    // Try to match sensor name first (handles both direct names and numbers)
    if (mapSensorNameToProfile(value, profile, sensorName, defaultInterval)) {
        // Successfully matched a sensor name directly
        // Continue to save configuration below
    }
    else {
        // Not a sensor name, try numeric selection
        int selection = value.toInt();
        
        if (selection < 1 || selection > 11) {
            Serial.println();
            Serial.println("❌ Invalid selection. Enter a number (1-11) or sensor name:");
            Serial.print("Select sensor (1-11 or name): ");
            return;
        }
        
        // Map number to sensor profile
        switch (selection) {
            case 1:
                profile = SensorProfile::SLT5007;
                sensorName = "SLT5007";
                defaultInterval = 180000; // 3 minutes
                break;
            case 2:
                profile = SensorProfile::SHT31;
                sensorName = "SHT31";
                defaultInterval = 60000; // 1 minute
                break;
            case 3:
                profile = SensorProfile::CWTPSS;
                sensorName = "CWTPSS";
                defaultInterval = 60000; // 1 minute
                break;
            case 4:
                profile = SensorProfile::LEAFTHSN;
                sensorName = "LEAFTHSN";
                defaultInterval = 60000; // 1 minute
                break;
            case 5:
                profile = SensorProfile::CWTSOILTHS;
                sensorName = "CWTSoilTHS";
                defaultInterval = 60000; // 1 minute
                break;
            case 6:
                profile = SensorProfile::TEROS12;
                sensorName = "TEROS12";
                defaultInterval = 180000; // 3 minutes
                break;
            case 7:
                profile = SensorProfile::EZOPH;
                sensorName = "EZOph";
                defaultInterval = 60000; // 1 minute
                break;
            case 8:
                profile = SensorProfile::EZOEC;
                sensorName = "EZOec";
                defaultInterval = 60000; // 1 minute
                break;
            case 9:
                profile = SensorProfile::DS18B20;
                sensorName = "DS18B20";
                defaultInterval = 60000; // 1 minute
                break;
            case 10:
                profile = SensorProfile::CWTTHXXS;
                sensorName = "CWTTHXXS";
                defaultInterval = 60000; // 1 minute
                break;
            case 11:
                profile = SensorProfile::NONE;
                sensorName = "None";
                defaultInterval = 60000; // 1 minute fallback
                break;
            default:
                Serial.println();
                Serial.println("❌ Invalid selection");
                sensorConfigPending_ = false;
                printSeparator();
                return;
        }
    }
    
    // Save sensor configuration
    Serial.println();
    Serial.println();
    Serial.println("💾 Saving sensor configuration...");
    
    config_->setSensorProfile(profile);
    config_->setSensorName(sensorName);
    config_->setSensorReadingInterval(defaultInterval);
    
    if (!config_->save()) {
        Serial.println("❌ Failed to save sensor configuration");
        sensorConfigPending_ = false;
        printSeparator();
        return;
    }
    
    Serial.println("✅ Sensor configuration saved successfully!");
    Serial.println();
    Serial.println("   Sensor Type:      " + sensorName);
    Serial.println("   Reading Interval: " + String(defaultInterval / 1000) + " seconds (" + String(defaultInterval / 60000.0, 1) + " min)");
    Serial.println("   Profile ID:       " + String(static_cast<int>(profile)));
    Serial.println();
    
    sensorConfigPending_ = false;
    printSeparator();
}

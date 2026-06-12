#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/**
 * @brief LeafNode Configuration Reset Tool (Cleaned Version)
 * 
 * This utility resets configurations stored in the ESP32 Preferences
 * that are used by the LeafNode firmware. Uses unified configuration
 * structure without redundant locations.
 * 
 * Usage:
 * 1. Flash this firmware to the same ESP32-C3 device
 * 2. Open Serial Monitor (115200 baud)
 * 3. Follow the prompts to reset configurations
 * 4. Flash back the original LeafNode firmware
 */

// Configuration namespace and keys (must match LeafNode firmware)
const char* CONFIG_NAMESPACE = "leafnode";
const char* CONFIG_KEY = "config";

// Function prototypes
void printHeader();
void printCurrentConfig();
void resetWiFiCredentials();
void resetSetupStatus();
void resetAllConfig();
void cleanRedundantConfig();
void showMenu();
void handleMenuSelection();
String getConfigAsString();
bool saveConfig(const DynamicJsonDocument& config);
DynamicJsonDocument loadConfig();

Preferences preferences;

void setup() {
    Serial.begin(115200);
    delay(2000); // Give Serial time to initialize
    
    printHeader();
    
    // Initialize preferences
    if (!preferences.begin(CONFIG_NAMESPACE, false)) {
        Serial.println("❌ ERROR: Failed to initialize preferences!");
        Serial.println("Please check the ESP32 and try again.");
        while (true) {
            delay(1000);
        }
    }
    
    Serial.println("✅ Connected to ESP32 Preferences");
    Serial.println();
    
    // Show current configuration
    printCurrentConfig();
    
    // Show menu
    showMenu();
}

void loop() {
    if (Serial.available()) {
        handleMenuSelection();
    }
    delay(100);
}

void printHeader() {
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║              LeafNode Configuration Reset Tool            ║");
    Serial.println("║                     Version 1.2 (Cleaned)                ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println();
    Serial.println("🔧 This tool can reset various configurations stored by LeafNode firmware");
    Serial.println("⚠️  Use with caution - changes are permanent!");
    Serial.println();
}

void printCurrentConfig() {
    Serial.println("📄 CURRENT CONFIGURATION:");
    Serial.println("═══════════════════════════");
    
    DynamicJsonDocument config = loadConfig();
    
    if (config.isNull()) {
        Serial.println("❌ No configuration found or configuration is empty");
        Serial.println("   This might be a fresh device or the configuration is corrupted.");
        return;
    }
    
    // Display device information
    Serial.println("Device Information:");
    Serial.print("  • Device Name: ");
    Serial.println(config["device"]["name"].as<String>());
    Serial.print("  • Serial Number: ");
    Serial.println(config["device"]["serial_number"].as<String>());
    Serial.print("  • Setup Complete: ");
    Serial.println(config["device"]["setup_complete"].as<bool>() ? "✅ YES" : "❌ NO");
    
    Serial.println();
    Serial.println("WiFi Configuration:");
    
    // Single WiFi configuration location (unified structure)
    String ssid = config["wifi"]["ssid"].as<String>();
    String password = config["wifi"]["password"].as<String>();
    bool autoConnect = config["wifi"]["auto_connect"].as<bool>();
    
    if (ssid.length() > 0) {
        Serial.print("  • SSID: ");
        Serial.println(ssid);
        Serial.print("  • Password: ");
        // Mask password for security
        if (password.length() > 0) {
            Serial.print("*");
            for (int i = 1; i < password.length(); i++) {
                Serial.print("*");
            }
            Serial.println(" (hidden)");
        } else {
            Serial.println("(not set)");
        }
        Serial.print("  • Auto Connect: ");
        Serial.println(autoConnect ? "Enabled" : "Disabled");
    } else {
        Serial.println("  • No WiFi credentials configured");
    }
    
    Serial.println();
    Serial.println("MQTT Configuration:");
    Serial.print("  • MQTT Server: ");
    Serial.println(config["mqtt"]["server"].as<String>());
    Serial.print("  • MQTT Port: ");
    Serial.println(config["mqtt"]["port"].as<int>());
    Serial.print("  • MQTT Username: ");
    Serial.println(config["mqtt"]["username"].as<String>());
    Serial.print("  • MQTT Client ID: ");
    Serial.println(config["mqtt"]["client_id"].as<String>());
    
    Serial.println();
    Serial.println("Other Settings:");
    Serial.print("  • Log Level: ");
    Serial.println(config["logging"]["level"].as<String>());
    Serial.print("  • Debug Mode: ");
    Serial.println(config["system"]["debug_mode"].as<bool>() ? "Enabled" : "Disabled");
    Serial.print("  • Heartbeat Interval: ");
    Serial.println(String(config["system"]["heartbeat_interval"].as<int>()) + "ms");
    
    Serial.println();
}

void showMenu() {
    Serial.println("🔽 RESET OPTIONS:");
    Serial.println("═══════════════════");
    Serial.println("1️⃣  Reset WiFi credentials only");
    Serial.println("2️⃣  Reset device setup status (setup_complete = false)");
    Serial.println("3️⃣  Reset entire configuration to defaults");
    Serial.println("4️⃣  Show current configuration");
    Serial.println("5️⃣  Show configuration as JSON");
    Serial.println("6️⃣  Clean redundant config structure");
    Serial.println("7️⃣  Exit (restart device)");
    Serial.println();
    Serial.print("👉 Enter your choice (1-7): ");
}

void handleMenuSelection() {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    Serial.println(input);
    Serial.println();
    
    if (input == "1") {
        resetWiFiCredentials();
    } else if (input == "2") {
        resetSetupStatus();
    } else if (input == "3") {
        resetAllConfig();
    } else if (input == "4") {
        printCurrentConfig();
    } else if (input == "5") {
        Serial.println("📋 CONFIGURATION AS JSON:");
        Serial.println("═══════════════════════════");
        Serial.println(getConfigAsString());
        Serial.println();
    } else if (input == "6") {
        cleanRedundantConfig();
    } else if (input == "7") {
        Serial.println("🔄 Restarting device...");
        Serial.println("You can now flash the original LeafNode firmware.");
        delay(2000);
        ESP.restart();
    } else {
        Serial.println("❌ Invalid selection. Please enter 1-7.");
    }
    
    Serial.println();
    showMenu();
}

void resetWiFiCredentials() {
    Serial.println("🔄 RESETTING WIFI CREDENTIALS...");
    Serial.println("═══════════════════════════════════");
    
    DynamicJsonDocument config = loadConfig();
    
    if (config.isNull()) {
        Serial.println("❌ No configuration found to modify!");
        return;
    }
    
    // Reset WiFi credentials (single unified location)
    config["wifi"]["ssid"] = "";
    config["wifi"]["password"] = "";
    config["wifi"]["auto_connect"] = false;
    
    if (saveConfig(config)) {
        Serial.println("✅ WiFi credentials have been reset successfully!");
        Serial.println("   • SSID: (cleared)");
        Serial.println("   • Password: (cleared)");
        Serial.println("   • Auto Connect: Disabled");
        Serial.println();
        Serial.println("📱 The device will now use BLE configuration mode on next boot.");
    } else {
        Serial.println("❌ Failed to save configuration!");
    }
}

void resetSetupStatus() {
    Serial.println("🔄 RESETTING DEVICE SETUP STATUS...");
    Serial.println("═══════════════════════════════════════");
    
    DynamicJsonDocument config = loadConfig();
    
    if (config.isNull()) {
        Serial.println("❌ No configuration found to modify!");
        return;
    }
    
    // Get current setup status
    bool currentStatus = config["device"]["setup_complete"].as<bool>();
    Serial.println("Current setup status: " + String(currentStatus ? "COMPLETE" : "NOT COMPLETE"));
    
    // Reset setup status to false
    config["device"]["setup_complete"] = false;
    
    if (saveConfig(config)) {
        Serial.println("✅ Device setup status has been reset successfully!");
        Serial.println("   • Setup Complete: ❌ FALSE");
        Serial.println();
        Serial.println("🔄 The device will now perform initial registration on next boot:");
        Serial.println("   1. Connect to WiFi");
        Serial.println("   2. Connect to MQTT");
        Serial.println("   3. Send registration to: lai/devices/{serial}/register");
        Serial.println("   4. Wait for: lai/devices/{serial}/registration_ack");
        Serial.println("   5. Mark setup as complete");
        Serial.println();
        Serial.println("📡 Use MQTT monitor to observe the registration process:");
        Serial.println("   ./monitor_mqtt.sh");
    } else {
        Serial.println("❌ Failed to save configuration!");
    }
}

void resetAllConfig() {
    Serial.println("⚠️  WARNING: This will reset ALL configuration to defaults!");
    Serial.print("   Type 'CONFIRM' to proceed: ");
    
    // Wait for confirmation
    while (!Serial.available()) {
        delay(100);
    }
    
    String confirmation = Serial.readStringUntil('\n');
    confirmation.trim();
    Serial.println(confirmation);
    
    if (confirmation != "CONFIRM") {
        Serial.println("❌ Reset cancelled.");
        return;
    }
    
    Serial.println();
    Serial.println("🔄 RESETTING ALL CONFIGURATION...");
    Serial.println("═══════════════════════════════════");
    
    // Create default configuration (unified structure)
    DynamicJsonDocument config(2048);
    
    // Device settings
    config["device"]["name"] = "LeafNode-" + String(random(1000, 9999));
    config["device"]["serial_number"] = "LN" + String(ESP.getEfuseMac(), HEX);
    config["device"]["setup_complete"] = false; // Reset setup status
    
    // WiFi settings (single unified location)
    config["wifi"]["ssid"] = "";
    config["wifi"]["password"] = "";
    config["wifi"]["auto_connect"] = false;
    
    // BLE settings
    config["ble"]["key"] = String(random(100000, 999999));
    
    // MQTT settings (empty/default)
    config["mqtt"]["server"] = "";
    config["mqtt"]["port"] = 1883;
    config["mqtt"]["username"] = "";
    config["mqtt"]["password"] = "";
    config["mqtt"]["client_id"] = "";
    config["mqtt"]["auto_connect"] = false;
    
    // System settings
    config["system"]["debug_mode"] = false;
    config["system"]["heartbeat_interval"] = 30000; // 30 seconds
    
    // Logging settings
    config["logging"]["level"] = "INFO";
    
    if (saveConfig(config)) {
        Serial.println("✅ All configuration has been reset to defaults!");
        Serial.println("   • Setup Complete: ❌ FALSE");
        Serial.println("   • WiFi: Cleared");
        Serial.println("   • MQTT: Cleared");
        Serial.println("   • Device is now in factory state");
        Serial.println();
        Serial.println("📱 Use BLE to configure WiFi and MQTT on next boot.");
    } else {
        Serial.println("❌ Failed to save default configuration!");
    }
}

void cleanRedundantConfig() {
    Serial.println("🧹 CLEANING REDUNDANT CONFIG STRUCTURE...");
    Serial.println("═══════════════════════════════════════════");
    
    DynamicJsonDocument config = loadConfig();
    
    if (config.isNull()) {
        Serial.println("❌ No configuration found to clean!");
        return;
    }
    
    bool hasRedundantStructure = false;
    
    // Check if redundant network.wifi structure exists
    if (config.containsKey("network") && config["network"].containsKey("wifi")) {
        Serial.println("🔍 Found redundant 'network.wifi' structure");
        
        // Remove the entire 'network' section
        config.remove("network");
        hasRedundantStructure = true;
        
        Serial.println("✅ Removed redundant 'network.wifi' structure");
    }
    
    // Check for other potential redundant structures
    if (config.containsKey("ble") && config["ble"].containsKey("device_name")) {
        // Clean up device_name if it differs from main device name
        String mainDeviceName = config["device"]["name"].as<String>();
        String bleDeviceName = config["ble"]["device_name"].as<String>();
        
        if (bleDeviceName != mainDeviceName) {
            config["ble"]["device_name"] = mainDeviceName;
            hasRedundantStructure = true;
            Serial.println("✅ Synchronized BLE device name with main device name");
        }
    }
    
    if (hasRedundantStructure) {
        if (saveConfig(config)) {
            Serial.println();
            Serial.println("✅ Configuration cleaned successfully!");
            Serial.println("   • Removed redundant structures");
            Serial.println("   • Configuration is now unified");
            Serial.println();
            Serial.println("📋 Updated configuration:");
            printCurrentConfig();
        } else {
            Serial.println("❌ Failed to save cleaned configuration!");
        }
    } else {
        Serial.println("✅ No redundant structures found - configuration is already clean!");
    }
}

String getConfigAsString() {
    DynamicJsonDocument config = loadConfig();
    
    if (config.isNull()) {
        return "{}";
    }
    
    String output;
    serializeJsonPretty(config, output);
    return output;
}

DynamicJsonDocument loadConfig() {
    DynamicJsonDocument config(2048);
    
    if (!preferences.isKey(CONFIG_KEY)) {
        return config; // Return empty document
    }
    
    String jsonString = preferences.getString(CONFIG_KEY, "{}");
    
    DeserializationError error = deserializeJson(config, jsonString);
    if (error) {
        Serial.println("❌ Error parsing stored configuration: " + String(error.c_str()));
        return DynamicJsonDocument(2048); // Return empty document
    }
    
    return config;
}

bool saveConfig(const DynamicJsonDocument& config) {
    String jsonString;
    serializeJson(config, jsonString);
    
    if (jsonString.length() > 4000) {
        Serial.println("❌ Configuration too large to save!");
        return false;
    }
    
    size_t written = preferences.putString(CONFIG_KEY, jsonString);
    return written == jsonString.length();
}

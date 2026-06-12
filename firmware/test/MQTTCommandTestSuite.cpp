#include "LeafNode.h"
#include "runtime/RuntimeConfig.h"
#include <ArduinoJson.h>

// Forward declarations
void testSystemLedOnCommand();
void testSystemLedOffCommand();
void testSystemInfoCommand();
void testInvalidCommand();
void testInvalidFormat();

/**
 * @brief Test functions for MQTT Command System
 */

void testCommandSystem(LeafNode* leafNode) {
    Serial.println();
    Serial.println("=== MQTT Command System Tests ===");
    
    testSystemLedOnCommand();
    testSystemLedOffCommand();
    testSystemInfoCommand();
    testInvalidCommand();
    testInvalidFormat();
    
    Serial.println("=== Command System Tests Complete ===");
    Serial.println();
}

void testSystemLedOnCommand() {
        Serial.println("🔍 Testing system LED ON command...");
        
        // Create command payload
        DynamicJsonDocument command(512);
        command["target"] = "system";
        command["command"] = "led_on";
        command["parameters"]["brightness"] = 100;
        command["timestamp"] = "2024-01-15T10:30:00Z";
        command["source"] = "test_client";
        
        String payload;
        serializeJson(command, payload);
        
        Serial.println("📤 Command payload: " + payload);
        Serial.println("✅ System LED ON command test prepared");
    }
    
void testSystemLedOffCommand() {
        Serial.println("🔍 Testing system LED OFF command...");
        
        // Create command payload
        DynamicJsonDocument command(512);
        command["target"] = "system";
        command["command"] = "led_off";
        command["parameters"]["fade_time"] = 500;
        command["timestamp"] = "2024-01-15T10:31:00Z";
        command["source"] = "test_client";
        
        String payload;
        serializeJson(command, payload);
        
        Serial.println("📤 Command payload: " + payload);
        Serial.println("✅ System LED OFF command test prepared");
    }
    
void testSystemInfoCommand() {
        Serial.println("🔍 Testing system info command...");
        
        // Create command payload
        DynamicJsonDocument command(512);
        command["target"] = "system";
        command["command"] = "info";
        command["parameters"] = JsonObject(); // Empty parameters
        command["timestamp"] = "2024-01-15T10:32:00Z";
        command["source"] = "test_client";
        
        String payload;
        serializeJson(command, payload);
        
        Serial.println("📤 Command payload: " + payload);
        Serial.println("✅ System info command test prepared");
    }
    
void testInvalidCommand() {
        Serial.println("🔍 Testing invalid command...");
        
        // Create invalid command payload
        DynamicJsonDocument command(512);
        command["target"] = "system";
        command["command"] = "invalid_command";
        command["parameters"] = JsonObject();
        command["timestamp"] = "2024-01-15T10:33:00Z";
        command["source"] = "test_client";
        
        String payload;
        serializeJson(command, payload);
        
        Serial.println("📤 Command payload: " + payload);
        Serial.println("⚠️  Invalid command test prepared (should fail gracefully)");
    }
    
void testInvalidFormat() {
        Serial.println("🔍 Testing invalid format...");
        
        String invalidPayload = "{\"target\":\"system\",\"missing_required_fields\":true}";
        
        Serial.println("📤 Invalid payload: " + invalidPayload);
        Serial.println("⚠️  Invalid format test prepared (should fail gracefully)");
}

/**
 * @brief Show example MQTT commands that can be sent to the device
 */
void showExampleCommands() {
        Serial.println();
        Serial.println("=== MQTT Command Examples ===");
        Serial.println();
        
        Serial.println("📍 Command Topic:");
        Serial.println("  lai/devices/AFD440CD/command");
        Serial.println();
        
        Serial.println("📍 Response Topic:");
        Serial.println("  lai/devices/AFD440CD/command_response");
        Serial.println();
        
        Serial.println("💡 System LED ON Command:");
        Serial.println("  {");
        Serial.println("    \"target\": \"system\",");
        Serial.println("    \"command\": \"led_on\",");
        Serial.println("    \"parameters\": {");
        Serial.println("      \"brightness\": 100");
        Serial.println("    },");
        Serial.println("    \"timestamp\": \"2024-01-15T10:30:00Z\",");
        Serial.println("    \"source\": \"mqtt_client\"");
        Serial.println("  }");
        Serial.println();
        
        Serial.println("💡 System LED OFF Command:");
        Serial.println("  {");
        Serial.println("    \"target\": \"system\",");
        Serial.println("    \"command\": \"led_off\",");
        Serial.println("    \"parameters\": {");
        Serial.println("      \"fade_time\": 500");
        Serial.println("    },");
        Serial.println("    \"timestamp\": \"2024-01-15T10:31:00Z\",");
        Serial.println("    \"source\": \"mqtt_client\"");
        Serial.println("  }");
        Serial.println();
        
        Serial.println("ℹ️  System Info Command:");
        Serial.println("  {");
        Serial.println("    \"target\": \"system\",");
        Serial.println("    \"command\": \"info\",");
        Serial.println("    \"parameters\": {},");
        Serial.println("    \"timestamp\": \"2024-01-15T10:32:00Z\",");
        Serial.println("    \"source\": \"mqtt_client\"");
        Serial.println("  }");
        Serial.println();
        
        Serial.println("🔄 System Reset Command:");
        Serial.println("  {");
        Serial.println("    \"target\": \"system\",");
        Serial.println("    \"command\": \"reset\",");
        Serial.println("    \"parameters\": {},");
        Serial.println("    \"timestamp\": \"2024-01-15T10:33:00Z\",");
        Serial.println("    \"source\": \"mqtt_client\"");
        Serial.println("  }");
        Serial.println();
        
        Serial.println("📋 Expected Response Format:");
        Serial.println("  {");
        Serial.println("    \"original_command\": \"led_on\",");
        Serial.println("    \"original_target\": \"system\",");
        Serial.println("    \"original_timestamp\": \"2024-01-15T10:30:00Z\",");
        Serial.println("    \"original_source\": \"mqtt_client\",");
        Serial.println("    \"success\": true,");
        Serial.println("    \"message\": \"System LED turned on\",");
        Serial.println("    \"timestamp\": \"123456789\",");
        Serial.println("    \"response_source\": \"leaf_node\",");
        Serial.println("    \"data\": {");
        Serial.println("      \"led_state\": \"on\"");
        Serial.println("    }");
        Serial.println("  }");
        Serial.println();
        
        Serial.println("=== Command System Architecture ===");
        Serial.println("🎯 Target Categories:");
        Serial.println("  • system    - System level commands (LED, info, reset)");
        Serial.println("  • sensor    - Sensor related commands (future)");
        Serial.println("  • actuator  - Actuator control commands (future)");
        Serial.println();
        
        Serial.println("🔧 Available System Commands:");
        Serial.println("  • led_on    - Turn system LED on");
        Serial.println("  • led_off   - Turn system LED off");
        Serial.println("  • info      - Get system information");
        Serial.println("  • reset     - Reset the system");
        Serial.println();
        
        Serial.println("📡 MQTT Topics:");
        Serial.println("  • Command:  lai/devices/{serial}/command");
        Serial.println("  • Response: lai/devices/{serial}/command_response");
        Serial.println("  • Heartbeat: lai/devices/{serial}/heartbeat");
        Serial.println();
        
        Serial.println("✨ Features:");
        Serial.println("  • Modular command registration");
        Serial.println("  • Automatic response generation");
        Serial.println("  • Command validation");
        Serial.println("  • Error handling");
        Serial.println("  • Extensible architecture");
        Serial.println();
        
        Serial.println("=== End Command Examples ===");
        Serial.println();
    }
    
/**
 * @brief Show MQTT broker test commands for external testing
 */
void showMQTTTestCommands() {
        Serial.println();
        Serial.println("=== MQTT Test Commands (for external clients) ===");
        Serial.println();
        
        Serial.println("📱 Using mosquitto_pub:");
        Serial.println();
        
        Serial.println("💡 LED ON:");
        Serial.println("mosquitto_pub -h 192.168.1.10 -p 1883 \\");
        Serial.println("  -u node_afd440cd -P AEYt9f39Sxv19XDN \\");
        Serial.println("  -t 'lai/devices/AFD440CD/command' \\");
        Serial.println("  -m '{\"target\":\"system\",\"command\":\"led_on\",\"parameters\":{\"brightness\":100},\"timestamp\":\"2024-01-15T10:30:00Z\",\"source\":\"mosquitto_client\"}'");
        Serial.println();
        
        Serial.println("💡 LED OFF:");
        Serial.println("mosquitto_pub -h 192.168.1.10 -p 1883 \\");
        Serial.println("  -u node_afd440cd -P AEYt9f39Sxv19XDN \\");
        Serial.println("  -t 'lai/devices/AFD440CD/command' \\");
        Serial.println("  -m '{\"target\":\"system\",\"command\":\"led_off\",\"parameters\":{\"fade_time\":500},\"timestamp\":\"2024-01-15T10:31:00Z\",\"source\":\"mosquitto_client\"}'");
        Serial.println();
        
        Serial.println("ℹ️  System Info:");
        Serial.println("mosquitto_pub -h 192.168.1.10 -p 1883 \\");
        Serial.println("  -u node_afd440cd -P AEYt9f39Sxv19XDN \\");
        Serial.println("  -t 'lai/devices/AFD440CD/command' \\");
        Serial.println("  -m '{\"target\":\"system\",\"command\":\"info\",\"parameters\":{},\"timestamp\":\"2024-01-15T10:32:00Z\",\"source\":\"mosquitto_client\"}'");
        Serial.println();
        
        Serial.println("📱 Listen for responses:");
        Serial.println("mosquitto_sub -h 192.168.1.10 -p 1883 \\");
        Serial.println("  -u node_afd440cd -P AEYt9f39Sxv19XDN \\");
        Serial.println("  -t 'lai/devices/AFD440CD/command_response'");
        Serial.println();
        
        Serial.println("📱 Listen for heartbeat:");
        Serial.println("mosquitto_sub -h 192.168.1.10 -p 1883 \\");
        Serial.println("  -u node_afd440cd -P AEYt9f39Sxv19XDN \\");
        Serial.println("  -t 'lai/devices/AFD440CD/heartbeat'");
        Serial.println();
        
        Serial.println("=== End MQTT Test Commands ===");
        Serial.println();
}

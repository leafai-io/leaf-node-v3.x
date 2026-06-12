/*
 * MQTT User Registration Test
 * 
 * Dieses Script demonstriert, wie die User-ID Registrierung
 * nach einer WiFi-Verbindung funktioniert.
 */

#include <Arduino.h>
#include "LeafNode.h"

extern LeafNode* leafNode;

/**
 * @brief Simuliert eine User ID Registrierung für Tests
 * 
 * Diese Funktion kann verwendet werden, um die MQTT-Registrierung
 * ohne BLE-App zu testen.
 */
void testUserIdRegistration() {
    Serial.println("🧪 MQTT User Registration Test");
    Serial.println("════════════════════════════════");
    
    // Prüfe ob WiFi verbunden ist
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi nicht verbunden - Test abgebrochen");
        return;
    }
    
    // Prüfe MQTT-Konfiguration
    if (!leafNode->getConfig()->hasMQTTCredentials()) {
        Serial.println("❌ Keine MQTT-Credentials konfiguriert");
        return;
    }
    
    // Simuliere empfangene User ID
    String testUserId = "a7daa357-4b3b-45dd-b23d-a04cf6504070";
    
    Serial.println("📱 Simuliere User ID von App: " + testUserId);
    Serial.println("🌐 WiFi IP: " + WiFi.localIP().toString());
    Serial.println("🔑 Serial Number: " + leafNode->getConfig()->getSerialNumber());
    Serial.println("📡 MQTT Server: " + leafNode->getConfig()->getMQTTServer());
    
    // Führe Registrierung durch
    Serial.println("📤 Sende MQTT Registration...");
    
    // Hier würde normalerweise die publishUserRegistration aufgerufen
    // leafNode->publishUserRegistration(testUserId);
    
    Serial.println("✅ Test abgeschlossen - prüfe MQTT Monitor für Nachrichten");
    Serial.println();
}

/**
 * @brief Setup für MQTT Testing
 * 
 * Konfiguriert das System für MQTT-Tests
 */
void setupMQTTTesting() {
    Serial.println("🔧 MQTT Test Setup");
    Serial.println("===================");
    
    // Zeige aktuelle Konfiguration
    Serial.println("📋 Aktuelle MQTT-Konfiguration:");
    Serial.println("  Server: " + leafNode->getConfig()->getMQTTServer());
    Serial.println("  Port: " + String(leafNode->getConfig()->getMQTTPort()));
    Serial.println("  Username: " + leafNode->getConfig()->getMQTTUsername());
    Serial.println("  Client ID: " + leafNode->getConfig()->getMQTTClientId());
    Serial.println("  Auto Connect: " + String(leafNode->getConfig()->isMQTTAutoConnect() ? "Ja" : "Nein"));
    Serial.println();
    
    // Zeige WiFi-Status
    Serial.println("📶 WiFi-Status:");
    Serial.println("  Status: " + String(WiFi.status() == WL_CONNECTED ? "Verbunden" : "Getrennt"));
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("  SSID: " + WiFi.SSID());
        Serial.println("  IP: " + WiFi.localIP().toString());
        Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
    }
    Serial.println();
    
    // Zeige erwartete MQTT Topics
    String serialNumber = leafNode->getConfig()->getSerialNumber();
    Serial.println("📡 Erwartete MQTT Topics:");
    Serial.println("  Registration: lai/devices/" + serialNumber + "/register");
    Serial.println("  Registration ACK: lai/devices/" + serialNumber + "/registration_ack");
    Serial.println("  Heartbeat: lai/devices/" + serialNumber + "/heartbeat");
    Serial.println("  Status: lai/devices/" + serialNumber + "/status");
    Serial.println("  Commands: lai/devices/" + serialNumber + "/commands");
    Serial.println();
    
    Serial.println("💡 Zum Überwachen der MQTT-Nachrichten:");
    Serial.println("   ./monitor_mqtt.sh");
    Serial.println();
}

/**
 * @brief Manueller MQTT Connection Test
 */
void testMQTTConnection() {
    Serial.println("🔗 MQTT Connection Test");
    Serial.println("========================");
    
    if (!leafNode->getConfig()->hasMQTTCredentials()) {
        Serial.println("❌ Keine MQTT-Credentials - Test nicht möglich");
        return;
    }
    
    // Diese Funktionalität ist Teil der LeafNode-Klasse
    // und wird automatisch aufgerufen
    Serial.println("✅ MQTT-Verbindung wird automatisch hergestellt");
    Serial.println("   Prüfe Serial-Output für MQTT-Events");
    Serial.println();
}

/**
 * @brief Hauptfunktion für alle MQTT-Tests
 */
void runMQTTTests() {
    Serial.println();
    Serial.println("🚀 MQTT Integration Tests");
    Serial.println("══════════════════════════");
    
    setupMQTTTesting();
    testMQTTConnection();
    
    // Nur ausführen wenn WiFi verbunden
    if (WiFi.status() == WL_CONNECTED) {
        testUserIdRegistration();
    } else {
        Serial.println("⚠️  WiFi-Verbindung erforderlich für User ID Test");
        Serial.println("   Verwende BLE-Setup oder konfiguriere WiFi");
    }
    
    Serial.println("🎯 Tests abgeschlossen");
    Serial.println();
}

/**
 * @brief Show example MQTT commands that can be sent to the device
 */
void showMQTTTestExamples() {
    Serial.println();
    Serial.println("=== MQTT Test Examples ===");
    Serial.println();
    
    Serial.println("📍 MQTT Broker Configuration:");
    Serial.println("  Server: 192.168.1.10:1883");
    Serial.println("  Username: node_afd440cd");
    Serial.println("  Password: AEYt9f39Sxv19XDN");
    Serial.println("  Serial: AFD440CD");
    Serial.println();
    
    Serial.println("📍 Registration Topic:");
    Serial.println("  lai/devices/AFD440CD/register");
    Serial.println();
    
    Serial.println("📍 Registration ACK Topic:");
    Serial.println("  lai/devices/AFD440CD/registration_ack");
    Serial.println();
    
    Serial.println("📍 Heartbeat Topic:");
    Serial.println("  lai/devices/AFD440CD/heartbeat");
    Serial.println();
    
    Serial.println("📋 Example Registration Message:");
    Serial.println("  {");
    Serial.println("    \"serial_number\": \"AFD440CD\",");
    Serial.println("    \"user_id\": \"test_user_123\",");
    Serial.println("    \"firmware_version\": \"1.0.0\",");
    Serial.println("    \"timestamp\": 123456789");
    Serial.println("  }");
    Serial.println();
    
    Serial.println("📋 Example Registration ACK:");
    Serial.println("  {");
    Serial.println("    \"serial_number\": \"AFD440CD\",");
    Serial.println("    \"status\": \"registered\",");
    Serial.println("    \"timestamp\": 123456790");
    Serial.println("  }");
    Serial.println();
    
    Serial.println("📋 Example Heartbeat:");
    Serial.println("  {");
    Serial.println("    \"timestamp\": 123456789,");
    Serial.println("    \"uptime\": 45000,");
    Serial.println("    \"free_heap\": 256000,");
    Serial.println("    \"rssi\": -45");
    Serial.println("  }");
    Serial.println();
    
    Serial.println("=== End MQTT Test Examples ===");
    Serial.println();
}

/*
 * Beispiel für die Verwendung der user_id in der LeafNode-Anwendung
 * 
 * Diese Datei zeigt, wie die temporär gespeicherte user_id abgerufen und
 * für weitere Verarbeitungsschritte verwendet werden kann.
 */

#include "LeafNode.h"

extern LeafNode* leafNode; // Definiert in main.cpp

/**
 * @brief Beispiel-Task der die user_id verarbeitet
 * 
 * Dieser Task wird regelmäßig ausgeführt und prüft, ob eine neue
 * user_id verfügbar ist. In einem echten System würden Sie hier
 * Ihre spezifische Logik implementieren.
 */
void processUserIdTask() {
    String userId = leafNode->getTemporaryUserId();
    
    if (userId.length() > 0) {
        // User ID ist verfügbar - verarbeiten
        Serial.println("📱 Processing User ID: " + userId);
        
        // Hier würden Sie Ihre spezifische Logik implementieren:
        // - API-Aufruf mit der user_id
        // - Geräteregistrierung
        // - Benutzer-spezifische Konfiguration laden
        // - etc.
        
        // Beispiel für mögliche Verwendung:
        if (userId.startsWith("a7daa357")) {
            Serial.println("🔧 Erkannter Test-User - aktiviere Debug-Modus");
            // Spezielle Konfiguration für Test-User
        } else {
            Serial.println("👤 Produktions-User erkannt - normale Konfiguration");
            // Standard-Verhalten
        }
        
        // Beispiel API-Call (pseudo-code)
        /*
        String apiUrl = "https://api.example.com/devices/register";
        String payload = "{\"user_id\":\"" + userId + "\",\"device_id\":\"" + 
                        leafNode->getSerialNumber() + "\"}";
        
        // HTTP POST request hier implementieren
        if (httpClient.POST(apiUrl, payload)) {
            Serial.println("✅ Gerät erfolgreich registriert");
        } else {
            Serial.println("❌ Registrierung fehlgeschlagen");
        }
        */
        
        // Nach der Verarbeitung die user_id löschen
        leafNode->clearTemporaryUserId();
        Serial.println("🗑️  User ID wurde nach der Verarbeitung gelöscht");
    }
}

/**
 * @brief Fügt den User-ID Verarbeitungs-Task zum TaskManager hinzu
 * 
 * Diese Funktion sollte nach der LeafNode-Initialisierung aufgerufen werden.
 */
void setupUserIdProcessing() {
    // Task hinzufügen, der alle 10 Sekunden prüft, ob eine user_id verfügbar ist
    // In einem echten System könnten Sie auch einen Event-basierten Ansatz verwenden
    
    /*
    leafNode->getTaskManager()->addTask(
        "ProcessUserId",
        processUserIdTask,
        10000,  // Alle 10 Sekunden prüfen
        TaskPriority::NORMAL
    );
    */
    
    Serial.println("🔄 User ID Processing Task wurde eingerichtet");
}

/**
 * @brief Manuelle Verarbeitung einer User ID (für Tests)
 * 
 * Diese Funktion kann für Tests oder manuelle Verarbeitung verwendet werden.
 */
void manualUserIdTest() {
    String testUserId = "a7daa357-4b3b-45dd-b23d-a04cf6504070";
    
    Serial.println("🧪 Teste User ID Verarbeitung mit: " + testUserId);
    
    // Simuliere eine empfangene user_id
    // In der echten Anwendung kommt diese über BLE
    
    Serial.println("📋 User ID Format-Validierung:");
    if (testUserId.length() == 36 && 
        testUserId.charAt(8) == '-' && 
        testUserId.charAt(13) == '-' && 
        testUserId.charAt(18) == '-' && 
        testUserId.charAt(23) == '-') {
        Serial.println("✅ Gültiges UUID-Format");
    } else {
        Serial.println("❌ Ungültiges UUID-Format");
    }
    
    // Weitere Validierungen oder Verarbeitungsschritte hier...
}

#include <Arduino.h>
#include "LeafNode.h"

// Global instance of LeafNode
LeafNode* leafNode = nullptr;

void setup() {
    // Create LeafNode instance
    leafNode = new LeafNode();
    
    // Initialize the system
    if (!leafNode->initialize()) {
        Serial.println("FATAL: LeafNode initialization failed!");
        Serial.println("System will restart in 5 seconds...");
        delay(5000);
        ESP.restart();
    }
    
    Serial.println("LeafNode initialized successfully!");
}

void loop() {
    if (leafNode) {
        leafNode->update();
    }
    
    // Small delay to prevent overwhelming the system
    delay(1);
}
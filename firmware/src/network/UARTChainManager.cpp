#include "UARTChainManager.h"
#include "../hardware/StatusLED.h"
#include "config.h"

// Discovery protocol message constants
const char* UARTChainManager::MSG_DISCOVERY = "CHAIN_DISCOVER";
const char* UARTChainManager::MSG_ACK = "CHAIN_ACK";
const char* UARTChainManager::MSG_POSITION = "CHAIN_POS:";
const char* UARTChainManager::MSG_CONFIG = "CHAIN_CONFIG:";

UARTChainManager::UARTChainManager()
    : uartInput_(nullptr)
    , uartOutput_(nullptr)
    , discoveryStatus_(DiscoveryStatus::NOT_STARTED)
    , initialized_(false) {
}

UARTChainManager::~UARTChainManager() {
    // HardwareSerial instances are managed by ESP32 framework, don't delete
}

bool UARTChainManager::initialize() {
    if (initialized_) {
        return true;
    }

    Serial.println("[UARTChain] Initializing UART Chain Manager...");

    // Initialize Input UART (Serial1)
    uartInput_ = &Serial1;
    uartInput_->begin(UART_CHAIN_BAUD_RATE, SERIAL_8N1, UART_INPUT_RX_PIN, UART_INPUT_TX_PIN);
    
    // Initialize Output UART (Serial2)
    uartOutput_ = &Serial2;
    uartOutput_->begin(UART_CHAIN_BAUD_RATE, SERIAL_8N1, UART_OUTPUT_RX_PIN, UART_OUTPUT_TX_PIN);

    delay(100); // Give UARTs time to initialize

    // Clear any garbage in buffers
    clearBuffer(uartInput_);
    clearBuffer(uartOutput_);

    initialized_ = true;
    Serial.println("[UARTChain] Initialization complete");
    Serial.printf("[UARTChain] Input Port:  RX=%d TX=%d\n", UART_INPUT_RX_PIN, UART_INPUT_TX_PIN);
    Serial.printf("[UARTChain] Output Port: RX=%d TX=%d\n", UART_OUTPUT_RX_PIN, UART_OUTPUT_TX_PIN);

    return true;
}

UARTChainManager::DiscoveryStatus UARTChainManager::discoverChainPosition(uint32_t timeoutMs) {
    if (!initialized_) {
        Serial.println("[UARTChain] ERROR: Not initialized!");
        discoveryStatus_ = DiscoveryStatus::ERROR;
        return discoveryStatus_;
    }

    Serial.println("[UARTChain] ========================================");
    Serial.println("[UARTChain] Starting Chain Discovery Protocol...");
    Serial.println("[UARTChain] ========================================");

    discoveryStatus_ = DiscoveryStatus::IN_PROGRESS;
    
    // Add random startup delay (0-500ms) to prevent simultaneous discovery
    uint32_t randomDelay = random(0, 500);
    Serial.printf("[UARTChain] Random startup delay: %dms\n", randomDelay);
    delay(randomDelay);
    
    // Step 1: INITIAL LISTENING PHASE - Check if left neighbor is already sending
    Serial.println("[UARTChain] Step 1: Initial listening phase (1 second)...");
    unsigned long initialListenStart = millis();
    bool receivedInitialDiscovery = false;
    uint8_t leftNeighborPosition = 0;
    
    while (millis() - initialListenStart < 1000) {
        if (uartInput_->available()) {
            String msg = "";
            while (uartInput_->available()) {
                char c = uartInput_->read();
                if (c == '\n' || c == '\r') {
                    if (msg.length() > 0) {
                        msg.trim();
                        Serial.printf("[UARTChain] ← Input (initial): %s\n", msg.c_str());
                        
                        if (msg.indexOf(MSG_DISCOVERY) >= 0) {
                            receivedInitialDiscovery = true;
                            Serial.println("[UARTChain] ✓ Received discovery from left during initial listen!");
                            // Send ACK immediately
                            sendAckBackward();
                        } else if (msg.indexOf(MSG_POSITION) >= 0) {
                            leftNeighborPosition = extractPosition(msg);
                            Serial.printf("[UARTChain] ✓ Left neighbor is Node #%d\n", leftNeighborPosition);
                        }
                        msg = "";
                    }
                } else {
                    msg += c;
                }
            }
        }
        
        // If we got position, we can break early
        if (leftNeighborPosition > 0) {
            break;
        }
        
        delay(10);
    }
    
    // If we received discovery during initial phase, we're NOT Node #1
    if (receivedInitialDiscovery || leftNeighborPosition > 0) {
        Serial.println("[UARTChain] → I am NOT Node #1 (received discovery from left)");
        chainInfo_.hasLeftNeighbor = true;
        
        // Determine our position
        if (leftNeighborPosition > 0) {
            chainInfo_.chainPosition = leftNeighborPosition + 1;
        } else {
            // Wait for position if we haven't received it yet
            Serial.println("[UARTChain] Waiting for position from left...");
            String posMsg = listenOnInput(3000);
            if (posMsg.indexOf(MSG_POSITION) >= 0) {
                leftNeighborPosition = extractPosition(posMsg);
                chainInfo_.chainPosition = leftNeighborPosition + 1;
            } else {
                Serial.println("[UARTChain] ERROR: Position timeout");
                discoveryStatus_ = DiscoveryStatus::TIMEOUT;
                return discoveryStatus_;
            }
        }
        
        Serial.printf("[UARTChain] → I am Node #%d\n", chainInfo_.chainPosition);
        
        // Send our position forward
        sendPositionForward(chainInfo_.chainPosition);
        
        // Now send discovery backward and check for right neighbor
        sendDiscoveryBackward();
        
        // Listen for right neighbor's discovery
        String rightMsg = listenOnInput(2000);
        if (rightMsg.indexOf(MSG_DISCOVERY) >= 0) {
            Serial.println("[UARTChain] ✓ Right neighbor detected");
            chainInfo_.hasRightNeighbor = true;
            sendAckBackward();
        }
        
        discoveryStatus_ = DiscoveryStatus::COMPLETED;
        
        // Print results and exit
        Serial.println("[UARTChain] ========================================");
        Serial.println("[UARTChain] Discovery Complete!");
        Serial.printf("[UARTChain] Chain Position: %d\n", chainInfo_.chainPosition);
        Serial.printf("[UARTChain] Left Neighbor:  %s\n", chainInfo_.hasLeftNeighbor ? "YES" : "NO");
        Serial.printf("[UARTChain] Right Neighbor: %s\n", chainInfo_.hasRightNeighbor ? "YES" : "NO");
        Serial.println("[UARTChain] ========================================");
        
        return discoveryStatus_;
    }
    
    // Step 2: No initial discovery received - We might be Node #1
    Serial.println("[UARTChain] Step 2: No initial discovery - sending discovery backward...");
    sendDiscoveryBackward();
    
    // Step 3: PARALLEL LISTENING - Listen on BOTH ports simultaneously!
    Serial.printf("[UARTChain] Step 3: Listening on BOTH ports (timeout: %dms)...\n", timeoutMs);
    
    unsigned long startTime = millis();
    bool gotAckFromLeft = false;      // ACK on Output port = left neighbor exists
    bool gotDiscoveryFromRight = false; // Discovery on Input port = right neighbor exists
    // leftNeighborPosition already declared above, reuse it
    leftNeighborPosition = 0; // Reset for this phase
    
    // Listen on both ports until timeout
    while (millis() - startTime < timeoutMs) {
        // Check Output port for messages from RIGHT neighbor (forward direction)
        if (uartOutput_->available()) {
            String msg = "";
            while (uartOutput_->available()) {
                char c = uartOutput_->read();
                if (c == '\n' || c == '\r') {
                    if (msg.length() > 0) {
                        msg.trim();
                        Serial.printf("[UARTChain] ← Output: %s\n", msg.c_str());
                        
                        // Discovery on Output = Right neighbor is also searching!
                        if (msg.indexOf(MSG_DISCOVERY) >= 0) {
                            gotDiscoveryFromRight = true;
                            Serial.println("[UARTChain] ✓ RIGHT neighbor sent discovery!");
                            // Send ACK forward on Output
                            uartOutput_->println(MSG_ACK);
                            Serial.println("[UARTChain] → Sent ACK forward on Output");
                        } else if (msg.indexOf(MSG_ACK) >= 0) {
                            gotAckFromLeft = true;
                            Serial.println("[UARTChain] ✓ ACK from left neighbor!");
                        } else if (msg.indexOf(MSG_POSITION) >= 0) {
                            leftNeighborPosition = extractPosition(msg);
                            Serial.printf("[UARTChain] ✓ Left neighbor is Node #%d\n", leftNeighborPosition);
                        }
                        msg = "";
                    }
                } else {
                    msg += c;
                }
            }
        }
        
        // Check Input port for messages from LEFT neighbor
        if (uartInput_->available()) {
            String msg = "";
            while (uartInput_->available()) {
                char c = uartInput_->read();
                if (c == '\n' || c == '\r') {
                    if (msg.length() > 0) {
                        msg.trim();
                        Serial.printf("[UARTChain] ← Input: %s\n", msg.c_str());
                        
                        // ACK on Input = left neighbor acknowledged our discovery
                        if (msg.indexOf(MSG_ACK) >= 0) {
                            gotAckFromLeft = true;
                            Serial.println("[UARTChain] ✓ ACK from left neighbor on Input!");
                        } 
                        // POSITION on Input = left neighbor sent their position
                        else if (msg.indexOf(MSG_POSITION) >= 0) {
                            leftNeighborPosition = extractPosition(msg);
                            Serial.printf("[UARTChain] ✓ Left neighbor is Node #%d (via Input)\n", leftNeighborPosition);
                        }
                        // Discovery on Input = right neighbor is searching
                        else if (msg.indexOf(MSG_DISCOVERY) >= 0) {
                            gotDiscoveryFromRight = true;
                            Serial.println("[UARTChain] ✓ Discovery from right neighbor!");
                            
                            // Immediately send ACK back
                            sendAckBackward();
                        }
                        msg = "";
                    }
                } else {
                    msg += c;
                }
            }
        }
        
        // Early exit if we got ACK and position from left
        if (gotAckFromLeft && leftNeighborPosition > 0) {
            Serial.println("[UARTChain] Got all info from left neighbor, continuing...");
            break;
        }
        
        delay(10);
    }
    
    // Determine chain position
    if (!gotAckFromLeft) {
        // No left neighbor = We are Node #1
        Serial.println("[UARTChain] ✓ No left neighbor -> I am Node #1");
        chainInfo_.chainPosition = 1;
        chainInfo_.hasLeftNeighbor = false;
    } else {
        // We have a left neighbor
        chainInfo_.hasLeftNeighbor = true;
        
        if (leftNeighborPosition > 0) {
            chainInfo_.chainPosition = leftNeighborPosition + 1;
            Serial.printf("[UARTChain] ✓ I am Node #%d\n", chainInfo_.chainPosition);
        } else {
            // Wait a bit more for position message
            String posMsg = listenOnInput(2000);
            if (posMsg.indexOf(MSG_POSITION) >= 0) {
                leftNeighborPosition = extractPosition(posMsg);
                chainInfo_.chainPosition = leftNeighborPosition + 1;
                Serial.printf("[UARTChain] ✓ I am Node #%d\n", chainInfo_.chainPosition);
            } else {
                Serial.println("[UARTChain] ERROR: Could not determine position");
                discoveryStatus_ = DiscoveryStatus::ERROR;
                return discoveryStatus_;
            }
        }
    }
    
    // Set right neighbor status
    chainInfo_.hasRightNeighbor = gotDiscoveryFromRight;
    
    // Send our position forward so next node can discover
    if (chainInfo_.chainPosition > 0) {
        sendPositionForward(chainInfo_.chainPosition);
        Serial.printf("[UARTChain] → Sent position %d forward\n", chainInfo_.chainPosition);
    }
    
    // Print final results
    Serial.println("[UARTChain] ========================================");
    Serial.println("[UARTChain] Discovery Complete!");
    Serial.printf("[UARTChain] Chain Position: %d\n", chainInfo_.chainPosition);
    Serial.printf("[UARTChain] Left Neighbor:  %s\n", chainInfo_.hasLeftNeighbor ? "YES" : "NO");
    Serial.printf("[UARTChain] Right Neighbor: %s\n", chainInfo_.hasRightNeighbor ? "YES" : "NO");
    Serial.println("[UARTChain] ========================================");
    
    discoveryStatus_ = DiscoveryStatus::COMPLETED;
    return discoveryStatus_;
}

void UARTChainManager::sendDiscoveryBackward() {
    if (uartInput_ && initialized_) {
        uartInput_->println(MSG_DISCOVERY);
        Serial.printf("[UARTChain] → Sent: %s (backward on Input port)\n", MSG_DISCOVERY);
    }
}

bool UARTChainManager::waitForAckOnOutput(uint32_t timeoutMs) {
    String msg = listenOnOutput(timeoutMs);
    
    if (msg.length() > 0 && msg.indexOf(MSG_ACK) >= 0) {
        Serial.printf("[UARTChain] ← Received: %s (on Output port)\n", MSG_ACK);
        return true;
    }
    
    return false;
}

void UARTChainManager::sendAckBackward() {
    if (uartInput_ && initialized_) {
        uartInput_->println(MSG_ACK);
        Serial.printf("[UARTChain] → Sent: %s (backward on Input port)\n", MSG_ACK);
    }
}

void UARTChainManager::sendPositionForward(uint8_t position) {
    if (uartOutput_ && initialized_) {
        String msg = String(MSG_POSITION) + String(position);
        uartOutput_->println(msg);
        Serial.printf("[UARTChain] → Sent: %s (forward on Output port)\n", msg.c_str());
    }
}

String UARTChainManager::listenOnInput(uint32_t timeoutMs) {
    if (!uartInput_ || !initialized_) {
        return "";
    }
    
    unsigned long startTime = millis();
    String received = "";
    
    while (millis() - startTime < timeoutMs) {
        if (uartInput_->available()) {
            char c = uartInput_->read();
            
            if (c == '\n' || c == '\r') {
                if (received.length() > 0) {
                    received.trim();
                    Serial.printf("[UARTChain] ← Received on Input: %s\n", received.c_str());
                    return received;
                }
            } else {
                received += c;
            }
        }
        delay(10);
    }
    
    return "";
}

String UARTChainManager::listenOnOutput(uint32_t timeoutMs) {
    if (!uartOutput_ || !initialized_) {
        return "";
    }
    
    unsigned long startTime = millis();
    String received = "";
    
    while (millis() - startTime < timeoutMs) {
        if (uartOutput_->available()) {
            char c = uartOutput_->read();
            
            if (c == '\n' || c == '\r') {
                if (received.length() > 0) {
                    received.trim();
                    Serial.printf("[UARTChain] ← Received on Output: %s\n", received.c_str());
                    return received;
                }
            } else {
                received += c;
            }
        }
        delay(10);
    }
    
    return "";
}

String UARTChainManager::parseMessageType(const String& message) {
    if (message.indexOf(MSG_DISCOVERY) >= 0) {
        return MSG_DISCOVERY;
    } else if (message.indexOf(MSG_ACK) >= 0) {
        return MSG_ACK;
    } else if (message.indexOf(MSG_POSITION) >= 0) {
        return MSG_POSITION;
    }
    return "";
}

uint8_t UARTChainManager::extractPosition(const String& message) {
    int posIndex = message.indexOf(MSG_POSITION);
    
    if (posIndex >= 0) {
        String posStr = message.substring(posIndex + strlen(MSG_POSITION));
        posStr.trim();
        return (uint8_t)posStr.toInt();
    }
    
    return 0;
}

void UARTChainManager::clearBuffer(HardwareSerial* serial) {
    if (serial) {
        while (serial->available()) {
            serial->read();
            delay(1);
        }
    }
}

String UARTChainManager::getStatusString() const {
    switch (discoveryStatus_) {
        case DiscoveryStatus::NOT_STARTED:
            return "Not Started";
        case DiscoveryStatus::IN_PROGRESS:
            return "In Progress";
        case DiscoveryStatus::COMPLETED:
            return "Completed";
        case DiscoveryStatus::TIMEOUT:
            return "Timeout";
        case DiscoveryStatus::ERROR:
            return "Error";
        default:
            return "Unknown";
    }
}

void UARTChainManager::sendConfigForward(const String& ssid, const String& password, 
                                          const String& userId) {
    if (!uartOutput_ || !initialized_) {
        Serial.println("[UARTChain] ERROR: Cannot send config - not initialized");
        return;
    }
    
    // Build JSON config message: CHAIN_CONFIG:{"ssid":"...","password":"...","user_id":"..."}
    String configMsg = String(MSG_CONFIG) + "{";
    configMsg += "\"ssid\":\"" + ssid + "\",";
    configMsg += "\"password\":\"" + password + "\",";
    configMsg += "\"user_id\":\"" + userId + "\"";
    configMsg += "}";
    
    uartOutput_->println(configMsg);
    Serial.println("[UARTChain] → Sent config forward:");
    Serial.printf("[UARTChain]   SSID: %s\n", ssid.c_str());
    Serial.printf("[UARTChain]   User ID: %s\n", userId.c_str());
}

bool UARTChainManager::waitForConfig(String& ssid, String& password, 
                                      String& userId, StatusLED* statusLED) {
    if (!uartInput_ || !initialized_) {
        Serial.println("[UARTChain] ERROR: Cannot receive config - not initialized");
        return false;
    }
    
    Serial.println("[UARTChain] Waiting for config from left neighbor (no timeout - waiting indefinitely)...");
    
    String received = "";
    
    while (true) {  // NO TIMEOUT - wait forever!
        // Keep LED updating while waiting
        if (statusLED) {
            statusLED->update();
        }
        
        if (uartInput_->available()) {
            char c = uartInput_->read();
            
            if (c == '\n' || c == '\r') {
                if (received.length() > 0) {
                    received.trim();
                    Serial.printf("[UARTChain] ← Received: %s\n", received.c_str());
                    
                    // Check if it's a config message
                    if (received.indexOf(MSG_CONFIG) >= 0) {
                        // Extract JSON part
                        int jsonStart = received.indexOf('{');
                        if (jsonStart >= 0) {
                            String jsonStr = received.substring(jsonStart);
                            
                            // Parse JSON manually (simple parsing)
                            // Format: {"ssid":"...","password":"...","user_id":"..."}
                            
                            // Extract ssid
                            int ssidStart = jsonStr.indexOf("\"ssid\":\"") + 8;
                            int ssidEnd = jsonStr.indexOf("\"", ssidStart);
                            if (ssidStart > 8 && ssidEnd > ssidStart) {
                                ssid = jsonStr.substring(ssidStart, ssidEnd);
                            }
                            
                            // Extract password
                            int pwStart = jsonStr.indexOf("\"password\":\"") + 12;
                            int pwEnd = jsonStr.indexOf("\"", pwStart);
                            if (pwStart > 12 && pwEnd > pwStart) {
                                password = jsonStr.substring(pwStart, pwEnd);
                            }
                            
                            // Extract user_id
                            int uidStart = jsonStr.indexOf("\"user_id\":\"") + 11;
                            int uidEnd = jsonStr.indexOf("\"", uidStart);
                            if (uidStart > 11 && uidEnd > uidStart) {
                                userId = jsonStr.substring(uidStart, uidEnd);
                            }
                            
                            Serial.println("[UARTChain] ✓ Config received successfully!");
                            Serial.printf("[UARTChain]   SSID: %s\n", ssid.c_str());
                            Serial.printf("[UARTChain]   User ID: %s\n", userId.c_str());
                            
                            return true;
                        }
                    }
                    
                    received = "";
                }
            } else {
                received += c;
            }
        }
        
        delay(10);
    }
    
    // This should never be reached since we wait forever
    return false;
}


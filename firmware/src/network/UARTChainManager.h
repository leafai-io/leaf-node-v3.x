#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

/**
 * @brief Chain Information Structure
 * 
 * Stores information about this node's position in the UART chain
 */
struct ChainInfo {
    uint8_t chainPosition;      // 1, 2, 3, ... (0 = unknown/not discovered yet)
    bool hasLeftNeighbor;       // true if there's a node on the Input side
    bool hasRightNeighbor;      // true if there's a node on the Output side
    
    ChainInfo() : chainPosition(0), hasLeftNeighbor(false), hasRightNeighbor(false) {}
};

/**
 * @brief UART Chain Manager
 * 
 * Manages Node-to-Node UART communication for auto-discovery of chain position.
 * Uses two UART ports:
 * - Input Port (RX17/TX18): Communicates with left neighbor
 * - Output Port (RX33/TX42): Communicates with right neighbor
 * 
 * Discovery Protocol:
 * 1. Each node sends discovery message on Input port (backward)
 * 2. If no response on Output port within timeout -> Position 1 (first node)
 * 3. If response received on Output port -> not first node
 * 4. Node receives discovery on Input port -> sends acknowledgment
 * 5. Node determines its position and forwards to next node on Output port
 */
class UARTChainManager {
public:
    /**
     * @brief Discovery result status
     */
    enum class DiscoveryStatus {
        NOT_STARTED,
        IN_PROGRESS,
        COMPLETED,
        TIMEOUT,
        ERROR
    };

    UARTChainManager();
    ~UARTChainManager();

    /**
     * @brief Initialize UART ports
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Perform chain discovery to determine node position
     * @param timeoutMs Maximum time to wait for discovery (default: 10 seconds)
     * @return Discovery status
     */
    DiscoveryStatus discoverChainPosition(uint32_t timeoutMs = 10000);

    /**
     * @brief Get current chain information
     * @return ChainInfo structure
     */
    ChainInfo getChainInfo() const { return chainInfo_; }

    /**
     * @brief Get chain position (1-based)
     * @return Chain position or 0 if not discovered
     */
    uint8_t getChainPosition() const { return chainInfo_.chainPosition; }

    /**
     * @brief Check if node has left neighbor
     */
    bool hasLeftNeighbor() const { return chainInfo_.hasLeftNeighbor; }

    /**
     * @brief Check if node has right neighbor
     */
    bool hasRightNeighbor() const { return chainInfo_.hasRightNeighbor; }

    /**
     * @brief Get human-readable discovery status
     */
    String getStatusString() const;

    /**
     * @brief Send WiFi configuration forward through the chain
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @param userId User ID for MQTT registration
     */
    void sendConfigForward(const String& ssid, const String& password, 
                           const String& userId);

    /**
     * @brief Listen for configuration message from left neighbor
     * @param ssid Output: WiFi SSID
     * @param password Output: WiFi password
     * @param userId Output: User ID
     * @param statusLED Optional: StatusLED to keep updating while waiting
     * @return true if config received successfully
     */
    bool waitForConfig(String& ssid, String& password, 
                       String& userId,
                       class StatusLED* statusLED = nullptr);

private:
    // UART Instances
    HardwareSerial* uartInput_;   // Serial1 for Input port (RX17/TX18)
    HardwareSerial* uartOutput_;  // Serial2 for Output port (RX33/TX42)

    // Discovery state
    ChainInfo chainInfo_;
    DiscoveryStatus discoveryStatus_;
    bool initialized_;

    // Discovery protocol messages
    static const char* MSG_DISCOVERY;
    static const char* MSG_ACK;
    static const char* MSG_POSITION;
    static const char* MSG_CONFIG;

    /**
     * @brief Send discovery message on Input port (backward)
     */
    void sendDiscoveryBackward();

    /**
     * @brief Wait for acknowledgment on Output port
     * @param timeoutMs Timeout in milliseconds
     * @return true if acknowledgment received
     */
    bool waitForAckOnOutput(uint32_t timeoutMs);

    /**
     * @brief Send acknowledgment on Input port
     */
    void sendAckBackward();

    /**
     * @brief Send position information forward on Output port
     * @param position This node's position
     */
    void sendPositionForward(uint8_t position);

    /**
     * @brief Listen for incoming messages on Input port
     * @param timeoutMs Timeout in milliseconds
     * @return Received message or empty string if timeout
     */
    String listenOnInput(uint32_t timeoutMs);

    /**
     * @brief Listen for incoming messages on Output port
     * @param timeoutMs Timeout in milliseconds
     * @return Received message or empty string if timeout
     */
    String listenOnOutput(uint32_t timeoutMs);

    /**
     * @brief Parse incoming message
     * @param message Raw message string
     * @return Message type or empty if invalid
     */
    String parseMessageType(const String& message);

    /**
     * @brief Extract position from message
     * @param message Message containing position
     * @return Position value or 0 if invalid
     */
    uint8_t extractPosition(const String& message);

    /**
     * @brief Clear UART buffer
     * @param serial UART instance to clear
     */
    void clearBuffer(HardwareSerial* serial);
};

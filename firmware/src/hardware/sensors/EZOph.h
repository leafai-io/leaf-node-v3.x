#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "../../diagnostics/Logger.h"
#include "../../network/MQTTManager.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief EZO pH Sensor Driver
 * 
 * This class provides interface for the Atlas Scientific EZO pH sensor
 * using I2C communication protocol with direct MQTT publishing capability.
 * 
 * Features:
 * - Asynchronous reading with 815ms response time
 * - Comprehensive response code handling (1=Success, 2=Failed, 254=Pending, 255=No Data)
 * - Sleep mode support
 * - Calibration commands support
 */
class EZOph {
public:
    /**
     * @brief EZO pH Response Codes
     */
    enum class ResponseCode : uint8_t {
        SUCCESS = 1,      // Command successful
        FAILED = 2,       // Command failed
        PENDING = 254,    // Command still processing
        NO_DATA = 255     // No data to send
    };

    /**
     * @brief Constructor
     * @param logger Pointer to Logger instance
     */
    EZOph(Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~EZOph();

    /**
     * @brief Initialize the sensor
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor data (blocking with 815ms delay)
     * @return EZOphData structure with sensor readings
     */
    EZOphData readSensor();

    /**
     * @brief Start asynchronous pH reading (non-blocking)
     * @return true if command sent successfully
     */
    bool startReading();

    /**
     * @brief Check if reading is ready and retrieve data
     * @param data Output parameter for pH data
     * @return true if data is ready and valid
     */
    bool getReading(EZOphData& data);

    /**
     * @brief Read sensor data and publish directly to MQTT
     * @param topic MQTT topic to publish to
     * @return true if reading and publishing successful
     */
    bool readAndPublishData(const String& topic);

    /**
     * @brief Set MQTT manager instance for direct publishing
     * @param mqttManager Pointer to MQTTManager instance
     */
    void setMQTTManager(MQTTManager* mqttManager);

    /**
     * @brief Check if sensor is available/responding
     * @return true if sensor is available
     */
    bool isAvailable();

    /**
     * @brief Put sensor into sleep mode
     * @return true if sleep command successful
     */
    bool sleep();

    /**
     * @brief Wake sensor from sleep mode
     * @return true if sensor wakes up successfully
     */
    bool wake();

    /**
     * @brief Get sensor status for debugging
     * @return Status string
     */
    String getStatus();

    /**
     * @brief Get last response code
     * @return Last ResponseCode received from sensor
     */
    ResponseCode getLastResponseCode() const;

    /**
     * @brief Send custom command to sensor
     * @param command Command string to send
     * @return true if command sent successfully
     */
    bool sendCommand(const String& command);

    /**
     * @brief Send custom command to sensor and get response
     * @param command Command string to send (e.g., "Cal,?", "Cal,mid,7.00", "T,25.5", "i")
     * @param response Output parameter for sensor response string
     * @param responseCode Output parameter for response code
     * @return true if command sent and response received successfully
     */
    bool sendCommandAndGetResponse(const String& command, String& response, ResponseCode& responseCode);

    /**
     * @brief Set I2C address for sensor (if different from default 0x63)
     * @param address New I2C address (e.g., 0x63, 0x64, etc.)
     */
    void setI2CAddress(uint8_t address);

    /**
     * @brief Scan I2C bus and return found devices
     * @return String with list of found I2C addresses
     */
    String scanI2CBus();

private:
    Logger* logger_;
    MQTTManager* mqttManager_;
    bool initialized_;
    uint8_t i2cAddress_;
    ResponseCode lastResponseCode_;
    unsigned long lastCommandTime_;
    bool readingInProgress_;
    
    static const uint8_t RESPONSE_BUFFER_SIZE = 32;
    static const uint16_t READING_DELAY_MS = 1000;     // For pH readings (Atlas Scientific: 900ms typical + 100ms safety buffer)
    static const uint16_t COMMAND_DELAY_MS = 250;      // For other commands
    
    /**
     * @brief Initialize I2C bus
     * @return true if I2C initialization successful
     */
    bool initializeI2C();

    /**
     * @brief Send command to sensor via I2C
     * @param command Command string
     * @param isReadingCommand If true, uses 815ms delay, else 250ms
     * @return true if command sent successfully
     */
    bool sendI2CCommand(const String& command, bool isReadingCommand = false);

    /**
     * @brief Read response from sensor via I2C
     * @param buffer Buffer to store response
     * @param bufferSize Size of buffer
     * @return Number of bytes read (0 if error)
     */
    uint8_t readI2CResponse(char* buffer, uint8_t bufferSize);

    /**
     * @brief Parse pH value from response string
     * @param response Response string from sensor
     * @param phValue Output parameter for parsed pH value
     * @return true if parsing successful
     */
    bool parsePhValue(const char* response, float& phValue);

    /**
     * @brief Create JSON payload from sensor data
     * @param data Sensor data to convert
     * @return JSON string for MQTT publishing
     */
    String createJsonPayload(const EZOphData& data);

    /**
     * @brief Get response code description
     * @param code Response code to describe
     * @return Human-readable description
     */
    String getResponseCodeDescription(ResponseCode code) const;
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../hardware/RS485Manager.h"
#include "../diagnostics/Logger.h"

/**
 * @brief RS485 Command Handler
 * 
 * Handles RS485-related commands for remote testing and debugging.
 * Integrates with the command system to allow remote RS485 operations.
 */
class RS485CommandHandler {
public:
    /**
     * @brief Constructor
     * @param rs485Manager RS485 manager instance
     * @param logger Logger instance
     */
    RS485CommandHandler(RS485Manager& rs485Manager, Logger& logger);
    
    /**
     * @brief Process RS485 command
     * @param command Command name
     * @param parameters Command parameters as JSON
     * @return Response as JSON string
     */
    String processCommand(const String& command, const JsonObject& parameters);
    
    /**
     * @brief Get list of supported commands
     * @return JSON array of command names
     */
    String getSupportedCommands();

private:
    RS485Manager& rs485Manager_;
    Logger& logger_;
    
    /**
     * @brief Handle raw send command
     * @param parameters Command parameters
     * @return Response JSON
     */
    String handleRawSend(const JsonObject& parameters);
    
    /**
     * @brief Handle send with retry command
     * @param parameters Command parameters
     * @return Response JSON
     */
    String handleSendWithRetry(const JsonObject& parameters);
    
    /**
     * @brief Handle get statistics command
     * @return Statistics JSON
     */
    String handleGetStatistics();
    
    /**
     * @brief Handle reset statistics command
     * @return Response JSON
     */
    String handleResetStatistics();
    
    /**
     * @brief Handle set timeout command
     * @param parameters Command parameters
     * @return Response JSON
     */
    String handleSetTimeout(const JsonObject& parameters);
    
    /**
     * @brief Convert hex string to byte array
     * @param hexString Hex string (e.g., "01020304")
     * @param bytes Output byte array
     * @param maxBytes Maximum number of bytes
     * @return Number of bytes converted
     */
    size_t hexStringToBytes(const String& hexString, byte* bytes, size_t maxBytes);
    
    /**
     * @brief Convert byte array to hex string
     * @param bytes Byte array
     * @param length Number of bytes
     * @return Hex string
     */
    String bytesToHexString(const byte* bytes, size_t length);
};

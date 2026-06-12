#include "RS485CommandHandler.h"

RS485CommandHandler::RS485CommandHandler(RS485Manager& rs485Manager, Logger& logger)
    : rs485Manager_(rs485Manager), logger_(logger) {
}

String RS485CommandHandler::processCommand(const String& command, const JsonObject& parameters) {
    logger_.debug("RS485CommandHandler", "Processing command: " + command);
    
    if (command == "raw_send") {
        return handleRawSend(parameters);
    } else if (command == "send_with_retry") {
        return handleSendWithRetry(parameters);
    } else if (command == "get_statistics") {
        return handleGetStatistics();
    } else if (command == "reset_statistics") {
        return handleResetStatistics();
    } else if (command == "set_timeout") {
        return handleSetTimeout(parameters);
    } else {
        DynamicJsonDocument response(256);
        response["success"] = false;
        response["error"] = "Unknown RS485 command: " + command;
        
        String result;
        serializeJson(response, result);
        return result;
    }
}

String RS485CommandHandler::getSupportedCommands() {
    DynamicJsonDocument doc(512);
    JsonArray commands = doc.createNestedArray("commands");
    
    JsonObject rawSend = commands.createNestedObject();
    rawSend["name"] = "raw_send";
    rawSend["description"] = "Send raw RS485 command";
    rawSend["parameters"] = "hex_data (string)";
    
    JsonObject sendWithRetry = commands.createNestedObject();
    sendWithRetry["name"] = "send_with_retry";
    sendWithRetry["description"] = "Send RS485 command with retry";
    sendWithRetry["parameters"] = "hex_data (string), retries (int, optional)";
    
    JsonObject getStats = commands.createNestedObject();
    getStats["name"] = "get_statistics";
    getStats["description"] = "Get RS485 communication statistics";
    getStats["parameters"] = "none";
    
    JsonObject resetStats = commands.createNestedObject();
    resetStats["name"] = "reset_statistics";
    resetStats["description"] = "Reset RS485 communication statistics";
    resetStats["parameters"] = "none";
    
    JsonObject setTimeout = commands.createNestedObject();
    setTimeout["name"] = "set_timeout";
    setTimeout["description"] = "Set RS485 response timeout";
    setTimeout["parameters"] = "timeout_ms (int)";
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RS485CommandHandler::handleRawSend(const JsonObject& parameters) {
    DynamicJsonDocument response(1024);
    
    if (!rs485Manager_.isReady()) {
        response["success"] = false;
        response["error"] = "RS485 not initialized";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    if (!parameters.containsKey("hex_data")) {
        response["success"] = false;
        response["error"] = "Missing hex_data parameter";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    String hexData = parameters["hex_data"].as<String>();
    
    // Convert hex string to bytes
    byte command[64];
    size_t commandLength = hexStringToBytes(hexData, command, sizeof(command));
    
    if (commandLength == 0) {
        response["success"] = false;
        response["error"] = "Invalid hex_data format";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    // Send command
    byte responseBytes[256];
    size_t responseLength;
    
    bool success = rs485Manager_.sendRawCommand(command, commandLength, responseBytes, sizeof(responseBytes), responseLength);
    
    response["success"] = success;
    response["command_sent"] = hexData;
    response["response_length"] = responseLength;
    
    if (responseLength > 0) {
        response["response_data"] = bytesToHexString(responseBytes, responseLength);
    }
    
    String result;
    serializeJson(response, result);
    return result;
}

String RS485CommandHandler::handleSendWithRetry(const JsonObject& parameters) {
    DynamicJsonDocument response(1024);
    
    if (!rs485Manager_.isReady()) {
        response["success"] = false;
        response["error"] = "RS485 not initialized";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    if (!parameters.containsKey("hex_data")) {
        response["success"] = false;
        response["error"] = "Missing hex_data parameter";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    String hexData = parameters["hex_data"].as<String>();
    int retries = parameters.containsKey("retries") ? parameters["retries"].as<int>() : 3;
    
    // Convert hex string to bytes
    byte command[64];
    size_t commandLength = hexStringToBytes(hexData, command, sizeof(command));
    
    if (commandLength == 0) {
        response["success"] = false;
        response["error"] = "Invalid hex_data format";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    // Send command with retry
    byte responseBytes[256];
    size_t responseLength;
    
    bool success = rs485Manager_.sendCommandWithRetry(command, commandLength, responseBytes, sizeof(responseBytes), responseLength, retries);
    
    response["success"] = success;
    response["command_sent"] = hexData;
    response["retries_used"] = retries;
    response["response_length"] = responseLength;
    
    if (responseLength > 0) {
        response["response_data"] = bytesToHexString(responseBytes, responseLength);
    }
    
    String result;
    serializeJson(response, result);
    return result;
}

String RS485CommandHandler::handleGetStatistics() {
    DynamicJsonDocument response(512);
    
    const auto& stats = rs485Manager_.getStatistics();
    
    response["success"] = true;
    response["statistics"]["total_commands"] = stats.totalCommands;
    response["statistics"]["successful_commands"] = stats.successfulCommands;
    response["statistics"]["failed_commands"] = stats.failedCommands;
    response["statistics"]["timeouts"] = stats.timeouts;
    response["statistics"]["retries"] = stats.retries;
    
    if (stats.totalCommands > 0) {
        float successRate = (float)stats.successfulCommands / stats.totalCommands * 100.0f;
        response["statistics"]["success_rate_percent"] = successRate;
    } else {
        response["statistics"]["success_rate_percent"] = 0;
    }
    
    response["current_timeout_ms"] = rs485Manager_.getResponseTimeout();
    
    String result;
    serializeJson(response, result);
    return result;
}

String RS485CommandHandler::handleResetStatistics() {
    DynamicJsonDocument response(256);
    
    rs485Manager_.resetStatistics();
    
    response["success"] = true;
    response["message"] = "Statistics reset successfully";
    
    String result;
    serializeJson(response, result);
    return result;
}

String RS485CommandHandler::handleSetTimeout(const JsonObject& parameters) {
    DynamicJsonDocument response(256);
    
    if (!parameters.containsKey("timeout_ms")) {
        response["success"] = false;
        response["error"] = "Missing timeout_ms parameter";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    unsigned long timeout = parameters["timeout_ms"].as<unsigned long>();
    
    if (timeout < 100 || timeout > 10000) {
        response["success"] = false;
        response["error"] = "Timeout must be between 100 and 10000 ms";
        String result;
        serializeJson(response, result);
        return result;
    }
    
    rs485Manager_.setResponseTimeout(timeout);
    
    response["success"] = true;
    response["new_timeout_ms"] = timeout;
    
    String result;
    serializeJson(response, result);
    return result;
}

size_t RS485CommandHandler::hexStringToBytes(const String& hexString, byte* bytes, size_t maxBytes) {
    size_t length = hexString.length();
    
    // Must be even length
    if (length % 2 != 0) {
        return 0;
    }
    
    size_t byteCount = length / 2;
    if (byteCount > maxBytes) {
        return 0;
    }
    
    for (size_t i = 0; i < byteCount; i++) {
        String byteStr = hexString.substring(i * 2, i * 2 + 2);
        bytes[i] = (byte)strtol(byteStr.c_str(), nullptr, 16);
    }
    
    return byteCount;
}

String RS485CommandHandler::bytesToHexString(const byte* bytes, size_t length) {
    String result = "";
    
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] < 16) {
            result += "0";
        }
        result += String(bytes[i], HEX);
    }
    
    result.toUpperCase();
    return result;
}

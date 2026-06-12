#include "SDI12Manager.h"
#include <driver/gpio.h>

SDI12Manager::SDI12Manager(Logger& logger) 
    : logger_(logger), sdi12_(nullptr), dataPin_(-1), initialized_(false) {
    resetStatistics();
}

SDI12Manager::~SDI12Manager() {
    if (sdi12_) {
        sdi12_->end();
        delete sdi12_;
    }
}

bool SDI12Manager::initialize(int dataPin) {
    logger_.info("SDI12Manager", "Initializing SDI-12 communication...");
    
    dataPin_ = dataPin;
    
    // Install GPIO ISR service first (required for ESP32)
    gpio_install_isr_service(0);
    
    // Create SDI-12 instance (no pin in constructor)
    sdi12_ = new SDI12(dataPin);
    
    if (!sdi12_) {
        logger_.error("SDI12Manager", "Failed to create SDI-12 instance");
        return false;
    }
    
    // Initialize SDI-12 bus with pin
    sdi12_->begin();
    delay(300); // Wait 300ms for SDI-12 (as per original code)
    
    initialized_ = true;
    logger_.info("SDI12Manager", "SDI-12 manager initialized successfully");
    logger_.info("SDI12Manager", "Data Pin configured: " + String(dataPin));
    logger_.info("SDI12Manager", "Data Pin active: " + String(sdi12_->getDataPin()));
    
    return true;
}

bool SDI12Manager::isReady() const {
    return initialized_ && sdi12_ != nullptr;
}

void SDI12Manager::shutdown() {
    if (!initialized_) {
        return; // Already shutdown
    }
    
    logger_.info("SDI12Manager", "Shutting down SDI-12 communication...");
    
    if (sdi12_) {
        sdi12_->end(); // End SDI-12 communication
        delete sdi12_;
        sdi12_ = nullptr;
    }
    
    initialized_ = false;
    dataPin_ = -1;
    
    logger_.info("SDI12Manager", "SDI-12 manager shutdown complete");
}

bool SDI12Manager::sendCommand(const String& command, String& response, unsigned long timeout) {
    if (!isReady()) {
        logger_.error("SDI12Manager", "SDI-12 not initialized");
        return false;
    }
    
    stats_.totalCommands++;
    
    logger_.debug("SDI12Manager", "Sending command: " + command);
    
    // Clear any existing data
    clearBuffer();
    
    // SDI-12 library requires non-const String reference, so we need to make a copy
    String cmdCopy = command;
    sdi12_->sendCommand(cmdCopy);
    
    // Wait 300ms after command (as per original code)
    delay(300);
    
    // Read response as complete line
    if (!sdi12_->available()) {
        logger_.warning("SDI12Manager", "No response to command: " + command);
        stats_.failedCommands++;
        stats_.timeouts++;
        return false;
    }
    
    // Read response until newline
    response = sdi12_->readStringUntil('\n');
    response.trim(); // Remove whitespace
    
    clearBuffer(); // Clear remaining data
    
    if (response.length() == 0) {
        logger_.warning("SDI12Manager", "Empty response to command: " + command);
        stats_.failedCommands++;
        return false;
    }
    
    stats_.successfulCommands++;
    logger_.debug("SDI12Manager", "Response: " + response);
    
    return true;
}

bool SDI12Manager::sendIdentification(char address, String& response) {
    String command = String(address) + "I!";
    return sendCommand(command, response);
}

bool SDI12Manager::startMeasurement(char address, int& waitTime, int& numValues) {
    String command = String(address) + "C!"; // Concurrent measurement
    String response;
    
    if (!sendCommand(command, response)) {
        return false;
    }
    
    return parseMeasurementResponse(response, waitTime, numValues);
}

bool SDI12Manager::startStandardMeasurement(char address, int& waitTime, int& numValues) {
    String command = String(address) + "M!"; // Standard measurement
    String response;
    
    if (!sendCommand(command, response)) {
        return false;
    }
    
    return parseMeasurementResponse(response, waitTime, numValues);
}

bool SDI12Manager::getData(char address, int valueIndex, float* values, int maxValues, int& numValues) {
    if (valueIndex < 0 || valueIndex > 9) {
        logger_.error("SDI12Manager", "Invalid value index: " + String(valueIndex));
        return false;
    }
    
    String command = String(address) + "D" + String(valueIndex) + "!";
    String response;
    
    if (!sendCommand(command, response)) {
        return false;
    }
    
    return parseDataResponse(response, values, maxValues, numValues);
}

bool SDI12Manager::verifySensor(char address) {
    String response;
    return sendCommand(String(address) + "!", response, 100);
}

bool SDI12Manager::changeAddress(char oldAddress, char newAddress) {
    String command = String(oldAddress) + "A" + String(newAddress) + "!";
    String response;
    
    if (!sendCommand(command, response)) {
        return false;
    }
    
    // Response should be the new address
    if (response.length() > 0 && response[0] == newAddress) {
        logger_.info("SDI12Manager", "Address changed from " + String(oldAddress) + 
                                    " to " + String(newAddress));
        return true;
    }
    
    return false;
}

void SDI12Manager::resetStatistics() {
    stats_.totalCommands = 0;
    stats_.successfulCommands = 0;
    stats_.failedCommands = 0;
    stats_.timeouts = 0;
    
    logger_.info("SDI12Manager", "Statistics reset");
}

bool SDI12Manager::parseMeasurementResponse(const String& response, int& waitTime, int& numValues) {
    // Response format: atttn
    // a = address
    // ttt = time in seconds (0-999)
    // n = number of values (0-9)
    
    if (response.length() < 5) {
        logger_.error("SDI12Manager", "Invalid measurement response length: " + String(response.length()));
        return false;
    }
    
    // Extract wait time (characters 1-3)
    String waitStr = response.substring(1, 4);
    waitTime = waitStr.toInt();
    
    // Extract number of values (character 4)
    numValues = response.substring(4, 5).toInt();
    
    logger_.debug("SDI12Manager", "Measurement started - Wait: " + String(waitTime) + 
                                 "s, Values: " + String(numValues));
    
    return true;
}

bool SDI12Manager::parseDataResponse(const String& response, float* values, int maxValues, int& numValues) {
    // Response format: a+value1+value2+value3...
    // a = address
    // values are separated by + or -
    
    if (response.length() < 2) {
        logger_.error("SDI12Manager", "Invalid data response length: " + String(response.length()));
        return false;
    }
    
    numValues = 0;
    int startPos = 1; // Skip address character
    
    for (int i = 0; i < maxValues; i++) {
        // Find next value separator (+ or -)
        int nextSep = -1;
        for (int j = startPos + 1; j < response.length(); j++) {
            if (response[j] == '+' || response[j] == '-') {
                nextSep = j;
                break;
            }
        }
        
        // Extract value
        String valueStr;
        if (nextSep == -1) {
            // Last value
            valueStr = response.substring(startPos);
        } else {
            valueStr = response.substring(startPos, nextSep);
        }
        
        if (valueStr.length() > 0) {
            values[numValues] = valueStr.toFloat();
            numValues++;
            logger_.debug("SDI12Manager", "Value " + String(numValues) + ": " + String(values[numValues - 1]));
        }
        
        if (nextSep == -1) {
            break; // No more values
        }
        
        startPos = nextSep;
    }
    
    logger_.debug("SDI12Manager", "Parsed " + String(numValues) + " values from response");
    
    return numValues > 0;
}

void SDI12Manager::clearBuffer() {
    if (!sdi12_) return;
    
    while (sdi12_->available()) {
        sdi12_->read();
    }
}

bool SDI12Manager::waitForResponse(unsigned long timeout) {
    if (!sdi12_) return false;
    
    unsigned long startTime = millis();
    while ((millis() - startTime) < timeout) {
        if (sdi12_->available()) {
            return true;
        }
        delay(10);
    }
    
    return false;
}

#include "RS485Manager.h"

RS485Manager::RS485Manager(Logger& logger) 
    : logger_(logger), rs485_(nullptr), initialized_(false) {
    resetStatistics();
}

RS485Manager::~RS485Manager() {
    delete rs485_;
}

bool RS485Manager::initialize(int deRePin, unsigned long responseTimeout, unsigned long baudRate) {
    logger_.info("RS485Manager", "Initializing RS485 communication...");
    
    // Create RS485 instance
    rs485_ = new RS485(deRePin, responseTimeout);
    
    if (!rs485_) {
        logger_.error("RS485Manager", "Failed to create RS485 instance");
        return false;
    }
    
    // Initialize RS485 with specified baud rate
    rs485_->begin(baudRate);
    
    if (!rs485_->isInitialized()) {
        logger_.error("RS485Manager", "Failed to initialize RS485");
        delete rs485_;
        rs485_ = nullptr;
        return false;
    }
    
    initialized_ = true;
    logger_.info("RS485Manager", "RS485 manager initialized successfully");
    logger_.info("RS485Manager", "DE/RE Pin: " + String(deRePin) + 
                                ", Timeout: " + String(responseTimeout) + "ms" +
                                ", Baud: " + String(baudRate));
    
    return true;
}

bool RS485Manager::isReady() const {
    return initialized_ && rs485_ && rs485_->isInitialized();
}

void RS485Manager::shutdown() {
    if (!initialized_) {
        return; // Already shutdown
    }
    
    logger_.info("RS485Manager", "Shutting down RS485 communication...");
    
    if (rs485_) {
        delete rs485_;
        rs485_ = nullptr;
    }
    
    initialized_ = false;
    logger_.info("RS485Manager", "RS485 manager shutdown complete");
}

bool RS485Manager::sendRawCommand(const byte* command, size_t commandLength, 
                                 byte* response, size_t maxResponseLength, size_t& responseLength) {
    if (!isReady()) {
        logger_.error("RS485Manager", "RS485 not initialized");
        return false;
    }
    
    stats_.totalCommands++;
    
    logCommunicationAttempt(command, commandLength, 1);
    
    bool success = rs485_->sendCommand(command, commandLength, response, maxResponseLength, responseLength);
    
    if (success) {
        stats_.successfulCommands++;
    } else {
        stats_.failedCommands++;
        if (responseLength == 0) {
            stats_.timeouts++;
        }
    }
    
    logCommunicationResult(success, responseLength);
    
    return success;
}

bool RS485Manager::sendCommandWithRetry(const byte* command, size_t commandLength, 
                                       byte* response, size_t maxResponseLength, size_t& responseLength,
                                       int maxRetries) {
    if (!isReady()) {
        logger_.error("RS485Manager", "RS485 not initialized");
        return false;
    }
    
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        stats_.totalCommands++;
        
        logCommunicationAttempt(command, commandLength, attempt);
        
        bool success = rs485_->sendCommand(command, commandLength, response, maxResponseLength, responseLength);
        
        if (success) {
            stats_.successfulCommands++;
            if (attempt > 1) {
                stats_.retries += (attempt - 1);
                logger_.info("RS485Manager", "Command succeeded on attempt " + String(attempt));
            }
            logCommunicationResult(true, responseLength);
            return true;
        } else {
            stats_.failedCommands++;
            if (responseLength == 0) {
                stats_.timeouts++;
            }
            
            if (attempt < maxRetries) {
                logger_.warning("RS485Manager", "Command failed on attempt " + String(attempt) + ", retrying...");
                delay(100); // Small delay between retries
            }
        }
    }
    
    logger_.error("RS485Manager", "Command failed after " + String(maxRetries) + " attempts");
    logCommunicationResult(false, responseLength);
    
    return false;
}

void RS485Manager::setResponseTimeout(unsigned long timeout) {
    if (rs485_) {
        rs485_->setResponseTimeout(timeout);
        logger_.info("RS485Manager", "Response timeout set to " + String(timeout) + "ms");
    }
}

unsigned long RS485Manager::getResponseTimeout() const {
    if (rs485_) {
        return rs485_->getResponseTimeout();
    }
    return 0;
}

void RS485Manager::setBaudRate(unsigned long baudRate) {
    if (rs485_) {
        rs485_->setBaudRate(baudRate);
        logger_.info("RS485Manager", "Baud rate set to " + String(baudRate));
    } else {
        logger_.error("RS485Manager", "Cannot set baud rate - RS485 not initialized");
    }
}

unsigned long RS485Manager::getBaudRate() const {
    if (rs485_) {
        return rs485_->getBaudRate();
    }
    return 0;
}

void RS485Manager::resetStatistics() {
    stats_.totalCommands = 0;
    stats_.successfulCommands = 0;
    stats_.failedCommands = 0;
    stats_.timeouts = 0;
    stats_.retries = 0;
    
    logger_.info("RS485Manager", "Statistics reset");
}

void RS485Manager::logCommunicationAttempt(const byte* command, size_t commandLength, int attempt) {
    String commandStr = "Command (attempt " + String(attempt) + "): ";
    for (size_t i = 0; i < commandLength && i < 8; i++) { // Limit to first 8 bytes for readability
        commandStr += "0x" + String(command[i], HEX) + " ";
    }
    if (commandLength > 8) {
        commandStr += "... (" + String(commandLength) + " bytes total)";
    }
    
    logger_.debug("RS485Manager", commandStr);
}

void RS485Manager::logCommunicationResult(bool success, size_t responseLength) {
    if (success) {
        logger_.debug("RS485Manager", "Communication successful, received " + String(responseLength) + " bytes");
    } else {
        if (responseLength == 0) {
            logger_.warning("RS485Manager", "Communication timeout - no response received");
        } else {
            logger_.warning("RS485Manager", "Communication failed - received " + String(responseLength) + " bytes");
        }
    }
}

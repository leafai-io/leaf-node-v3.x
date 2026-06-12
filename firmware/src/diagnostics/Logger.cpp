#include "Logger.h"
#include <stdarg.h>

Logger::Logger() 
    : logLevel_(LogLevel::INFO)
    , serialEnabled_(false)
    , timestampEnabled_(true)
    , colorEnabled_(false) {
}

Logger::~Logger() {
}

bool Logger::initialize(LogLevel level, bool enableSerial) {
    logLevel_ = level;
    serialEnabled_ = enableSerial;
    
    if (serialEnabled_ && !Serial) {
        Serial.begin(115200);
        delay(100);
    }
    
    return true;
}

void Logger::log(LogLevel level, const String& tag, const String& message) {
    if (level < logLevel_) {
        return;
    }
    
    String formattedMessage = formatMessage(level, tag, message);
    
    if (serialEnabled_) {
        Serial.println(formattedMessage);
    }
    
    // Here you could add file logging, network logging, etc.
}

void Logger::logf(LogLevel level, const String& tag, const char* format, ...) {
    if (level < logLevel_) {
        return;
    }
    
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log(level, tag, String(buffer));
}

String Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:   return "TRACE";
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:               return "UNKNOWN";
    }
}

LogLevel Logger::stringToLevel(const String& levelStr) {
    String upper = levelStr;
    upper.toUpperCase();
    
    if (upper == "TRACE")   return LogLevel::TRACE;
    if (upper == "DEBUG")   return LogLevel::DEBUG;
    if (upper == "INFO")    return LogLevel::INFO;
    if (upper == "WARNING" || upper == "WARN") return LogLevel::WARNING;
    if (upper == "ERROR")   return LogLevel::ERROR;
    if (upper == "FATAL")   return LogLevel::FATAL;
    
    return LogLevel::INFO; // Default
}

String Logger::formatMessage(LogLevel level, const String& tag, const String& message) {
    String formatted;
    
    if (colorEnabled_) {
        formatted += getColorCode(level);
    }
    
    if (timestampEnabled_) {
        formatted += "[" + getTimestamp() + "] ";
    }
    
    formatted += "[" + levelToString(level) + "] ";
    formatted += "[" + tag + "] ";
    formatted += message;
    
    if (colorEnabled_) {
        formatted += getResetColor();
    }
    
    return formatted;
}

String Logger::getTimestamp() {
    uint32_t ms = millis();
    uint32_t seconds = ms / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    
    ms %= 1000;
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%03u", 
             (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds, (unsigned int)ms);
    
    return String(buffer);
}

String Logger::getColorCode(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:   return "\033[37m";  // White
        case LogLevel::DEBUG:   return "\033[36m";  // Cyan
        case LogLevel::INFO:    return "\033[32m";  // Green
        case LogLevel::WARNING: return "\033[33m";  // Yellow
        case LogLevel::ERROR:   return "\033[31m";  // Red
        case LogLevel::FATAL:   return "\033[35m";  // Magenta
        default:               return "";
    }
}

String Logger::getResetColor() {
    return "\033[0m";
}

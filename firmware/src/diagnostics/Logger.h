#pragma once

#include <Arduino.h>
#include "../LeafNodeTypes.h"

/**
 * @brief Logging system for debugging and monitoring
 * 
 * Provides structured logging with different levels, timestamps,
 * and optional output to Serial and/or persistent storage.
 */
class Logger {
public:
    Logger();
    ~Logger();

    /**
     * @brief Initialize the logger
     * @param level Minimum log level to output
     * @param enableSerial Enable output to Serial
     * @return true if initialization was successful
     */
    bool initialize(LogLevel level = LogLevel::INFO, bool enableSerial = true);

    /**
     * @brief Set the minimum log level
     * @param level New minimum log level
     */
    void setLogLevel(LogLevel level) { logLevel_ = level; }

    /**
     * @brief Get current log level
     * @return Current log level
     */
    LogLevel getLogLevel() const { return logLevel_; }

    /**
     * @brief Log a message
     * @param level Log level
     * @param tag Tag/category for the message
     * @param message Message to log
     */
    void log(LogLevel level, const String& tag, const String& message);

    /**
     * @brief Log with printf-style formatting
     * @param level Log level
     * @param tag Tag/category for the message
     * @param format Printf-style format string
     * @param ... Arguments for formatting
     */
    void logf(LogLevel level, const String& tag, const char* format, ...);

    // Convenience methods for different log levels
    void trace(const String& tag, const String& message) { log(LogLevel::TRACE, tag, message); }
    void debug(const String& tag, const String& message) { log(LogLevel::DEBUG, tag, message); }
    void info(const String& tag, const String& message) { log(LogLevel::INFO, tag, message); }
    void warning(const String& tag, const String& message) { log(LogLevel::WARNING, tag, message); }
    void error(const String& tag, const String& message) { log(LogLevel::ERROR, tag, message); }
    void fatal(const String& tag, const String& message) { log(LogLevel::FATAL, tag, message); }

    /**
     * @brief Enable/disable timestamp in log messages
     * @param enabled true to enable timestamps
     */
    void setTimestampEnabled(bool enabled) { timestampEnabled_ = enabled; }

    /**
     * @brief Enable/disable color output (for terminals that support it)
     * @param enabled true to enable colors
     */
    void setColorEnabled(bool enabled) { colorEnabled_ = enabled; }

    /**
     * @brief Get log level as string
     * @param level Log level
     * @return String representation of log level
     */
    static String levelToString(LogLevel level);

    /**
     * @brief Parse log level from string
     * @param levelStr String representation of log level
     * @return Log level, or LogLevel::INFO if parsing fails
     */
    static LogLevel stringToLevel(const String& levelStr);

private:
    LogLevel logLevel_;
    bool serialEnabled_;
    bool timestampEnabled_;
    bool colorEnabled_;

    String formatMessage(LogLevel level, const String& tag, const String& message);
    String getTimestamp();
    String getColorCode(LogLevel level);
    String getResetColor();
};

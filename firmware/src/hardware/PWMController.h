#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../diagnostics/Logger.h"
#include "config.h"

/**
 * @brief PWM Controller for GPIO2
 * 
 * This class provides PWM output control on GPIO2 using ESP32 LEDC.
 * Supports value and percentage control with 8-bit resolution (0-255).
 * Includes MQTT integration for remote control and status reporting.
 * 
 * Features:
 * - 8-bit resolution (0-255)
 * - Value/Percentage control
 * - MQTT command interface
 * - Status tracking
 */
class PWMController {
public:
    /**
     * @brief Constructor
     * @param logger Pointer to Logger instance
     */
    PWMController(Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~PWMController();

    /**
     * @brief Initialize the PWM controller
     * @param pin GPIO pin number (default: 2)
     * @param channel LEDC channel (default: 3)
     * @param frequency PWM frequency in Hz (default: 5000)
     * @param resolution PWM resolution in bits (default: 8)
     * @return true if initialization successful
     */
    bool initialize(uint8_t pin = 2, uint8_t channel = 3, uint32_t frequency = 5000, uint8_t resolution = 8);

    /**
     * @brief Set PWM output value
     * @param value PWM value (0-255 for 8-bit)
     * @return true if write successful
     */
    bool setValue(uint16_t value);

    /**
     * @brief Set PWM output value with auto-off timer
     * @param value PWM value (0-255 for 8-bit)
     * @param durationMillis Duration in milliseconds before auto-off (0 = no timer)
     * @return true if write successful
     */
    bool setValueWithDuration(uint16_t value, uint32_t durationMillis);

    /**
     * @brief Set PWM output as percentage
     * @param percent Percentage of full scale (0.0 to 100.0)
     * @return true if write successful
     */
    bool setPercent(float percent);

    /**
     * @brief Set PWM output as percentage with auto-off timer
     * @param percent Percentage of full scale (0.0 to 100.0)
     * @param durationMillis Duration in milliseconds before auto-off (0 = no timer)
     * @return true if write successful
     */
    bool setPercentWithDuration(float percent, uint32_t durationMillis);

    /**
     * @brief Set PWM output as voltage
     * @param voltage Voltage level (0.0 to 3.3V)
     * @return true if write successful
     */
    bool setVoltage(float voltage);

    /**
     * @brief Set PWM output as voltage with auto-off timer
     * @param voltage Voltage level (0.0 to 3.3V)
     * @param durationMillis Duration in milliseconds before auto-off (0 = no timer)
     * @return true if write successful
     */
    bool setVoltageWithDuration(float voltage, uint32_t durationMillis);

    /**
     * @brief Turn PWM off (set to 0)
     * @return true if successful
     */
    bool turnOff();

    /**
     * @brief Get current PWM value
     * @return Current PWM value (0-255)
     */
    uint16_t getCurrentValue() const { return currentValue_; }

    /**
     * @brief Get current output percentage
     * @return Current percentage (0.0-100.0)
     */
    float getCurrentPercent() const;

    /**
     * @brief Get current output voltage
     * @return Current voltage (0.0-3.3V)
     */
    float getCurrentVoltage() const;

    /**
     * @brief Get last command type
     * @return "value", "percent", "voltage", or "" if not set
     */
    String getLastCommandType() const { return lastCommandType_; }

    /**
     * @brief Check if auto-off timer is active
     * @return true if timer is running
     */
    bool isTimerActive() const { return timerActive_; }

    /**
     * @brief Get remaining timer duration in milliseconds
     * @return Remaining milliseconds, or 0 if timer not active
     */
    uint32_t getRemainingMillis() const;

    /**
     * @brief Check and process timer (call from main loop)
     * @return true if timer expired and PWM was turned off
     */
    bool checkTimer();

    /**
     * @brief Check if PWM controller is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Handle MQTT command
     * @param command Command name (set_value, set_percent, off, read, status)
     * @param params JSON parameters
     * @return true if command executed successfully
     */
    bool handleCommand(const String& command, JsonVariantConst params);

    /**
     * @brief Get status as JSON
     * @param doc JSON document to populate
     */
    void getStatus(JsonDocument& doc);

    /**
     * @brief Set LED update callback
     * @param callback Function to call when PWM value changes
     */
    void setLEDUpdateCallback(void (*callback)());

private:
    Logger* logger_;                    // Logger instance
    uint8_t pin_;                       // GPIO pin
    uint8_t channel_;                   // LEDC channel
    uint32_t frequency_;                // PWM frequency
    uint8_t resolution_;                // PWM resolution in bits
    uint16_t maxValue_;                 // Maximum value (2^resolution - 1)
    uint16_t currentValue_;             // Current PWM value
    bool initialized_;                  // Initialization state
    String lastCommandType_;            // Last command type executed
    void (*ledUpdateCallback_)();       // LED update callback
    
    // Timer for auto-off functionality
    bool timerActive_;                  // Timer active flag
    uint32_t timerEndMillis_;           // Timer end timestamp (millis())

    /**
     * @brief Clamp value to valid range
     * @param value Input value
     * @return Clamped value
     */
    uint16_t clampValue(uint16_t value);

    /**
     * @brief Write PWM value to hardware
     * @param value PWM value
     * @return true if successful
     */
    bool writePWM(uint16_t value);
};

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "../diagnostics/Logger.h"
#include "../network/MQTTManager.h"

/**
 * @brief MCP4725 12-bit Digital-to-Analog Converter (DAC) Driver
 * 
 * This class provides interface for the MCP4725 DAC using I2C communication.
 * Supports voltage output control (0-VDD) with 12-bit resolution (4096 steps).
 * Includes MQTT integration for remote control and status reporting.
 * 
 * Features:
 * - 12-bit resolution (0-4095)
 * - Fast mode and EEPROM write mode
 * - Voltage/Percentage control
 * - MQTT command interface
 * - Status publishing
 */
class MCP4725 {
public:
    /**
     * @brief Power-down modes for the DAC
     */
    enum PowerDownMode {
        NORMAL = 0x00,        // Normal mode (no power down)
        PD_1K = 0x01,         // Power down with 1kΩ to ground
        PD_100K = 0x02,       // Power down with 100kΩ to ground
        PD_500K = 0x03        // Power down with 500kΩ to ground
    };

    /**
     * @brief Constructor
     * @param logger Pointer to Logger instance
     * @param vdd Supply voltage in volts (default 3.3V for ESP32)
     * @param outputGain Amplifier gain factor (default 3.06 for LM358 setup)
     */
    MCP4725(Logger* logger, float vdd = 3.3, float outputGain = 3.06);
    
    /**
     * @brief Destructor
     */
    ~MCP4725();

    /**
     * @brief Initialize the DAC
     * @param address I2C address (default 0x60)
     * @return true if initialization successful
     */
    bool initialize(uint8_t address = 0x60);

    /**
     * @brief Set DAC output value (fast mode - writes to DAC register only)
     * @param value 12-bit value (0-4095)
     * @param powerDown Power-down mode (default: NORMAL)
     * @return true if write successful
     */
    bool setValue(uint16_t value, PowerDownMode powerDown = NORMAL);

    /**
     * @brief Set DAC output voltage (accounts for amplifier gain)
     * @param voltage Desired final output voltage (0 to VDD * outputGain)
     * @return true if write successful
     */
    bool setVoltage(float voltage);
    
    /**
     * @brief Set raw DAC voltage (before amplifier)
     * @param voltage Desired DAC voltage (0 to VDD)
     * @return true if write successful
     */
    bool setRawVoltage(float voltage);

    /**
     * @brief Set DAC output as percentage
     * @param percent Percentage of full scale (0.0 to 100.0)
     * @return true if write successful
     */
    bool setPercent(float percent);

    /**
     * @brief Write value to both DAC register and EEPROM
     * @param value 12-bit value (0-4095)
     * @param powerDown Power-down mode (default: NORMAL)
     * @return true if write successful
     * @note This writes to EEPROM which has limited write cycles (~1 million)
     */
    bool setValueWithEEPROM(uint16_t value, PowerDownMode powerDown = NORMAL);

    /**
     * @brief Read current DAC value from device
     * @return Current 12-bit value (0-4095), or 0xFFFF on error
     */
    uint16_t readValue();

    /**
     * @brief Read current DAC value and EEPROM settings
     * @param dacValue Output: Current DAC register value
     * @param eepromValue Output: EEPROM stored value
     * @param powerDown Output: Power-down mode
     * @return true if read successful
     */
    bool readSettings(uint16_t& dacValue, uint16_t& eepromValue, PowerDownMode& powerDown);

    /**
     * @brief Get current output voltage based on last set value
     * @return Current voltage
     */
    float getCurrentVoltage() const;
    
    /**
     * @brief Get current DAC value (0-4095)
     * @return Current 12-bit DAC value
     */
    uint16_t getCurrentValue() const { return currentValue_; }

    /**
     * @brief Get current output percentage
     * @return Current percentage (0.0-100.0)
     */
    float getCurrentPercent() const;

    /**
     * @brief Get last command type
     * @return "value", "voltage", "percent", or "" if not set
     */
    String getLastCommandType() const { return lastCommandType_; }

    /**
     * @brief Check if DAC is available/responding
     * @return true if DAC is available
     */
    bool isAvailable();

    /**
     * @brief Set MQTT manager for remote control
     * @param mqttManager Pointer to MQTTManager instance
     */
    void setMQTTManager(MQTTManager* mqttManager);
    
    /**
     * @brief Set LED update callback
     * @param callback Function to call when DAC value changes
     */
    void setLEDUpdateCallback(void (*callback)());

    /**
     * @brief Publish current DAC status to MQTT
     * @param topic MQTT topic to publish to
     * @return true if publish successful
     */
    bool publishStatus(const String& topic);

    /**
     * @brief Handle MQTT command parameters for DAC control
     * @param params JSON parameters object from command handler (const reference)
     * @param command The specific command name
     * @return true if command executed successfully
     * 
     * Called by CommandHandler with standardized format.
     * Commands registered: dac_set_value, dac_set_voltage, dac_set_percent, etc.
     */
    bool handleCommand(const String& command, JsonVariantConst params);

    /**
     * @brief Get status string for debugging
     * @return Status information string
     */
    String getStatus();

    /**
     * @brief Reset DAC to zero output
     * @return true if successful
     */
    bool reset();

private:
    Logger* logger_;
    MQTTManager* mqttManager_;
    uint8_t i2cAddress_;
    float vdd_;                 // DAC supply voltage (typically 3.3V)
    float outputGain_;          // Amplifier gain factor (default 3.06)
    uint16_t currentValue_;     // Current DAC value (0-4095)
    bool initialized_;
    void (*ledUpdateCallback_)(); // Callback to update LED when DAC changes
    String lastCommandType_;    // Last command type: "value", "voltage", or "percent"
    
    /**
     * @brief Initialize I2C bus
     * @return true if I2C initialization successful
     */
    bool initializeI2C();
    
    /**
     * @brief Write command to DAC (internal helper)
     * @param value 12-bit value
     * @param mode Write mode (0x40=fast, 0x60=EEPROM)
     * @param powerDown Power-down bits
     * @return true if write successful
     */
    bool writeDAC(uint16_t value, uint8_t mode, uint8_t powerDown);
    
    /**
     * @brief Convert value to voltage
     * @param value 12-bit value (0-4095)
     * @return Voltage
     */
    float valueToVoltage(uint16_t value) const;
    
    /**
     * @brief Convert voltage to value
     * @param voltage Voltage (0-VDD)
     * @return 12-bit value (0-4095)
     */
    uint16_t voltageToValue(float voltage) const;
    
    /**
     * @brief Create JSON status payload
     * @return JSON string
     */
    String createStatusPayload();
    
    /**
     * @brief Validate 12-bit value
     * @param value Value to validate
     * @return Clamped value (0-4095)
     */
    uint16_t clampValue(uint16_t value) const;
};

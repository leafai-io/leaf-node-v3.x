#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "THRS485Sensor.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief LEAFTHSN Leaf Wetness and Temperature Sensor Driver
 * 
 * This class provides interface for the LEAFTHSN (Leaf Temperature Humidity Sensor Network)
 * using RS485 communication protocol with direct MQTT publishing capability.
 * Measures both leaf wetness/humidity and leaf temperature.
 */
class LEAFTHSN : public THRS485Sensor<LEAFTHSNData> {
public:
    // RS485 communication settings
    static const unsigned long BAUDRATE = 4800;  // LEAFTHSN uses 4800 baud
    
    // LEAFTHSN Commands
    static const byte CMD_READ_HUMIDITY[8];     // Read leaf humidity/wetness
    static const byte CMD_READ_TEMPERATURE[8];  // Read leaf temperature
    
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     */
    LEAFTHSN(RS485Manager* rs485Manager, Logger* logger);

protected:
    // Implementation of pure virtual methods from base class
    String getSensorName() const override { return "LEAFTHSN"; }
    String getValueName1() const override { return "leaf_humidity"; }
    String getValueName2() const override { return "leaf_temperature"; }
    
    String createJsonPayload(const LEAFTHSNData& data) const override;
    
    LEAFTHSNData createInvalidData() const override {
        return {0.0, 0.0, false, millis()};
    }
    
    LEAFTHSNData populateDataStruct(double value1, double value2, bool valid, unsigned long timestamp) const override {
        return {value1, value2, valid, timestamp};
    }
    
    bool isDataValid(const LEAFTHSNData& data) const override {
        return data.valid;
    }
};

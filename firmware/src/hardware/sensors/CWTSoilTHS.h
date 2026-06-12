#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "THRS485Sensor.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief CWTSoilTHS Soil Temperature and Humidity Sensor Driver
 * 
 * This class provides interface for the CWT Soil Temperature and Humidity Sensor
 * using RS485 communication protocol with direct MQTT publishing capability.
 * Measures both soil humidity and soil temperature.
 * 
 * Note: Uses identical RS485 commands as LEAFTHSN sensor.
 */
class CWTSoilTHS : public THRS485Sensor<CWTSoilTHSData> {
public:
    // RS485 communication settings
    static const unsigned long BAUDRATE = 4800;  // CWTSoilTHS uses 4800 baud
    
    // CWTSoilTHS Commands (same as LEAFTHSN)
    static const byte CMD_READ_HUMIDITY[8];     // Read soil humidity
    static const byte CMD_READ_TEMPERATURE[8];  // Read soil temperature
    
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     */
    CWTSoilTHS(RS485Manager* rs485Manager, Logger* logger);

protected:
    // Implementation of pure virtual methods from base class
    String getSensorName() const override { return "CWTSoilTHS"; }
    String getValueName1() const override { return "VWC"; }
    String getValueName2() const override { return "Soil Temp"; }
    
    String createJsonPayload(const CWTSoilTHSData& data) const override;
    
    CWTSoilTHSData createInvalidData() const override {
        return {0.0, 0.0, false, millis()};
    }
    
    CWTSoilTHSData populateDataStruct(double value1, double value2, bool valid, unsigned long timestamp) const override {
        return {value1, value2, valid, timestamp};
    }
    
    bool isDataValid(const CWTSoilTHSData& data) const override {
        return data.valid;
    }
};

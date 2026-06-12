#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "THRS485Sensor.h"
#include "../../LeafNodeTypes.h"

/**
 * @brief CWTTHXXS Air Temperature and Humidity Sensor Driver
 * 
 * This class provides interface for the CWT-TH-XXS Air Temperature and Humidity Sensor
 * using RS485 communication protocol with direct MQTT publishing capability.
 * Measures both air humidity and air temperature.
 * 
 * Note: Uses identical RS485 commands as CWTSoilTHS and LEAFTHSN sensors.
 */
class CWTTHXXS : public THRS485Sensor<CWTTHXXSData> {
public:
    // RS485 communication settings
    static const unsigned long BAUDRATE = 4800;  // CWTTHXXS uses 4800 baud
    
    // CWTTHXXS Commands (same as LEAFTHSN and CWTSoilTHS)
    static const byte CMD_READ_HUMIDITY[8];     // Read air humidity
    static const byte CMD_READ_TEMPERATURE[8];  // Read air temperature
    
    /**
     * @brief Constructor
     * @param rs485Manager Pointer to RS485Manager communication instance
     * @param logger Pointer to Logger instance
     */
    CWTTHXXS(RS485Manager* rs485Manager, Logger* logger);
    
    /**
     * @brief Read sensor data and calculate VPD
     * @return CWTTHXXSData structure with sensor readings including VPD
     */
    CWTTHXXSData readSensor();
    
    /**
     * @brief Read sensor data and publish directly to MQTT (with VPD)
     * @param topic MQTT topic to publish to
     * @return true if reading and publishing successful
     */
    bool readAndPublishData(const String& topic);

protected:
    // Implementation of pure virtual methods from base class
    String getSensorName() const override { return "CWTTHXXS"; }
    String getValueName1() const override { return "humidity"; }
    String getValueName2() const override { return "temperature"; }
    
    String createJsonPayload(const CWTTHXXSData& data) const override;
    
    CWTTHXXSData createInvalidData() const override {
        return {0.0, 0.0, 0.0, false, millis()};
    }
    
    CWTTHXXSData populateDataStruct(double value1, double value2, bool valid, unsigned long timestamp) const override {
        return {value1, value2, 0.0, valid, timestamp};
    }
    
    bool isDataValid(const CWTTHXXSData& data) const override {
        return data.valid;
    }

private:
    /**
     * @brief Calculate Vapor Pressure Deficit (VPD)
     * @param temperature Air temperature in °C
     * @param humidity Relative humidity in %
     * @return VPD in kPa
     */
    static double calculateVPD(double temperature, double humidity);
};

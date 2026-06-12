#include "CWTSoilTHS.h"

// Static command definitions (identical to LEAFTHSN)
// Command to read soil humidity
const byte CWTSoilTHS::CMD_READ_HUMIDITY[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
// Command to read soil temperature
const byte CWTSoilTHS::CMD_READ_TEMPERATURE[8] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xd5, 0xca};

CWTSoilTHS::CWTSoilTHS(RS485Manager* rs485Manager, Logger* logger) 
    : THRS485Sensor<CWTSoilTHSData>(rs485Manager, logger, 
                                     CMD_READ_HUMIDITY, CMD_READ_TEMPERATURE, BAUDRATE) {
}

String CWTSoilTHS::createJsonPayload(const CWTSoilTHSData& data) const {
    DynamicJsonDocument doc(256);
    
    // Sensor identification
    doc["sensor_type"] = "CWTSoilTHS";
    
    // Sensor data (rounded to 2 decimal places)
    doc["VWC"] = round(data.VWC * 100.0) / 100.0;
    doc["Soil Temp"] = round(data.SoilTemp * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        if (logger_) {
            logger_->error("CWTSoilTHS", "Failed to serialize JSON");
        }
        return "";
    }
    
    return jsonString;
}

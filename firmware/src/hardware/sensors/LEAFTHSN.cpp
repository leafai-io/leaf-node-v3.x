#include "LEAFTHSN.h"

// Static command definitions
// Command to read leaf humidity/wetness
const byte LEAFTHSN::CMD_READ_HUMIDITY[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a};
// Command to read leaf temperature
const byte LEAFTHSN::CMD_READ_TEMPERATURE[8] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xd5, 0xca};

LEAFTHSN::LEAFTHSN(RS485Manager* rs485Manager, Logger* logger) 
    : THRS485Sensor<LEAFTHSNData>(rs485Manager, logger, 
                                  CMD_READ_HUMIDITY, CMD_READ_TEMPERATURE, BAUDRATE) {
}

String LEAFTHSN::createJsonPayload(const LEAFTHSNData& data) const {
    DynamicJsonDocument doc(256);
    
    // Sensor identification
    doc["sensor_type"] = "LEAFTHSN";
    
    // Sensor data (rounded to 2 decimal places)
    doc["Leaf Humidity"] = round(data.leaf_humidity * 100.0) / 100.0;
    doc["Leaf Temp"] = round(data.leaf_temperature * 100.0) / 100.0;
    doc["timestamp"] = data.timestamp;
    
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        if (logger_) {
            logger_->error("LEAFTHSN", "Failed to serialize JSON");
        }
        return "";
    }
    
    return jsonString;
}

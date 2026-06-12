#include "TEROS12.h"

TEROS12::TEROS12(SDI12Manager* sdi12Manager, Logger* logger, char sensorAddress)
    : sdi12Manager_(sdi12Manager), logger_(logger), mqttManager_(nullptr),
      sensorAddress_(sensorAddress), initialized_(false) {
}

TEROS12::~TEROS12() {
}

bool TEROS12::initialize() {
    if (!sdi12Manager_) {
        logger_->error("TEROS12", "SDI-12 Manager instance is null");
        return false;
    }
    
    logger_->info("TEROS12", "Initializing TEROS 12 sensor at address '" + String(sensorAddress_) + "'");
    
    // Verify sensor is present and responding
    if (!sdi12Manager_->verifySensor(sensorAddress_)) {
        logger_->error("TEROS12", "Sensor not responding at address '" + String(sensorAddress_) + "'");
        return false;
    }
    
    // Get sensor identification
    String sensorInfo = getSensorInfo();
    if (sensorInfo.length() > 0) {
        logger_->info("TEROS12", "Sensor info: " + sensorInfo);
    } else {
        logger_->warning("TEROS12", "Could not retrieve sensor identification");
    }
    
    initialized_ = true;
    logger_->info("TEROS12", "TEROS 12 sensor initialized and ready");
    return true;
}

TEROS12Data TEROS12::readSensor() {
    TEROS12Data result = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, millis()};
    
    if (!initialized_) {
        logger_->error("TEROS12", "Sensor not initialized");
        return result;
    }
    
    logger_->debug("TEROS12", "Reading sensor data");
    
    // Start measurement (using concurrent measurement for faster response)
    int waitTime = 0;
    int numValues = 0;
    
    if (!sdi12Manager_->startMeasurement(sensorAddress_, waitTime, numValues)) {
        logger_->error("TEROS12", "Failed to start measurement");
        return result;
    }
    
    logger_->debug("TEROS12", "Measurement started - values: " + String(numValues));
    
    // TEROS12 specific: Always wait 1 second after 0C! command
    delay(1000);
    
    // Retrieve data
    float values[3] = {0.0, 0.0, 0.0}; // RAW VWC, Temperature, EC
    int retrievedValues = 0;
    
    if (!sdi12Manager_->getData(sensorAddress_, 0, values, 3, retrievedValues)) {
        logger_->error("TEROS12", "Failed to retrieve measurement data");
        return result;
    }
    
    if (retrievedValues < 2) {
        logger_->error("TEROS12", "Insufficient data received (expected at least 2, got " + 
                                 String(retrievedValues) + ")");
        return result;
    }
    
    // Parse values
    result.rawVWC = values[0];        // Raw VWC count (needs calibration)
    result.temperature = values[1];   // Temperature (°C)
    
    // EC is optional (depends on TEROS 12 version)
    if (retrievedValues >= 3) {
        result.EC_Bulk = values[2];   // Electrical Conductivity (mS/cm)
    } else {
        result.EC_Bulk = 0.0;         // Not available
    }
    
    // Convert RAW to VWC for mineral soil using METER linear calibration
    // Formula: Θ (m³/m³) = 3.879 × 10⁻⁴ × RAW - 0.6956
    // Valid for mineral soils with EC 0-8 dS/m
    result.VWC_Soil = (0.0003879 * result.rawVWC) - 0.6956;
    
    // Convert RAW to VWC for soilless media using METER cubic calibration (Equation 7)
    // Formula: Θ = 6.771×10⁻¹⁰ × RAW³ - 5.105×10⁻⁶ × RAW² + 1.302×10⁻² × RAW - 10.848
    // Valid for hydroponics, coco, rockwool, perlite, etc.
    double raw = result.rawVWC;
    double raw2 = raw * raw;           // RAW²
    double raw3 = raw2 * raw;          // RAW³
    result.VWC_Soilless = (6.771e-10 * raw3) - (5.105e-06 * raw2) + (0.01302 * raw) - 10.848;
    
    // Calculate apparent dielectric permittivity (εₐ) using Equation 8
    // Formula: εₐ = (2.887×10⁻⁹ × RAW³ - 2.080×10⁻⁵ × RAW² + 5.276×10⁻² × RAW - 43.39)²
    double temp_perm = (2.887e-09 * raw3) - (2.080e-05 * raw2) + (0.05276 * raw) - 43.39;
    result.permittivity = temp_perm * temp_perm;  // Square the result
    
    // Calculate Pore Water EC using Hilhorst equation
    // Formula: EC_Pore = ((εₐ - ε_solid) / (ε_water - ε_solid)) × EC_Bulk
    // Constants: ε_water = 80.3, ε_solid = 4.3 (mineral soil standard)
    if (result.EC_Bulk > 0.0 && result.permittivity > 0.0) {
        const double epsilon_water = 80.3;
        const double epsilon_solid = 4.3;
        result.EC_Pore = ((result.permittivity - epsilon_solid) / (epsilon_water - epsilon_solid)) * result.EC_Bulk;
    } else {
        result.EC_Pore = 0.0;  // Cannot calculate without EC or valid permittivity
    }
    
    result.valid = true;
    result.timestamp = millis();
    
    // Validate data ranges
    if (!validateData(result)) {
        logger_->warning("TEROS12", "Sensor data validation failed - values out of range");
        result.valid = false;
        return result;
    }
    
    logger_->info("TEROS12", "Sensor reading successful - RAW: " + String(result.rawVWC, 1) + 
                            ", VWC Soil: " + String(result.VWC_Soil, 3) + " m³/m³ (" + 
                            String(result.VWC_Soil * 100.0, 1) + "%), VWC Soilless: " + 
                            String(result.VWC_Soilless, 3) + " m³/m³ (" + 
                            String(result.VWC_Soilless * 100.0, 1) + "%), Temp: " + 
                            String(result.temperature, 2) + " °C" +
                            (retrievedValues >= 3 ? ", EC Bulk: " + String(result.EC_Bulk, 2) + " mS/cm" : "") +
                            (result.EC_Pore > 0.0 ? ", EC Pore: " + String(result.EC_Pore, 2) + " mS/cm" : "") +
                            ", εₐ: " + String(result.permittivity, 2));
    
    return result;
}

bool TEROS12::readAndPublishData(const String& topic) {
    if (!initialized_) {
        logger_->error("TEROS12", "Sensor not initialized");
        return false;
    }
    
    if (!mqttManager_) {
        logger_->error("TEROS12", "MQTT manager not set");
        return false;
    }
    
    if (!mqttManager_->isConnected()) {
        logger_->warning("TEROS12", "MQTT not connected");
        return false;
    }
    
    // Read sensor data
    TEROS12Data sensorData = readSensor();
    if (!sensorData.valid) {
        logger_->error("TEROS12", "Failed to read sensor data");
        return false;
    }
    
    // Create JSON payload
    String jsonPayload = createJsonPayload(sensorData);
    if (jsonPayload.isEmpty()) {
        logger_->error("TEROS12", "Failed to create JSON payload");
        return false;
    }
    
    // Publish to MQTT
    if (mqttManager_->publish(topic, jsonPayload)) {
        logger_->info("TEROS12", "Sensor data published to MQTT: " + topic);
        return true;
    } else {
        logger_->error("TEROS12", "Failed to publish sensor data to MQTT");
        return false;
    }
}

void TEROS12::setMQTTManager(MQTTManager* mqttManager) {
    mqttManager_ = mqttManager;
}

bool TEROS12::isAvailable() {
    if (!initialized_) {
        return false;
    }
    
    return sdi12Manager_->verifySensor(sensorAddress_);
}

String TEROS12::getSensorInfo() {
    if (!sdi12Manager_) {
        return "";
    }
    
    String info;
    if (sdi12Manager_->sendIdentification(sensorAddress_, info)) {
        return info;
    }
    
    return "";
}

bool TEROS12::changeAddress(char newAddress) {
    if (!initialized_) {
        logger_->error("TEROS12", "Sensor not initialized");
        return false;
    }
    
    if (sdi12Manager_->changeAddress(sensorAddress_, newAddress)) {
        logger_->info("TEROS12", "Sensor address changed from '" + String(sensorAddress_) + 
                                "' to '" + String(newAddress) + "'");
        sensorAddress_ = newAddress;
        return true;
    }
    
    return false;
}

void TEROS12::printRawResponse() {
    if (!initialized_) {
        logger_->error("TEROS12", "Sensor not initialized");
        return;
    }
    
    // Get identification
    String info;
    if (sdi12Manager_->sendIdentification(sensorAddress_, info)) {
        logger_->info("TEROS12", "Identification: " + info);
    }
    
    // Start measurement and get response
    int waitTime, numValues;
    if (sdi12Manager_->startMeasurement(sensorAddress_, waitTime, numValues)) {
        logger_->info("TEROS12", "Measurement response - Wait: " + String(waitTime) + 
                                "s, Values: " + String(numValues));
        
        // Wait and retrieve data
        delay(waitTime * 1000 + 100);
        
        float values[3];
        int retrieved;
        if (sdi12Manager_->getData(sensorAddress_, 0, values, 3, retrieved)) {
            String dataStr = "Raw data values: ";
            for (int i = 0; i < retrieved; i++) {
                dataStr += String(values[i], 3);
                if (i == 0) dataStr += " (RAW VWC) ";
                else if (i == 1) dataStr += " (Temp °C) ";
                else if (i == 2) dataStr += " (EC mS/cm) ";
            }
            logger_->info("TEROS12", dataStr);
            
            // Show calibrated values
            if (retrieved >= 1) {
                double raw = values[0];
                double raw2 = raw * raw;
                double raw3 = raw2 * raw;
                
                // VWC calculations
                double vwc_soil = (0.0003879 * raw) - 0.6956;
                double vwc_soilless = (6.771e-10 * raw3) - (5.105e-06 * raw2) + (0.01302 * raw) - 10.848;
                
                // Permittivity calculation
                double temp_perm = (2.887e-09 * raw3) - (2.080e-05 * raw2) + (0.05276 * raw) - 43.39;
                double permittivity = temp_perm * temp_perm;
                
                logger_->info("TEROS12", "Calibrated VWC_Soil: " + String(vwc_soil, 3) + 
                                        " m³/m³ (" + String(vwc_soil * 100.0, 1) + "%)");
                logger_->info("TEROS12", "Calibrated VWC_Soilless: " + String(vwc_soilless, 3) + 
                                        " m³/m³ (" + String(vwc_soilless * 100.0, 1) + "%)");
                logger_->info("TEROS12", "Permittivity εₐ: " + String(permittivity, 2));
                
                // EC Pore calculation if EC is available
                if (retrieved >= 3 && values[2] > 0.0) {
                    const double epsilon_water = 80.3;
                    const double epsilon_solid = 4.3;
                    double ec_pore = ((permittivity - epsilon_solid) / (epsilon_water - epsilon_solid)) * values[2];
                    logger_->info("TEROS12", "EC Pore: " + String(ec_pore, 2) + " mS/cm");
                }
            }
        }
    }
}

String TEROS12::createJsonPayload(const TEROS12Data& data) {
    StaticJsonDocument<512> doc;
    
    doc["sensor"] = "TEROS12";
    doc["timestamp"] = data.timestamp;
    
    // Raw VWC count
    doc["VWC Raw"] = serialized(String(data.rawVWC, 1));
    
    // VWC for mineral soil in percentage
    doc["VWC Soil"] = serialized(String(data.VWC_Soil * 100.0, 1));  // %
    
    // VWC for soilless media in percentage
    doc["VWC Soilless"] = serialized(String(data.VWC_Soilless * 100.0, 1));  // %
    
    // Temperature
    doc["Soil Temp"] = serialized(String(data.temperature, 2));  // °C
    
    // Permittivity
    doc["Permittivity"] = serialized(String(data.permittivity, 2));  // εₐ
    
    // EC Bulk (Electrical Conductivity) - only if available
    if (data.EC_Bulk > 0.0) {
        doc["EC Bulk"] = serialized(String(data.EC_Bulk, 2));  // mS/cm
    }
    
    // EC Pore (Pore Water EC) - only if calculated
    if (data.EC_Pore > 0.0) {
        doc["EC Pore"] = serialized(String(data.EC_Pore, 2));  // mS/cm
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

bool TEROS12::validateData(const TEROS12Data& data) {
    // Log warnings for unusual values but don't reject data
    
    // Raw VWC typical range check (but don't fail)
    if (data.rawVWC < 500.0 || data.rawVWC > 5000.0) {
        logger_->warning("TEROS12", "Raw VWC unusual: " + String(data.rawVWC, 1));
    }
    
    // VWC_Soil typical range check (but don't fail)
    if (data.VWC_Soil < -0.2 || data.VWC_Soil > 1.0) {
        logger_->warning("TEROS12", "VWC_Soil unusual: " + String(data.VWC_Soil, 3));
    }
    
    // VWC_Soilless typical range check (but don't fail)
    if (data.VWC_Soilless < -0.2 || data.VWC_Soilless > 1.2) {
        logger_->warning("TEROS12", "VWC_Soilless unusual: " + String(data.VWC_Soilless, 3));
    }
    
    // Permittivity typical range check (but don't fail)
    if (data.permittivity < 1.0 || data.permittivity > 100.0) {
        logger_->warning("TEROS12", "Permittivity unusual: " + String(data.permittivity, 2));
    }
    
    // Temperature typical range check (but don't fail)
    if (data.temperature < -50.0 || data.temperature > 70.0) {
        logger_->warning("TEROS12", "Temperature unusual: " + String(data.temperature, 2));
    }
    
    // EC Bulk typical range check (but don't fail)
    // EC in mS/cm can go up to 200+ in very saline conditions
    if (data.EC_Bulk > 0.0 && data.EC_Bulk > 200.0) {
        logger_->warning("TEROS12", "EC Bulk unusual: " + String(data.EC_Bulk, 2) + " mS/cm");
    }
    
    // EC Pore typical range check (but don't fail)
    if (data.EC_Pore > 0.0 && data.EC_Pore > 500.0) {
        logger_->warning("TEROS12", "EC Pore unusual: " + String(data.EC_Pore, 2) + " mS/cm");
    }
    
    // Always return true - we want the data even if values are unusual
    return true;
}

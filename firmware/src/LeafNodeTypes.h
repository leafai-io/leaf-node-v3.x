#pragma once

#include <Arduino.h>
#include "config.h"

/**
 * @brief System-wide configuration constants and definitions
 * 
 * Note: All configuration constants are now defined in /config.h
 * This namespace provides only build-time information.
 */
namespace LeafNodeConstants {
    // Build information (generated at compile time)
    [[maybe_unused]] static const char* BUILD_DATE = __DATE__ " " __TIME__;
}

/**
 * @brief System status enumeration
 */
enum class SystemStatus {
    BOOTING,
    INITIALIZING,
    RUNNING,
    ERROR,
    UPDATING,
    SLEEPING,
    FACTORY_RESET,
    OTA_UPDATING  // Added for OTA updates
};

/**
 * @brief Network status enumeration
 */
enum class NetworkStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
    BLE_CONFIG,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    WIFI_RECONNECTING,  // WiFi reconnection in progress
    BLE_CONFIG_MODE,
    BLE_ACTIVE
};

/**
 * @brief MQTT connection status enumeration
 */
enum class MQTTStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED,
    RECONNECTING,
    ERROR
};

/**
 * @brief LED status enumeration for WS2812 status indication
 */
enum class LEDStatus {
    OFF,
    FACTORY_MODE,     // White (all LEDs on) - Factory configuration mode
    BOOTING,          // Blue pulse
    SUCCESS_FADE,     // Green fade in and out after successful startup
    CONNECTING,       // Orange
    CONNECTED,        // Green
    WIFI_CONNECTING,  // Orange
    WIFI_CONNECTED,   // Green
    WIFI_FAILED,      // Red
    WIFI_RECONNECTING, // Blue-Red alternating blink - WiFi reconnection in progress
    BLE_CONFIG,       // Blue solid
    BLE_SCANNING,     // Yellow
    WAITING_CHAIN_CONFIG, // Blue breathing/fade - Waiting for config from chain
    OTA_DOWNLOADING,  // Blue pulsing - OTA download
    OTA_INSTALLING,   // Purple - OTA installation
    ERROR            // Red blink
};

/**
 * @brief Task priority levels
 */
enum class TaskPriority {
    CRITICAL = 4,
    HIGH_PRIORITY = 3,
    NORMAL = 2,
    LOW_PRIORITY = 1,  // Renamed to avoid conflict with Arduino LOW macro
    BACKGROUND = 0
};

/**
 * @brief Log levels for the logging system
 */
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    FATAL = 5
};

/**
 * @brief Sensor profile enumeration
 */
enum class SensorProfile {
    NONE = 0,
    SLT5007 = 1,
    SHT31 = 2,
    CWTPSS = 3,      // CWT-PS-S PAR Sensor
    LEAFTHSN = 4,    // Leaf Temperature Humidity Sensor Network
    CWTSOILTHS = 5,  // CWT Soil Temperature Humidity Sensor
    TEROS12 = 6,     // METER TEROS 12 Soil Moisture Sensor (SDI-12)
    EZOPH = 7,       // Atlas Scientific EZO pH Sensor (I2C)
    EZOEC = 8,       // Atlas Scientific EZO EC Sensor (I2C)
    DS18B20 = 9,     // Dallas DS18B20 Temperature Sensor (1-Wire)
    CWTTHXXS = 10    // CWT Air Temperature & Humidity Sensor (RS485)
};

/**
 * @brief Sensor data structure for SLT5007
 */
struct SLT5007Data {
    double VWC_Soil;    // Volumetric Water Content (%) - Soil
    double Bulk_EC;     // Bulk Electrical Conductivity (dS/m)
    double Soil_Temp;   // Soil Temperature (°C)
    double VWC_Rock;    // VWC for Rockwool (%)
    double VWC_Coco;    // VWC for Coconut (%) 
    double Pore_EC;     // Pore Electrical Conductivity (dS/m)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for SHT31
 */
struct SHT31Data {
    double temperature;  // Temperature (°C)
    double humidity;     // Relative Humidity (%)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for CWTPSS (PAR Sensor)
 */
struct CWTPSSData {
    double PAR;  // Photosynthetically Active Radiation (μmol/m²/s)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for LEAFTHSN (Leaf Wetness & Temperature)
 */
struct LEAFTHSNData {
    double leaf_humidity;     // Leaf wetness/humidity (%)
    double leaf_temperature;  // Leaf temperature (°C)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for CWTSoilTHS (Soil Temperature & Humidity)
 */
struct CWTSoilTHSData {
    double VWC;        // Volumetric Water Content (%)
    double SoilTemp;   // Soil temperature (°C)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for CWTTHXXS (Air Temperature & Humidity)
 */
struct CWTTHXXSData {
    double air_humidity;      // Air relative humidity (%)
    double air_temperature;   // Air temperature (°C)
    double vpd;               // Vapor Pressure Deficit (kPa)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for TEROS12 (Soil Moisture Sensor)
 */
struct TEROS12Data {
    double rawVWC;        // Raw VWC count from sensor
    double VWC_Soil;      // Volumetric Water Content for mineral soil (m³/m³) - Linear calibration
    double VWC_Soilless;  // Volumetric Water Content for soilless media (m³/m³) - Cubic calibration
    double temperature;   // Soil Temperature (°C)
    double EC_Bulk;       // Bulk Electrical Conductivity (mS/cm)
    double permittivity;  // Apparent dielectric permittivity (εₐ)
    double EC_Pore;       // Pore water electrical conductivity (mS/cm)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for EZOph (pH Sensor)
 */
struct EZOphData {
    double pH;            // pH value (0-14)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for EZOec (EC Sensor)
 */
struct EZOecData {
    double EC;            // Electrical Conductivity (µS/cm)
    double TDS;           // Total Dissolved Solids (ppm)
    double SAL;           // Salinity (PSU - Practical Salinity Units)
    double GRAV;          // Specific Gravity
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Sensor data structure for DS18B20 (Temperature Sensor)
 */
struct DS18B20Data {
    double temperature;   // Temperature (°C)
    String deviceAddress; // OneWire device address (hex string)
    bool valid;
    unsigned long timestamp;
};

/**
 * @brief Generic sensor data structure
 */
struct SensorData {
    SensorProfile profile;
    union {
        SLT5007Data slt5007;
        SHT31Data sht31;
        CWTPSSData cwtpss;
        LEAFTHSNData leafthsn;
        CWTSoilTHSData cwtsoilths;
        TEROS12Data teros12;
        EZOphData ezoph;
        EZOecData ezoec;
        DS18B20Data ds18b20;
        CWTTHXXSData cwtthxxs;
    } data;
    unsigned long timestamp;
    bool valid;
};

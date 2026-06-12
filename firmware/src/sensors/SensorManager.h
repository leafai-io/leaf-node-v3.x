#pragma once

#include <Arduino.h>
#include "../runtime/RuntimeConfig.h"
#include "../diagnostics/Logger.h"
#include "../network/MQTTManager.h"
#include "../hardware/RS485Manager.h"
#include "../hardware/SDI12Manager.h"
#include "../hardware/OneWireManager.h"
#include "../LeafNodeTypes.h"
#include "config.h"

// Always include all sensor types (no conditional compilation)
#include "../hardware/sensors/SLT5007.h"
#include "../hardware/sensors/SHT31.h"
#include "../hardware/sensors/CWTPSS.h"
#include "../hardware/sensors/LEAFTHSN.h"
#include "../hardware/sensors/CWTSoilTHS.h"
#include "../hardware/sensors/TEROS12.h"
#include "../hardware/sensors/EZOph.h"
#include "../hardware/sensors/EZOec.h"
#include "../hardware/sensors/DS18B20.h"
#include "../hardware/sensors/CWTTHXXS.h"

/**
 * @brief Universal Sensor Manager with Runtime Configuration
 * 
 * Manages sensor reading and direct MQTT publishing with runtime sensor selection.
 * Supports all sensor types in a single firmware and allows dynamic configuration
 * via MQTT commands stored persistently.
 */
class SensorManager {
public:
    /**
     * @brief Constructor
     * @param config Configuration instance
     * @param rs485Manager RS485 manager instance
     * @param sdi12Manager SDI-12 manager instance
     * @param oneWireManager OneWire manager instance
     * @param mqttManager MQTT manager instance
     * @param logger Logger instance
     */
    SensorManager(RuntimeConfig* config, RS485Manager* rs485Manager, SDI12Manager* sdi12Manager,
                  OneWireManager* oneWireManager, MQTTManager* mqttManager, Logger* logger);
    
    /**
     * @brief Destructor
     */
    ~SensorManager();

    /**
     * @brief Initialize the sensor manager
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Read sensor and publish to MQTT
     * @return true if reading and publishing successful
     */
    bool readAndPublish();

    /**
     * @brief Check if it's time for next sensor reading (considering error backoff)
     * @return true if sensor reading is due
     */
    bool isReadingDue();

    /**
     * @brief Update last reading timestamp
     */
    void updateLastReading();

    /**
     * @brief Get sensor reading interval
     * @return Reading interval in milliseconds
     */
    uint32_t getReadingInterval() const;

    /**
     * @brief Configure sensor profile via runtime command
     * @param profile Sensor profile to activate
     * @return true if sensor was configured successfully
     */
    bool configureSensor(SensorProfile profile);

    /**
     * @brief Get current active sensor profile
     * @return Current sensor profile
     */
    SensorProfile getCurrentProfile() const;

    /**
     * @brief Check if any sensor is configured
     * @return true if a sensor is configured and ready
     */
    bool hasSensorConfigured() const;

    /**
     * @brief Check if sensor is properly initialized and available for reading
     * @return true if sensor is available and functional
     */
    bool isSensorAvailable() const;

    /**
     * @brief Set sensor reading interval
     * @param intervalMs New reading interval in milliseconds
     * @return true if interval was set successfully
     */
    bool setReadingInterval(uint32_t intervalMs);
    
    /**
     * @brief Reset error counters and backoff (when sensor recovers)
     */
    void resetErrorState();
    
    /**
     * @brief Get current sensor health status
     * @return true if sensor is healthy, false if problematic
     */
    bool getSensorHealthStatus() const;
    
    /**
     * @brief Get consecutive error count
     * @return Number of consecutive errors
     */
    uint8_t getConsecutiveErrors() const;
    
    /**
     * @brief Get current backoff interval
     * @return Current backoff interval in milliseconds
     */
    uint32_t getBackoffInterval() const;
    
    /**
     * @brief Get sensor type name as string
     * @return Sensor type name or "None"
     */
    String getSensorTypeName() const;

    /**
     * @brief Pause sensor operations (for OTA updates)
     */
    void pause();

    /**
     * @brief Resume sensor operations after pause
     */
    void resume();

    /**
     * @brief Get EZOph sensor instance
     * @return Pointer to EZOph sensor or nullptr if not configured
     */
    EZOph* getEZOphSensor() const;

private:
    RuntimeConfig* config_;
    RS485Manager* rs485Manager_;
    SDI12Manager* sdi12Manager_;
    OneWireManager* oneWireManager_;
    MQTTManager* mqttManager_;
    Logger* logger_;
    bool initialized_;
    bool paused_;
    unsigned long lastReading_;
    uint32_t readingInterval_;
    SensorProfile currentProfile_;
    
    // Track which bus is currently initialized
    bool rs485Initialized_;
    bool sdi12Initialized_;
    bool oneWireInitialized_;
    
    // Error handling and backoff mechanism
    uint8_t consecutiveErrors_;
    unsigned long lastErrorTime_;
    uint32_t errorBackoffInterval_;
    static const uint8_t MAX_CONSECUTIVE_ERRORS = SENSOR_MAX_CONSECUTIVE_ERRORS;
    static const uint32_t BASE_BACKOFF_INTERVAL = SENSOR_BASE_BACKOFF_INTERVAL;
    static const uint32_t MAX_BACKOFF_INTERVAL = SENSOR_MAX_BACKOFF_INTERVAL;
    
    // Sensor instances (all sensors available at runtime)
    SLT5007* slt5007Sensor_;
    SHT31* sht31Sensor_;
    CWTPSS* cwtpssSensor_;
    LEAFTHSN* leafthsnSensor_;
    CWTSoilTHS* cwtsoilthsSensor_;
    TEROS12* teros12Sensor_;
    EZOph* ezophSensor_;
    EZOec* ezoecSensor_;
    DS18B20* ds18b20Sensor_;
    CWTTHXXS* cwtthxxsSensor_;
    
    /**
     * @brief Initialize specific sensor based on profile
     * @return true if sensor initialization successful
     */
    bool initializeSensor();
    
    /**
     * @brief Cleanup all sensor instances
     */
    void cleanupSensors();
    
    /**
     * @brief Cleanup bus managers (shutdown active buses)
     */
    void cleanupBusManagers();
    
    /**
     * @brief Initialize required bus for sensor type
     * @param profile Sensor profile that needs bus initialization
     * @return true if bus initialization successful
     */
    bool initializeBusForSensor(SensorProfile profile);
    
    /**
     * @brief Get MQTT topic for sensor data
     * @return MQTT topic string
     */
    String getSensorDataTopic() const;
    
    /**
     * @brief Handle sensor reading error and update backoff state
     */
    void handleSensorError();
    
    /**
     * @brief Handle successful sensor reading (reset error state)
     */
    void handleSensorSuccess();
};

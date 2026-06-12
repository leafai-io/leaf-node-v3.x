#pragma once

#include <Arduino.h>
#include "../LeafNodeTypes.h"
#include "../network/UARTChainManager.h"

// Forward declarations
class StatusLED;
class MCP4725;
class PWMController;

/**
 * @brief System Manager
 * 
 * Manager responsible for hardware initialization and monitoring.
 * Manages system-level operations including hardware setup, power management,
 * watchdog timer, and system health monitoring.
 */
class SystemManager {
public:
    SystemManager();
    ~SystemManager();

    /**
     * @brief Initialize system hardware and peripherals
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Update system monitoring (should be called regularly)
     */
    void update();

    /**
     * @brief Get current system status
     * @return Current system status
     */
    SystemStatus getStatus() const { return status_; }

    /**
     * @brief Set system status
     * @param status New system status
     */
    void setStatus(SystemStatus status);

    /**
     * @brief Get system uptime in milliseconds
     * @return System uptime
     */
    uint32_t getUptime() const;

    /**
     * @brief Get free heap memory
     * @return Free heap memory in bytes
     */
    size_t getFreeHeap() const;

    /**
     * @brief Get minimum free heap since boot
     * @return Minimum free heap memory in bytes
     */
    size_t getMinFreeHeap() const;

    /**
     * @brief Restart the system
     */
    void restart();

    /**
     * @brief Enable/disable watchdog timer
     * @param enabled true to enable, false to disable
     */
    void setWatchdogEnabled(bool enabled);

    /**
     * @brief Feed the watchdog timer
     */
    void feedWatchdog();

    /**
     * @brief Get system information as JSON
     * @return System information
     */
    String getSystemInfo() const;

    /**
     * @brief Set RGB LED colors with PWM values (0-255) - for MQTT commands
     * @param red Red LED brightness (0-255)
     * @param green Green LED brightness (0-255)  
     * @param blue Blue LED brightness (0-255)
     */
    void setRGBLED(uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * @brief Get current RGB LED values
     * @param red Reference to store red value
     * @param green Reference to store green value  
     * @param blue Reference to store blue value
     */
    void getCurrentRGBLED(uint8_t& red, uint8_t& green, uint8_t& blue) const;
    
    /**
     * @brief Set RGB LED states (boolean on/off) - for MQTT commands
     * @param red Red LED state
     * @param green Green LED state  
     * @param blue Blue LED state
     */
    void setRGBLED(bool red, bool green, bool blue);
    
    /**
     * @brief Perform a success fade animation (green fade in/out)
     */
    void startSuccessFade();

    /**
     * @brief Stop the success fade animation and resume automatic LED control
     */
    void stopSuccessFade();

    /**
     * @brief Set LED to show network status
     * @param networkStatus Network status to display
     */
    void setNetworkLED(NetworkStatus networkStatus);

    /**
     * @brief Set LED to show actuator active state (yellow/orange)
     * @param active true if actuator is active
     */
    void setActuatorLED(bool active);
    
    /**
     * @brief Update LED based on actuator and DAC state
     * Checks both actuator and DAC, shows orange LED if either is active
     */
    void updateActuatorDACLED();

    /**
     * @brief Set the status LED instance (called by LeafNode)
     * @param led Pointer to StatusLED instance
     */
    void setStatusLED(StatusLED* led);

    /**
     * @brief Get the status LED instance
     * @return Pointer to StatusLED instance, or nullptr if not set
     */
    StatusLED* getStatusLED() const { return statusLED_; }

    /**
     * @brief Get the UART Chain Manager instance
     * @return Pointer to UARTChainManager instance
     */
    UARTChainManager* getChainManager() { return &chainManager_; }

    /**
     * @brief Initialize MCP4725 DAC
     * @return true if initialization was successful
     */
    bool initializeMCP4725();

    /**
     * @brief Get the MCP4725 DAC instance
     * @return Pointer to MCP4725 instance, or nullptr if not initialized
     */
    MCP4725* getMCP4725() { return mcp4725_; }

    /**
     * @brief Initialize PWM Controller
     * @return true if initialization was successful
     */
    bool initializePWMController();

    /**
     * @brief Get the PWM Controller instance (IO2)
     * @return Pointer to PWMController instance, or nullptr if not initialized
     */
    PWMController* getPWMController() { return pwmController_; }

    /**
     * @brief Initialize PWM Controller for MOSFET
     * @return true if initialization was successful
     */
    bool initializePWMControllerMOSFET();

    /**
     * @brief Get the PWM Controller MOSFET instance
     * @return Pointer to PWMController instance, or nullptr if not initialized
     */
    PWMController* getPWMControllerMOSFET() { return pwmControllerMOSFET_; }

    /**
     * @brief Check PWM timers and auto-turn-off if expired
     * Call this from main loop to handle duration-based PWM commands
     */
    void updatePWMTimers();

private:
    SystemStatus status_;
    bool initialized_;
    bool watchdogEnabled_;
    size_t minFreeHeap_;
    uint32_t lastWatchdogFeed_;
    
    // Timing
    unsigned long lastHeartbeat_;
    unsigned long bootTime_;
    
    // Status LED controller (pointer managed by LeafNode)
    StatusLED* statusLED_;
    
    // UART Chain Manager
    UARTChainManager chainManager_;
    
    // MCP4725 DAC
    MCP4725* mcp4725_;
    
    // PWM Controller (IO2)
    PWMController* pwmController_;
    
    // PWM Controller (MOSFET)
    PWMController* pwmControllerMOSFET_;
    
    // LED priority state
    bool actuatorActive_;
    
    void initializeHardware();
    void setupPins();
    void updateMemoryStats();
    void checkWatchdog();
    void updateStatusLED();
};

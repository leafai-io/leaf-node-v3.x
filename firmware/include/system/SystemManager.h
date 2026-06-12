#pragma once

#include <Arduino.h>
#include "../LeafNodeTypes.h"

/**
 * @brief System manager responsible for hardware initialization and monitoring
 * 
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

private:
    SystemStatus status_;
    uint32_t bootTime_;
    uint32_t lastWatchdogFeed_;
    bool watchdogEnabled_;
    size_t minFreeHeap_;

    void initializeHardware();
    void setupPins();
    void updateMemoryStats();
    void checkWatchdog();
    void updateStatusLED();
};

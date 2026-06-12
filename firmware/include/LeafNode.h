#pragma once

#include <Arduino.h>
#include "runtime/RuntimeConfig.h"
#include "system/SystemManager.h"
#include "core/TaskManager.h"
#include "diagnostics/Logger.h"

/**
 * @brief Main LeafNode class - Central coordinator for the entire system
 * 
 * This class acts as the main orchestrator for the LeafNode firmware,
 * managing initialization, configuration, and coordinating between
 * different system components.
 */
class LeafNode {
public:
    LeafNode();
    ~LeafNode();

    /**
     * @brief Initialize the LeafNode system
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Main update loop - should be called from Arduino loop()
     */
    void update();

    /**
     * @brief Get system status
     * @return Current system status
     */
    SystemStatus getSystemStatus() const;

    /**
     * @brief Reset the system to factory defaults
     */
    void factoryReset();

private:
    RuntimeConfig* config_;
    SystemManager* systemManager_;
    TaskManager* taskManager_;
    Logger* logger_;
    
    bool initialized_;
    uint32_t lastHeartbeat_;
    
    void setupLogging();
    void printStartupInfo();
    bool validateConfiguration();
};

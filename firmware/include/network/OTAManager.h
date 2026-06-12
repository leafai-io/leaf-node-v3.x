#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <functional>
#include "../../src/LeafNodeTypes.h"

/**
 * @brief OTA Update status enumeration
 */
enum class OTAStatus {
    IDLE,
    DOWNLOADING,
    INSTALLING,
    COMPLETE,
    FAILED,
    VERIFICATION_FAILED,
    DOWNLOAD_FAILED,
    INSTALL_FAILED
};

/**
 * @brief OTA Progress information
 */
struct OTAProgress {
    OTAStatus status;
    uint32_t bytesDownloaded;
    uint32_t totalBytes;
    uint8_t percentage;
    String currentVersion;
    String targetVersion;
    String errorMessage;
    unsigned long startTime;
    unsigned long estimatedTimeRemaining;
};

/**
 * @brief OTA Manager - Over-The-Air firmware update manager
 * 
 * Handles firmware updates via HTTP/HTTPS download and installation.
 * Integrates with MQTT command system and provides progress feedback.
 */
class OTAManager {
public:
    OTAManager();
    ~OTAManager();

    /**
     * @brief Initialize the OTA manager
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Update OTA manager (should be called regularly)
     */
    void update();

    /**
     * @brief Start firmware update from URL
     * @param firmwareVersion Target firmware version
     * @param downloadUrl URL to download firmware from
     * @param fileSize Expected file size in bytes
     * @param forceUpdate Force update even if same version
     * @param timeoutMinutes Download timeout in minutes (default: 30)
     * @return true if update was started successfully
     */
    bool startUpdate(const String& firmwareVersion, const String& downloadUrl, 
                    uint32_t fileSize, bool forceUpdate = false, 
                    uint32_t timeoutMinutes = 30);

    /**
     * @brief Cancel ongoing update
     */
    void cancelUpdate();

    /**
     * @brief Get current update progress
     * @return Current OTA progress information
     */
    OTAProgress getProgress() const { return progress_; }

    /**
     * @brief Get current OTA status
     * @return Current OTA status
     */
    OTAStatus getStatus() const { return progress_.status; }

    /**
     * @brief Set progress callback for update notifications
     * @param callback Function to call when progress updates
     */
    void setProgressCallback(std::function<void(const OTAProgress&)> callback) {
        progressCallback_ = callback;
    }

    /**
     * @brief Set completion callback for when update finishes
     * @param callback Function to call when update completes or fails
     */
    void setCompletionCallback(std::function<void(bool, const String&)> callback) {
        completionCallback_ = callback;
    }

    /**
     * @brief Set logger for OTA operations
     */
    void setLogger(class Logger* logger) { logger_ = logger; }

    /**
     * @brief Set system manager for LED feedback and system control
     */
    void setSystemManager(class SystemManager* systemManager) { systemManager_ = systemManager; }

    /**
     * @brief Set task manager to pause tasks during update
     */
    void setTaskManager(class TaskManager* taskManager) { taskManager_ = taskManager; }

    /**
     * @brief Set sensor manager to stop sensors during update
     */
    void setSensorManager(class SensorManager* sensorManager) { sensorManager_ = sensorManager; }

    /**
     * @brief Get current firmware version
     * @return Current firmware version string
     */
    String getCurrentVersion() const;

    /**
     * @brief Check if update is in progress
     * @return true if update is currently running
     */
    bool isUpdateInProgress() const {
        return progress_.status == OTAStatus::DOWNLOADING || 
               progress_.status == OTAStatus::INSTALLING;
    }

    /**
     * @brief Get OTA statistics as JSON
     * @return JSON document with OTA statistics
     */
    DynamicJsonDocument getStatistics() const;

private:
    bool initialized_;
    class Logger* logger_;
    class SystemManager* systemManager_;
    class TaskManager* taskManager_;
    class SensorManager* sensorManager_;
    
    // Update state
    OTAProgress progress_;
    bool tasksWerePaused_;
    HTTPClient httpClient_;
    
    // Configuration
    String downloadUrl_;
    uint32_t expectedFileSize_;
    uint32_t timeoutMs_;
    bool forceUpdate_;
    
    // Timing and statistics
    unsigned long updateStartTime_;
    unsigned long lastProgressUpdate_;
    unsigned long downloadStartTime_;
    unsigned long installStartTime_;
    uint32_t averageDownloadSpeed_;
    uint32_t totalUpdatesAttempted_;
    uint32_t totalUpdatesSuccessful_;
    uint32_t totalUpdatesFailed_;
    
    // Callbacks
    std::function<void(const OTAProgress&)> progressCallback_;
    std::function<void(bool, const String&)> completionCallback_;
    
    // Internal methods
    bool validateParameters(const String& firmwareVersion, const String& downloadUrl, 
                           uint32_t fileSize);
    bool checkVersionCompatibility(const String& targetVersion);
    bool initializeDownload();
    void performDownload();
    bool performInstallation();
    void updateProgress();
    void completeUpdate(bool success, const String& message);
    void resetProgress();
    void updateLEDStatus();
    void calculateTimeEstimate();
    
    // System control
    void pauseSystemTasks();
    void resumeSystemTasks();
    
    // HTTP download helpers
    bool connectToServer();
    int downloadChunk(uint8_t* buffer, size_t bufferSize);
    bool verifyDownload();
    
    // Update helpers
    bool beginUpdate();
    bool writeUpdateChunk(const uint8_t* data, size_t len);
    bool finalizeUpdate();
    
    // Utility methods
    String formatBytes(uint32_t bytes) const;
    String formatTime(unsigned long milliseconds) const;
    uint8_t calculatePercentage(uint32_t current, uint32_t total) const;
};
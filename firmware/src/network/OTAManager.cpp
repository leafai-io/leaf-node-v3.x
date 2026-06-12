#include "../../include/network/OTAManager.h"
#include "../diagnostics/Logger.h"
#include "../system/SystemManager.h"
#include "../hardware/StatusLED.h"
#include "../core/TaskManager.h"
#include "../sensors/SensorManager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_ota_ops.h>

OTAManager::OTAManager()
    : initialized_(false)
    , logger_(nullptr)
    , systemManager_(nullptr)
    , taskManager_(nullptr)
    , sensorManager_(nullptr)
    , tasksWerePaused_(false)
    , expectedFileSize_(0)
    , timeoutMs_(30 * 60 * 1000) // 30 minutes default
    , forceUpdate_(false)
    , updateStartTime_(0)
    , lastProgressUpdate_(0)
    , downloadStartTime_(0)
    , installStartTime_(0)
    , averageDownloadSpeed_(0)
    , totalUpdatesAttempted_(0)
    , totalUpdatesSuccessful_(0)
    , totalUpdatesFailed_(0) {
    resetProgress();
}

OTAManager::~OTAManager() {
    cancelUpdate();
}

bool OTAManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Initializing OTA Manager...");
    }
    
    resetProgress();
    progress_.currentVersion = getCurrentVersion();
    
    initialized_ = true;
    
    if (logger_) {
        logger_->info("OTAManager", "OTA Manager initialized. Current version: " + progress_.currentVersion);
    }
    
    return true;
}

void OTAManager::update() {
    if (!initialized_) {
        return;
    }
    
    // Update progress and handle ongoing operations
    if (isUpdateInProgress()) {
        updateProgress();
        updateLEDStatus();
        
        // Send progress updates every 2 seconds, even if no new data
        static unsigned long lastGlobalProgressUpdate = 0;
        if (millis() - lastGlobalProgressUpdate > 2000) {
            lastGlobalProgressUpdate = millis();
            if (progressCallback_) {
                progressCallback_(progress_);
            }
        }
        
        // Check for timeout
        if (millis() - updateStartTime_ > timeoutMs_) {
            completeUpdate(false, "Update timeout exceeded");
            return;
        }
        
        // Handle download phase - continue downloading while installing
        if (progress_.status == OTAStatus::DOWNLOADING || progress_.status == OTAStatus::INSTALLING) {
            performDownload();
        }
    }
}

bool OTAManager::startUpdate(const String& firmwareVersion, const String& downloadUrl, 
                            uint32_t fileSize, bool forceUpdate, uint32_t timeoutMinutes) {
    if (!initialized_) {
        if (logger_) {
            logger_->error("OTAManager", "Manager not initialized");
        }
        return false;
    }
    
    if (isUpdateInProgress()) {
        if (logger_) {
            logger_->warning("OTAManager", "Update already in progress");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Starting firmware update to version: " + firmwareVersion);
        logger_->info("OTAManager", "Download URL: " + downloadUrl);
        logger_->info("OTAManager", "File size: " + formatBytes(fileSize));
    }
    
    // Validate parameters
    if (!validateParameters(firmwareVersion, downloadUrl, fileSize)) {
        return false;
    }
    
    // Check version compatibility
    if (!forceUpdate && !checkVersionCompatibility(firmwareVersion)) {
        completeUpdate(false, "Version check failed - update not needed or incompatible");
        return false;
    }
    
    // ⚠️ CRITICAL: Pause all system tasks and sensors before starting update
    if (logger_) {
        logger_->info("OTAManager", "Pausing all system tasks and sensors for firmware update");
    }
    pauseSystemTasks();
    
    // Store update parameters
    downloadUrl_ = downloadUrl;
    expectedFileSize_ = fileSize;
    timeoutMs_ = timeoutMinutes * 60 * 1000;
    forceUpdate_ = forceUpdate;
    
    // Initialize progress tracking
    resetProgress();
    progress_.targetVersion = firmwareVersion;
    progress_.totalBytes = fileSize;
    progress_.status = OTAStatus::DOWNLOADING;
    updateStartTime_ = millis();
    downloadStartTime_ = millis();
    totalUpdatesAttempted_++;
    
    // Initialize download
    if (!initializeDownload()) {
        completeUpdate(false, "Failed to initialize download");
        return false;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Firmware update started successfully");
    }
    
    // Update LED to show download in progress
    updateLEDStatus();
    
    // Send initial progress update
    updateProgress();
    if (progressCallback_) {
        progressCallback_(progress_);
    }
    
    return true;
}

void OTAManager::cancelUpdate() {
    if (!isUpdateInProgress()) {
        return;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Cancelling firmware update");
    }
    
    // Clean up HTTP connection
    httpClient_.end();
    
    // Clean up update process if in progress
    if (progress_.status == OTAStatus::INSTALLING) {
        Update.abort();
    }
    
    completeUpdate(false, "Update cancelled by user");
}

String OTAManager::getCurrentVersion() const {
    // Try to get version from config or define
    #ifdef FIRMWARE_VERSION
        return String(FIRMWARE_VERSION);
    #else
        return "Unknown";
    #endif
}

DynamicJsonDocument OTAManager::getStatistics() const {
    DynamicJsonDocument stats(512);
    
    stats["current_version"] = progress_.currentVersion;
    stats["status"] = static_cast<int>(progress_.status);
    stats["total_attempted"] = totalUpdatesAttempted_;
    stats["total_successful"] = totalUpdatesSuccessful_;
    stats["total_failed"] = totalUpdatesFailed_;
    
    if (isUpdateInProgress()) {
        stats["target_version"] = progress_.targetVersion;
        stats["progress_percent"] = progress_.percentage;
        stats["bytes_downloaded"] = progress_.bytesDownloaded;
        stats["total_bytes"] = progress_.totalBytes;
        stats["download_speed_bps"] = averageDownloadSpeed_;
        stats["estimated_time_remaining"] = progress_.estimatedTimeRemaining;
    }
    
    return stats;
}

bool OTAManager::validateParameters(const String& firmwareVersion, const String& downloadUrl, 
                                   uint32_t fileSize) {
    if (firmwareVersion.isEmpty()) {
        if (logger_) {
            logger_->error("OTAManager", "Invalid firmware version");
        }
        return false;
    }
    
    if (downloadUrl.isEmpty() || (!downloadUrl.startsWith("http://") && !downloadUrl.startsWith("https://"))) {
        if (logger_) {
            logger_->error("OTAManager", "Invalid download URL");
        }
        return false;
    }
    
    if (fileSize == 0 || fileSize > 4 * 1024 * 1024) { // Max 4MB
        if (logger_) {
            logger_->error("OTAManager", "Invalid file size: " + String(fileSize));
        }
        return false;
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        if (logger_) {
            logger_->error("OTAManager", "WiFi not connected");
        }
        return false;
    }
    
    return true;
}

bool OTAManager::checkVersionCompatibility(const String& targetVersion) {
    String currentVersion = getCurrentVersion();
    
    if (logger_) {
        logger_->debug("OTAManager", "Version check - Current: " + currentVersion + ", Target: " + targetVersion);
    }
    
    // If force update, skip version check
    if (forceUpdate_) {
        return true;
    }
    
    // If current version is unknown, allow update
    if (currentVersion == "Unknown") {
        return true;
    }
    
    // Simple version comparison - in a real implementation, you might want semantic versioning
    if (currentVersion == targetVersion) {
        if (logger_) {
            logger_->info("OTAManager", "Target version is same as current version");
        }
        return false; // Same version, no update needed
    }
    
    return true; // Allow update
}

bool OTAManager::initializeDownload() {
    if (logger_) {
        logger_->debug("OTAManager", "Initializing download from: " + downloadUrl_);
    }
    
    httpClient_.begin(downloadUrl_);
    httpClient_.setTimeout(30000); // 30 second timeout - increased for large files
    httpClient_.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpClient_.setReuse(false); // Don't reuse connections
    
    // Add headers for better compatibility
    httpClient_.addHeader("User-Agent", "LeafNode-OTA/1.0");
    httpClient_.addHeader("Connection", "keep-alive"); // Keep connection alive during download
    httpClient_.addHeader("Cache-Control", "no-cache");
    
    return connectToServer();
}

bool OTAManager::connectToServer() {
    int httpCode = httpClient_.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        if (logger_) {
            logger_->error("OTAManager", "HTTP request failed with code: " + String(httpCode));
        }
        return false;
    }
    
    // Verify content length
    int contentLength = httpClient_.getSize();
    if (contentLength > 0 && (uint32_t)contentLength != expectedFileSize_) {
        if (logger_) {
            logger_->warning("OTAManager", "Content length mismatch. Expected: " + 
                           String(expectedFileSize_) + ", Got: " + String(contentLength));
        }
        // Update expected size with actual size
        expectedFileSize_ = contentLength;
        progress_.totalBytes = contentLength;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Connected to server. Content length: " + formatBytes(contentLength));
    }
    
    return true;
}

void OTAManager::performDownload() {
    WiFiClient* stream = httpClient_.getStreamPtr();
    
    if (!stream) {
        completeUpdate(false, "Lost connection to server");
        return;
    }
    
    // Check connection status first
    if (!httpClient_.connected()) {
        if (logger_) {
            logger_->error("OTAManager", "HTTP client not connected in performDownload()");
        }
        if (progress_.bytesDownloaded < progress_.totalBytes) {
            completeUpdate(false, "Connection lost during download");
        }
        return;
    }
    
    // Only proceed if data is available
    int available = stream->available();
    if (available <= 0) {
        // No data available right now - this is normal during streaming
        return;
    }
    
    // Read available data, but not more than buffer size
    uint8_t buffer[1024]; // Increased buffer size
    int bytesToRead = min(available, (int)sizeof(buffer));
    
    // Set a longer timeout for larger chunks
    stream->setTimeout(5000); // 5 second timeout
    
    // Try to read data
    int bytesRead = stream->readBytes(buffer, bytesToRead);
    
    // Log debug info every 2 seconds
    if (logger_ && (millis() - lastProgressUpdate_ > 2000)) {
        logger_->info("OTAManager", "Read attempt - Available: " + String(available) + 
                     ", Requested: " + String(bytesToRead) + ", Got: " + String(bytesRead) + 
                     ", Downloaded: " + formatBytes(progress_.bytesDownloaded) + "/" + 
                     formatBytes(progress_.totalBytes) + " (" + String(progress_.percentage) + "%)");
    }
    
    if (bytesRead > 0) {
        // First chunk - begin update
        if (progress_.bytesDownloaded == 0) {
            if (!beginUpdate()) {
                completeUpdate(false, "Failed to begin update");
                return;
            }
            progress_.status = OTAStatus::INSTALLING;
            installStartTime_ = millis();
            
            if (logger_) {
                logger_->info("OTAManager", "Starting firmware installation...");
            }
        }
        
        // Write chunk to update partition
        if (!writeUpdateChunk(buffer, bytesRead)) {
            completeUpdate(false, "Failed to write update chunk");
            return;
        }
        
        progress_.bytesDownloaded += bytesRead;
        
        // Update progress every 2 seconds or when complete
        if (millis() - lastProgressUpdate_ > 2000 || progress_.bytesDownloaded >= progress_.totalBytes) {
            updateProgress();
            lastProgressUpdate_ = millis();
            
            if (logger_) {
                logger_->info("OTAManager", "Progress: " + String(progress_.percentage) + "% (" + 
                             formatBytes(progress_.bytesDownloaded) + "/" + formatBytes(progress_.totalBytes) + 
                             ") - Speed: " + formatBytes(averageDownloadSpeed_) + "/s");
            }
            
            if (progressCallback_) {
                progressCallback_(progress_);
            }
        }
        
        // Check if download is complete
        if (progress_.bytesDownloaded >= progress_.totalBytes) {
            httpClient_.end();
            
            if (logger_) {
                logger_->info("OTAManager", "Download complete, finalizing installation...");
            }
            
            if (!finalizeUpdate()) {
                completeUpdate(false, "Failed to finalize update");
                return;
            }
            
            completeUpdate(true, "Firmware update completed successfully");
        }
    } else {
        // Read error or timeout
        if (logger_) {
            logger_->error("OTAManager", "Read error or timeout occurred during download. Available: " + 
                         String(available) + ", Requested: " + String(bytesToRead) + ", Got: " + String(bytesRead));
        }
        completeUpdate(false, "Read error or timeout occurred during download");
    }
}

bool OTAManager::beginUpdate() {
    if (logger_) {
        logger_->info("OTAManager", "Beginning firmware installation...");
    }
    
    if (!Update.begin(progress_.totalBytes)) {
        if (logger_) {
            logger_->error("OTAManager", "Update begin failed: " + String(Update.getError()));
        }
        return false;
    }
    
    return true;
}

bool OTAManager::writeUpdateChunk(const uint8_t* data, size_t len) {
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        if (logger_) {
            logger_->error("OTAManager", "Update write failed: " + String(Update.getError()));
        }
        return false;
    }
    
    return true;
}

bool OTAManager::finalizeUpdate() {
    if (logger_) {
        logger_->info("OTAManager", "Finalizing firmware installation...");
    }
    
    if (!Update.end()) {
        if (logger_) {
            logger_->error("OTAManager", "Update finalize failed: " + String(Update.getError()));
        }
        return false;
    }
    
    if (!Update.isFinished()) {
        if (logger_) {
            logger_->error("OTAManager", "Update not finished properly");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "Firmware installation completed successfully");
    }
    
    return true;
}

void OTAManager::updateProgress() {
    progress_.percentage = calculatePercentage(progress_.bytesDownloaded, progress_.totalBytes);
    
    // Calculate download speed and time estimate
    calculateTimeEstimate();
}

void OTAManager::calculateTimeEstimate() {
    unsigned long elapsed = millis() - downloadStartTime_;
    if (elapsed > 0 && progress_.bytesDownloaded > 0) {
        averageDownloadSpeed_ = (progress_.bytesDownloaded * 1000) / elapsed; // bytes per second
        
        if (averageDownloadSpeed_ > 0) {
            uint32_t remainingBytes = progress_.totalBytes - progress_.bytesDownloaded;
            progress_.estimatedTimeRemaining = (remainingBytes / averageDownloadSpeed_) * 1000; // milliseconds
        }
    }
}

void OTAManager::completeUpdate(bool success, const String& message) {
    if (success) {
        progress_.status = OTAStatus::COMPLETE;
        totalUpdatesSuccessful_++;
        
        if (logger_) {
            logger_->info("OTAManager", "Update completed successfully: " + message);
        }
    } else {
        progress_.status = OTAStatus::FAILED;
        progress_.errorMessage = message;
        totalUpdatesFailed_++;
        
        if (logger_) {
            logger_->error("OTAManager", "Update failed: " + message);
        }
        
        // If update failed, resume system tasks
        if (tasksWerePaused_) {
            if (logger_) {
                logger_->info("OTAManager", "Resuming system tasks after failed update");
            }
            resumeSystemTasks();
        }
    }
    
    // Clean up
    httpClient_.end();
    
    // Update LED status
    updateLEDStatus();
    
    // Notify completion callback
    if (completionCallback_) {
        completionCallback_(success, message);
    }
    
    // Final progress callback
    if (progressCallback_) {
        progressCallback_(progress_);
    }
    
    // If successful, schedule restart
    if (success) {
        if (logger_) {
            logger_->info("OTAManager", "Restarting system in 5 seconds...");
        }
        
        // Give some time for final MQTT messages
        delay(5000);
        
        if (systemManager_) {
            systemManager_->restart();
        } else {
            ESP.restart();
        }
    }
}

void OTAManager::resetProgress() {
    progress_.status = OTAStatus::IDLE;
    progress_.bytesDownloaded = 0;
    progress_.totalBytes = 0;
    progress_.percentage = 0;
    progress_.currentVersion = getCurrentVersion();
    progress_.targetVersion = "";
    progress_.errorMessage = "";
    progress_.startTime = 0;
    progress_.estimatedTimeRemaining = 0;
}

void OTAManager::updateLEDStatus() {
    if (!systemManager_) {
        return;
    }
    
    switch (progress_.status) {
        case OTAStatus::DOWNLOADING:
            // Blue pulsing for download - get StatusLED from SystemManager
            if (auto statusLED = systemManager_->getStatusLED()) {
                statusLED->setStatus(LEDStatus::OTA_DOWNLOADING);
            }
            break;
            
        case OTAStatus::INSTALLING:
            // Purple for installation
            if (auto statusLED = systemManager_->getStatusLED()) {
                statusLED->setStatus(LEDStatus::OTA_INSTALLING);
            }
            break;
            
        case OTAStatus::COMPLETE:
            // Green success fade
            systemManager_->startSuccessFade();
            break;
            
        case OTAStatus::FAILED:
            // Red for failure
            if (auto statusLED = systemManager_->getStatusLED()) {
                statusLED->setStatus(LEDStatus::ERROR);
            }
            break;
            
        default:
            // Let system manager handle normal LED status
            break;
    }
}

String OTAManager::formatBytes(uint32_t bytes) const {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return String(bytes / 1024.0, 1) + " KB";
    } else {
        return String(bytes / (1024.0 * 1024.0), 1) + " MB";
    }
}

String OTAManager::formatTime(unsigned long milliseconds) const {
    unsigned long seconds = milliseconds / 1000;
    unsigned long minutes = seconds / 60;
    seconds = seconds % 60;
    
    if (minutes > 0) {
        return String(minutes) + "m " + String(seconds) + "s";
    } else {
        return String(seconds) + "s";
    }
}

uint8_t OTAManager::calculatePercentage(uint32_t current, uint32_t total) const {
    if (total == 0) {
        return 0;
    }
    
    uint32_t percentage = (current * 100) / total;
    return (uint8_t)min(percentage, 100U);
}

void OTAManager::pauseSystemTasks() {
    tasksWerePaused_ = false;
    
    // Pause all non-critical tasks in TaskManager
    if (taskManager_) {
        if (logger_) {
            logger_->info("OTAManager", "Pausing all task manager tasks");
        }
        taskManager_->pauseAll();
        tasksWerePaused_ = true;
    }
    
    // Disable all sensor readings via SensorManager
    if (sensorManager_) {
        if (logger_) {
            logger_->info("OTAManager", "Pausing sensor manager operations");
        }
        sensorManager_->pause();
        tasksWerePaused_ = true;
    }
    
    if (logger_) {
        logger_->info("OTAManager", "✓ All background processes paused for firmware update");
    }
}

void OTAManager::resumeSystemTasks() {
    if (!tasksWerePaused_) {
        return;
    }
    
    // Resume task manager tasks
    if (taskManager_) {
        if (logger_) {
            logger_->info("OTAManager", "Resuming task manager tasks");
        }
        taskManager_->resumeAll();
    }
    
    // Resume sensor manager
    if (sensorManager_) {
        if (logger_) {
            logger_->info("OTAManager", "Resuming sensor manager operations");
        }
        sensorManager_->resume();
    }
    
    tasksWerePaused_ = false;
    
    if (logger_) {
        logger_->info("OTAManager", "✓ All background processes resumed");
    }
}

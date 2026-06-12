#include "../../include/system/ScheduleManager.h"
#include "../diagnostics/Logger.h"
#include "../runtime/RuntimeConfig.h"
#include "../hardware/Actuator.h"
#include "../hardware/MCP4725.h"
#include "../hardware/PWMController.h"
#include "../network/ActuatorStatusPublisher.h"
#include <sys/time.h>

ScheduleManager::ScheduleManager()
    : logger_(nullptr)
    , config_(nullptr)
    , actuator_(nullptr)
    , mcp4725_(nullptr)
    , pwmController_(nullptr)
    , pwmControllerMOSFET_(nullptr)
    , actuatorStatusPublisher_(nullptr)
    , initialized_(false)
    , paused_(false)
    , lastCheckedHour_(-1)
    , lastCheckedMinute_(-1)
    , rampStartTime_(0)
    , rampStartValue_(0.0f)
    , rampTargetValue_(0.0f)
    , isRamping_(false) {
}

ScheduleManager::~ScheduleManager() {
}

bool ScheduleManager::initialize(Logger* logger, RuntimeConfig* config, Actuator* actuator, MCP4725* mcp4725, PWMController* pwmController, PWMController* pwmControllerMOSFET, ActuatorStatusPublisher* statusPublisher) {
    if (initialized_) {
        return true;
    }
    
    logger_ = logger;
    config_ = config;
    actuator_ = actuator;
    mcp4725_ = mcp4725;
    pwmController_ = pwmController;
    pwmControllerMOSFET_ = pwmControllerMOSFET;
    actuatorStatusPublisher_ = statusPublisher;
    
    if (!logger_ || !config_ || !actuator_) {
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid dependencies");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("ScheduleManager", "Initializing...");
    }
    
    // Schedule aus Config laden
    if (!loadFromConfig()) {
        if (logger_) {
            logger_->warning("ScheduleManager", "No schedule loaded from config");
        }
    }
    
    initialized_ = true;
    
    if (logger_) {
        logger_->info("ScheduleManager", "Initialized successfully");
        if (currentSchedule_.active) {
            logger_->info("ScheduleManager", "Restored schedule: " + 
                         currentSchedule_.actuatorType + " " + 
                         currentSchedule_.onAt + " - " + currentSchedule_.offAt);
        }
    }
    
    return true;
}
bool ScheduleManager::setSchedule(const String& actuatorType, const String& onAt, 
                                  const String& offAt, uint16_t validDays, int32_t timezone) {
    if (!initialized_) {
        if (logger_) {
            logger_->error("ScheduleManager", "Not initialized");
        }
        return false;
    }
    
    // Validierung
    if (!validateActuatorType(actuatorType)) {
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid actuator type: " + actuatorType);
        }
        return false;
    }
    
    if (!validateTimeFormat(onAt)) {
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid on_at format: " + onAt);
        }
        return false;
    }
    
    if (!validateTimeFormat(offAt)) {
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid off_at format: " + offAt);
        }
        return false;
    }
    
    if (onAt == offAt) {
        if (logger_) {
            logger_->error("ScheduleManager", "on_at and off_at must be different");
        }
        return false;
    }
    
    if (validDays > 365) {
        if (logger_) {
            logger_->error("ScheduleManager", "valid_days must be <= 365");
        }
        return false;
    }
    
    // Wenn sich der actuator_type ändert, schalte den vorherigen aus
    if (currentSchedule_.active && currentSchedule_.actuatorType != actuatorType) {
        String previousType = currentSchedule_.actuatorType;
        
        if (previousType == "mosfet" || previousType == "relay") {
            if (actuator_) {
                Actuator::Type type = (previousType == "mosfet") ? 
                                      Actuator::Type::MOSFET : Actuator::Type::RELAY;
                actuator_->turnOff(type);
                if (logger_) {
                    logger_->info("ScheduleManager", "Turned off previous actuator: " + previousType);
                }
            }
        } else if (previousType.startsWith("dac_") && mcp4725_) {
            mcp4725_->setValue(0);
            if (config_) {
                config_->setLastDACValue(0);
            }
            if (logger_) {
                logger_->info("ScheduleManager", "Turned off previous DAC");
            }
        }
    }
    
    // Schedule setzen
    currentSchedule_.active = true;
    currentSchedule_.actuatorType = actuatorType;
    currentSchedule_.onAt = onAt;
    currentSchedule_.offAt = offAt;
    currentSchedule_.validDays = validDays;
    currentSchedule_.startTime = time(nullptr);
    currentSchedule_.timezone = timezone;
    paused_ = false;
    
    // Reset trigger tracking
    lastCheckedHour_ = -1;
    lastCheckedMinute_ = -1;
    
    // In Config speichern
    if (!saveToConfig()) {
        if (logger_) {
            logger_->error("ScheduleManager", "Failed to save schedule to config");
        }
        return false;
    }
    
    if (logger_) {
        logger_->info("ScheduleManager", "Schedule set: " + actuatorType + 
                     " ON=" + onAt + " OFF=" + offAt + 
                     " days=" + String(validDays) + 
                     " tz=" + String(timezone));
    }
    
    // Prüfen ob aktuelle Zeit im Schedule-Zeitfenster liegt
    // Wenn ja: Actuator sofort einschalten
    if (isTimeSynced() && actuator_) {
        if (isWithinScheduleTime(onAt, offAt)) {
            // Aktuell im ON-Zeitfenster → Actuator einschalten
            if (actuatorType == "mosfet") {
                actuator_->turnOnMillis(Actuator::Type::MOSFET, 0);
            } else if (actuatorType == "relay") {
                actuator_->turnOnMillis(Actuator::Type::RELAY, 0);
            }
            if (logger_) {
                logger_->info("ScheduleManager", "Current time is within schedule - " + actuatorType + " turned ON");
            }
        } else {
            // Aktuell außerhalb des Zeitfensters → Actuator ausschalten
            if (actuatorType == "mosfet") {
                actuator_->turnOff(Actuator::Type::MOSFET);
            } else if (actuatorType == "relay") {
                actuator_->turnOff(Actuator::Type::RELAY);
            }
            if (logger_) {
                logger_->info("ScheduleManager", "Current time is outside schedule - " + actuatorType + " turned OFF");
            }
        }
    }
    
    return true;
}

bool ScheduleManager::clearSchedule(bool turnOffActuator) {
    if (!initialized_) {
        return false;
    }
    
    if (!currentSchedule_.active) {
        if (logger_) {
            logger_->info("ScheduleManager", "No active schedule to clear");
        }
        return true;
    }
    
    String actuatorType = currentSchedule_.actuatorType;
    
    // Schedule deaktivieren
    currentSchedule_.active = false;
    currentSchedule_.actuatorType = "";
    currentSchedule_.onAt = "";
    currentSchedule_.offAt = "";
    currentSchedule_.validDays = 0;
    currentSchedule_.startTime = 0;
    paused_ = false;
    
    // Aus Config löschen
    if (!saveToConfig()) {
        if (logger_) {
            logger_->error("ScheduleManager", "Failed to clear schedule from config");
        }
        return false;
    }
    
    // Actuator ausschalten wenn gewünscht
    if (turnOffActuator) {
        if (actuatorType == "mosfet" || actuatorType == "relay") {
            if (actuator_) {
                Actuator::Type type = (actuatorType == "mosfet") ? 
                                      Actuator::Type::MOSFET : Actuator::Type::RELAY;
                actuator_->turnOff(type);
                
                if (logger_) {
                    logger_->info("ScheduleManager", "Turned off " + actuatorType);
                }
                
                // Publish status update
                if (actuatorStatusPublisher_) {
                    if (logger_) {
                        logger_->debug("ScheduleManager", "Publishing clear_schedule status for " + actuatorType);
                    }
                    actuatorStatusPublisher_->publishStatus("clear_schedule", "schedule", "", actuatorType);
                } else {
                    if (logger_) {
                        logger_->warning("ScheduleManager", "ActuatorStatusPublisher not available for clear_schedule");
                    }
                }
            }
        } else if (actuatorType.startsWith("dac_") && mcp4725_) {
            // DAC auf 0 setzen
            mcp4725_->setValue(0);
            if (config_) {
                config_->setLastDACValue(0);
            }
            
            if (logger_) {
                logger_->info("ScheduleManager", "Turned off DAC");
            }
            
            // Publish status update
            if (actuatorStatusPublisher_) {
                if (logger_) {
                    logger_->debug("ScheduleManager", "Publishing clear_schedule status for DAC");
                }
                actuatorStatusPublisher_->publishStatus("clear_schedule", "schedule", "", "dac");
            } else {
                if (logger_) {
                    logger_->warning("ScheduleManager", "ActuatorStatusPublisher not available for clear_schedule");
                }
            }
        }
    }
    
    if (logger_) {
        logger_->info("ScheduleManager", "Schedule cleared");
    }
    
    return true;
}

ScheduleManager::Schedule ScheduleManager::getSchedule() const {
    if (logger_) {
        logger_->debug("ScheduleManager", "getSchedule() - actuatorType: '" + currentSchedule_.actuatorType + 
                      "' active: " + String(currentSchedule_.active ? "true" : "false"));
    }
    return currentSchedule_;
}

bool ScheduleManager::isScheduleActive() const {
    return currentSchedule_.active && !paused_;
}

bool ScheduleManager::isExpired() const {
    if (!currentSchedule_.active || currentSchedule_.validDays == 0) {
        return false;
    }
    
    time_t now = time(nullptr);
    time_t elapsed = now - currentSchedule_.startTime;
    uint16_t daysPassed = elapsed / 86400; // Sekunden pro Tag
    
    return daysPassed >= currentSchedule_.validDays;
}

uint16_t ScheduleManager::getDaysRemaining() const {
    if (!currentSchedule_.active || currentSchedule_.validDays == 0) {
        return 0;
    }
    
    time_t now = time(nullptr);
    time_t elapsed = now - currentSchedule_.startTime;
    uint16_t daysPassed = elapsed / 86400;
    
    if (daysPassed >= currentSchedule_.validDays) {
        return 0;
    }
    
    return currentSchedule_.validDays - daysPassed;
}

String ScheduleManager::getNextAction() const {
    if (!currentSchedule_.active) {
        return "";
    }
    
    // Aktuelle Zeit abrufen (mit Timezone)
    time_t now = time(nullptr) + currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    
    // Schedule-Zeiten parsen
    int onHour, onMin, offHour, offMin;
    if (!parseTime(currentSchedule_.onAt, onHour, onMin) ||
        !parseTime(currentSchedule_.offAt, offHour, offMin)) {
        return "";
    }
    
    // Aktuelle Minuten seit Mitternacht
    int currentMinutes = currentHour * 60 + currentMinute;
    int onMinutes = onHour * 60 + onMin;
    int offMinutes = offHour * 60 + offMin;
    
    // Prüfen ob Mitternachts-Überlauf
    if (onMinutes < offMinutes) {
        // Normaler Fall: z.B. 06:00 - 18:00
        if (currentMinutes < onMinutes) {
            return "on";  // Nächste Aktion: einschalten
        } else if (currentMinutes < offMinutes) {
            return "off"; // Nächste Aktion: ausschalten
        } else {
            return "on";  // Nächster Tag
        }
    } else {
        // Mitternachts-Überlauf: z.B. 23:00 - 06:00
        if (currentMinutes < offMinutes) {
            return "off"; // Heute morgen ausschalten
        } else if (currentMinutes < onMinutes) {
            return "on";  // Heute abend einschalten
        } else {
            return "off"; // Morgen früh ausschalten
        }
    }
}

String ScheduleManager::getNextActionTime() const {
    if (!currentSchedule_.active) {
        return "";
    }
    
    String nextAction = getNextAction();
    if (nextAction == "on") {
        return currentSchedule_.onAt;
    } else if (nextAction == "off") {
        return currentSchedule_.offAt;
    }
    
    return "";
}

String ScheduleManager::getStartDate() const {
    if (!currentSchedule_.active || currentSchedule_.startTime == 0) {
        return "";
    }
    
    time_t startTime = currentSchedule_.startTime + currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&startTime, &timeinfo);
    
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    
    return String(buffer);
}

String ScheduleManager::getExpiresOn() const {
    if (!currentSchedule_.active || currentSchedule_.validDays == 0) {
        return "";
    }
    
    time_t expiresTime = currentSchedule_.startTime + 
                        (currentSchedule_.validDays * 86400) + 
                        currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&expiresTime, &timeinfo);
    
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    
    return String(buffer);
}

bool ScheduleManager::setTimezone(int32_t timezone) {
    currentSchedule_.timezone = timezone;
    
    if (config_) {
        config_->setTimezone(timezone);
        if (!config_->save()) {
            if (logger_) {
                logger_->error("ScheduleManager", "Failed to save timezone");
            }
            return false;
        }
    }
    
    if (logger_) {
        logger_->info("ScheduleManager", "Timezone set to: " + String(timezone));
    }
    
    return true;
}

int32_t ScheduleManager::getTimezone() const {
    return currentSchedule_.timezone;
}

void ScheduleManager::syncTime(const char* ntpServer1, const char* ntpServer2) {
    if (logger_) {
        logger_->info("ScheduleManager", "Syncing time with NTP...");
    }
    
    // Timezone aus Config laden (in Sekunden)
    long gmtOffset = 0;
    if (config_) {
        gmtOffset = config_->getTimezone();
        if (logger_) {
            logger_->info("ScheduleManager", "Using timezone offset: " + String(gmtOffset) + " seconds");
        }
    }
    
    // NTP-Server konfigurieren mit Timezone
    configTime(gmtOffset, 0, ntpServer1, ntpServer2);
    
    // Warten auf erste Synchronisation (max 10 Sekunden)
    int attempts = 0;
    while (!isTimeSynced() && attempts < 20) {
        delay(500);
        attempts++;
        if (logger_ && attempts % 4 == 0) {
            logger_->info("ScheduleManager", "Waiting for NTP sync... attempt " + String(attempts));
        }
    }
    
    if (isTimeSynced()) {
        if (logger_) {
            logger_->info("ScheduleManager", "Time synced: " + getLocalTime());
        }
    } else {
        if (logger_) {
            logger_->warning("ScheduleManager", "Time sync failed after 10 seconds");
        }
    }
}

bool ScheduleManager::isTimeSynced() const {
    time_t now = time(nullptr);
    bool synced = now > 1577836800; // > 01.01.2020
    
    // Debug Log
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 30000) { // Alle 30 Sekunden
        Serial.printf("[ScheduleManager] Time sync check: %s (timestamp: %lu)\n", 
                     synced ? "SYNCED" : "NOT SYNCED", (unsigned long)now);
        lastLog = millis();
    }
    
    return synced;
}

String ScheduleManager::getLocalTime() const {
    if (!isTimeSynced()) {
        return "Time not synced";
    }
    
    time_t now = time(nullptr) + currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    return String(buffer);
}

void ScheduleManager::update() {
    if (!initialized_ || !currentSchedule_.active || paused_) {
        return;
    }
    
    // Zeit-Sync prüfen
    if (!isTimeSynced()) {
        // Log nur alle 60 Sekunden um Spam zu vermeiden
        static unsigned long lastWarning = 0;
        if (millis() - lastWarning > 60000) {
            if (logger_) {
                logger_->warning("ScheduleManager", "Time not synced, schedule paused");
            }
            lastWarning = millis();
        }
        return;
    }
    
    // Schedule-Ablauf prüfen
    if (isExpired()) {
        if (logger_) {
            logger_->info("ScheduleManager", "Schedule expired, deactivating");
        }
        clearSchedule(true);
        return;
    }
    
    // DAC-Ramping Logik
    if (isDACSchedule() && isRamping_) {
        float currentValue = calculateRampedValue(rampStartValue_, rampTargetValue_, 
                                                  rampStartTime_, currentSchedule_.rampSeconds);
        applyDACValue(currentValue);
        
        // Prüfen ob Ramping abgeschlossen
        time_t now = time(nullptr);
        if ((now - rampStartTime_) >= (time_t)currentSchedule_.rampSeconds) {
            isRamping_ = false;
            applyDACValue(rampTargetValue_); // Finalen Wert setzen
            if (logger_) {
                logger_->info("ScheduleManager", "DAC ramping completed");
            }
            
            // Publish status update (ramping completed - no ramping params)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", "dac");
            }
        }
        return; // Während Ramping keine Schedule-Trigger mehr prüfen
    }
    
    // Debug: Log aktuelle Zeit alle 10 Sekunden
    static unsigned long lastDebugLog = 0;
    if (millis() - lastDebugLog > 10000) {
        if (logger_) {
            logger_->debug("ScheduleManager", "Update check - Local time: " + getLocalTime() + 
                          " Schedule: " + currentSchedule_.onAt + "-" + currentSchedule_.offAt);
        }
        lastDebugLog = millis();
    }
    
    // Schedule-Trigger prüfen
    String action = checkScheduleTrigger(currentSchedule_.onAt, currentSchedule_.offAt);
    
    if (action.length() > 0) {
        // DAC-Schedules
        if (isDACSchedule() && mcp4725_) {
            if (action == "on") {
                if (currentSchedule_.rampSeconds > 0) {
                    // Ramping starten
                    rampStartTime_ = time(nullptr);
                    rampStartValue_ = currentSchedule_.offValue;
                    rampTargetValue_ = currentSchedule_.onValue;
                    isRamping_ = true;
                    if (logger_) {
                        logger_->info("ScheduleManager", "DAC schedule trigger: ON (ramping " + 
                                     String(currentSchedule_.rampSeconds) + "s)");
                    }
                    // Publish status update (ramping start)
                    if (actuatorStatusPublisher_) {
                        actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", "dac",
                                                               true, rampTargetValue_, currentSchedule_.rampSeconds, 0);
                    }
                } else {
                    // Sofortiges Schalten ohne Ramping
                    applyDACValue(currentSchedule_.onValue);
                    if (config_) {
                        config_->setLastDACValue(mcp4725_->getCurrentValue());
                    }
                    if (logger_) {
                        logger_->info("ScheduleManager", "DAC schedule trigger: ON");
                    }
                    // Publish status update
                    if (actuatorStatusPublisher_) {
                        actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", "dac");
                    }
                }
            } else if (action == "off") {
                if (currentSchedule_.rampSeconds > 0) {
                    // Ramping starten (zurück zu OFF)
                    rampStartTime_ = time(nullptr);
                    rampStartValue_ = currentSchedule_.onValue;
                    rampTargetValue_ = currentSchedule_.offValue;
                    isRamping_ = true;
                    if (logger_) {
                        logger_->info("ScheduleManager", "DAC schedule trigger: OFF (ramping " + 
                                     String(currentSchedule_.rampSeconds) + "s)");
                    }
                    // Publish status update (ramping start)
                    if (actuatorStatusPublisher_) {
                        actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", "dac",
                                                               true, rampTargetValue_, currentSchedule_.rampSeconds, 0);
                    }
                } else {
                    // Sofortiges Schalten ohne Ramping
                    applyDACValue(currentSchedule_.offValue);
                    if (config_) {
                        config_->setLastDACValue(mcp4725_->getCurrentValue());
                    }
                    if (logger_) {
                        logger_->info("ScheduleManager", "DAC schedule trigger: OFF");
                    }
                    // Publish status update
                    if (actuatorStatusPublisher_) {
                        actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", "dac");
                    }
                }
            }
        }
        // MOSFET/Relay-Schedules
        else if (actuator_) {
            Actuator::Type type = (currentSchedule_.actuatorType == "mosfet") ? 
                                  Actuator::Type::MOSFET : Actuator::Type::RELAY;
            
            if (action == "on") {
                actuator_->turnOnMillis(type, 0);
                if (logger_) {
                    logger_->info("ScheduleManager", "Schedule trigger: ON " + 
                                 currentSchedule_.actuatorType);
                }
                // Publish status update
                if (actuatorStatusPublisher_) {
                    actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", currentSchedule_.actuatorType);
                }
            } else if (action == "off") {
                actuator_->turnOff(type);
                if (logger_) {
                    logger_->info("ScheduleManager", "Schedule trigger: OFF " + 
                                 currentSchedule_.actuatorType);
                }
                // Publish status update
                if (actuatorStatusPublisher_) {
                    actuatorStatusPublisher_->publishStatus("schedule", "schedule", "", currentSchedule_.actuatorType);
                }
            }
        }
    }
}

void ScheduleManager::pauseSchedule() {
    paused_ = true;
    if (logger_) {
        logger_->info("ScheduleManager", "Schedule paused");
    }
}

void ScheduleManager::resumeSchedule() {
    paused_ = false;
    // Reset trigger tracking beim Fortsetzen
    lastCheckedHour_ = -1;
    lastCheckedMinute_ = -1;
    
    if (logger_) {
        logger_->info("ScheduleManager", "Schedule resumed");
    }
}

bool ScheduleManager::isPaused() const {
    return paused_;
}

bool ScheduleManager::parseTime(const String& timeStr, int& hour, int& minute) const {
    if (timeStr.length() != 5 || timeStr.charAt(2) != ':') {
        return false;
    }
    
    hour = timeStr.substring(0, 2).toInt();
    minute = timeStr.substring(3, 5).toInt();
    
    return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

bool ScheduleManager::validateTimeFormat(const String& timeStr) const {
    if (timeStr.length() != 5) {
        return false;
    }
    
    if (timeStr.charAt(2) != ':') {
        return false;
    }
    
    String hourStr = timeStr.substring(0, 2);
    String minStr = timeStr.substring(3, 5);
    
    // Prüfen ob nur Ziffern
    for (int i = 0; i < 2; i++) {
        if (!isdigit(hourStr.charAt(i)) || !isdigit(minStr.charAt(i))) {
            return false;
        }
    }
    
    int hour = hourStr.toInt();
    int minute = minStr.toInt();
    
    return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59);
}

bool ScheduleManager::validateActuatorType(const String& type) const {
    return (type == "mosfet" || type == "relay");
}

String ScheduleManager::subtractSecondsFromTime(const String& timeStr, uint32_t seconds) {
    // Parse HH:MM
    int hour, minute;
    if (!parseTime(timeStr, hour, minute)) {
        return timeStr;
    }
    
    // Konvertiere zu Minuten seit Mitternacht
    int totalMinutes = hour * 60 + minute;
    
    // Subtrahiere Sekunden (konvertiert zu Minuten)
    int subtractMinutes = seconds / 60;
    totalMinutes -= subtractMinutes;
    
    // Handle Mitternachts-Überlauf (negative Werte)
    if (totalMinutes < 0) {
        totalMinutes += 24 * 60; // Füge einen Tag hinzu
    }
    
    // Zurück zu HH:MM
    int newHour = totalMinutes / 60;
    int newMinute = totalMinutes % 60;
    
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", newHour, newMinute);
    
    return String(buffer);
}

String ScheduleManager::checkScheduleTrigger(const String& onAt, const String& offAt) {
    // Aktuelle Zeit abrufen (mit Timezone)
    time_t now = time(nullptr) + currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    
    // Debug Log
    static int lastLoggedMinute = -1;
    if (currentMinute != lastLoggedMinute) {
        Serial.printf("[ScheduleManager] Current time: %02d:%02d (checking against %s-%s)\n",
                     currentHour, currentMinute, onAt.c_str(), offAt.c_str());
        lastLoggedMinute = currentMinute;
    }
    
    // Doppel-Trigger verhindern
    if (currentHour == lastCheckedHour_ && currentMinute == lastCheckedMinute_) {
        return ""; // Bereits in dieser Minute geprüft
    }
    
    // Schedule-Zeiten parsen
    int onHour, onMin, offHour, offMin;
    if (!parseTime(onAt, onHour, onMin) || !parseTime(offAt, offHour, offMin)) {
        return "";
    }
    
    // Trigger prüfen (mit Pre-Ramping nur für OFF)
    String action = "";
    
    // Berechne OFF Ramping-Start-Zeit wenn rampSeconds > 0
    int rampOffHour = offHour, rampOffMin = offMin;
    
    if (isDACSchedule() && currentSchedule_.rampSeconds > 0) {
        // Pre-Ramping nur für OFF: Ramping startet vor der off_at Zeit
        String rampOffTime = subtractSecondsFromTime(offAt, currentSchedule_.rampSeconds);
        parseTime(rampOffTime, rampOffHour, rampOffMin);
    }
    
    // Prüfe Trigger
    if (isDACSchedule() && currentSchedule_.rampSeconds > 0) {
        // ON: Normaler Trigger (kein Pre-Ramping)
        if (currentHour == onHour && currentMinute == onMin) {
            action = "on";
            Serial.printf("[ScheduleManager] TRIGGER MATCH: ON at %02d:%02d (ramping %us)\n", 
                         currentHour, currentMinute, currentSchedule_.rampSeconds);
        }
        // OFF: Pre-Ramping Trigger
        else if (currentHour == rampOffHour && currentMinute == rampOffMin) {
            action = "off";
            Serial.printf("[ScheduleManager] TRIGGER MATCH: OFF ramping start at %02d:%02d (target: %s)\n", 
                         currentHour, currentMinute, offAt.c_str());
        }
    } else {
        // Normaler Trigger (kein Ramping oder nicht DAC)
        if (currentHour == onHour && currentMinute == onMin) {
            action = "on";
            Serial.printf("[ScheduleManager] TRIGGER MATCH: ON at %02d:%02d\n", currentHour, currentMinute);
        } else if (currentHour == offHour && currentMinute == offMin) {
            action = "off";
            Serial.printf("[ScheduleManager] TRIGGER MATCH: OFF at %02d:%02d\n", currentHour, currentMinute);
        }
    }
    
    // Tracking aktualisieren
    lastCheckedHour_ = currentHour;
    lastCheckedMinute_ = currentMinute;
    
    return action;
}

bool ScheduleManager::isWithinScheduleTime(const String& onAt, const String& offAt) const {
    if (!isTimeSynced()) {
        return false;
    }
    
    // Aktuelle Zeit abrufen
    time_t now = time(nullptr) + currentSchedule_.timezone;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentMinutes = currentHour * 60 + currentMinute;
    
    // Schedule-Zeiten parsen
    int onHour, onMin, offHour, offMin;
    if (!parseTime(onAt, onHour, onMin) || !parseTime(offAt, offHour, offMin)) {
        return false;
    }
    
    int onMinutes = onHour * 60 + onMin;
    int offMinutes = offHour * 60 + offMin;
    
    // Prüfen ob Mitternachts-Überlauf
    if (onMinutes < offMinutes) {
        // Normaler Fall: z.B. 06:00 - 18:00
        return (currentMinutes >= onMinutes && currentMinutes < offMinutes);
    } else {
        // Mitternachts-Überlauf: z.B. 23:00 - 06:00
        return (currentMinutes >= onMinutes || currentMinutes < offMinutes);
    }
}

bool ScheduleManager::loadFromConfig() {
    if (!config_) {
        return false;
    }
    
    currentSchedule_.active = config_->getScheduleActive();
    
    if (!currentSchedule_.active) {
        return false;
    }
    
    currentSchedule_.actuatorType = config_->getScheduleActuatorType();
    currentSchedule_.onAt = config_->getScheduleOnAt();
    currentSchedule_.offAt = config_->getScheduleOffAt();
    currentSchedule_.validDays = config_->getScheduleValidDays();
    currentSchedule_.startTime = config_->getScheduleStartTime();
    currentSchedule_.timezone = config_->getTimezone();
    
    // DAC-Parameter laden falls es ein DAC-Schedule ist
    if (currentSchedule_.actuatorType == "dac_voltage" ||
        currentSchedule_.actuatorType == "dac_percent" ||
        currentSchedule_.actuatorType == "dac_value") {
        currentSchedule_.onValue = config_->getScheduleOnValue();
        currentSchedule_.offValue = config_->getScheduleOffValue();
        currentSchedule_.rampSeconds = config_->getScheduleRampSeconds();
    }
    
    return true;
}

bool ScheduleManager::saveToConfig() {
    if (!config_) {
        return false;
    }
    
    config_->setScheduleActive(currentSchedule_.active);
    config_->setScheduleActuatorType(currentSchedule_.actuatorType);
    config_->setScheduleOnAt(currentSchedule_.onAt);
    config_->setScheduleOffAt(currentSchedule_.offAt);
    config_->setScheduleValidDays(currentSchedule_.validDays);
    config_->setScheduleStartTime(currentSchedule_.startTime);
    config_->setTimezone(currentSchedule_.timezone);
    
    // DAC-spezifische Parameter speichern
    if (isDACSchedule()) {
        config_->setScheduleOnValue(currentSchedule_.onValue);
        config_->setScheduleOffValue(currentSchedule_.offValue);
        config_->setScheduleRampSeconds(currentSchedule_.rampSeconds);
    }
    
    return config_->save();
}

// DAC Schedule Methoden
bool ScheduleManager::isDACSchedule() const {
    return (currentSchedule_.actuatorType == "dac_voltage" ||
            currentSchedule_.actuatorType == "dac_percent" ||
            currentSchedule_.actuatorType == "dac_value");
}

bool ScheduleManager::setDACSchedule(const String& actuatorType, const String& onAt, const String& offAt,
                                     float onValue, float offValue,
                                     uint32_t rampSeconds, uint16_t validDays, int32_t timezone) {
    if (!initialized_ || !mcp4725_) {
        if (logger_) {
            logger_->error("ScheduleManager", "MCP4725 not available for DAC schedule");
        }
        return false;
    }
    
    // Validierung
    if (actuatorType != "dac_voltage" && actuatorType != "dac_percent" && actuatorType != "dac_value") {
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid actuatorType: " + actuatorType);
        }
        return false;
    }
    
    if (!validateTimeFormat(onAt) || !validateTimeFormat(offAt)) {
        return false;
    }
    
    if (onAt == offAt) {
        if (logger_) {
            logger_->error("ScheduleManager", "on_at and off_at must be different");
        }
        return false;
    }
    
    // Prüfe ob Schedule bereits existiert und was sich geändert hat
    bool scheduleExists = currentSchedule_.active && currentSchedule_.actuatorType.startsWith("dac_");
    bool scheduleIdentical = false;
    bool onlyValuesChanged = false;
    bool timesChanged = false;
    
    if (scheduleExists) {
        // Prüfe ob komplett identisch
        scheduleIdentical = (currentSchedule_.actuatorType == actuatorType &&
                            currentSchedule_.onAt == onAt &&
                            currentSchedule_.offAt == offAt &&
                            currentSchedule_.onValue == onValue &&
                            currentSchedule_.offValue == offValue &&
                            currentSchedule_.rampSeconds == rampSeconds &&
                            currentSchedule_.validDays == validDays &&
                            currentSchedule_.timezone == timezone);
        
        // Prüfe ob nur Werte geändert
        if (!scheduleIdentical) {
            onlyValuesChanged = (currentSchedule_.actuatorType == actuatorType &&
                                currentSchedule_.onAt == onAt &&
                                currentSchedule_.offAt == offAt &&
                                (currentSchedule_.onValue != onValue || currentSchedule_.offValue != offValue));
            
            // Prüfe ob Zeiten geändert
            timesChanged = (currentSchedule_.onAt != onAt || currentSchedule_.offAt != offAt);
        }
    }
    
    // Wenn sich der actuator_type ändert, schalte den vorherigen aus
    if (currentSchedule_.active && !currentSchedule_.actuatorType.startsWith("dac_")) {
        String previousType = currentSchedule_.actuatorType;
        
        if (previousType == "mosfet" || previousType == "relay") {
            if (actuator_) {
                Actuator::Type type = (previousType == "mosfet") ? 
                                      Actuator::Type::MOSFET : Actuator::Type::RELAY;
                actuator_->turnOff(type);
                if (logger_) {
                    logger_->info("ScheduleManager", "Turned off previous actuator: " + previousType);
                }
            }
        }
    }
    
    // Schedule setzen
    currentSchedule_.active = true;
    currentSchedule_.actuatorType = actuatorType;
    currentSchedule_.onAt = onAt;
    currentSchedule_.offAt = offAt;
    currentSchedule_.validDays = validDays;
    currentSchedule_.startTime = time(nullptr);
    currentSchedule_.timezone = timezone;
    currentSchedule_.onValue = onValue;
    currentSchedule_.offValue = offValue;
    currentSchedule_.rampSeconds = rampSeconds;
    paused_ = false;
    isRamping_ = false;
    
    // Reset trigger tracking
    lastCheckedHour_ = -1;
    lastCheckedMinute_ = -1;
    
    // In Config speichern
    if (!saveToConfig()) {
        if (logger_) {
            logger_->error("ScheduleManager", "Failed to save DAC schedule");
        }
        return false;
    }
    
    if (logger_) {
        String unit = (actuatorType == "dac_voltage") ? "V" : 
                     (actuatorType == "dac_percent") ? "%" : "";
        logger_->info("ScheduleManager", "DAC schedule set: " + actuatorType + " " +
                     String(onValue, 2) + unit + "-" + String(offValue, 2) + unit + ", " +
                     onAt + "-" + offAt + ", ramp=" + String(rampSeconds) + "s");
    }
    
    // DAC nur ändern bei Re-Configuration wenn nötig
    // Komplett identisch: Nichts tun
    if (scheduleIdentical) {
        if (logger_) {
            logger_->info("ScheduleManager", "Schedule identical - no DAC change needed");
        }
        return true;
    }
    
    // Nur Werte geändert oder Zeiten geändert: Direktes Setzen (KEIN Ramping bei Re-Config)
    if (isTimeSynced() && mcp4725_ && (onlyValuesChanged || timesChanged)) {
        if (isWithinScheduleTime(onAt, offAt)) {
            // Im ON-Zeitfenster → onValue direkt setzen
            applyDACValue(onValue);
            if (logger_) {
                logger_->info("ScheduleManager", "Schedule updated - DAC set to ON value (no ramping)");
            }
            // Publish status update (schedule re-configuration)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "dac");
            }
        } else {
            // Im OFF-Zeitfenster → offValue direkt setzen
            applyDACValue(offValue);
            if (logger_) {
                logger_->info("ScheduleManager", "Schedule updated - DAC set to OFF value (no ramping)");
            }
            // Publish status update (schedule re-configuration)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "dac");
            }
        }
    } else if (!scheduleExists && isTimeSynced() && mcp4725_) {
        // Neuer Schedule: Initiales Setzen (kein Ramping)
        if (isWithinScheduleTime(onAt, offAt)) {
            applyDACValue(onValue);
            if (logger_) {
                logger_->info("ScheduleManager", "New schedule - DAC set to ON value");
            }
            // Publish status update (new schedule)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "dac");
            }
        } else {
            applyDACValue(offValue);
            if (logger_) {
                logger_->info("ScheduleManager", "New schedule - DAC set to OFF value");
            }
            // Publish status update (new schedule)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "dac");
            }
        }
    }
    
    return true;
}

float ScheduleManager::calculateRampedValue(float startValue, float targetValue, 
                                            time_t rampStartTime, uint32_t rampDuration) const {
    if (rampDuration == 0) {
        return targetValue; // Kein Ramping
    }
    
    time_t now = time(nullptr);
    time_t elapsed = now - rampStartTime;
    
    if (elapsed >= (time_t)rampDuration) {
        return targetValue; // Rampe beendet
    }
    
    // Linear interpolation
    float progress = (float)elapsed / (float)rampDuration;
    return startValue + (targetValue - startValue) * progress;
}

void ScheduleManager::applyDACValue(float value) const {
    if (!mcp4725_) {
        return;
    }
    
    if (currentSchedule_.actuatorType == "dac_voltage") {
        mcp4725_->setVoltage(value);
    } else if (currentSchedule_.actuatorType == "dac_percent") {
        mcp4725_->setPercent(value);
    } else if (currentSchedule_.actuatorType == "dac_value") {
        mcp4725_->setValue((uint16_t)value);
    }
    
    // Letzten Wert für Power-Loss Recovery speichern
    if (config_) {
        config_->setLastDACValue(mcp4725_->getCurrentValue());
    }
}

// PWM Schedule Methoden
bool ScheduleManager::isPWMSchedule() const {
    return (currentSchedule_.actuatorType == "pwm_io2_value" ||
            currentSchedule_.actuatorType == "pwm_io2_percent" ||
            currentSchedule_.actuatorType == "pwm_mosfet_value" ||
            currentSchedule_.actuatorType == "pwm_mosfet_percent" ||
            // Legacy support
            currentSchedule_.actuatorType == "pwm_value" ||
            currentSchedule_.actuatorType == "pwm_percent");
}

void ScheduleManager::applyPWMValue(float value) const {
    // Determine which controller to use based on actuatorType
    PWMController* controller = nullptr;
    bool isValueMode = false;
    
    if (currentSchedule_.actuatorType == "pwm_mosfet_value" ||
        currentSchedule_.actuatorType == "pwm_mosfet_percent") {
        controller = pwmControllerMOSFET_;
        isValueMode = (currentSchedule_.actuatorType == "pwm_mosfet_value");
    } else if (currentSchedule_.actuatorType == "pwm_io2_value" ||
               currentSchedule_.actuatorType == "pwm_io2_percent") {
        controller = pwmController_;
        isValueMode = (currentSchedule_.actuatorType == "pwm_io2_value");
    } else if (currentSchedule_.actuatorType == "pwm_value" ||
               currentSchedule_.actuatorType == "pwm_percent") {
        // Legacy: default to IO2
        controller = pwmController_;
        isValueMode = (currentSchedule_.actuatorType == "pwm_value");
    }
    
    if (!controller) {
        return;
    }
    
    // Apply value based on mode
    if (isValueMode) {
        controller->setValue((uint16_t)value);
    } else {
        controller->setPercent(value);
    }
}

bool ScheduleManager::setPWMSchedule(const String& actuatorType, const String& onAt, const String& offAt,
                                     float onValue, float offValue,
                                     uint32_t rampSeconds, uint16_t validDays, int32_t timezone) {
    // Check if appropriate controller is available
    PWMController* controller = nullptr;
    
    if (actuatorType == "pwm_mosfet_value" || actuatorType == "pwm_mosfet_percent") {
        controller = pwmControllerMOSFET_;
    } else if (actuatorType == "pwm_io2_value" || actuatorType == "pwm_io2_percent") {
        controller = pwmController_;
    } else if (actuatorType == "pwm_value" || actuatorType == "pwm_percent") {
        // Legacy: default to IO2
        controller = pwmController_;
    }
    
    if (!initialized_ || !controller) {
        if (logger_) {
            logger_->error("ScheduleManager", "PWM Controller not available for PWM schedule: " + actuatorType);
        }
        return false;
    }
    
    // Validierung
    if (actuatorType != "pwm_io2_value" && actuatorType != "pwm_io2_percent" &&
        actuatorType != "pwm_mosfet_value" && actuatorType != "pwm_mosfet_percent" &&
        actuatorType != "pwm_value" && actuatorType != "pwm_percent") {  // Legacy support
        if (logger_) {
            logger_->error("ScheduleManager", "Invalid actuatorType: " + actuatorType);
        }
        return false;
    }
    
    if (!validateTimeFormat(onAt) || !validateTimeFormat(offAt)) {
        return false;
    }
    
    if (onAt == offAt) {
        if (logger_) {
            logger_->error("ScheduleManager", "on_at and off_at must be different");
        }
        return false;
    }
    
    // Prüfe ob Schedule bereits existiert und was sich geändert hat
    bool scheduleExists = currentSchedule_.active && currentSchedule_.actuatorType.startsWith("pwm_");
    bool scheduleIdentical = false;
    bool onlyValuesChanged = false;
    bool timesChanged = false;
    
    if (scheduleExists) {
        // Prüfe ob komplett identisch
        scheduleIdentical = (currentSchedule_.actuatorType == actuatorType &&
                            currentSchedule_.onAt == onAt &&
                            currentSchedule_.offAt == offAt &&
                            currentSchedule_.onValue == onValue &&
                            currentSchedule_.offValue == offValue &&
                            currentSchedule_.rampSeconds == rampSeconds &&
                            currentSchedule_.validDays == validDays &&
                            currentSchedule_.timezone == timezone);
        
        // Prüfe ob nur Werte geändert
        if (!scheduleIdentical) {
            onlyValuesChanged = (currentSchedule_.actuatorType == actuatorType &&
                                currentSchedule_.onAt == onAt &&
                                currentSchedule_.offAt == offAt &&
                                (currentSchedule_.onValue != onValue || currentSchedule_.offValue != offValue));
            
            // Prüfe ob Zeiten geändert
            timesChanged = (currentSchedule_.onAt != onAt || currentSchedule_.offAt != offAt);
        }
    }
    
    // Wenn sich der actuator_type ändert, schalte den vorherigen aus
    if (currentSchedule_.active && !currentSchedule_.actuatorType.startsWith("pwm_")) {
        String previousType = currentSchedule_.actuatorType;
        
        if (previousType == "mosfet" || previousType == "relay") {
            if (actuator_) {
                Actuator::Type type = (previousType == "mosfet") ? 
                                      Actuator::Type::MOSFET : Actuator::Type::RELAY;
                actuator_->turnOff(type);
                if (logger_) {
                    logger_->info("ScheduleManager", "Turned off previous actuator: " + previousType);
                }
            }
        } else if (previousType.startsWith("dac_") && mcp4725_) {
            mcp4725_->setValue(0);
            if (logger_) {
                logger_->info("ScheduleManager", "Turned off previous DAC");
            }
        }
    }
    
    // Schedule setzen
    currentSchedule_.active = true;
    currentSchedule_.actuatorType = actuatorType;
    currentSchedule_.onAt = onAt;
    currentSchedule_.offAt = offAt;
    currentSchedule_.validDays = validDays;
    currentSchedule_.startTime = time(nullptr);
    currentSchedule_.timezone = timezone;
    currentSchedule_.onValue = onValue;
    currentSchedule_.offValue = offValue;
    currentSchedule_.rampSeconds = rampSeconds;
    paused_ = false;
    isRamping_ = false;
    
    // Reset trigger tracking
    lastCheckedHour_ = -1;
    lastCheckedMinute_ = -1;
    
    // In Config speichern
    if (!saveToConfig()) {
        if (logger_) {
            logger_->error("ScheduleManager", "Failed to save PWM schedule");
        }
        return false;
    }
    
    if (logger_) {
        String unit = (actuatorType == "pwm_percent") ? "%" : "";
        logger_->info("ScheduleManager", "PWM schedule set: " + actuatorType + " " +
                     String(onValue, 1) + unit + "-" + String(offValue, 1) + unit + ", " +
                     onAt + "-" + offAt + ", ramp=" + String(rampSeconds) + "s");
    }
    
    // PWM nur ändern bei Re-Configuration wenn nötig
    // Komplett identisch: Nichts tun
    if (scheduleIdentical) {
        if (logger_) {
            logger_->info("ScheduleManager", "Schedule identical - no PWM change needed");
        }
        return true;
    }
    
    // Nur Werte geändert oder Zeiten geändert: Direktes Setzen (KEIN Ramping bei Re-Config)
    if (isTimeSynced() && pwmController_ && (onlyValuesChanged || timesChanged)) {
        if (isWithinScheduleTime(onAt, offAt)) {
            // Im ON-Zeitfenster → onValue direkt setzen
            applyPWMValue(onValue);
            if (logger_) {
                logger_->info("ScheduleManager", "Re-configured schedule - PWM set to ON value");
            }
            // Publish status update
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "pwm");
            }
        } else {
            // Außerhalb ON-Zeitfenster → offValue direkt setzen
            applyPWMValue(offValue);
            if (logger_) {
                logger_->info("ScheduleManager", "Re-configured schedule - PWM set to OFF value");
            }
            // Publish status update
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "pwm");
            }
        }
    } else if (!scheduleExists && isTimeSynced() && pwmController_) {
        // Neuer Schedule: Initiales Setzen (kein Ramping)
        if (isWithinScheduleTime(onAt, offAt)) {
            applyPWMValue(onValue);
            if (logger_) {
                logger_->info("ScheduleManager", "New schedule - PWM set to ON value");
            }
            // Publish status update (new schedule)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "pwm");
            }
        } else {
            applyPWMValue(offValue);
            if (logger_) {
                logger_->info("ScheduleManager", "New schedule - PWM set to OFF value");
            }
            // Publish status update (new schedule)
            if (actuatorStatusPublisher_) {
                actuatorStatusPublisher_->publishStatus("schedule_update", "schedule", "", "pwm");
            }
        }
    }
    
    return true;
}

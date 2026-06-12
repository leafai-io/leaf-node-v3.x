#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <time.h>

// Forward declarations
class Logger;
class RuntimeConfig;
class Actuator;
class MCP4725;
class PWMController;
class ActuatorStatusPublisher;

/**
 * @brief ScheduleManager - Zeitgesteuerte Actuator-Steuerung
 * 
 * Features:
 * - Ein Schedule für einen Actuator (MOSFET oder Relay)
 * - NTP-Synchronisation
 * - Timezone-Handling (Sekunden-Offset von UTC)
 * - Mitternachts-Überlauf Support (23:00 - 06:00)
 * - Automatische Deaktivierung nach valid_days
 * - Exklusiv mit Timer (nur einer kann aktiv sein)
 */
class ScheduleManager {
public:
    /**
     * @brief Schedule-Konfiguration
     */
    struct Schedule {
        bool active;              // Schedule aktiv?
        String actuatorType;      // "mosfet", "relay", "dac_voltage", "dac_percent", "dac_value", "pwm_io2_percent", "pwm_io2_value", "pwm_mosfet_percent", "pwm_mosfet_value"
        String onAt;              // "HH:MM" Format
        String offAt;             // "HH:MM" Format
        uint16_t validDays;       // 0 = unbegrenzt, >0 = Tage gültig
        time_t startTime;         // Unix timestamp des Schedule-Starts
        int32_t timezone;         // Sekunden-Offset von UTC
        
        // DAC-spezifische Parameter (Interpretation abhängig von actuatorType)
        float onValue;            // Voltage (0-10V), Percent (0-100%), oder Value (0-4095)
        float offValue;           // Voltage (0-10V), Percent (0-100%), oder Value (0-4095)
        uint32_t rampSeconds;     // Ramping-Dauer (0 = kein Ramping)
        
        Schedule() 
            : active(false)
            , actuatorType("")
            , onAt("")
            , offAt("")
            , validDays(0)
            , startTime(0)
            , timezone(0)
            , onValue(0.0f)
            , offValue(0.0f)
            , rampSeconds(0) {}
    };
    
    ScheduleManager();
    ~ScheduleManager();
    
    /**
     * @brief Initialisierung
     */
    bool initialize(Logger* logger, RuntimeConfig* config, Actuator* actuator, MCP4725* mcp4725 = nullptr, PWMController* pwmController = nullptr, PWMController* pwmControllerMOSFET = nullptr, ActuatorStatusPublisher* statusPublisher = nullptr);
    
    /**
     * @brief Schedule setzen
     * @param actuatorType "mosfet", "relay", "dac_voltage", "dac_percent", "dac_value", "pwm_io2_percent", "pwm_io2_value", "pwm_mosfet_percent", "pwm_mosfet_value"
     * @param onAt Einschaltzeit "HH:MM"
     * @param offAt Ausschaltzeit "HH:MM"
     * @param validDays Gültigkeitsdauer (0 = unbegrenzt)
     * @param timezone Timezone-Offset in Sekunden
     * @return true wenn erfolgreich
     */
    bool setSchedule(const String& actuatorType, const String& onAt, 
                     const String& offAt, uint16_t validDays, int32_t timezone);
    
    /**
     * @brief DAC Schedule setzen (alle Modi)
     * @param actuatorType "dac_voltage", "dac_percent", oder "dac_value"
     * @param onValue Wert wenn ON (Interpretation abhängig von actuatorType)
     * @param offValue Wert wenn OFF (Interpretation abhängig von actuatorType)
     */
    bool setDACSchedule(const String& actuatorType, const String& onAt, const String& offAt, 
                        float onValue, float offValue,
                        uint32_t rampSeconds, uint16_t validDays, int32_t timezone);
    
    /**
     * @brief PWM Schedule setzen (alle Modi)
     * @param actuatorType "pwm_io2_percent", "pwm_io2_value", "pwm_mosfet_percent", "pwm_mosfet_value"
     * @param onValue Wert wenn ON (0-1023 für value, 0-100 für percent)
     * @param offValue Wert wenn OFF
     */
    bool setPWMSchedule(const String& actuatorType, const String& onAt, const String& offAt, 
                        float onValue, float offValue,
                        uint32_t rampSeconds, uint16_t validDays, int32_t timezone);
    
    /**
     * @brief Schedule löschen
     * @param turnOffActuator Soll der Actuator auch ausgeschaltet werden?
     * @return true wenn erfolgreich
     */
    bool clearSchedule(bool turnOffActuator = true);
    
    /**
     * @brief Aktuellen Schedule abrufen
     */
    Schedule getSchedule() const;
    
    /**
     * @brief Ist ein Schedule aktiv?
     */
    bool isScheduleActive() const;
    
    /**
     * @brief Ist der Schedule abgelaufen (valid_days)?
     */
    bool isExpired() const;
    
    /**
     * @brief Verbleibende Tage bis zum Ablauf
     * @return Tage (0 wenn abgelaufen oder unbegrenzt)
     */
    uint16_t getDaysRemaining() const;
    
    /**
     * @brief Nächste Schedule-Aktion
     * @return "on" oder "off" oder "" wenn kein Schedule
     */
    String getNextAction() const;
    
    /**
     * @brief Zeitpunkt der nächsten Aktion
     * @return "HH:MM" oder "" wenn kein Schedule
     */
    String getNextActionTime() const;
    
    /**
     * @brief Start-Datum des Schedules (ISO 8601)
     * @return "YYYY-MM-DD" oder ""
     */
    String getStartDate() const;
    
    /**
     * @brief Ablauf-Datum des Schedules (ISO 8601)
     * @return "YYYY-MM-DD" oder "" wenn unbegrenzt
     */
    String getExpiresOn() const;
    
    /**
     * @brief Timezone setzen (global)
     */
    bool setTimezone(int32_t timezone);
    
    /**
     * @brief Aktuelle Timezone abrufen
     */
    int32_t getTimezone() const;
    
    /**
     * @brief NTP-Zeit synchronisieren
     * @param ntpServer1 Primärer NTP-Server
     * @param ntpServer2 Sekundärer NTP-Server (optional)
     */
    void syncTime(const char* ntpServer1 = "pool.ntp.org", 
                  const char* ntpServer2 = "time.nist.gov");
    
    /**
     * @brief Ist die Zeit synchronisiert?
     */
    bool isTimeSynced() const;
    
    /**
     * @brief Aktuelle lokale Zeit (mit Timezone)
     * @return ISO 8601 Format "YYYY-MM-DDTHH:MM:SS" oder "Time not synced"
     */
    String getLocalTime() const;
    
    /**
     * @brief Update-Loop - muss zyklisch aufgerufen werden
     * Prüft Schedule-Trigger und führt Aktionen aus
     */
    void update();
    
    /**
     * @brief Schedule pausieren (z.B. wenn Timer gestartet wird)
     */
    void pauseSchedule();
    
    /**
     * @brief Schedule fortsetzen
     */
    void resumeSchedule();
    
    /**
     * @brief Ist Schedule pausiert?
     */
    bool isPaused() const;

private:
    Logger* logger_;
    RuntimeConfig* config_;
    Actuator* actuator_;
    MCP4725* mcp4725_;
    PWMController* pwmController_;
    PWMController* pwmControllerMOSFET_;
    ActuatorStatusPublisher* actuatorStatusPublisher_;
    
    Schedule currentSchedule_;
    bool initialized_;
    bool paused_;
    
    // Zeit-Tracking für Trigger-Logik
    int lastCheckedHour_;
    int lastCheckedMinute_;
    
    // Ramping State
    time_t rampStartTime_;
    float rampStartValue_;
    float rampTargetValue_;
    bool isRamping_;
    
    /**
     * @brief Berechne aktuellen Ramping-Wert (Linear Interpolation)
     * @return Aktueller Wert zwischen start und target
     */
    float calculateRampedValue(float startValue, float targetValue, 
                               time_t rampStartTime, uint32_t rampDuration) const;
    
    /**
     * @brief DAC auf berechneten Wert setzen (je nach Modus)
     */
    void applyDACValue(float value) const;
    
    /**
     * @brief PWM auf berechneten Wert setzen (je nach Modus)
     */
    void applyPWMValue(float value) const;
    
    /**
     * @brief Ist Schedule-Typ ein DAC-Modus?
     */
    bool isDACSchedule() const;
    
    /**
     * @brief Ist Schedule-Typ ein PWM-Modus?
     */
    bool isPWMSchedule() const;
    
    /**
     * @brief Zeit-String parsen und validieren
     * @param timeStr "HH:MM" Format
     * @param hour Output: Stunden (0-23)
     * @param minute Output: Minuten (0-59)
     * @return true wenn gültig
     */
    bool parseTime(const String& timeStr, int& hour, int& minute) const;
    
    /**
     * @brief Zeit-String validieren
     */
    bool validateTimeFormat(const String& timeStr) const;
    
    /**
     * @brief Actuator-Typ validieren
     */
    bool validateActuatorType(const String& type) const;
    
    /**
     * @brief Prüfen ob jetzt Schedule-Zeit ist
     * @param onAt Einschaltzeit "HH:MM"
     * @param offAt Ausschaltzeit "HH:MM"
     * @return "on", "off" oder "" (keine Aktion)
     */
    String checkScheduleTrigger(const String& onAt, const String& offAt);
    
    /**
     * @brief Subtrahiere Sekunden von einer Zeit (HH:MM)
     * @param timeStr Zeit im Format "HH:MM"
     * @param seconds Sekunden die subtrahiert werden sollen
     * @return Neue Zeit im Format "HH:MM" (mit Mitternachts-Überlauf)
     */
    String subtractSecondsFromTime(const String& timeStr, uint32_t seconds);
    
    /**
     * @brief Ist aktuelle Zeit innerhalb des Schedule-Zeitraums?
     * Berücksichtigt Mitternachts-Überlauf
     */
    bool isWithinScheduleTime(const String& onAt, const String& offAt) const;
    
    /**
     * @brief Schedule aus Config laden
     */
    bool loadFromConfig();
    
    /**
     * @brief Schedule in Config speichern
     */
    bool saveToConfig();
};

#endif // SCHEDULE_MANAGER_H

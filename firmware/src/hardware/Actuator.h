#pragma once

#include <Arduino.h>

// Forward declaration
class SystemManager;

/**
 * @brief Actuator control class for MOSFET and Relay
 * 
 * Controls two types of actuators: MOSFET (GPIO 36) and Relay (GPIO 38)
 * Provides ON/OFF/Toggle control with optional timer functionality.
 */
class Actuator {
public:
    /**
     * @brief Actuator type enumeration
     */
    enum class Type {
        MOSFET,
        RELAY
    };

    /**
     * @brief Convert string to Type enum
     */
    static Type stringToType(const String& typeStr);
    
    /**
     * @brief Convert Type enum to string
     */
    static String typeToString(Type type);

    Actuator();
    ~Actuator();

    /**
     * @brief Initialize both actuators
     * @param mosfetPin GPIO pin for MOSFET (default: 36)
     * @param relayPin GPIO pin for Relay (default: 38)
     * @return true if initialization was successful
     */
    bool initialize(uint8_t mosfetPin = 36, uint8_t relayPin = 38);

    /**
     * @brief Turn the actuator ON with millisecond precision
     * @param type Actuator type (MOSFET or RELAY)
     * @param durationMillis Optional duration in milliseconds (0 = no timer)
     * @return true if successful
     * @note Hardware timer provides ±100µs precision for all durations
     */
    bool turnOnMillis(Type type, uint32_t durationMillis = 0);
    
    /**
     * @brief Turn the actuator OFF
     * @param type Actuator type (MOSFET or RELAY)
     * @return true if successful
     */
    bool turnOff(Type type);

    /**
     * @brief Toggle the actuator state with millisecond precision
     * @param type Actuator type (MOSFET or RELAY)
     * @param durationMillis Optional duration in milliseconds (0 = no timer)
     * @return true if successful
     * @note Hardware timer provides ±100µs precision
     */
    bool toggleMillis(Type type, uint32_t durationMillis = 0);
    
    /**
     * @brief Cancel active timer for the actuator
     * @param type Actuator type (MOSFET or RELAY)
     * @return true if timer was active and cancelled
     */
    bool cancelTimer(Type type);

    /**
     * @brief Update function - must be called regularly to handle timers
     * Automatically turns off actuator when timer expires
     */
    void update();

    /**
     * @brief Get current actuator state
     * @param type Actuator type (MOSFET or RELAY)
     * @return true if ON, false if OFF
     */
    bool getState(Type type) const;

    /**
     * @brief Check if timer is active for actuator
     * @param type Actuator type (MOSFET or RELAY)
     * @return true if timer is active
     */
    bool isTimerActive(Type type) const;

    /**
     * @brief Get remaining timer milliseconds
     * @param type Actuator type (MOSFET or RELAY)
     * @return remaining milliseconds, 0 if no timer active
     */
    uint32_t getRemainingMillis(Type type) const;


    /**
     * @brief Check if actuator is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Set the system manager for LED control
     * @param systemManager Pointer to SystemManager instance
     */
    void setSystemManager(SystemManager* systemManager) { systemManager_ = systemManager; }

    /**
     * @brief Set callback for timer expired events
     * @param callback Function to call when timer expires (type, oldState)
     */
    using TimerExpiredCallback = std::function<void(Type, bool)>;
    void setTimerExpiredCallback(TimerExpiredCallback callback) { 
        timerExpiredCallback_ = callback; 
    }
    
    /**
     * @brief Set callback for timer started events (for schedule pause)
     * @param callback Function to call when timer starts (type, durationMillis)
     * @note Callback receives duration in MILLISECONDS
     */
    using TimerStartedCallback = std::function<void(Type, uint32_t)>;
    void setTimerStartedCallback(TimerStartedCallback callback) {
        timerStartedCallback_ = callback;
    }
    
    /**
     * @brief Set runtime config for state persistence
     * @param config Pointer to RuntimeConfig instance
     */
    void setRuntimeConfig(class RuntimeConfig* config) { config_ = config; }
    
    /**
     * @brief Save current actuator states to persistent storage
     */
    void saveStates();
    
    /**
     * @brief Restore actuator states from persistent storage
     */
    void restoreStates();

private:
    /**
     * @brief Internal structure for each actuator instance
     * @note Uses volatile for ISR-safe access
     */
    struct ActuatorInstance {
        uint8_t pin;
        volatile bool state;
        volatile unsigned long timerEndMillis;
        volatile bool timerActive;
        volatile bool timerExpiredFlag;  // Set by ISR, cleared by main loop
        
        ActuatorInstance() : pin(0), state(false), timerEndMillis(0), 
                            timerActive(false), timerExpiredFlag(false) {}
    };

    /**
     * @brief Set the physical state of an actuator
     */
    bool setPhysicalState(Type type, bool state);

    /**
     * @brief Get actuator instance by type
     */
    ActuatorInstance& getInstance(Type type);
    const ActuatorInstance& getInstance(Type type) const;
    
    /**
     * @brief Hardware timer ISR handler (must be static)
     * @note Checks all active timers every 100µs for precision
     */
    static void IRAM_ATTR onHardwareTimerISR();

    bool initialized_;
    ActuatorInstance mosfet_;
    ActuatorInstance relay_;
    SystemManager* systemManager_;
    TimerExpiredCallback timerExpiredCallback_;
    TimerStartedCallback timerStartedCallback_;
    class RuntimeConfig* config_;
    
    // Hardware timer for precise millisecond timing (±100µs)
    static hw_timer_t* hardwareTimer_;
    static Actuator* instance_;  // Singleton for ISR access
};

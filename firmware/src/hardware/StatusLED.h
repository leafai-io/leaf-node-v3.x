#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef USE_WS2812B_LED
    #include <FastLED.h>
#endif

#include "../LeafNodeTypes.h"

/**
 * @brief Status LED controller
 * Supports both WS2812B addressable LED (V3) and separate RGB LEDs (V2)
 * Hardware version is selected in config.h
 */
class StatusLED {
public:
    StatusLED();
    ~StatusLED();

#ifdef USE_WS2812B_LED
    /**
     * @brief Initialize the status LED with WS2812B (Hardware V3)
     * @param dataPin GPIO pin for WS2812B data line
     * @param brightness LED brightness (0-255)
     * @return true if initialization was successful
     */
    bool initialize(uint8_t dataPin = LED_WS2812B_PIN, 
                   uint8_t brightness = 255);
#endif

#ifdef USE_RGB_LED
    /**
     * @brief Initialize the status LED with RGB LEDs (Hardware V2)
     * @param redPin GPIO pin for red LED
     * @param greenPin GPIO pin for green LED
     * @param bluePin GPIO pin for blue LED
     * @param brightness LED brightness (0-255)
     * @return true if initialization was successful
     */
    bool initialize(uint8_t redPin = LED_RED_PIN,
                   uint8_t greenPin = LED_GREEN_PIN,
                   uint8_t bluePin = LED_BLUE_PIN,
                   uint8_t brightness = 255);
#endif

    /**
     * @brief Update LED animation (should be called regularly)
     */
    void update();

    /**
     * @brief Set LED status
     * @param status New status to display
     */
    void setStatus(LEDStatus status);

    /**
     * @brief Get current LED status
     * @return Current status
     */
    LEDStatus getStatus() const { return currentStatus_; }

    /**
     * @brief Set brightness
     * @param brightness LED brightness (0-255)
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current brightness
     * @return Current brightness (0-255)
     */
    uint8_t getBrightness() const { return brightness_; }

    /**
     * @brief Manually set RGB values (overrides status control)
     * @param red Red value (0-255)
     * @param green Green value (0-255)
     * @param blue Blue value (0-255)
     */
    void setRGB(uint8_t red, uint8_t green, uint8_t blue);

    /**
     * @brief Enable/disable automatic status control
     * @param enabled true for automatic, false for manual control
     */
    void setAutoMode(bool enabled);

    /**
     * @brief Get current RGB values
     * @param red Reference to store red value
     * @param green Reference to store green value  
     * @param blue Reference to store blue value
     */
    void getCurrentRGB(uint8_t& red, uint8_t& green, uint8_t& blue) const;

private:
#ifdef USE_WS2812B_LED
    static const uint8_t NUM_LEDS = 1;
    CRGB leds_[NUM_LEDS];
    uint8_t dataPin_;
#endif

#ifdef USE_RGB_LED
    uint8_t redPin_;
    uint8_t greenPin_;
    uint8_t bluePin_;
#endif
    
    uint8_t brightness_;
    LEDStatus currentStatus_;
    bool initialized_;
    bool autoMode_;
    
    // Current RGB state
    uint8_t currentRed_;
    uint8_t currentGreen_;
    uint8_t currentBlue_;
    
    // Animation state
    unsigned long lastUpdate_;
    uint32_t animationStartTime_;
    
    // Success fade specific
    bool successFadeActive_;
    uint32_t successFadeStartTime_;
    static const uint32_t SUCCESS_FADE_DURATION = 3000; // 3 seconds
    
    void updateAnimation();
    void updateSuccessFade();
    void setSolid(uint8_t red, uint8_t green, uint8_t blue);
    void setPulse(uint8_t red, uint8_t green, uint8_t blue, uint32_t period);
    void setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t period);
    void setAlternateBlink(uint8_t red1, uint8_t green1, uint8_t blue1, 
                          uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t period);
    uint8_t calculatePulse(uint32_t period);
    void writeRGB(uint8_t red, uint8_t green, uint8_t blue);
};

#include "StatusLED.h"

StatusLED::StatusLED() 
#ifdef USE_WS2812B_LED
    : dataPin_(0)
#endif
#ifdef USE_RGB_LED
    : redPin_(0)
    , greenPin_(0)
    , bluePin_(0)
#endif
    , brightness_(255)
    , currentStatus_(LEDStatus::OFF)
    , initialized_(false)
    , autoMode_(true)
    , currentRed_(0)
    , currentGreen_(0)
    , currentBlue_(0)
    , lastUpdate_(0)
    , animationStartTime_(0)
    , successFadeActive_(false)
    , successFadeStartTime_(0) {
}

StatusLED::~StatusLED() {
    if (initialized_) {
        writeRGB(0, 0, 0);
#ifdef USE_WS2812B_LED
        FastLED.show();
#endif
    }
}

#ifdef USE_WS2812B_LED
bool StatusLED::initialize(uint8_t dataPin, uint8_t brightness) {
    dataPin_ = dataPin;
    brightness_ = brightness;
    
    // Initialize FastLED for SK6812 (SK68XXMINI-HS)
    // Using GRB color order - common for SK6812
    // If colors are wrong, try: RGB, BGR, or BRG
    FastLED.addLeds<SK6812, LED_WS2812B_PIN, GRB>(leds_, NUM_LEDS);
    FastLED.setBrightness(brightness_);
    
    // Turn off LED initially
    writeRGB(0, 0, 0);
    FastLED.show();
    
    initialized_ = true;
    currentStatus_ = LEDStatus::OFF;
    lastUpdate_ = millis();
    animationStartTime_ = lastUpdate_;
    
    return true;
}
#endif

#ifdef USE_RGB_LED
bool StatusLED::initialize(uint8_t redPin, uint8_t greenPin, uint8_t bluePin, uint8_t brightness) {
    redPin_ = redPin;
    greenPin_ = greenPin;
    bluePin_ = bluePin;
    brightness_ = brightness;
    
    // Configure LED pins as outputs
    pinMode(redPin_, OUTPUT);
    pinMode(greenPin_, OUTPUT);
    pinMode(bluePin_, OUTPUT);
    
    // Configure PWM for LED pins
    ledcSetup(0, 5000, 8); // Channel 0, 5kHz, 8-bit resolution
    ledcSetup(1, 5000, 8); // Channel 1
    ledcSetup(2, 5000, 8); // Channel 2
    
    ledcAttachPin(redPin_, 0);
    ledcAttachPin(greenPin_, 1);
    ledcAttachPin(bluePin_, 2);
    
    // Turn off LED initially
    writeRGB(0, 0, 0);
    
    initialized_ = true;
    currentStatus_ = LEDStatus::OFF;
    lastUpdate_ = millis();
    animationStartTime_ = lastUpdate_;
    
    return true;
}
#endif

void StatusLED::update() {
    if (!initialized_ || !autoMode_) {
        return;
    }
    
    unsigned long now = millis();
    if (now - lastUpdate_ < 50) { // Update every 50ms
        return;
    }
    
    lastUpdate_ = now;
    
    // Handle success fade separately
    if (successFadeActive_) {
        updateSuccessFade();
        return;
    }
    
    updateAnimation();
}

void StatusLED::setStatus(LEDStatus status) {
    if (currentStatus_ != status) {
        currentStatus_ = status;
        animationStartTime_ = millis();
        
        // Start success fade for SUCCESS_FADE status
        if (status == LEDStatus::SUCCESS_FADE) {
            successFadeActive_ = true;
            successFadeStartTime_ = millis();
        } else {
            successFadeActive_ = false;
        }
    }
}

void StatusLED::setBrightness(uint8_t brightness) {
    brightness_ = brightness;
#ifdef USE_WS2812B_LED
    if (initialized_) {
        FastLED.setBrightness(brightness_);
        FastLED.show();
    }
#endif
}

void StatusLED::setRGB(uint8_t red, uint8_t green, uint8_t blue) {
    if (!initialized_) return;
    
    // Store current RGB values
    currentRed_ = red;
    currentGreen_ = green;
    currentBlue_ = blue;
    
    autoMode_ = false; // Disable automatic mode
    writeRGB(red, green, blue);
}

void StatusLED::setAutoMode(bool enabled) {
    autoMode_ = enabled;
    if (enabled) {
        // Resume automatic status control
        animationStartTime_ = millis();
    }
}

void StatusLED::getCurrentRGB(uint8_t& red, uint8_t& green, uint8_t& blue) const {
    red = currentRed_;
    green = currentGreen_;
    blue = currentBlue_;
}

void StatusLED::updateSuccessFade() {
    unsigned long elapsed = millis() - successFadeStartTime_;
    
    if (elapsed >= SUCCESS_FADE_DURATION) {
        // Fade complete - turn off LED and return to OFF status
        writeRGB(0, 0, 0);
        successFadeActive_ = false;
        currentStatus_ = LEDStatus::OFF;
        return;
    }
    
    // Calculate fade brightness (fade in for first half, fade out for second half)
    float progress = (float)elapsed / SUCCESS_FADE_DURATION;
    float brightness;
    
    if (progress < 0.5f) {
        // Fade in (0 to 1)
        brightness = progress * 2.0f;
    } else {
        // Fade out (1 to 0)
        brightness = 2.0f - (progress * 2.0f);
    }
    
    // Apply to green LED only
    uint8_t greenValue = (uint8_t)(brightness * brightness_);
    writeRGB(0, greenValue, 0);
}

void StatusLED::updateAnimation() {
    switch (currentStatus_) {
        case LEDStatus::OFF:
            setSolid(0, 0, 0);
            break;
        
        case LEDStatus::FACTORY_MODE:
            // White (all LEDs on) - Factory configuration mode
            setSolid(255, 255, 255);
            break;
            
        case LEDStatus::BOOTING:
        case LEDStatus::BLE_CONFIG:
            // Solid blue for setup/config mode
            setSolid(0, 0, brightness_);
            break;
        
        case LEDStatus::WAITING_CHAIN_CONFIG:
            // Blue breathing while waiting for config from chain
            setPulse(0, 0, brightness_, 2000); // Blue breathing
            break;
            
        case LEDStatus::CONNECTING:
        case LEDStatus::WIFI_CONNECTING:
            // Blinking blue while connecting
            setBlink(0, 0, brightness_, 1000);
            break;
            
        case LEDStatus::CONNECTED:
        case LEDStatus::WIFI_CONNECTED:
            // Will be handled by success fade, then OFF
            setSolid(0, 0, 0);
            break;
            
        case LEDStatus::WIFI_FAILED:
        case LEDStatus::ERROR:
            // Fast blinking red for errors
            setBlink(brightness_, 0, 0, 500);
            break;
            
        case LEDStatus::WIFI_RECONNECTING:
            // Alternate between blue and red for WiFi reconnection attempts
            setAlternateBlink(0, 0, brightness_, brightness_, 0, 0, 1000);
            break;
            
        case LEDStatus::OTA_DOWNLOADING:
            // Blue pulsing for OTA download
            setPulse(0, 0, brightness_, 2000);
            break;
            
        case LEDStatus::OTA_INSTALLING:
            // Purple/Magenta solid for OTA installation
            setSolid(brightness_/2, 0, brightness_/2);
            break;
            
        case LEDStatus::SUCCESS_FADE:
            // Handled in updateSuccessFade()
            break;
            
        default:
            setSolid(0, 0, 0);
            break;
    }
}

void StatusLED::setSolid(uint8_t red, uint8_t green, uint8_t blue) {
    writeRGB(red, green, blue);
}

void StatusLED::setPulse(uint8_t red, uint8_t green, uint8_t blue, uint32_t period) {
    uint8_t brightness = calculatePulse(period);
    writeRGB((red * brightness) / 255, (green * brightness) / 255, (blue * brightness) / 255);
}

void StatusLED::setBlink(uint8_t red, uint8_t green, uint8_t blue, uint32_t period) {
    unsigned long elapsed = millis() - animationStartTime_;
    bool isOn = (elapsed % period) < (period / 2);
    writeRGB(isOn ? red : 0, isOn ? green : 0, isOn ? blue : 0);
}

void StatusLED::setAlternateBlink(uint8_t red1, uint8_t green1, uint8_t blue1, 
                                 uint8_t red2, uint8_t green2, uint8_t blue2, uint32_t period) {
    unsigned long elapsed = millis() - animationStartTime_;
    bool useFirstColor = (elapsed % period) < (period / 2);
    
    if (useFirstColor) {
        writeRGB(red1, green1, blue1);
    } else {
        writeRGB(red2, green2, blue2);
    }
}

uint8_t StatusLED::calculatePulse(uint32_t period) {
    unsigned long elapsed = millis() - animationStartTime_;
    float phase = (float)(elapsed % period) / period;
    
    // Sine wave pulse
    float brightness = 0.5f + 0.5f * sin(phase * 2.0f * PI);
    return (uint8_t)(brightness * 255);
}

void StatusLED::writeRGB(uint8_t red, uint8_t green, uint8_t blue) {
    if (!initialized_) return;
    
    // Store current RGB values
    currentRed_ = red;
    currentGreen_ = green;
    currentBlue_ = blue;
    
#ifdef USE_WS2812B_LED
    // Set SK6812 LED color
    leds_[0] = CRGB(red, green, blue);
    FastLED.show();
#endif

#ifdef USE_RGB_LED
    // Set RGB LED colors via PWM (applying brightness)
    uint8_t r = (red * brightness_) / 255;
    uint8_t g = (green * brightness_) / 255;
    uint8_t b = (blue * brightness_) / 255;
    
    ledcWrite(0, r); // Red channel
    ledcWrite(1, g); // Green channel
    ledcWrite(2, b); // Blue channel
#endif
}

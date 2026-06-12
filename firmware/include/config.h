#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// LEAF NODE FIRMWARE CONFIGURATION
// Zentrale Konfigurationsdatei für alle Konstanten und Defines
// ============================================================================

// ============================================================================
// DEVICE INFORMATION
// ============================================================================
#define FIRMWARE_VERSION            "1.19.3"
#define MANUFACTURER                "Leaf AI"
#define DEVICE_TYPE                 "LeafNode"
#define DEFAULT_DEVICE_NAME         "LeafNode"
#define BLE_DEVICE_KEY              "LeafNode2025"

// ============================================================================
// HARDWARE VERSION
// Uncomment ONE of the following to select hardware version
// ============================================================================
// #define HARDWARE_V2    // Uses separate RGB LEDs (Red, Green, Blue pins)
#define HARDWARE_V3      // Uses WS2812B addressable LED (default)

// ============================================================================
// HARDWARE PINS
// ============================================================================

#ifdef HARDWARE_V3
    // WS2812B Status LED (addressable RGB LED) - Hardware V3
    #define LED_WS2812B_PIN             48
    #define USE_WS2812B_LED             1
#endif

#ifdef HARDWARE_V2
    // RGB Status LED (separate LEDs) - Hardware V2
    #define LED_RED_PIN                 41
    #define LED_GREEN_PIN               40
    #define LED_BLUE_PIN                39
    #define USE_RGB_LED                 1
#endif

// RS485 Communication
#define RS485_DE_RE_PIN             16      // DE/RE Direction control
#define RS485_RX_PIN                14      // Serial1 RX
#define RS485_TX_PIN                13      // Serial1 TX

// Node-to-Node UART Chain Communication
#define UART_INPUT_RX_PIN           17      // Input port RX (from left neighbor)
#define UART_INPUT_TX_PIN           18      // Input port TX (to left neighbor)
#define UART_OUTPUT_RX_PIN          33      // Output port RX (from right neighbor)
#define UART_OUTPUT_TX_PIN          42      // Output port TX (to right neighbor)
#define UART_CHAIN_BAUD_RATE        115200  // Baud rate for chain communication

// I2C Communication
#define I2C_SDA_PIN                 8       // I2C Data
#define I2C_SCL_PIN                 9       // I2C Clock

// I2C Sensor Addresses
#define SHT31_I2C_ADDRESS           0x44    // SHT31 Temperature & Humidity Sensor
#define EZOPH_I2C_ADDRESS           0x63    // EZO pH Sensor (99 decimal)
#define EZOEC_I2C_ADDRESS           0x64    // EZO EC Sensor (100 decimal)

// I2C Actuator Addresses
#define MCP4725_I2C_ADDRESS         0x62    // MCP4725 12-bit DAC (default address, can be 0x61)

// SDI-12 Communication
#define SDI12_DATA_PIN              12      // SDI-12 Data (single wire bidirectional)
#define SDI12_SENSOR_ADDRESS        '1'     // Default TEROS 12 sensor address

// OneWire Communication (1-Wire)
#define ONEWIRE_DATA_PIN            12      // OneWire Data (shared with SDI-12, manual bridge switch)

// Actuator Control
#define MOSFET_PIN                  36      // MOSFET control pin
#define RELAY_PIN                   38      // Relay control pin

// PWM Control - GPIO2
#define PWM_IO2_PIN                 2       // PWM output on GPIO2
#define PWM_IO2_CHANNEL             3       // LEDC channel (0-2 used by RGB LED)
#define PWM_IO2_FREQUENCY           1000    // PWM frequency in Hz
#define PWM_IO2_RESOLUTION          10      // PWM resolution in bits (8=0-255, 10=0-1023, 12=0-4095)
#define PWM_IO2_MAX_VOLTAGE         3.3f    // Maximum output voltage (ESP32 logic level)

// PWM Control - MOSFET (variable power control)
#define PWM_MOSFET_PIN              36      // PWM output on MOSFET gate (same as MOSFET_PIN)
#define PWM_MOSFET_CHANNEL          4       // LEDC channel (separate from IO2)
#define PWM_MOSFET_FREQUENCY        5000    // PWM frequency in Hz
#define PWM_MOSFET_RESOLUTION       10      // PWM resolution in bits (8=0-255, 10=0-1023, 12=0-4095)
#define PWM_MOSFET_MAX_VOLTAGE      10.0f   // Maximum output voltage (switched load voltage)

// ============================================================================
// MQTT TOPIC STRUCTURE
// ============================================================================

// Base topic prefix: lai/devices/{serial_number}/...
#define MQTT_TOPIC_PREFIX           "lai/devices/"

// Device Topics (Suffixes)
#define MQTT_TOPIC_REGISTER         "/register"
#define MQTT_TOPIC_REGISTER_ACK     "/registration_ack"
#define MQTT_TOPIC_STATUS           "/status"
#define MQTT_TOPIC_HEARTBEAT        "/heartbeat"
#define MQTT_TOPIC_COMMAND          "/command"
#define MQTT_TOPIC_COMMAND_RESPONSE "/command_response"
#define MQTT_TOPIC_COMMANDS         "/commands"         // Legacy

// Sensor Topics: sensors/{serial_number}/data
#define MQTT_SENSOR_PREFIX          "sensors/"
#define MQTT_SENSOR_DATA            "/data"

// ============================================================================
// MQTT CONNECTION
// ============================================================================
#define MQTT_DEFAULT_PORT           1883
#define MQTT_KEEPALIVE              60                  // Seconds
#define MQTT_RECONNECT_DELAY        5000                // ms
#define MQTT_BUFFER_SIZE            1024                // bytes

// DNS Failure Handling
#define DNS_MAX_FAILURES_BEFORE_RESTART  5              // System restart after X DNS failures

// ============================================================================
// TIMING & INTERVALS
// ============================================================================

// System Intervals
#define HEARTBEAT_INTERVAL          60000               // 60 seconds
#define WATCHDOG_TIMEOUT            60000               // 60 seconds
#define CONFIG_SAVE_INTERVAL        300000              // 5 minutes
#define LED_UPDATE_INTERVAL         50                  // 50ms
#define SETUP_TIMEOUT               30000               // 30 seconds

// WiFi Timeouts & Retries
#define NETWORK_TIMEOUT             10000               // 10 seconds
#define WIFI_MAX_RETRY              3                   // Max retry attempts
#define WIFI_RETRY_DELAY            5000                // 5 seconds between retries
#define WIFI_CONNECTION_TIMEOUT     30000               // 30 seconds

// Sensor Reading Intervals - Defaults (can be overridden at runtime via RuntimeConfig)
#define SENSOR_READING_INTERVAL     30000               // 30 seconds default

// Sensor Error Handling
#define SENSOR_MAX_CONSECUTIVE_ERRORS   5               // Max errors before backoff
#define SENSOR_BASE_BACKOFF_INTERVAL    60000           // 1 minute base backoff
#define SENSOR_MAX_BACKOFF_INTERVAL     600000          // 10 minutes max backoff

// ============================================================================
// MEMORY & PERFORMANCE
// ============================================================================
#define STACK_SIZE_DEFAULT          4096
#define STACK_SIZE_LARGE            8192
#define JSON_BUFFER_SIZE            1024
#define JSON_BUFFER_SIZE_LARGE      2048

// ============================================================================
// STRING LIMITS
// ============================================================================
#define MAX_DEVICE_NAME_LENGTH      32
#define MAX_SERIAL_NUMBER_LENGTH    32
#define MAX_SSID_LENGTH             32
#define MAX_PASSWORD_LENGTH         64
#define MAX_MQTT_SERVER_LENGTH      64
#define MAX_MQTT_USERNAME_LENGTH    32
#define MAX_MQTT_PASSWORD_LENGTH    64

// ============================================================================
// PERSISTENT STORAGE (NVS)
// ============================================================================
#define NVS_NAMESPACE               "leafnode"
#define NVS_CONFIG_KEY              "config"
#define NVS_MAX_CONFIG_SIZE         4000                // bytes

// ============================================================================
// SERIAL DEBUG
// ============================================================================
#define DEBUG_SERIAL                true
#define SERIAL_BAUD_RATE            115200

// ============================================================================
// FACTORY MODE (Serial Console Configuration)
// ============================================================================
// Factory Mode is activated when essential configuration is missing
// Configuration is done via serial console commands:
//   - factory    : Show configuration menu
//   - setserial  : Set device serial number
//   - setsensor  : Set sensor profile
//   - setwifi    : Configure WiFi credentials
//   - setmqtt    : Configure MQTT server
// LED Status: White (all LEDs on) - FACTORY_MODE

// Legacy WebServer support (deprecated, but kept for compatibility)
#define FACTORY_WEBSERVER_PORT      80

// ============================================================================
// BUILD MODE (Production vs Development)
// ============================================================================

// Uncomment for production builds
// #define PRODUCTION_MODE

#ifdef PRODUCTION_MODE
    #define DEFAULT_LOG_LEVEL       "INFO"
    #define ENABLE_DEBUG_LOGS       false
    #define ENABLE_VERBOSE_MQTT     false
#else
    #define DEFAULT_LOG_LEVEL       "DEBUG"
    #define ENABLE_DEBUG_LOGS       true
    #define ENABLE_VERBOSE_MQTT     true
#endif

// ============================================================================
// FACTORY/TEST MQTT CONFIGURATION (Development Only)
// ============================================================================
// #ifndef PRODUCTION_MODE
//     #define TEST_MQTT_SERVER        "192.168.1.2"
//     #define TEST_MQTT_PORT          1883
//     #define TEST_MQTT_USER_PREFIX   ""             // + lowercase serial
//     #define TEST_MQTT_PASSWORD      ""
// #endif

#endif // CONFIG_H

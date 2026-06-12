#!/bin/bash

# MQTT Monitor Script for LeafNode Testing
# Monitors all relevant MQTT topics for the LeafNode system

MQTT_SERVER="${MQTT_SERVER:-192.168.1.10}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_USERNAME="${MQTT_USERNAME:?Please set MQTT_USERNAME as environment variable}"
MQTT_PASSWORD="${MQTT_PASSWORD:?Please set MQTT_PASSWORD as environment variable}"

echo "🔍 MQTT LeafNode Monitor started"
echo "📡 Server: $MQTT_SERVER:$MQTT_PORT"
echo "🔑 User: $MQTT_USERNAME"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check if mosquitto_sub is available
if ! command -v mosquitto_sub &> /dev/null; then
    echo "❌ mosquitto_sub not found!"
    echo "   Install with: brew install mosquitto (macOS)"
    echo "   or: sudo apt-get install mosquitto-clients (Ubuntu)"
    exit 1
fi

echo "👂 Monitoring MQTT topics for LeafNode AFD440CD..."
echo ""

# Monitor all LAI device topics
mosquitto_sub -h $MQTT_SERVER -p $MQTT_PORT -u $MQTT_USERNAME -P $MQTT_PASSWORD \
    -t "lai/devices/AFD440CD/+" \
    -v \
    --pretty | while read line; do

    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] $line"

    # Parse specific events
    if [[ $line == *"/register"* ]]; then
        echo "   🎯 USER REGISTRATION DETECTED!"
    elif [[ $line == *"/status"* ]]; then
        echo "   📊 Device Status Update"
    elif [[ $line == *"/commands"* ]]; then
        echo "   ⚡ Command Received"
    fi

    echo ""
done

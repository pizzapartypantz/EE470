
# ==================================================
# EE470 - MQTT Assignment
# Python MQTT-to-Hostinger Database Relay
# Version 2.0
# ==================================================

import os
import requests
import paho.mqtt.client as mqtt

# --------------------------------------------------
# MQTT Broker Configuration
# --------------------------------------------------

BROKER = "broker.hivemq.com"
PORT = 1883

TOPIC = "testtopic/temp/outTopic/jasondowning"

# --------------------------------------------------
# Hostinger Database API
# --------------------------------------------------

API_URL = "https://jasondowning.com/mqtt_insert.php"

# API token stored in PowerShell environment variable
API_TOKEN = os.environ.get("MQTT_API_TOKEN")

NODE_NAME = "esp_mqtt_1"

if not API_TOKEN:
    raise SystemExit(
        "ERROR: MQTT_API_TOKEN is missing. "
        "Set it in PowerShell before running."
    )

# --------------------------------------------------
# Send Received Data to Hostinger
# --------------------------------------------------

def send_to_database(value):

    data = {
        "node_name": NODE_NAME,
        "potentiometer_value": value
    }

    headers = {
        "X-API-Key": API_TOKEN
    }

    try:
        response = requests.post(
            API_URL,
            json=data,
            headers=headers,
            timeout=10
        )

        if response.status_code == 200:

            result = response.json()

            if result.get("success") is True:
                print("Database INSERT: SUCCESS")
                print(f"Stored ADC Value: {value}")

            else:
                print("Database response:", result)

        else:
            print(
                "Database HTTP Error:",
                response.status_code
            )
            print(response.text[:300])

    except requests.RequestException as error:
        print("HTTP Request Failed:", error)

    except ValueError:
        print("Invalid JSON response from Hostinger")

# --------------------------------------------------
# MQTT Connection Callback
# --------------------------------------------------

def on_connect(
    client,
    userdata,
    flags,
    reason_code,
    properties
):

    if reason_code == 0:

        print("Connected to HiveMQ!")

        result, mid = client.subscribe(TOPIC)

        if result == mqtt.MQTT_ERR_SUCCESS:
            print("Subscribed to:", TOPIC)
        else:
            print("MQTT subscription failed:", result)

    else:
        print("MQTT Connection Failed:", reason_code)

# --------------------------------------------------
# MQTT Message Callback
# --------------------------------------------------

def on_message(client, userdata, msg):

    try:

        value = int(msg.payload.decode("utf-8"))

        if not 0 <= value <= 1023:
            print("Invalid ADC value:", value)
            return

        print("\n--------------------------------")
        print("MQTT Topic:", msg.topic)
        print("Potentiometer Value:", value)

        # Forward MQTT data to Hostinger
        send_to_database(value)

    except (ValueError, UnicodeDecodeError):
        print("Invalid MQTT payload received")

# --------------------------------------------------
# Create MQTT Client
# --------------------------------------------------

client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2,
    client_id="EE470-PythonRelay"
)

client.on_connect = on_connect
client.on_message = on_message

# --------------------------------------------------
# Start MQTT Relay
# --------------------------------------------------

print("================================")
print("EE470 MQTT-to-Hostinger Relay V2.0")
print("================================")

print("Connecting to HiveMQ...")

try:

    client.connect(BROKER, PORT, 60)

    client.loop_forever()

except KeyboardInterrupt:

    print("\nMQTT Relay stopped by user.")

except OSError as error:

    print("MQTT Network Error:", error)

finally:

    client.disconnect()

    print("Relay terminated.")

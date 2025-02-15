#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import json
import time
import argparse
import logging

# Set up basic logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Default subscription topic: subscribes to all machines, modules, units, and points.
DEFAULT_TOPIC = "#/#/#/#"

def on_connect(client, userdata, flags, rc):
    """
    Callback function for MQTT connection.
    Subscribes to the topic provided in userdata.
    """
    if rc == 0:
        logging.info("Connected to MQTT broker successfully.")
        topic = userdata.get("topic", DEFAULT_TOPIC)
        client.subscribe(topic)
        logging.info(f"Subscribed to topic: {topic}")
    else:
        logging.error(f"Failed to connect to MQTT broker with result code {rc}")

def on_message(client, userdata, msg):
    """
    Callback function for incoming MQTT messages.
    Decodes the message payload and simulates processing.
    """
    try:
        payload_str = msg.payload.decode()
        data = json.loads(payload_str)
        logging.info(f"Received data on topic '{msg.topic}': {data}")
        # Simulate processing delay (adjust as needed)
        time.sleep(0.01)
    except Exception as e:
        logging.error(f"Error processing message: {e}")

def main():
    parser = argparse.ArgumentParser(
        description="MQTT point data decoder with adjustable subscription topic."
    )
    parser.add_argument(
        "--topic",
        type=str,
        default=DEFAULT_TOPIC,
        help="MQTT subscription topic (default: '#/#/#/#')"
    )
    args = parser.parse_args()

    # Create MQTT client with userdata containing the subscription topic
    client = mqtt.Client(userdata={"topic": args.topic})
    client.on_connect = on_connect
    client.on_message = on_message

    # Connect to the local MQTT broker
    client.connect("localhost", 1883, 60)
    logging.info("Starting MQTT loop.")
    client.loop_forever()

if __name__ == "__main__":
    main()


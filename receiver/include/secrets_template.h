// secrets_template.h - copy to secrets.h (gitignored) and fill in.
//
//   cp include/secrets_template.h include/secrets.h
//
// Leave WIFI_SSID empty ("") to run serial-only: the receiver still prints
// decoded JSON over USB, it just skips WiFi + MQTT. Fill it in to publish to a
// local MQTT broker (e.g. mosquitto on your laptop).

#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID        ""              // empty = serial-only mode
#define WIFI_PASSWORD    ""

// Optional second AP. WiFiMulti joins whichever configured network is in
// range (strongest wins). Leave the SSID empty to use a single network.
#define WIFI_SSID2       ""
#define WIFI_PASSWORD2   ""

#define MQTT_BROKER_HOST "192.168.1.10"  // your laptop / Pi running the broker
#define MQTT_BROKER_PORT 1883
#define MQTT_TOPIC       "wood/sensor"
#define MQTT_CLIENT_ID   "heltec-wood-rx"

#define MQTT_USERNAME    ""              // optional broker auth (leave "" if none)
#define MQTT_PASSWORD    ""

#endif // SECRETS_H

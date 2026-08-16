#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
//  SmartPad - MQTT version - user configuration
//
//  This is the ONLY file you need to edit before flashing the board.
//  Replace every "CHANGE_ME..." value below with your own settings.
//
//  WARNING: after you fill this file in it contains your passwords.
//  Do not commit it to a public repository.
// ---------------------------------------------------------------------------

// --- Wi-Fi network the SmartPad will connect to (2.4 GHz only) -------------
#define WIFI_SSID                "CHANGE_ME_WIFI_NAME"
#define WIFI_PASSWORD            "CHANGE_ME_WIFI_PASSWORD"

// --- MQTT broker -----------------------------------------------------------
#define MQTT_SERVER              "CHANGE_ME_BROKER_IP"   // e.g. "192.168.0.100"
#define MQTT_PORT                1883
#define MQTT_USER                "CHANGE_ME_MQTT_USER"
#define MQTT_PASSWORD            "CHANGE_ME_MQTT_PASSWORD"
#define MQTT_CLIENT_ID           "Smart_Pad01"           // must be unique per device

// --- MQTT topics -----------------------------------------------------------
#define MQTT_TOPIC_DATA          "Smart/Pad01"
#define MQTT_TOPIC_SETUP         "Smart/Pad01/Setup"
#define MQTT_TOPIC_DEBUG         "Smart/Pad01/Debug"
#define MQTT_TOPIC_NOTIFICATIONS "Smart/Pad01/Notifications"

// --- Over-the-air firmware update page (http://<device-ip>/update) ---------
#define OTA_USER                 "CHANGE_ME_OTA_USER"
#define OTA_PASSWORD             "CHANGE_ME_OTA_PASSWORD"

#endif  // CONFIG_H

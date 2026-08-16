#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
//  SmartPad - Web server version - user configuration
//
//  This is the ONLY file you need to edit before flashing the board.
//  The ESP32 creates its own Wi-Fi network (access point) using the name and
//  password below; you then connect your phone/laptop to it and open
//  http://192.168.4.1 in a browser.
//
//  WARNING: after you fill this file in it contains the password of your
//  device network. Do not commit it to a public repository.
// ---------------------------------------------------------------------------

// Name of the Wi-Fi network created by the SmartPad
#define AP_SSID      "CHANGE_ME_NETWORK_NAME"

// Password of that network - must be AT LEAST 8 characters long
#define AP_PASSWORD  "CHANGE_ME_PASSWORD"

#endif  // CONFIG_H

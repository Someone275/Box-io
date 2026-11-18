/*
  Ping watchdog on the Arduino.

  The board checks 1.1.1.1. After 3 missed checks it drives GPIO 2 HIGH,
  waits 5 seconds, then drives it LOW. BoxIO.run() does the check.
  A saved Ping watchdog widget replaces these settings.
*/
#define BOXIO_AUTH "bx_paste_your_device_key_here"
#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"
#define BOXIO_HOST "192.168.1.10"
#define BOXIO_PORT 5923

#include <BoxIOEsp32.h>

void setup() {
  BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, BOXIO_HOST, BOXIO_PORT);
  BoxIO.setPingWatchdog("1.1.1.1", 30, 3, 2, BOX_HIGH_TO_LOW, 5);
}

void loop() {
  BoxIO.run();
}

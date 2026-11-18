/*
  Toggle a digital pin from a Box IO button widget on V0.
  Echo the state to V1 so an LED or Value widget can follow it.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923
#define RELAY_PIN   2

#include <BoxIOEsp32.h>

BOX_WRITE(V0) {
  int on = param.asInt();
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  BoxIO.virtualWrite(V1, on);
  BoxIO.setProperty(V1, "color", on ? "#2ee0c5" : "#314057");
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  Serial.begin(115200);
  BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, BOXIO_HOST, BOXIO_PORT);
}

void loop() {
  BoxIO.run();
}

/*
  Read lines from the Arduino serial monitor and publish them to virtual pin V1.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923

#include <BoxIOEsp32.h>

String line;

void setup() {
  Serial.begin(115200);
  BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, BOXIO_HOST, BOXIO_PORT);
  Serial.println(F("Type a value and press Enter to send V1."));
}

void loop() {
  BoxIO.run();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        BoxIO.virtualWrite(V1, line);
        Serial.print(F("Sent V1 = "));
        Serial.println(line);
        line = "";
      }
    } else {
      line += c;
    }
  }
}

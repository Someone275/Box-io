/*
  Call your own functions on a repeating interval, and cap how often
  virtualWrite talks to the server.

  Default cap: 30 virtualWrite messages per second.
  setMaxSendsPerMinute switches that cap to "this many times per minute".

  Each setTimer adds another timer. The interval is in milliseconds.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923

#include <BoxIOEsp32.h>

int tempTimer = -1;

void sendTemp() {
  BoxIO.virtualWrite(V1, analogRead(34));
}

void sendStatus() {
  BoxIO.virtualWrite(V2, "ok");
}

void setup() {
  Serial.begin(115200);
  BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, BOXIO_HOST, BOXIO_PORT);

  // 30 virtual writes per minute, instead of the default 30 per second.
  BoxIO.setMaxSendsPerMinute(30);

  tempTimer = BoxIO.setTimer(1000, sendTemp);    // every 1 second
  BoxIO.setTimer(10000, sendStatus);             // every 10 seconds
}

void loop() {
  BoxIO.run();
  // BoxIO.deleteTimer(tempTimer);  // stop one timer and leave the others running
}

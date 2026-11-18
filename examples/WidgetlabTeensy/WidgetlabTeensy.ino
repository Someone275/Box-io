/*
  Teensy 4.1 with the onboard Ethernet jack.

  The 4.1 has no Wi-Fi. Do not include BoxIOEsp32.h, and do not call the
  Wi-Fi BoxIO.begin(key, ssid, pass, host, port). The Arduino Ethernet
  library drives a W5500 over SPI and does not use this board's PHY.

  In Library Manager, install QNEthernet by Shawn Silverman. Fit the
  Ethernet jack and magjack. BOXIO_HOST is the LAN address of the computer
  running Box IO. localhost on the Teensy is the Teensy itself.

  A dashboard round button on V0 drives pin 2.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define BOXIO_HOST  "localhost"
#define BOXIO_PORT  5923
#define RELAY_PIN   2

#include <QNEthernet.h>
using namespace qindesign::network;

#include <BoxIO.h>

EthernetClient client;

BOX_WRITE(V0) {
  digitalWrite(RELAY_PIN, param.asInt() ? HIGH : LOW);
}
static uint32_t lastPulse = 0;
static int gridCells[4] = {21, 22, 40, 18};
static int meter;
int tempTimer = -1;
BOX_WRITE(V4) { Serial.printf("input V4: %s\n", param.asStr()); }
BOX_WRITE(V5) { Serial.printf("grid V5: %s\n", param.asStr()); }
BOX_WRITE(V6) { Serial.printf("round V6: %s\n", param.asStr()); }
BOX_WRITE(V7) { Serial.printf("oval V7: %s\n", param.asStr()); }
BOX_WRITE(V8) { Serial.printf("slider_h V8: %s\n", param.asStr()); }
BOX_WRITE(V9) { Serial.printf("slider_v V9: %s\n", param.asStr()); }

void pushProperties() {
  BoxIO.setProperty(V0, "label", "LED Lab");
  BoxIO.setProperty(V0, "colorOn", "#ff5d73");
  BoxIO.setProperty(V0, "colorOff", "#1a2333");

  BoxIO.setProperty(V1, "label", "Value Lab");
  BoxIO.setProperty(V1, "color", "#f5b942");

  BoxIO.setProperty(V2, "label", "Circle Lab");
  BoxIO.setProperty(V2, "min", "0");
  BoxIO.setProperty(V2, "max", "200");
  BoxIO.setProperty(V2, "color", "#2ee0c5");
  BoxIO.setProperty(V2, "colorMode", "values");
  BoxIO.setProperty(V2, "colorStops", "0:#2ee0c5;50:#f5b942;100:#ff5d73");

  BoxIO.setProperty(V3, "label", "Bar Lab");
  BoxIO.setProperty(V3, "min", "10");
  BoxIO.setProperty(V3, "max", "90");
  BoxIO.setProperty(V3, "color", "#7aa2ff");
  BoxIO.setProperty(V3, "colorMode", "percentage");
  BoxIO.setProperty(V3, "colorStops", "0:#2ee0c5;60:#f5b942;85:#ff5d73");

  BoxIO.setProperty(V4, "label", "Input Lab");
  BoxIO.setProperty(V4, "colorOn", "#2ee0c5");
  BoxIO.setProperty(V4, "colorOff", "#314057");

  BoxIO.setProperty(V5, "label", "Grid Lab");
  BoxIO.setProperty(V5, "rows", "2");
  BoxIO.setProperty(V5, "cols", "2");
  BoxIO.setProperty(V5, "colorOff", "#1a2333");
  BoxIO.setProperty(V5, "colorRules", "0-25:#2ee0c5:all;35-100:#ff5d73:all");

  // These setProperty calls are what the dashboard shows.
  // label is the caption. textOn / textOff are the words on the button.
  // A later setProperty replaces whatever was typed on the dashboard.
  BoxIO.setProperty(V6, "label", "Round");
  BoxIO.setProperty(V6, "textOn", "Go");
  BoxIO.setProperty(V6, "textOff", "Idle");
  BoxIO.setProperty(V6, "onValue", "go");
  BoxIO.setProperty(V6, "offValue", "0");
  BoxIO.setProperty(V6, "sendValue", "go");
  BoxIO.setProperty(V6, "colorOn", "#2ee0c5");
  BoxIO.setProperty(V6, "colorOff", "#314057");
  BoxIO.setProperty(V6, "textColorOn", "#071018");
  BoxIO.setProperty(V6, "textColorOff", "#e8eef8");

  BoxIO.setProperty(V7, "label", "Oval");
  BoxIO.setProperty(V7, "textOn", "Stop");
  BoxIO.setProperty(V7, "textOff", "Run");
  BoxIO.setProperty(V7, "onValue", "stop");
  BoxIO.setProperty(V7, "offValue", "0");
  BoxIO.setProperty(V7, "sendValue", "stop");
  BoxIO.setProperty(V7, "colorOn", "#ff5d73");
  BoxIO.setProperty(V7, "colorOff", "#314057");
  BoxIO.setProperty(V7, "textColorOn", "#071018");
  BoxIO.setProperty(V7, "textColorOff", "#e8eef8");

  BoxIO.setProperty(V8, "label", "H Slider");
  BoxIO.setProperty(V8, "min", "5");
  BoxIO.setProperty(V8, "max", "50");
  BoxIO.setProperty(V8, "color", "#7aa2ff");

  BoxIO.setProperty(V9, "label", "V Slider");
  BoxIO.setProperty(V9, "min", "-10");
  BoxIO.setProperty(V9, "max", "10");
  BoxIO.setProperty(V9, "color", "#f5b942");
}
void sendTemp() {
  //BoxIO.virtualWrite(V11, "77,600,78");
  BoxIO.deleteTimer(tempTimer);
   Serial.println("V11 data sent 77,600,78");
}

void sendStatus() {
  BoxIO.virtualWrite(V2, "ok");
}
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Box IO widget lab");
  pinMode(RELAY_PIN, OUTPUT);
  Ethernet.begin();
  Ethernet.waitForLocalIP(10000);
  BoxIO.begin(client, BOXIO_AUTH, BOXIO_HOST, BOXIO_PORT);
  Serial.println("Welcome Boxio connected");
  BoxIO.setPollInterval(250);
  pushProperties();
  BoxIO.virtualWrite(V0, 1);
  BoxIO.virtualWrite(V1, "hello-lab");
  BoxIO.virtualWrite(V2, 72);
  BoxIO.virtualWrite(V3, 40);
  BoxIO.virtualWrite(V5, gridCells, 4);
  Serial.println("properties + seed values sent");
  tempTimer = BoxIO.setTimer(10000, sendTemp);    // every 10 second
  BoxIO.setTimer(10000, sendStatus);

}

void loop() {
  BoxIO.run();
  if (millis() - lastPulse < 1000) return;
  lastPulse = millis();
  meter = (meter + 3) % 180;
  BoxIO.virtualWrite(V0, (meter / 10) % 2);
  BoxIO.virtualWrite(V1, meter);
  BoxIO.virtualWrite(V2, meter);
  BoxIO.virtualWrite(V3, 10 + (meter % 80));
  gridCells[0] = meter % 50;
  BoxIO.virtualWrite(V5, gridCells, 4);
}
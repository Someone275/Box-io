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
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923
#define RELAY_PIN   2

#include <QNEthernet.h>
using namespace qindesign::network;

#include <BoxIO.h>

EthernetClient client;

BOX_WRITE(V0) {
  digitalWrite(RELAY_PIN, param.asInt() ? HIGH : LOW);
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  Ethernet.begin();
  Ethernet.waitForLocalIP(10000);
  BoxIO.begin(client, BOXIO_AUTH, BOXIO_HOST, BOXIO_PORT);
}

void loop() {
  BoxIO.run();
}

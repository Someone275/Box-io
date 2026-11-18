/*
  Ethernet sample for AVR, MegaAVR, SAM, and SAMD boards with an Ethernet shield.
  Add a Box IO round button on V0 to drive pin 2.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923
#define RELAY_PIN   2

#include <Ethernet.h>
#include <BoxIOEthernet.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetClient client;

BOX_WRITE(V0) {
  digitalWrite(RELAY_PIN, param.asInt() ? HIGH : LOW);
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  Ethernet.begin(mac);
  BoxIO.begin(client, BOXIO_AUTH, BOXIO_HOST, BOXIO_PORT);
}

void loop() {
  BoxIO.run();
}

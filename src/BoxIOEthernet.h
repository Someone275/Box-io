#pragma once
/*
  Convenience header for Ethernet-equipped AVR, SAM, SAMD, and MegaAVR boards.

    #include <Ethernet.h>
    #include <BoxIOEthernet.h>
    Ethernet.begin(mac);
    EthernetClient client;
    BoxIO.begin(client, BOXIO_AUTH, "192.168.1.10", 5923);
*/
#include "BoxIO.h"

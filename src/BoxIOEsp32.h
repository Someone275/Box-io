#pragma once
/*
  Convenience header for ESP32 and ESP8266 (NodeMCU) Wi-Fi boards.
  ESP8266 cores, including 2.7.4, provide ESP8266WiFi.h.

    #include <BoxIOEsp32.h>
    BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, "192.168.1.10", 5923);
*/
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "BoxIO.h"

# BoxIO Arduino library

Use this library in Arduino IDE with ESP32, ESP8266 (NodeMCU 1.0), Teensy 4.1,
SAM, SAMD, MegaAVR, and classic AVR boards. The firmware talks HTTP to a Box IO
server on **port 5923**.

## Install

When BoxIO is listed in Library Manager, open **Sketch → Include Library → Manage Libraries…** and search for **BoxIO**.

The downloads page zip works whether or not that listing is up:

1. Download `BoxIO.zip` from the Box IO downloads page. Leave it zipped.
2. In Arduino IDE 2 or 1.8, choose **Sketch → Include Library → Add .ZIP Library…**
3. Select `BoxIO.zip` and open it. The IDE copies this folder into the sketchbook libraries folder.
4. Restart Arduino IDE if **File → Examples → BoxIO** is missing.

You can also copy the `BoxIO` folder into `Arduino/libraries/` yourself and restart the IDE. The zip on the downloads page already has this folder at the top, which is the layout Add .ZIP Library expects.

## ESP32 and ESP8266 (NodeMCU)

`BoxIOEsp32.h` selects `WiFi.h` on ESP32 and `ESP8266WiFi.h` on ESP8266, including
core 2.7.4 on NodeMCU 1.0 (ESP-12E). Board: **NodeMCU 1.0 (ESP-12E Module)**.

```cpp
#define BOXIO_AUTH "bx_your_device_key"
#include <BoxIOEsp32.h>

void setup() {
  BoxIO.begin(BOXIO_AUTH, "wifi-ssid", "wifi-pass", "boxio.local", 5923);
}

void loop() {
  BoxIO.run();
}

BOX_WRITE(V0) {
  digitalWrite(LED_BUILTIN, param.asInt());
}
```

## Ethernet (ESP32-S3 + W5500)

Generic **ESP32S3 Dev Module** cores often do **not** define `ETH_PHY_W5500`, so
`ETH.h` cannot drive the chip. The WidgetLab sketch uses the Arduino **Ethernet**
library over SPI instead.

1. Library Manager: install **Ethernet** by Arduino.
2. Keep `#define BOXIO_USE_ETHERNET 1` and `#define BOXIO_WIFI_FALLBACK 0`.
3. Set CS/SCK/MISO/MOSI/RST to the same pins as a sketch that already works
   on that board (defaults are Waveshare ESP32-S3-ETH: 14/13/12/11/9).

## Ethernet (AVR / SAM / SAMD / Mega)

```cpp
#include <Ethernet.h>
#include <BoxIOEthernet.h>

byte mac[] = { 0x02, 0x42, 0x49, 0x4F, 0x00, 0x01 };
EthernetClient client;

void setup() {
  Ethernet.init(10);                 // CS pin. Call this before begin.
  Ethernet.begin(mac, 3000, 1000);   // give up on DHCP after 3 seconds
  BoxIO.begin(client, "bx_your_device_key", "192.168.1.10", 5923);
}

void loop() {
  Ethernet.maintain();
  BoxIO.run();
}
```

`Ethernet.begin(mac)` alone waits up to 60 seconds. A board that resets on a
short watchdog never reaches `BoxIO.setTimer` or the IP print. Call
`Ethernet.maintain()` from `loop()` so the DHCP lease stays. The library keeps
the device socket open between polls. A hostname is looked up again each time
that socket has to be opened; an IP address avoids that lookup. Give each board
its own MAC. `DE:AD:BE:EF:FE:ED` is only an example, and two shields using it
take turns on the network.

## Teensy 4.1

The 4.1 has no Wi-Fi and its Ethernet jack is a native PHY, not a W5500. Install
**QNEthernet** by Shawn Silverman. Open **File → Examples → BoxIO → Teensy41**.
The full widget sketch is **File → Examples → BoxIO → WidgetlabTeensy**.
`BOXIO_HOST` is the LAN address of the Box IO computer.

```cpp
#include <QNEthernet.h>
using namespace qindesign::network;
#include <BoxIO.h>

EthernetClient client;

void setup() {
  Ethernet.begin();
  Ethernet.waitForLocalIP(10000);
  BoxIO.begin(client, "bx_your_device_key", "192.168.1.10", 5923);
}

void loop() {
  BoxIO.run();
}
```

Declare the client as `EthernetClient client;`. On NativeEthernet, `EthernetClient client = 0` does not mean "no socket". It means socket 0, which is usually the listener a Modbus or web server already opened. The first `BoxIO.run()` then closes or reuses that socket.

`run()` used to read until `connected()` went false. NativeEthernet often leaves that true after the HTTP body, so `run()` never returned. A sketch that resets an 8 second hardware watchdog from `loop()` reboots inside that call. The library now stops when `Content-Length` bytes have arrived, or after a short idle, and it asks the client for a 1.5 second connect timeout when the client supports that. Use the server's IP address in `BOXIO_HOST` if a hostname still sits in name lookup long enough to trip the watchdog.

If port 5923 does not connect, the next attempt uses port 80. The dashboard proxy serves `/hw/` there, which matters when a router allows port 80 and blocks 5923. A later success on 5923 is tried again after a minute.

## Virtual pins

| Call | Direction |
| --- | --- |
| `BoxIO.virtualWrite(V1, value)` | Device → app |
| `BoxIO.virtualWrite(V5, values, count)` | Device → app as CSV (int or float array, for Grid) |
| `BOX_WRITE(V1) { ... }` | App → device |
| `BoxIO.setProperty(V1, "color", "#FF0000")` | Change widget color / label / range (see below) |
| `BoxIO.setProperty(V5, "rows", "4")` | Grid rows / also `cols`, `colorRules` |
| `BoxIO.email(to, subject, body)` | Via server SMTP |
| `BoxIO.sms(to, body)` | Via server Twilio |
| `BoxIO.setMaxSendsPerSecond(30)` | Cap `virtualWrite` traffic. Default is 30 per second. `0` removes the cap |
| `BoxIO.setMaxSendsPerMinute(n)` | Same cap, counted per minute |
| `BoxIO.setTimer(ms, function)` | Call `function` every `ms` milliseconds |
| `BoxIO.deleteTimer(id)` | Stop one timer |
| `BoxIO.setPingWatchdog(host, sec, misses, gpio, dir, gap)` | On the board, check `host` and pulse GPIO `gpio` if it cannot be reached |
| `BoxIO.clearPingWatchdog()` | Stop that check |

Always call `BoxIO.run()` in `loop()`. That pulls widget commands, runs timers, and sends any virtual writes that were waiting on the rate cap.

## Send rate

Each `virtualWrite` opens an HTTP connection. The library allows **30 of those per second** unless you change it. Extra writes are not thrown away: the latest value for each pin is kept and sent on a later `BoxIO.run()` when the cap allows. `setProperty`, email, and SMS are not counted.

```cpp
BoxIO.setMaxSendsPerSecond(10);  // 10 virtualWrite messages per second
BoxIO.setMaxSendsPerMinute(30);  // or 30 per minute
BoxIO.setMaxSendsPerSecond(0);   // no cap
```

`setMaxSendsPerMinute` replaces the per-second cap. Call `setMaxSendsPerSecond` again to switch back. The waiting list holds 2 pins of 24 characters on Uno, Nano, Leonardo, Micro, and Leonardo ETH. Mega and every other board hold 12 pins of 96 characters. To change that, add `BoxIOConfig.h` in the sketch folder and define `BOXIO_MAX_PENDING` and `BOXIO_PENDING_LEN` there. The sketch folder has to be on the library include path for that file to be seen.

## Timers

`setTimer` registers another repeating call. Each timer has its own interval, in milliseconds, and its own function. The return value is the id to pass to `deleteTimer`. The table holds 4 timers on Uno, Nano, Leonardo, Micro, and Leonardo ETH, and 16 on Mega and every other board. To change that, add `BoxIOConfig.h` in the sketch folder and define `BOXIO_MAX_TIMERS` there. The sketch folder has to be on the library include path for that file to be seen.

```cpp
void sendTemp() {
  BoxIO.virtualWrite(V1, analogRead(34));
}

void sendStatus() {
  BoxIO.virtualWrite(V2, "ok");
}

void setup() {
  BoxIO.setTimer(1000, sendTemp);     // every 1 second
  BoxIO.setTimer(10000, sendStatus);  // every 10 seconds
}

void loop() {
  BoxIO.run();
}
```

## Ping watchdog

The check runs on the Arduino. `BoxIO.run()` tries to open a TCP connection to the host (port 80, or 443 for an `https://` URL). After the miss count, it drives a GPIO on this board high then low, or low then high, and waits `transition` seconds between those levels. One outage pulses once. A later success arms it again.

A saved Ping watchdog widget on the dashboard replaces this sketch setting the next time `run()` syncs.

```cpp
void setup() {
  // Every 30s. After 3 misses, pin 2 goes HIGH, then LOW five seconds later.
  BoxIO.setPingWatchdog("1.1.1.1", 30, 3, 2, BOX_HIGH_TO_LOW, 5);
}

void loop() {
  BoxIO.run();
}
```

`BOX_LOW_TO_HIGH` drives the pin LOW first, then HIGH. `clearPingWatchdog()` stops it.

## Widget properties from firmware

`setProperty` values override the saved dashboard layout for that pin (except Grid
read/write **mode**, which the dashboard controls).

| Widget | Properties |
| --- | --- |
| LED | `label`, `colorOn`, `colorOff`, `color` (alias for on-color) |
| Value | `label`, `color` |
| Circle meter | `label`, `min`, `max`, `startDeg`, `endDeg`, `color`, `colorMode` (`percentage` or `values`), `colorStops` |
| Bar meter | `label`, `min`, `max`, `color`, `colorMode` (`percentage` or `values`), `colorStops` |
| Input | `label`, `colorOn`, `colorOff` |
| Round / oval button | `label`, `colorOn`, `sendValue` |
| Slider H / V | `label`, `min`, `max`, `color` |
| Grid | `label`, `rows`, `cols`, `colorOff`, `colorRules` |

`colorStops` is `at:#hex` joined by `;`, e.g. `0:#2ee0c5;60:#f5b942;85:#ff5d73`.
Circle `startDeg` and `endDeg` are 0–360. 0 is straight up and degrees go clockwise. 225 to 135 is the lower-left to lower-right gauge.
`colorRules` is `low-high:#hex:all` or `low-high:#hex:0,1,5` joined by `;`.

The **WidgetLab** example (`examples/WidgetLab` and `samples/widget-lab`) sets
every property on V0–V9. ESP32-S3 + W5500 uses the Arduino **Ethernet** library
over SPI (generic Dev Module cores often have no `ETH_PHY_W5500`).

## Grid widget

Send a row-major array as CSV on one virtual pin. Size, mode, and color ranges can
be set in the dashboard or from firmware:

```cpp
int cells[] = {21, 22, 40, 18};
BoxIO.virtualWrite(V5, cells, 4);
BoxIO.setProperty(V5, "rows", "2");
BoxIO.setProperty(V5, "cols", "2");
BoxIO.setProperty(V5, "mode", "readonly");  // or "readwrite"
BoxIO.setProperty(V5, "colorRules", "0-25:#2ee0c5:all;35-100:#ff5d73:0,1");
```

`colorRules` is `low-high:#hex:all` or `low-high:#hex:0,1,5` (cell indices), joined by `;`.
Firmware `rows`, `cols`, `colorOff`, and `colorRules` override the saved layout. Read/write
mode is set on the widget in the dashboard (Arduino may set `mode` only if the widget left it unset).
Uno, Nano, Leonardo, Micro, and Leonardo ETH keep a 160-character URL buffer, so keep a grid on those boards modest. Mega and other larger AVR boards use a 512-character buffer. ESP32, Teensy, and other non-AVR boards send a full 16 by 16 grid in one virtualWrite. The library picks the smaller buffers on any AVR with about 2.5 KB of RAM. To raise them, add `BoxIOConfig.h` in the sketch folder and define `BOXIO_BUF` there. The sketch folder has to be on the library include path for that file to be seen.

/*
  Widget lab — ESP32-S3 Ethernet (W5500) or Wi-Fi

  Your Serial log means the ESP32 Arduino core does not define ETH_PHY_W5500
  (typical of "ESP32S3 Dev Module"). This sketch drives the W5500 over SPI with
  the Arduino Ethernet library instead, which is what most working S3+W5500
  projects use.

  Arduino IDE:
    1. Sketch → Include Library → Manage Libraries → install "Ethernet" by Arduino
    2. Copy arduino/BoxIO into Documents/Arduino/libraries
    3. Set BOXIO_AUTH below
    4. Copy CS/SCK/MISO/MOSI/RST from a sketch that already works on this board
       if the defaults do not match your wiring
    5. File → Save the sketch (do not compile an unsaved temp copy)
    6. Flash, Serial 115200. Expect: "eth up 192.168.…" then "properties + seed"

  Pin map: V0 LED, V1 value, V2 circle, V3 bar, V4 input, V5 grid,
           V6 round, V7 oval, V8 slider H, V9 slider V
*/

#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define BOXIO_HOST  "localhost"
#define BOXIO_PORT  5923

// 1 = Ethernet (W5500). 0 = Wi-Fi only.
#define BOXIO_USE_ETHERNET 1
// Do not fall back to Wi-Fi when Ethernet is requested (avoids a confusing timeout).
#define BOXIO_WIFI_FALLBACK 0

#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"

// Waveshare ESP32-S3-ETH defaults. Change these to match a working sketch.
#define ETH_PHY_CS   14
#define ETH_PHY_IRQ  10
#define ETH_PHY_RST  9
#define ETH_SPI_SCK  13
#define ETH_SPI_MISO 12
#define ETH_SPI_MOSI 11

#include <SPI.h>
#include <WiFi.h>
#include <Ethernet.h>
#include <BoxIO.h>

#if BOXIO_USE_ETHERNET
#define BOXIO_ETH_ARDUINO 1
EthernetClient boxClient;
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#include <NetworkClient.h>
NetworkClient boxClient;
#else
WiFiClient boxClient;
#endif

static bool linkReady = false;
static uint32_t lastPulse = 0;
static int gridCells[4] = {21, 22, 40, 18};
static int meter;

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

void resetW5500() {
  pinMode(ETH_PHY_RST, OUTPUT);
  digitalWrite(ETH_PHY_RST, LOW);
  delay(80);
  digitalWrite(ETH_PHY_RST, HIGH);
  delay(200);
}

bool beginEthernet() {
#if BOXIO_USE_ETHERNET
  Serial.printf(
      "W5500 via Arduino Ethernet  SPI SCK=%d MISO=%d MOSI=%d CS=%d RST=%d\n",
      ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_PHY_CS, ETH_PHY_RST);
  resetW5500();
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_PHY_CS);
  Ethernet.init(ETH_PHY_CS);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);

  int ok = Ethernet.begin(mac);
  int hw = (int)Ethernet.hardwareStatus();
  int link = (int)Ethernet.linkStatus();
  Serial.printf("Ethernet.begin=%d hw=%d link=%d ip=%s\n", ok, hw, link, Ethernet.localIP().toString().c_str());
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("No W5500 on this SPI bus. Copy CS/SCK/MISO/MOSI/RST from a working sketch.");
    return false;
  }
  uint32_t start = millis();
  while (Ethernet.linkStatus() == LinkOFF && millis() - start < 8000) delay(200);
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("W5500 found but link is down (cable/switch/RST pin).");
    return false;
  }
  if (!ok || Ethernet.localIP()[0] == 0) {
    Serial.println("Link is up but DHCP did not assign an IP.");
    return false;
  }
  Serial.printf("eth up %s\n", Ethernet.localIP().toString().c_str());
  return true;
#else
  Serial.println("Ethernet path is not compiled in.");
  return false;
#endif
}

bool beginWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi timeout");
    return false;
  }
  Serial.printf("wifi up %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Box IO widget lab");
#if defined(ESP_ARDUINO_VERSION_MAJOR)
  Serial.printf("ESP32 Arduino core %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH);
#endif

#if BOXIO_USE_ETHERNET
  linkReady = beginEthernet();
#endif
#if BOXIO_WIFI_FALLBACK
  if (!linkReady) linkReady = beginWifi();
#endif
  if (!linkReady) {
    Serial.println("no network");
    return;
  }

  BoxIO.begin(boxClient, BOXIO_AUTH, BOXIO_HOST, BOXIO_PORT);
  BoxIO.setPollInterval(250);
  pushProperties();
  BoxIO.virtualWrite(V0, 1);
  BoxIO.virtualWrite(V1, "hello-lab");
  BoxIO.virtualWrite(V2, 72);
  BoxIO.virtualWrite(V3, 40);
  BoxIO.virtualWrite(V5, gridCells, 4);
  Serial.println("properties + seed values sent");
}

void loop() {
  if (!linkReady) return;
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

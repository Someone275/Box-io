/*
  Publish a 2x2 numeric array to a Box IO Grid widget on V5.
  Color rules and size can be set here or in the dashboard properties panel.

  Add a Grid widget bound to V5. Leave it read-only to only display values,
  or set mode to read-and-write to edit cells from the app.
*/
#define BOXIO_AUTH  "bx_paste_your_device_key_here"
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
#define BOXIO_HOST  "192.168.1.10"
#define BOXIO_PORT  5923

#include <BoxIOEsp32.h>

int cells[] = {21, 22, 40, 18};

BOX_WRITE(V5) {
  // Full CSV of the grid when the widget is in read-and-write mode.
  Serial.println(param.asStr());
}

void setup() {
  Serial.begin(115200);
  BoxIO.begin(BOXIO_AUTH, WIFI_SSID, WIFI_PASS, BOXIO_HOST, BOXIO_PORT);
  BoxIO.setProperty(V5, "rows", "2");
  BoxIO.setProperty(V5, "cols", "2");
  BoxIO.setProperty(V5, "colorRules", "0-25:#2ee0c5:all;35-100:#ff5d73:all");
}

void loop() {
  BoxIO.run();
  cells[0] = (cells[0] + 1) % 50;
  BoxIO.virtualWrite(V5, cells, 4);
  delay(1000);
}

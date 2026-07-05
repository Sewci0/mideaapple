#include "HomeSpan.h"
#include "MideaHeaterCooler.h"
#include "WebUI.h"
#include <Preferences.h>

// AC UART. Midea's WiFi-dongle port runs 9600 8N1. The C3 has no Serial2, so we
// drive a dedicated UART1 on two safe GPIOs (set in platformio.ini).
#ifndef MIDEA_RX_PIN
#define MIDEA_RX_PIN 4    // ESP32-C3 in  <-  AC TX
#endif
#ifndef MIDEA_TX_PIN
#define MIDEA_TX_PIN 5    // ESP32-C3 out ->  AC RX
#endif

AirConditioner ac;
HardwareSerial MideaSerial(1);   // UART1

// Runtime RX/TX swap, persisted in NVS. Which AC data pin is TX vs RX varies by
// model, so this lets you flip the UART orientation from the web UI with no
// rebuild/reflash. Default (unswapped) = MIDEA_RX_PIN / MIDEA_TX_PIN.
Preferences prefs;
bool g_pinsSwapped = false;

static void beginMidea() {
  int rx = g_pinsSwapped ? MIDEA_TX_PIN : MIDEA_RX_PIN;
  int tx = g_pinsSwapped ? MIDEA_RX_PIN : MIDEA_TX_PIN;
  MideaSerial.end();
  MideaSerial.begin(9600, SERIAL_8N1, rx, tx);
  Serial.printf("Midea UART: RX=%d TX=%d (swap=%d)\n", rx, tx, g_pinsSwapped);
}

// Called from WebUI.h
bool mideaGetSwap() { return g_pinsSwapped; }
void mideaSetSwap(bool s) {
  if (s == g_pinsSwapped) return;
  g_pinsSwapped = s;
  prefs.begin("midea", false);
  prefs.putBool("swap", s);
  prefs.end();
  beginMidea();   // re-init UART on the swapped pins immediately
}

// The AC's USB port only sources ~300 mA. Once WiFi is up, trim the RF power-amp
// current spikes that cause brown-out resets on a weak rail. Raise the level if
// range/connectivity suffers; pair this with a bulk cap (>=470uF) across 5V.
void onWifiUp() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  webui::webBegin(&ac);   // start the local web control panel (port 80)
  Serial.printf("Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);

  prefs.begin("midea", false);                   // RW so first boot creates the
  g_pinsSwapped = prefs.getBool("swap", false);  // namespace (avoids NOT_FOUND log)
  prefs.end();
  beginMidea();
  ac.setStream(&MideaSerial);
  ac.setup();                   // begins the query/handshake with the AC mainboard

  homeSpan.setLogLevel(1);
  homeSpan.setWifiCallback(onWifiUp);
  homeSpan.enableOTA();         // OTA reflash once it's on WiFi
  homeSpan.begin(Category::AirConditioners, "Midea AC");

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Midea");
      new Characteristic::Model("mideaapple");
      new Characteristic::Name("Midea AC");
    new MideaHeaterCooler(&ac);
}

void loop() {
  homeSpan.poll();
  ac.loop();
  webui::webLoop();
}

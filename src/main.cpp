#include "HomeSpan.h"
#include "MideaHeaterCooler.h"
#include "WebUI.h"

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

  MideaSerial.begin(9600, SERIAL_8N1, MIDEA_RX_PIN, MIDEA_TX_PIN);
  ac.setStream(&MideaSerial);   // [verify] setStream()
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

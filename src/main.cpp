#include "HomeSpan.h"
#include "MideaHeaterCooler.h"
#include "WebUI.h"
#include <Preferences.h>

// AC UART. Midea's WiFi-dongle port runs 9600 8N1. The C3 has no Serial2, so we
// drive a dedicated UART1 on two safe GPIOs (set in platformio.ini).
#ifndef MIDEA_RX_PIN
#define MIDEA_RX_PIN 7    // ESP32-C3 in  <-  AC TX  (default; overridden in platformio.ini)
#endif
#ifndef MIDEA_TX_PIN
#define MIDEA_TX_PIN 10   // ESP32-C3 out ->  AC RX
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

// ---------------------------------------------------------------------------
// Auto-tuning WiFi TX power. Some C3 boards/supplies brown out transmitting at high power and then
// can't authenticate (especially to a strong/near AP -> reason 2). Instead of hardcoding a level,
// pick the TX power that yields the STRONGEST connection (best RSSI) — NOT merely the highest power
// that connects: on a brownout-prone board, higher TX can fail the strong/near AP and get shoved
// onto a weaker one, so "highest that connects" is wrong. Portable, no per-board hardcoding.
// Persisted in NVS ("midea" namespace) as txbest = chosen level index.
static const wifi_power_t TX_LV[]  = { WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm,
                                       WIFI_POWER_8_5dBm,  WIFI_POWER_5dBm,  WIFI_POWER_2dBm };
static const char *const  TX_LBL[] = { "19.5", "15", "11", "8.5", "5", "2" };
static const int TX_N = 6;
int  g_txIdx  = TX_N - 1;     // current level index in effect (default = 2 dBm, the safe floor)

static void applyTx() { WiFi.setTxPower(TX_LV[g_txIdx]); }   // also re-applied in STA_START

static int connectAt(int idx, const char *ssid, const char *pwd, int ms) {
  g_txIdx = idx;
  WiFi.disconnect(); delay(300);
  applyTx();
  WiFi.begin(ssid, pwd);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < (unsigned long)ms) delay(100);
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;   // 0 = didn't connect
}

static bool autoTuneTx(const char *ssid, const char *pwd) {
  WiFi.mode(WIFI_STA);
  prefs.begin("midea", false);
  int saved = prefs.getInt("txbest", -1), savedRssi = prefs.getInt("txrssi", -999);
  prefs.end();
  if (saved >= 0 && saved < TX_N) {                        // fast reboot path: reuse the known-good
    int r = connectAt(saved, ssid, pwd, 10000);            // level — but only if the link is still
    if (r < 0 && r >= savedRssi - 12) {                    // ~as good as when we picked it (else the
      Serial.printf("[tx-tune] using saved %s dBm (%d dBm)\n", TX_LBL[saved], r);   // AP/RF changed)
      return true;
    }
    Serial.println("[tx-tune] saved level weak/failed -> full re-search");
  }
  // Full search: try every level, keep the one with the STRONGEST resulting link. Iterating from
  // full power down with a strict '>' means ties (same AP) resolve to the higher TX power.
  int best = -1, bestRssi = -999;
  for (int i = 0; i < TX_N; i++) {
    int r = connectAt(i, ssid, pwd, 8000);
    if (r) { Serial.printf("[tx-tune] %s dBm -> %s (%d dBm)\n", TX_LBL[i], WiFi.BSSIDstr().c_str(), r);
             if (r > bestRssi) { bestRssi = r; best = i; } }
    else     Serial.printf("[tx-tune] %s dBm -> no connect\n", TX_LBL[i]);
  }
  if (best < 0) { Serial.println("[tx-tune] nothing connected"); return false; }
  Serial.printf("[tx-tune] LOCKED %s dBm (best link %d dBm)\n", TX_LBL[best], bestRssi);
  prefs.begin("midea", false);
  prefs.putInt("txbest", best); prefs.putInt("txrssi", bestRssi);
  prefs.end();
  return connectAt(best, ssid, pwd, 10000) != 0;            // final connect at the chosen level
}

// Called from WebUI.h — read-only, for display of the level auto-tune settled on.
const char *mideaGetTxLabel() { return TX_LBL[g_txIdx]; }

// WiFi diagnostics: log association stages + the disconnect reason code, so we can
// tell wrong-password (reason 15) from AP-not-found (201), auth-fail (202), etc.
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      applyTx();                             // (re-)assert the tuned/pinned TX power BEFORE assoc
      Serial.printf("[wifi] STA MAC %s (TX %s dBm)\n", WiFi.macAddress().c_str(), TX_LBL[g_txIdx]);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[wifi] associated (auth OK) - waiting for IP");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] got IP %s  |  AP BSSID=%s  RSSI=%d dBm  ch=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str(),
                    WiFi.RSSI(), WiFi.channel());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[wifi] disconnected, reason=%d\n",
                    info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

// WiFi TX power is auto-tuned before association (autoTuneTx + the STA_START handler): the firmware
// picks the highest level that connects and remembers it, so a board that browns out at high power
// (this one needs ~2 dBm) still joins strong/near APs without any per-board hardcoding.
void onWifiUp() {
  webui::webBegin(&ac);   // start the local web control panel (port 80)
  Serial.printf("Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);

  btStop();   // HomeSpan/HAP is WiFi-only — shut down the BT/BLE controller to free current+power

  prefs.begin("midea", false);                   // RW so first boot creates the
  g_pinsSwapped = prefs.getBool("swap", false);  // namespace (avoids NOT_FOUND log)
  prefs.end();
  beginMidea();
  ac.setStream(&MideaSerial);
  ac.setup();                   // begins the query/handshake with the AC mainboard

  WiFi.onEvent(onWifiEvent);    // register BEFORE WiFi starts, or no [wifi] logs

  // CLEAN BASELINE (reassessing from scratch): let HomeSpan do a plain WiFi.begin(ssid,pwd).
  // The only tweak is all-channel scan + connect-by-strongest-signal, which is standard for a
  // multi-AP SSID. No region override, no explicit-BSSID / persistence hacks — this isolates
  // whether those accumulated workarounds were themselves breaking the connection.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Auto-tune WiFi TX power BEFORE HomeSpan connects: read the stored creds and find the highest
  // level that authenticates (first boot walks 19.5 -> 2 dBm; the winner is saved for fast reboots).
  {
    char wssid[MAX_SSID + 1] = {0}, wpwd[MAX_PWD + 1] = {0};
    Preferences wp;
    if (wp.begin("WIFI", true)) {                 // HomeSpan's WiFi-cred namespace/key
      struct { char s[MAX_SSID + 1]; char p[MAX_PWD + 1]; } wd = {};
      wp.getBytes("WIFIDATA", &wd, sizeof(wd)); wp.end();
      memcpy(wssid, wd.s, sizeof(wssid)); memcpy(wpwd, wd.p, sizeof(wpwd));
    }
    if (wssid[0]) autoTuneTx(wssid, wpwd);        // connects at the best level; HomeSpan keeps it
  }

  homeSpan.setLogLevel(1);
  homeSpan.setStatusPixel(8);   // onboard WS2812 RGB (DevKitM-1 GPIO8): shows WiFi/pairing
                                // state at a glance — blinking=searching, steady=connected
  homeSpan.setPortNum(1201);    // move HAP off :80 so the web UI can own the default
                                // port; HomeKit still finds it — port is advertised via mDNS
  homeSpan.setWifiCallback(onWifiUp);
  homeSpan.enableOTA();         // OTA reflash once it's on WiFi
  homeSpan.begin(Category::Bridges, "Midea AC Bridge");

  // Bridge with two accessories: the AC (all controls grouped into ONE device) and a SEPARATE
  // Outdoor temperature sensor — so the sensor can live in its own room ("Outside") and be picked
  // cleanly as an automation trigger. HomeSpan requires the FIRST accessory of a bridge to be the
  // bridge itself (AccessoryInformation only); Home hides it, showing just the two below.
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Midea");
      new Characteristic::Model("mideaapple");
      new Characteristic::Name("Midea AC Bridge");

  // Accessory 1 — the AC: one tile, all controls. On HomeSpan 2.x each service carries a
  // ConfiguredName, so Home labels every tile ("Dry", "Fan Only", "Fan Auto") inside this device.
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Midea");
      new Characteristic::Model("mideaapple");
      new Characteristic::Name("Midea AC");
    new MideaHeaterCooler(&ac);
    new MideaModeSwitch(&ac, Mode::MODE_FAN_ONLY, "Fan Only");   // modes HeaterCooler can't hold;
    new MideaModeSwitch(&ac, Mode::MODE_DRY, "Dry");            // tile order = creation order:
    new MideaFanAutoSwitch(&ac);                               // Fan Only, Dry, Fan Auto

  // Accessory 2 — outdoor temp as its OWN device (assign it to an "Outside" room in Home).
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Midea");
      new Characteristic::Model("mideaapple");
      new Characteristic::Name("Outdoor");
    new MideaOutdoorTemp(&ac);
}

void loop() {
  homeSpan.poll();
  ac.loop();
  webui::webLoop();
}

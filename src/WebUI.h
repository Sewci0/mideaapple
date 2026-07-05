#pragma once
#include <WebServer.h>
#include <Appliance/AirConditioner/AirConditioner.h>

// Runtime UART pin-swap toggle (implemented in main.cpp).
bool mideaGetSwap();
void mideaSetSwap(bool swapped);
const char *mideaGetTxLabel();   // current auto-tuned WiFi TX power (dBm), read-only
dudanov::midea::ac::Mode mideaLastMode();  // last non-OFF AC mode — shown while the AC is powered off

// ---------------------------------------------------------------------------
//  Optional local web control panel (port 80 — HAP is moved to :1201 in main.cpp).
//  It writes to the SAME
//  AirConditioner object HomeKit uses, so HomeKit, the web page, and the AC's
//  own remote all stay in sync — the AC is the single source of truth, and
//  MideaHeaterCooler::loop() mirrors its state back into HomeKit at 1 Hz.
// ---------------------------------------------------------------------------

namespace webui {

using dudanov::midea::ac::AirConditioner;
using dudanov::midea::ac::Mode;
using dudanov::midea::ac::FanMode;
using dudanov::midea::ac::SwingMode;
using dudanov::midea::ac::Control;

static WebServer      server(80);     // HAP moved to :1201 (main.cpp) so the panel owns :80
static AirConditioner *g_ac = nullptr;
static bool           started = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Midea AC</title><style>
body{font-family:system-ui,sans-serif;margin:0;background:#0b0f14;color:#e6edf3}
.wrap{max-width:420px;margin:0 auto;padding:20px}h1{font-size:18px}
.card{background:#161b22;border:1px solid #21262d;border-radius:12px;padding:16px;margin:12px 0}
.row{display:flex;justify-content:space-between;align-items:center;margin:10px 0}
button,select{background:#21262d;color:#e6edf3;border:1px solid #30363d;border-radius:8px;padding:8px 12px;font-size:15px}
button.on{background:#238636;border-color:#238636}input[type=range]{width:100%}
input[type=range]:disabled{opacity:.35}
button:disabled,select:disabled{opacity:.4;cursor:not-allowed}
.big{font-size:34px;font-weight:700}.muted{color:#8b949e;font-size:13px}</style></head><body><div class=wrap>
<h1>Midea AC</h1>
<div class=card><div class=row><span class=muted>Indoor</span><span class=big id=indoor>--</span></div>
<div class=row><span class=muted>Outdoor</span><span id=outdoor>--</span></div></div>
<div class=card>
<div class=row><b>Power</b><button id=power onclick="tog()">--</button></div>
<div class=row><b>Mode</b><select id=mode onchange="set('mode',this.value)">
<option value=auto>Auto</option><option value=cool>Cool</option><option value=heat>Heat</option>
<option value=dry>Dry</option><option value=fan_only>Fan</option></select></div>
<div class=row><b>Target</b><span class=big><span id=tval>22</span>&deg;</span></div>
<input type=range min=16 max=30 step=0.5 id=temp oninput="tval.textContent=this.value" onchange="set('temp',this.value)">
<div class=row><b>Fan</b><select id=fan onchange="set('fan',this.value)">
<option value=auto>Auto</option><option value=low>Low</option><option value=medium>Medium</option>
<option value=high>High</option></select></div>
<div class=row><b>Swing</b><button id=swing onclick="tsw()">--</button></div></div>
<p class=muted>mideaapple &middot; native HomeKit + web</p></div><script>
let S={},pend={},dbPower=null,dbCount=0,dispPower='0';const j=u=>fetch(u).then(r=>r.json());
// state value in the string form /set uses, per control
function sv(k,s){return k=='power'?(s.power?'1':'0'):k=='temp'?String(s.target):k=='swing'?s.swing:s[k];}
// resolved display value: hold a just-set (optimistic) value until the AC confirms it, or ~10s passes
function rv(k,s){let real=sv(k,s),p=pend[k];if(p){if(real==p.v||Date.now()>=p.t)delete pend[k];else return p.v;}return real;}
// power: like rv, but after the optimistic hold, require 2 consecutive agreeing polls before flipping
// (debounces the AC's mid-transition report flicker so the button doesn't blink on/off).
function rvPower(s){let real=sv('power',s),p=pend.power;
if(p){if(real==p.v){delete pend.power;dbPower=real;dbCount=0;return real;}if(Date.now()>=p.t)delete pend.power;else return p.v;}
if(dbPower===null||real==dbPower){dbCount=0;dbPower=real;return real;}
if(++dbCount>=2){dbCount=0;dbPower=real;return real;}return dbPower;}
function set(k,v){pend[k]={v:String(v),t:Date.now()+10000};if(S.indoor!==undefined)render(S);j('/set?'+k+'='+encodeURIComponent(v));}
function tog(){set('power',dispPower=='1'?0:1)}
function tsw(){let c=rv('swing',S);set('swing',c=='off'?'vertical':'off')}
function render(s){S=s;if(s.indoor===undefined)return;
indoor.textContent=s.indoor.toFixed(1)+'°';
outdoor.textContent=(s.outdoor?s.outdoor.toFixed(1):'--')+'°';
dispPower=rvPower(s);power.textContent=dispPower=='1'?'ON':'OFF';power.className=dispPower=='1'?'on':'';
let md=rv('mode',s);mode.value=md;fan.value=rv('fan',s);
let sw=rv('swing',s);swing.textContent=sw=='off'?'OFF':'ON';swing.className=sw=='off'?'':'on';
let off=dispPower!='1';fan.disabled=off;swing.disabled=off;   // fan/swing are ignored while the AC is off
temp.disabled=(md=='fan_only');
if(document.activeElement!=temp){let t=rv('temp',s);temp.value=t;tval.textContent=md=='fan_only'?'--':t}}
const poll=()=>j('/state').then(render).catch(()=>{});setInterval(poll,1500);poll();
</script></body></html>)HTML";

static const char *modeStr(Mode m) {
  switch (m) {
    case Mode::MODE_COOL:     return "cool";
    case Mode::MODE_HEAT:     return "heat";
    case Mode::MODE_DRY:      return "dry";
    case Mode::MODE_FAN_ONLY: return "fan_only";
    case Mode::MODE_AUTO:     return "auto";
    default:                  return "off";
  }
}
static const char *fanStr(FanMode f) {
  switch (f) {
    case FanMode::FAN_LOW:    return "low";
    case FanMode::FAN_MEDIUM: return "medium";
    case FanMode::FAN_HIGH:   return "high";
    case FanMode::FAN_SILENT: return "silent";
    case FanMode::FAN_TURBO:  return "turbo";
    default:                  return "auto";
  }
}
static const char *swingStr(SwingMode s) {
  switch (s) {
    case SwingMode::SWING_VERTICAL:   return "vertical";
    case SwingMode::SWING_HORIZONTAL: return "horizontal";
    case SwingMode::SWING_BOTH:       return "both";
    default:                          return "off";
  }
}

static void handleState() {
  AirConditioner *ac = g_ac;
  String o = "{";
  o += "\"power\":";    o += ac->getPowerState() ? "true" : "false";
  Mode dispMode = ac->getPowerState() ? ac->getMode() : mideaLastMode();  // show last mode when off
  o += ",\"mode\":\"";  o += modeStr(dispMode);          o += "\"";
  o += ",\"target\":";  o += String(ac->getTargetTemp(), 1);
  o += ",\"indoor\":";  o += String(ac->getIndoorTemp(), 1);
  o += ",\"outdoor\":"; o += String(ac->getOutdoorTemp(), 1);
  o += ",\"fan\":\"";   o += fanStr(ac->getFanMode());   o += "\"";
  o += ",\"swing\":\""; o += swingStr(ac->getSwingMode()); o += "\"";
  o += ",\"swap\":";    o += mideaGetSwap() ? "true" : "false";
  o += ",\"bssid\":\""; o += WiFi.BSSIDstr();           o += "\"";   // which AP we're on
  o += ",\"rssi\":";    o += String(WiFi.RSSI());                     // signal to that AP (dBm)
  o += ",\"chan\":";    o += String(WiFi.channel());
  o += ",\"tx\":";      o += mideaGetTxLabel();                     // auto-tuned TX power (dBm)
  o += "}";
  server.send(200, "application/json", o);
}

// /scan — one-shot WiFi scan: every visible AP (incl. all same-SSID BSSIDs) with
// channel + RSSI as the CHIP hears it. Briefly interrupts the link while scanning.
static void handleScan() {
  int n = WiFi.scanNetworks();
  String o = "[";
  for (int i = 0; i < n; i++) {
    if (i) o += ",";
    o += "{\"ssid\":\"";  o += WiFi.SSID(i);       o += "\"";
    o += ",\"bssid\":\""; o += WiFi.BSSIDstr(i);    o += "\"";
    o += ",\"ch\":";      o += String(WiFi.channel(i));
    o += ",\"rssi\":";    o += String(WiFi.RSSI(i)); o += "}";
  }
  o += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", o);
}

static void handleSet() {
  AirConditioner *ac = g_ac;
  Control c;                                   // Optional<> fields: unset = no change
  if (server.hasArg("power")) ac->setPowerState(server.arg("power").toInt() != 0);
  if (server.hasArg("temp"))  c.targetTemp = server.arg("temp").toFloat();
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if      (m == "cool")     c.mode = Mode::MODE_COOL;
    else if (m == "heat")     c.mode = Mode::MODE_HEAT;
    else if (m == "dry")      c.mode = Mode::MODE_DRY;
    else if (m == "fan_only") c.mode = Mode::MODE_FAN_ONLY;
    else                      c.mode = Mode::MODE_AUTO;
  }
  if (server.hasArg("fan")) {
    String f = server.arg("fan");
    if      (f == "low")    c.fanMode = FanMode::FAN_LOW;
    else if (f == "medium") c.fanMode = FanMode::FAN_MEDIUM;
    else if (f == "high")   c.fanMode = FanMode::FAN_HIGH;
    else                    c.fanMode = FanMode::FAN_AUTO;
  }
  if (server.hasArg("swing")) {   // this AC has vertical louvers only
    c.swingMode = (server.arg("swing") == "vertical") ? SwingMode::SWING_VERTICAL
                                                      : SwingMode::SWING_OFF;
  }
  if (server.hasArg("swap")) mideaSetSwap(server.arg("swap").toInt() != 0);
  ac->control(c);
  handleState();   // echo current state back to the page
}

inline void webBegin(AirConditioner *ac) {
  if (started) return;
  g_ac = ac;
  server.on("/",      []() { server.send_P(200, "text/html", PAGE); });
  server.on("/state", handleState);
  server.on("/set",   handleSet);
  server.on("/scan",  handleScan);
  server.begin();
  started = true;
}

inline void webLoop() { if (started) server.handleClient(); }

}  // namespace webui

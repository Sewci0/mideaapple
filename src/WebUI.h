#pragma once
#include <WebServer.h>
#include <Appliance/AirConditioner/AirConditioner.h>

// Runtime UART pin-swap toggle (implemented in main.cpp).
bool mideaGetSwap();
void mideaSetSwap(bool swapped);

// ---------------------------------------------------------------------------
//  Optional local web control panel (port 80). It writes to the SAME
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

static WebServer      server(80);
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
<div class=card><div class=row><b>UART pins</b><button id=pins onclick="tpins()">--</button></div>
<p class=muted>Flip if the AC doesn't respond - swaps RX/TX (GPIO4/5). Saved across reboots.</p></div>
<p class=muted>mideaapple &middot; native HomeKit + web</p></div><script>
let S={};const j=u=>fetch(u).then(r=>r.json());
function set(k,v){j('/set?'+k+'='+encodeURIComponent(v)).then(render)}
function tog(){set('power',S.power?0:1)}function tsw(){set('swing',S.swing=='off'?'vertical':'off')}
function tpins(){set('swap',S.swap?0:1)}
function render(s){S=s;indoor.textContent=s.indoor.toFixed(1)+'°';
outdoor.textContent=(s.outdoor?s.outdoor.toFixed(1):'--')+'°';
power.textContent=s.power?'ON':'OFF';power.className=s.power?'on':'';
mode.value=s.mode;fan.value=s.fan;
if(document.activeElement!=temp){temp.value=s.target;tval.textContent=s.target}
swing.textContent=s.swing=='off'?'OFF':'ON';swing.className=s.swing=='off'?'':'on';
pins.textContent=s.swap?'B (swapped)':'A (normal)';}
const poll=()=>j('/state').then(render).catch(()=>{});setInterval(poll,2000);poll();
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
  o += ",\"mode\":\"";  o += modeStr(ac->getMode());     o += "\"";
  o += ",\"target\":";  o += String(ac->getTargetTemp(), 1);
  o += ",\"indoor\":";  o += String(ac->getIndoorTemp(), 1);
  o += ",\"outdoor\":"; o += String(ac->getOutdoorTemp(), 1);
  o += ",\"fan\":\"";   o += fanStr(ac->getFanMode());   o += "\"";
  o += ",\"swing\":\""; o += swingStr(ac->getSwingMode()); o += "\"";
  o += ",\"swap\":";    o += mideaGetSwap() ? "true" : "false";
  o += "}";
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
  if (server.hasArg("swing")) {
    c.swingMode = (server.arg("swing") == "off") ? SwingMode::SWING_OFF
                                                 : SwingMode::SWING_VERTICAL;
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
  server.begin();
  started = true;
}

inline void webLoop() { if (started) server.handleClient(); }

}  // namespace webui

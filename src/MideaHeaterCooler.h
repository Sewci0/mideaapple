#pragma once
#include "HomeSpan.h"
#include <Appliance/AirConditioner/AirConditioner.h>

// ---------------------------------------------------------------------------
//  Midea AC  <->  HomeKit, running natively on the ESP32-C3.
//  API verified against dudanov/MideaUART 1.1.9.
//
//  HeaterCooler models Auto/Heat/Cool + temp + fan speed + swing. Midea's Dry
//  and Fan-only modes (and fan Auto) have no HeaterCooler equivalent, so they're
//  exposed as extra Switch services; outdoor temp as its own TemperatureSensor.
// ---------------------------------------------------------------------------

using dudanov::midea::ac::AirConditioner;
using dudanov::midea::ac::Mode;
using dudanov::midea::ac::FanMode;
using dudanov::midea::ac::SwingMode;
using dudanov::midea::ac::Control;

// HomeKit TargetHeaterCoolerState
#define HK_AUTO 0
#define HK_HEAT 1
#define HK_COOL 2
// HomeKit CurrentHeaterCoolerState
#define HK_INACTIVE 0
#define HK_IDLE     1
#define HK_HEATING  2
#define HK_COOLING  3

// After any HomeKit write, pause ALL mirroring briefly so the 1 Hz sync can't push the AC's
// not-yet-applied state back over the value the user just set (the snap-back glitch). Shared across
// every service so a write to one isn't clobbered by another service's mirror loop.
inline uint32_t g_acHold = 0;
static inline void acHold()       { g_acHold = millis() + 2500; }
static inline bool acMirrorHeld() { return millis() < g_acHold; }

static FanMode fanFromPercent(int pct) {
  if (pct <= 33) return FanMode::FAN_LOW;
  if (pct <= 66) return FanMode::FAN_MEDIUM;
  return FanMode::FAN_HIGH;
}
static int fanToPercent(FanMode f) {
  if (f == FanMode::FAN_LOW)    return 33;
  if (f == FanMode::FAN_MEDIUM) return 66;
  if (f == FanMode::FAN_HIGH)   return 100;
  return -1;   // AUTO/SILENT/TURBO don't map onto the 3-speed slider
}

struct MideaHeaterCooler : Service::HeaterCooler {
  AirConditioner *ac;

  Characteristic::Active                      active{0, true};
  Characteristic::CurrentTemperature          curTemp{22, true};
  Characteristic::CurrentHeaterCoolerState    curState{HK_INACTIVE, true};
  Characteristic::TargetHeaterCoolerState     tgtState{HK_COOL, true};
  Characteristic::CoolingThresholdTemperature coolTo{24, true};
  Characteristic::HeatingThresholdTemperature heatTo{20, true};
  Characteristic::RotationSpeed               fan{66, true};
  Characteristic::SwingMode                   swing{0, true};

  uint32_t lastPush = 0;

  explicit MideaHeaterCooler(AirConditioner *ac) : Service::HeaterCooler(), ac(ac) {
    tgtState.setValidValues(3, HK_AUTO, HK_HEAT, HK_COOL);   // no Dry/Fan in HeaterCooler
    coolTo.setRange(17, 30, 0.5);
    heatTo.setRange(16, 30, 0.5);
    curTemp.setRange(-20, 50);
    fan.setRange(0, 100, 33);                                // 3 Midea speeds (low/med/high)
  }

  // ---- HomeKit write  ->  Midea -----------------------------------------
  boolean update() override {
    Control ctl;                                  // Optional<> fields: unset = no change

    if (tgtState.updated()) {
      switch (tgtState.getNewVal()) {
        case HK_HEAT: ctl.mode = Mode::MODE_HEAT; break;
        case HK_COOL: ctl.mode = Mode::MODE_COOL; break;
        default:      ctl.mode = Mode::MODE_AUTO; break;   // HK_AUTO
      }
    }
    if (coolTo.updated()) ctl.targetTemp = coolTo.getNewVal<float>();
    if (heatTo.updated()) ctl.targetTemp = heatTo.getNewVal<float>();
    if (fan.updated())    ctl.fanMode   = fanFromPercent(fan.getNewVal());
    if (swing.updated())  ctl.swingMode = swing.getNewVal() ? SwingMode::SWING_VERTICAL
                                                            : SwingMode::SWING_OFF;

    ac->setPowerState(active.getNewVal() != 0);   // power on/off is separate from mode
    ac->control(ctl);                             // send the delta
    acHold();                                     // don't mirror stale state back over this write
    return true;
  }

  // ---- Midea state  ->  HomeKit (1 Hz push) -----------------------------
  void loop() override {
    if (millis() - lastPush < 1000) return;
    lastPush = millis();
    if (acMirrorHeld()) return;                // just wrote — let the AC apply before mirroring

    float indoor = ac->getIndoorTemp();
    if (indoor > 0) curTemp.setVal(indoor);    // 0 = AC hasn't reported yet

    bool power = ac->getPowerState();
    active.setVal(power ? 1 : 0);

    float sp = ac->getTargetTemp();            // 0 until the AC reports a status
    if (sp >= 17 && sp <= 30) coolTo.setVal(sp);   // guard against out-of-range
    if (sp >= 16 && sp <= 30) heatTo.setVal(sp);   // setVal (esp. the "no data" 0)

    Mode m = ac->getMode();
    if (!power)                    curState.setVal(HK_INACTIVE);
    else if (m == Mode::MODE_HEAT) curState.setVal(HK_HEATING);
    else if (m == Mode::MODE_COOL) curState.setVal(HK_COOLING);
    else                           curState.setVal(HK_IDLE);

    if (m == Mode::MODE_HEAT)      tgtState.setVal(HK_HEAT);
    else if (m == Mode::MODE_COOL) tgtState.setVal(HK_COOL);
    else                           tgtState.setVal(HK_AUTO);   // Dry/Fan-only fall back to Auto here

    int pct = fanToPercent(ac->getFanMode());  // mirror fan/swing back (so remote changes show up);
    if (pct >= 0) fan.setVal(pct);             // AUTO can't map to the slider -> shown via Fan Auto
    swing.setVal(ac->getSwingMode() != SwingMode::SWING_OFF ? 1 : 0);
  }
};

// Outdoor temperature as its own HomeKit sensor tile.
struct MideaOutdoorTemp : Service::TemperatureSensor {
  AirConditioner *ac;
  Characteristic::CurrentTemperature temp{0, true};
  uint32_t lastPush = 0;

  explicit MideaOutdoorTemp(AirConditioner *ac) : Service::TemperatureSensor(), ac(ac) {
    temp.setRange(-50, 80);
    new Characteristic::ConfiguredName("Outdoor");
  }
  void loop() override {
    if (millis() - lastPush < 2000) return;
    lastPush = millis();
    float o = ac->getOutdoorTemp();
    if (o != 0) temp.setVal(o);                // 0 = no reading yet
  }
};

// A switch for a Midea mode the HeaterCooler can't express (Dry, Fan-only). On -> that mode;
// Off -> revert to Cool. It mirrors the AC's actual mode, so the mode switches stay exclusive.
struct MideaModeSwitch : Service::Switch {
  AirConditioner *ac;
  Mode mode;
  Characteristic::On on{false, true};
  uint32_t lastPush = 0;

  MideaModeSwitch(AirConditioner *ac, Mode mode, const char *name)
      : Service::Switch(), ac(ac), mode(mode) {
    new Characteristic::ConfiguredName(name);
  }
  boolean update() override {
    Control ctl;
    if (on.getNewVal()) { ctl.mode = mode; ac->setPowerState(true); }
    else                  ctl.mode = Mode::MODE_COOL;   // leaving this mode -> Cool
    ac->control(ctl);
    acHold();
    return true;
  }
  void loop() override {
    if (millis() - lastPush < 1000) return;
    lastPush = millis();
    if (acMirrorHeld()) return;
    on.setVal(ac->getPowerState() && ac->getMode() == mode);
  }
};

// Switch for fan Auto (the HeaterCooler speed slider can't show "auto"). Off -> a manual speed.
struct MideaFanAutoSwitch : Service::Switch {
  AirConditioner *ac;
  Characteristic::On on{false, true};
  uint32_t lastPush = 0;

  explicit MideaFanAutoSwitch(AirConditioner *ac) : Service::Switch(), ac(ac) {
    new Characteristic::ConfiguredName("Fan Auto");
  }
  boolean update() override {
    Control ctl;
    ctl.fanMode = on.getNewVal() ? FanMode::FAN_AUTO : FanMode::FAN_MEDIUM;
    ac->control(ctl);
    acHold();
    return true;
  }
  void loop() override {
    if (millis() - lastPush < 1000) return;
    lastPush = millis();
    if (acMirrorHeld()) return;
    on.setVal(ac->getPowerState() && ac->getFanMode() == FanMode::FAN_AUTO);   // off when AC is off,
  }                                                                            // so it doesn't make
};                                                                             // the device read "on"

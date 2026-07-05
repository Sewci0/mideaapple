#pragma once
#include "HomeSpan.h"
#include <Appliance/AirConditioner/AirConditioner.h>

// ---------------------------------------------------------------------------
//  Midea AC  <->  HomeKit HeaterCooler, running natively on the ESP32-C3.
//  API verified against dudanov/MideaUART 1.1.9.
//
//  HeaterCooler can only express Auto/Heat/Cool. Midea's Dry and Fan-only modes
//  have no HeaterCooler equivalent — add a separate Fan service / mode Switch
//  later if you want them exposed.
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

struct MideaHeaterCooler : Service::HeaterCooler {
  AirConditioner *ac;

  // Required characteristics for a HeaterCooler
  Characteristic::Active                      active{0, true};
  Characteristic::CurrentTemperature          curTemp{22, true};
  Characteristic::CurrentHeaterCoolerState    curState{HK_INACTIVE, true};
  Characteristic::TargetHeaterCoolerState     tgtState{HK_COOL, true};
  // Setpoints (Midea has one setpoint; both HK thresholds drive it)
  Characteristic::CoolingThresholdTemperature coolTo{24, true};
  Characteristic::HeatingThresholdTemperature heatTo{20, true};
  // Optional niceties
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
    return true;
  }

  // ---- Midea state  ->  HomeKit (1 Hz push) -----------------------------
  void loop() override {
    if (millis() - lastPush < 1000) return;
    lastPush = millis();

    float indoor = ac->getIndoorTemp();
    if (indoor > 0) curTemp.setVal(indoor);   // 0 = AC hasn't reported yet

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
    else                           tgtState.setVal(HK_AUTO);
  }

  static FanMode fanFromPercent(int pct) {
    if (pct <= 33) return FanMode::FAN_LOW;
    if (pct <= 66) return FanMode::FAN_MEDIUM;
    return FanMode::FAN_HIGH;
  }
};

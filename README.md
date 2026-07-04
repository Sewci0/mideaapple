# mideaapple

[![build](https://github.com/Sewci0/mideaapple/actions/workflows/build.yml/badge.svg)](https://github.com/Sewci0/mideaapple/actions/workflows/build.yml)

Native **HomeKit** firmware for a Midea air conditioner, running directly on an
**ESP32-C3** — no Home Assistant, no ESPHome, no Pi bridge. The AC appears in the
Apple Home app on its own.

It replaces the ESPHome `midea` setup with two libraries:

| Layer        | Library                | Job                                            |
|--------------|------------------------|------------------------------------------------|
| HomeKit      | [HomeSpan](https://github.com/HomeSpan/HomeSpan) | HAP pairing, mDNS, the `HeaterCooler` service |
| AC protocol  | [dudanov/MideaUART](https://github.com/dudanov/MideaUART) | the UART frames ESPHome's `midea` component wraps |

`src/MideaHeaterCooler.h` is the glue: HomeKit characteristics ⇄ Midea state.

## Will it work with my AC? (Midea family & rebadges)

MideaUART speaks the protocol shared across Midea Group's own brands **and** the
many third-party ACs Midea OEM-manufactures. If your unit has the UART "WiFi
dongle" bay (and isn't IR-only), it's likely compatible — though per-model
support varies, and some newer units use a different protocol.

**Midea Group brands** (owned/sister brands): **Midea**, **COLMO**, **Toshiba**
(home-appliance division), **Comfee'**, **WAHIN / Hualing**, **Little Swan**,
**Bugu**, **Clivet**, **Eureka**. (Plus non-appliance arms: KUKA robotics,
GMCC & Welling compressors/motors.)

**Rebadged / OEM-compatible brands** (same Midea UART protocol — non-exhaustive):
ActronAir, Airfel, Alpine, Artel, Ballu, Beko, Bluestar, Bosch, Bryant, Carrier,
Danby, Electrolux, Idea, Inventor, Kaisai, KeepRite, Kentatsu, Keystone, KoolKing,
Lennox, MaxiCool, Mirage, Mitsui, Mr.Cool, Neoclima, Nippon, Olimpia Splendid,
Olmo, Pioneer, Pitsos, Qlima, Remko, Rotenso, RoyalClima, Senville, Simando,
Vestfrost, Zanussi.

Sources: [Midea Group brand portfolio](https://en.wikipedia.org/wiki/Midea_Group) ·
[SLWF-01 compatible-brand list](https://smlight.tech/product/slwf-01) ·
[ESPHome known-working models thread](https://community.home-assistant.io/t/midea-a-c-and-esphome-component-list-of-all-known-working-models-and-manufacturers/320784)

## The AC's "USB" port (⚠ read before plugging anything in)

The dongle bay looks like a USB-A port, but on Midea ACs it is **not real USB** —
it's the WiFi-dongle interface: **5 V power on VBUS and a 5 V TTL UART (9600 8N1)
on the data pins.** The UART is **5 V logic** (per the ESPHome midea docs — it
"does not appear to work with 3.3 V"), and ESP32-C3 GPIOs are **not 5 V tolerant**,
so a **level shifter on the data lines is mandatory**. Typical USB-A pinout
(⚠ still meter your specific unit to confirm):

```
USB-A pin        signal        ESP32-C3
  1  VBUS  ----  +5V     ----> 5V / VIN     (powers the board)
  2  D-    ----  UART ?  ----> GPIO4 (RX)   MIDEA_RX_PIN   <- AC TX
  3  D+    ----  UART ?  ----> GPIO5 (TX)   MIDEA_TX_PIN   -> AC RX
  4  GND   ----  GND     ----> GND
```

D-/D+ carry the two UART lines; which is TX vs RX varies — if it doesn't talk,
swap GPIO4/5. Route both through the level shifter (HV = AC 5 V side, LV = C3
3V3, common GND) — **never** land a 5 V line on a C3 GPIO directly.

### Power — the 300 mA limit

That port only sources ~**300 mA**. WiFi TX can spike past that and brown-out the
board, so this firmware caps TX power (`WIFI_POWER_8_5dBm` in `main.cpp`). Also
solder a **≥470 µF electrolytic across 5V→GND** right at the board to absorb the
transients. If it still resets under load, lower TX power further or feed the
board from a separate 5 V source.

## Build & flash

PlatformIO lives in a dedicated venv (Homebrew's Python is PEP-668
externally-managed, so `pip --user` is blocked; use python3.13, not broken 3.14):

```sh
# one-time
python3.13 -m venv ~/.venvs/pio
~/.venvs/pio/bin/pip install -U platformio
alias pio=~/.venvs/pio/bin/pio        # optional convenience

cd ~/projects/mideaapple
~/.venvs/pio/bin/pio run              # compile (also fetches HomeSpan + MideaUART)
~/.venvs/pio/bin/pio run -t upload    # flash over the C3's USB-C (bench)
~/.venvs/pio/bin/pio device monitor   # serial log @115200
```

**Toolchain notes:**
- **Apple Silicon Macs:** the ESP32 RISC-V toolchain is an x86_64 binary — install
  Rosetta 2 once (`softwareupdate --install-rosetta --agree-to-license`) or the
  build fails with `riscv32-esp-elf-g++: Bad CPU type in executable`.
- **HomeSpan is pinned to 1.9.1** (`platformio.ini`): the official PlatformIO
  espressif32 platform ships arduino-esp32 core 2.0.17, but HomeSpan 2.x needs
  core ≥ 3.3.0. To move to HomeSpan 2.x, switch `platform` to the pioarduino fork.

## Pair with HomeKit

1. First boot has no WiFi — provision it: in the serial monitor type `W` and
   follow HomeSpan's prompt (or use its temporary setup hotspot).
2. Home app → **Add Accessory** → **More options** → enter code **466-37-726**
   (HomeSpan's default; change it with the `S` serial command).

## Update over the air (OTA)

After the first USB flash, updates can go over WiFi (`enableOTA()` in `main.cpp`,
default password `homespan-ota`). Grab the device IP from the serial log, then:

```sh
~/.venvs/pio/bin/pio run -e ota -t upload --upload-port 192.168.x.y
```

Change the OTA password with the `O` serial command, or hardcode it via
`enableOTA("your-password")`.

## Web control panel

Besides HomeKit, the firmware serves a small control page on **port 80**
(`src/WebUI.h`). After it joins WiFi it prints its address to the serial log:

```
Web UI: http://192.168.x.y/
```

The page shows indoor/outdoor temp and controls power, mode (incl. Dry/Fan that
HomeKit's HeaterCooler can't show), target temp, fan, and swing. It writes to the
same `AirConditioner` object HomeKit uses, so the Home app, the web page, and the
AC's own remote stay in sync. `GET /state` returns JSON; `GET /set?<k>=<v>` applies
a change (`power,temp,mode,fan,swing`). It's HTTP + unauthenticated — LAN only.

## Troubleshooting

**Serial commands** (type `?` in the monitor for the full HomeSpan menu):

| Key | Action |
|-----|--------|
| `W` / `X` | set / erase WiFi credentials |
| `S` | change the HomeKit pairing code |
| `O` | change the OTA password |
| `U` | unpair (delete all HomeKit controllers) |
| `H` | delete pairing data + device ID |
| `F` / `R` | factory reset / reboot |

**Other issues:**
- **Won't enter flashing mode:** hold `BOOT`, tap `RST`, release `BOOT`, retry.
- **AC doesn't respond:** swap `MIDEA_RX_PIN`/`MIDEA_TX_PIN` (GPIO4↔5) — TX/RX
  orientation varies by model; confirm the level shifter is powered on both rails
  (HV = AC 5 V, LV = C3 3V3, common GND).
- **Random reboots during WiFi use:** brown-out on the 300 mA rail — lower TX
  power in `onWifiUp()` (`WIFI_POWER_5dBm` / `WIFI_POWER_2dBm`) or add a bulk cap.

## Status / TODO

- [x] Board: ESP32-C3 (small + low avg draw for the 300 mA bay).
- [x] Firmware builds green: HomeKit HeaterCooler + web control panel. API
      verified against real MideaUART/HomeSpan headers (~64% flash, 16% RAM).
- [x] Dry / Fan-only modes exposed via the web UI (HeaterCooler can't show them).
- [ ] Meter the USB-A pins: confirm 5 V on VBUS, UART TTL is 3.3 V (not 5 V),
      and which data pin is the AC's TX.
- [ ] Confirm AC model has this UART dongle bay (not IR-only) & MideaUART support.
- [ ] Add the ≥470 µF bulk cap; verify no brown-out during WiFi TX.
- [ ] Flash (`pio run -t upload`), pair (466-37-726), test against the real AC;
      swap GPIO4/5 if the AC doesn't respond (TX/RX orientation).

## Credits & license

`mideaapple` is MIT-licensed — see [LICENSE](LICENSE). It's the glue between two
MIT-licensed libraries that do all the real protocol/HAP work:

- **[HomeSpan](https://github.com/HomeSpan/HomeSpan)** — HomeKit Accessory
  Protocol for the Arduino-ESP32, © Gregg E. Berman.
- **[dudanov/MideaUART](https://github.com/dudanov/MideaUART)** — the Midea UART
  protocol (also the basis of ESPHome's `midea` component), © Sergey Dudanov.

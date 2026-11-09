# AGENTS.md

Firmware for an LCTech **ESP32-WROOM-32E 16-channel relay board** (fireplace + electric heating). Relays are **74HC595**, not ESP32 GPIOs. Do not treat this as a SuperMini / C3 project.

Sister folder (USB-TTL bridge only): `../esp32c3-usb-ttl-bridge`. Never flash `esp32c3` firmware here, or this `esp32dev` firmware onto the C3.

## Control logic (source of truth)

Fireplace is the **default**. There is no user mode switch. Electric is automatic backup only.

Thresholds on the DS18B20 at the fireplace pipe:

| Temp | Latch |
|---|---|
| ≥ 75 °C | `fireDump = true`, `dumpPumps = true` |
| ≤ 55 °C | `dumpPumps = false` (session stays) |
| ≤ 35 °C | `fireDump = false` |

Outputs:

- **fireDump** (fireplace session, 75 °C → 35 °C): valve = fireplace, heater OFF. Pumps: both ON if `dumpPumps`, else **both OFF** (the 55–35 °C band). Ignore thermostats for pumps.
- **Electric**: only if a thermostat is calling **and** (`T ≤ 35 °C` or sensor missing). Valve = electric, heater ON. Pump1 = TSTAT1, pump2 = TSTAT2.
- **Else** (default): valve = fireplace, heater OFF. Pump1 = TSTAT1, pump2 = TSTAT2.

`call` = TSTAT1 OR TSTAT2 (heater enable only). Pumps are per-zone except during dump.

Keep this in `controlStep()` in `src/main.cpp`. Do not go back to a user-selectable ELECTRIC/FIREPLACE mode or gang both pumps to one OR in normal operation.

## Pins

| Function | Pin / channel |
|---|---|
| 595 SER / SRCLK / RCLK | GPIO14 / 13 / 12 |
| 595 OE (active LOW) / SRCLR (keep HIGH) | GPIO5 / 4 |
| DS18B20 DQ | GPIO23 (4.7 kΩ to 3.3 V) |
| TSTAT1 / TSTAT2 | GPIO16 / GPIO17, dry contact to GND, INPUT_PULLUP |
| Relay 1 / 2 / 3 / 4 | Pump1 / Pump2 / electric heater / valve (ON = fireplace) |

Do not drive relays from GPIO 25/26/27. Header silkscreen on this board is unreliable; 595 pins are already on the PCB.

## Wiring

See `wiring.png`. Mains only on relay COM/NO. Thermostats must not carry 230 V. Board power is **5 V DC** (relay coils are 5 V — not 12 V).

## Flash

Serial: C3 SuperMini bridge, 115200, `no_reset`. Relay board: hold IO0, tap EN, release IO0, then `pio run -t upload -e relay`. After upload tap EN (IO0 up). Details and OTA: `README.md`.

Do not set `use_1200bps_touch` or pass `--before=no_reset` via `upload_flags` (PlatformIO still appends `default_reset` and reboots the C3). Use `board_upload.before_reset` / `after_reset` = `no_reset`.

## Code

- `src/main.cpp` — 595 + heating state machine + boot click of relays 1–4
- `src/net.cpp` / `net.h` — Wi-Fi manager, status page, OTA
- `platformio.ini` — `env:relay` serial, `env:relay-ota` Wi-Fi

Status line: `T=  mode=FIRE_DUMP|ELECTRIC|FIREPLACE  t1= t2=  valve=FP|EL  heat=  p1=  p2=  wifi=`

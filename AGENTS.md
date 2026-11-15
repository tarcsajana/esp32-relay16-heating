# AGENTS.md

Firmware for an LCTech **ESP32-WROOM-32E 16-channel relay board** (fireplace + electric heating). Relays are **74HC595**, not ESP32 GPIOs. Do not treat this as a SuperMini / C3 project.

Sister folder (USB-TTL bridge only): `../esp32c3-usb-ttl-bridge`. Never flash `esp32c3` firmware here, or this `esp32dev` firmware onto the C3.

## Control logic (source of truth)

Fireplace is the **default**. The JC3248W535C remote can request AUTO / ELECTRIC / FIREPLACE over UDP. AUTO uses the pipe temperature directly (no 75 °C latch). `fireDump` still overrides a remote ELECTRIC or FIREPLACE request.

AUTO, pipe sensor only:

| Pipe temp | Valve | Heater | Pumps |
|---|---|---|---|
| < 35 °C | electric | ON, thermostat ignored | pump1 = TSTAT1, pump2 = TSTAT2 |
| 35–55 °C | fireplace | off | both OFF |
| > 55 °C | fireplace | off | both ON |

At 35 °C the valve is still fireplace. At 55 °C both pumps are still off. No sensor: electric only when a thermostat is calling, heater follows that call.

Remote ELECTRIC / FIREPLACE, when not in AUTO: the 75/55/35 latches still apply. ≥ 75 °C sets `fireDump` and `dumpPumps`. ≤ 55 °C clears `dumpPumps`. ≤ 35 °C clears `fireDump`. During `fireDump`: valve fireplace, heater off, both pumps ON only while `dumpPumps`.

Keep this in `controlStep()` in `src/main.cpp`. Do not gang both pumps to one OR in the under-35 band. Remote mode is UDP only (`req=` on the status line).

## Pins

| Function | Pin / channel |
|---|---|
| 595 SER / SRCLK / RCLK | GPIO14 / 13 / 12 |
| 595 OE (active LOW) / SRCLR (keep HIGH) | GPIO5 / 4 |
| DS18B20 DQ | GPIO23 (4.7 kΩ to 3.3 V). Up to 3 chips on this one bus. The pipe address is learned when it is the only chip connected, then kept. `a=` and `b=` are the other two and do not run the latches. |
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
- `src/udp_heat.cpp` — UDP 4210 listen, broadcast status to 4211, persist `req`
- `src/remote/` — JC3248W535C panel (Arduino_GFX, not LVGL). USB-C upload, `env:remote`. Setup page: IP, Wi-Fi reset, GitHub OTA (`remote.bin`)
- `SCREEN.md` — panel SoC, QSPI/touch pins, flash, UDP. Read before editing `src/remote/`
- `platformio.ini` — `env:relay` serial, `env:relay-ota` Wi-Fi, `env:remote` panel, `env:remote-ota` panel Wi-Fi

Status line: `T=  a=  b=  mode=FIRE_DUMP|ELECTRIC|FIREPLACE  req=AUTO|ELECTRIC|FIREPLACE  t1= t2=  valve=FP|EL  heat=  p1=  p2=  wifi=`

UDP: relay `:4210` ← `MODE AUTO|ELECTRIC|FIREPLACE` / `HEAT?`. Relay → remote `:4211` `HEAT T= a= b= mode= req= ...`. Remote shows red `No connection` if no packet for 30 s. `T` is the fireplace pipe. `a` and `b` are the other two sensors on GPIO23.

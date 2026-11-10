# JC3248W535C remote panel

Guition **JC3248W535C** (often `JC3248W535C_I_Y`). This is the heating UDP remote, not the LCTech relay board. Firmware: `src/remote/`, PlatformIO `env:remote`. Flash over the panel USB-C. Never through the C3 SuperMini.

## SoC and panel

| | |
|---|---|
| MCU | ESP32-S3-WROOM-1, dual-core, 240 MHz, Wi-Fi + BLE |
| Flash / PSRAM | **16 MB** / **8 MB** octal |
| Display | 3.5" IPS, **320 × 480** portrait (native) |
| Driver | **AXS15231B** (LCD + capacitive touch in one chip) |
| LCD bus | QSPI, ~40 MHz |
| Touch | I²C, capacitive. Datasheet address **0x38**; some units ACK **0x3B** — probe both |
| Power | 5 V on USB-C (or battery JST). Typical ~150 mA |
| Module size | 94.5 × 62.0 mm, visible 73.4 × 49.0 mm |

GUI: **Arduino_GFX** (`Arduino_ESP32QSPI` + `Arduino_AXS15231B` + `Arduino_Canvas` in PSRAM). Do not use TFT_eSPI. LVGL is unreliable here (no hardware rotation, partial refresh broken — full-frame `flush()` only).

PSRAM is required (`board_build.arduino.memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`). Canvas is ~300 KB. Pin Arduino_GFX **1.6.0** (1.6.1 has been a blank screen on this panel).

## QSPI LCD

| Signal | GPIO |
|---|---|
| CS | 45 |
| PCLK / SCK | 47 |
| D0 | 21 |
| D1 | 48 |
| D2 | 40 |
| D3 | 39 |
| TE | 38 (unused in this firmware) |
| BL | 1 (HIGH = on) |
| RST | none (NC) |

```cpp
Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
Arduino_Canvas(320, 480, panel, 0, 0, 0);
```

Call `gfx->flush()` after drawing. Native orientation is portrait 320×480.

## Touch I²C

| Signal | GPIO |
|---|---|
| SDA | 4 |
| SCL | 8 |
| INT | 3 |
| RST | none — do **not** toggle GPIO12 (that is TF_CLK) |

Poll; INT is optional. Read command `{0xb5,0xab,0xa5,0x5a,…}`. Coordinates are 12-bit; axes may be swapped — clamp to 320×480.

## Other on-board (not used by the remote)

| Function | GPIO |
|---|---|
| TF / SD CS, MOSI, CLK, MISO | 10, 11, 12, 13 |
| BAT_ADC | 5 |
| I2S BCLK, LRCLK, SDATA (NS4168 speaker) | 42, 2, 41 |
| BOOT | 0 |

Connectors: USB-C, TF slot, JST1.25-2P speaker, JST1.25-2P battery, BAT_Key, UART JST1.25-4P, extra 4P. Side pads: **S1 = VIN, S2 and S4 = GND, S3 = 3.3 V**.

## Flash

USB-C on the **panel**. Espressif USB-Serial-JTAG, VID `303A` PID `1001` (same IDs as the C3 — check it is not COM9 / the SuperMini).

```
pio run -t upload -e remote
```

Default reset is OK (unlike the relay). If upload never connects: hold **BOOT**, tap **RST**, release BOOT.

First boot: AP `HeatRemote-Setup` / `heatsetup` at http://192.168.4.1. Same house Wi-Fi as the relay.

On-screen **SETUP**: IP, SSID, `heat-remote.local`, GitHub OTA (`remote.bin` from the latest release), Reset Wi-Fi (tap twice). Hold **BOOT** (GPIO0) 5 s to forget Wi-Fi. Web: `http://heat-remote.local/` login `admin` / `heatota`. PlatformIO OTA: `pio run -t upload -e remote-ota`.

## UDP (same network as the relay)

| | |
|---|---|
| Relay listen | **4210** — `MODE AUTO` / `MODE ELECTRIC` / `MODE FIREPLACE` / `HEAT?` |
| Remote listen | **4211** — `HEAT T=45.2 mode=FIREPLACE req=AUTO t1=0 t2=0 valve=FP heat=0 p1=0 p2=0` |
| Stale | no packet for **30 s** → red **No connection** |

`T=--` if the DS18B20 is missing. `mode=` is live output; `req=` is the last button. `fireDump` still overrides ELECTRIC. Details: `include/heat_udp.h`, `AGENTS.md`.

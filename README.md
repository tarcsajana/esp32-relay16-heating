# Flashing the LCTech ESP32 16-channel relay board

The relay board has no USB port. It is programmed through an ESP32-C3 SuperMini used as a USB-serial adapter. PlatformIO on the PC talks to the SuperMini; the SuperMini forwards that serial stream to the relay's ESP32.

Two folders, two chips. Do not mix them up.

| Folder | Chip | When |
|---|---|---|
| `esp32c3-usb-ttl-bridge` | SuperMini (ESP32-C3) | Once per SuperMini, or after changing the bridge sketch |
| `esp32-relay16-heating` (this folder) | Relay board (ESP32-WROOM-32E) | Every time the heating firmware changes |

The relay board in this setup is an ESP32-D0WD-V3 (revision v3.1), 4 MB flash, 40 MHz crystal.

## What you need

- PlatformIO (Cursor/VS Code extension, or the `pio` command).
- The SuperMini already running the bridge sketch from `esp32c3-usb-ttl-bridge`.
- A 5 V supply on the relay board's own DC jack. The SuperMini does not power the relay.
- Three wires: GND, TX, RX. Nothing else.

On Windows the SuperMini shows up as **USB Serial Device**, VID `303A` PID `1001`. Ignore Bluetooth COM ports. Set that COM port as `upload_port` in both `platformio.ini` files (this machine uses `COM9`).

## 1. Flash the bridge (once)

Unplug the three wires from the relay board, or leave them off.

Open `esp32c3-usb-ttl-bridge` and upload the `esp32c3` environment:

```
pio run -t upload -e esp32c3
```

If `pio` is not on PATH:

```
%USERPROFILE%\.platformio\penv\Scripts\pio.exe run -t upload -e esp32c3
```

A normal upload resets the SuperMini over USB and should succeed on its own. If it never connects, hold the SuperMini **BOOT** button (GPIO9), tap **RST**, release BOOT, and upload again.

The bridge sketch must be the one in that folder. It copies USB to UART on GPIO6 (TX) and GPIO7 (RX) at 115200, and it sets a 16 KB USB receive buffer. The ESP32-C3 USB-serial hardware drops bytes once that buffer is full, and esptool's packets are about 6 KB. An older byte-by-byte bridge fails mid-flash with `Serial data stream stopped`.

Do not flash this heating project at the SuperMini. If the log says `This chip is ESP32-C3 not ESP32`, the reset hit the SuperMini instead of the relay.

## 2. Wire the 6-pin programming header

Use the relay board's 6-pin programming header, not the large GPIO headers. The silkscreen on those headers is unreliable. The 6-pin header is `3V3, TX, RX, GND, GND, IO0`. EN is a button on the board, not a pin on that header.

On the SuperMini, use the pads labeled **6** and **7**. The pads labeled TX and RX are GPIO21 and GPIO20. This bridge does not use them.

| SuperMini | Relay 6-pin header |
|---|---|
| GND | GND |
| Pad 6 (GPIO6, adapter TX) | RX |
| Pad 7 (GPIO7, adapter RX) | TX |

Leave `3V3` and `IO0` on that header unconnected. Do not connect the SuperMini's 5 V or 3V3 to the relay. Do not jumper pad 6 to pad 7 while the relay is wired.

Power the relay from its 5 V jack and check that its power LED is on before uploading.

## 3. Put the relay ESP32 in download mode

Only use the buttons on the **relay** board. Do not press BOOT or RST on the SuperMini while flashing the relay.

1. Hold **IO0**.
2. Tap **EN**.
3. Release **IO0**.

The relay stays in the ROM bootloader until EN is tapped again with IO0 not held.

## 4. Upload the heating firmware

From this folder:

```
pio run -t upload -e relay
```

`platformio.ini` already sets the options that make this work:

- `upload_speed = 115200` — must match the bridge. Do not raise it.
- `board_upload.before_reset = no_reset` and `board_upload.after_reset = no_reset` — esptool must not toggle DTR/RTS. That reset line goes to the SuperMini, not the relay, and drops the bridge into its own bootloader.

Two settings look right and are not. Do not add them back:

- `board_upload.use_1200bps_touch = no` — this board has no such flag, so PlatformIO keeps the string `"no"`, and a non-empty string is treated as true. The 1200 bps pulse reboots the SuperMini.
- `--before=no_reset` inside `upload_flags` — PlatformIO prepends those flags, then appends its own `--before default_reset`. esptool keeps the later one, so the SuperMini still gets reset.

A good upload looks like this:

```
Chip is ESP32-D0WD-V3 (revision v3.1)
...
Hash of data verified.
Leaving...
Staying in bootloader.
```

`Staying in bootloader` is expected. `no_reset` means esptool does not start the app.

## 5. Start the app

Tap **EN** on the relay board once. Do not hold IO0.

Then open the serial monitor at 115200:

```
pio device monitor -e relay
```

A healthy boot prints a status line about four times a second:

```
T=NO_SENSOR  mode=FIREPLACE  t1=0 t2=0  valve=FP  heat=0  p1=0 p2=0
```

`NO_SENSOR` is normal when no DS18B20 is connected on GPIO23. With a sensor, `T=` is a temperature. Relays 1–4 click once at boot (the click test in the firmware). Channels 5–16 stay off. The status line also shows `wifi=setup`, `wifi=down`, or the board's IP.

This firmware has to be flashed over the serial bridge **once**. After that, updates can go over Wi-Fi.

## Wi-Fi setup

On first boot (or if the saved network cannot be joined) the board opens a setup network and keeps controlling the relays.

1. Join the Wi-Fi network `Heating-Setup`. Password: `heatsetup`.
2. Open `http://192.168.4.1` if the captive portal does not pop up. Use http, not https.
3. Pick the house network, enter its password, and save.

The serial log prints the address when it connects, for example `wifi=192.168.1.50`. The board is also at `http://heating.local/` when mDNS works.

To change networks later, either:

- open the page and use **Forget Wi-Fi**, or
- hold the relay board **IO0** button for 5 seconds.

Both erase the saved network and reboot into `Heating-Setup`.

## OTA

**Browser, on the house network.** Open `http://<board-ip>/` (or `http://heating.local/`). Log in as `admin` / `heatota`. Press **OTA update**. The board downloads `firmware.bin` from the latest GitHub release and reboots into it. The file upload under that button is only needed if you want a local build instead.

**Browser, from the setup network.** The setup portal at `http://192.168.4.1` has the same **OTA update** button. It can download the release only when the board is already joined to a network with internet. Otherwise use the portal's **Update** button and upload a `.bin` file. The setup portal is protected by the setup Wi-Fi password, not `heatota`.

**PlatformIO.** Password `heatota`. From this folder, once the board is on the network:

```
pio run -t upload -e relay-ota
```

That targets `heating.local`. If the name does not resolve, use the IP from the serial log:

```
pio run -t upload -e relay-ota --upload-port 192.168.x.x
```

Do not use the `relay` environment for this. `relay` is the serial bridge upload. `relay-ota` is the Wi-Fi upload. The partition table is `default.csv` (two app slots). OTA will fail if that is changed to a single huge app.

## When it fails

| What you see | What it means |
|---|---|
| `This chip is ESP32-C3 not ESP32` | The SuperMini was reset. Check `before_reset` / `after_reset` are `no_reset`, and that `use_1200bps_touch` is not set. Do not hold the SuperMini BOOT button. |
| `No serial data received` for all 30 attempts | The relay UART is not reaching pad 7. Power LED on? Wires on the 6-pin header, not the big GPIO headers? Pad 6 to RX and pad 7 to TX? If those three are already correct, swap pad 6 and pad 7, enter download mode again, and retry. |
| `Serial data stream stopped` or `The chip stopped responding` after the chip ID | The chip is the right one, but bytes are being dropped. Reflash the SuperMini from `esp32c3-usb-ttl-bridge` so it has the 16 KB USB buffer. Short or loose TX/RX/GND wires cause the same symptom. |
| Chip ID succeeds, then the log never shows status lines | Upload finished while the chip was still in the bootloader. Tap EN on the relay board with IO0 not held. |

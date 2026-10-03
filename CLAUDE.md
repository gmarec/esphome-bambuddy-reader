# Project context — M5Stack Dial port

Fork of bemble/esphome-bambuddy-reader (ESPHome, Bambu Lab NFC tag reader + BambuBuddy sync).
Branch `m5dial`: port to the M5Stack Dial (ESP32-S3 StampS3, no PSRAM). User docs: `README-dial.md`.

**Language: everything committed to the repo (code, comments, docs, UI strings, log messages, commit messages) is
in English.**

## Remotes
- `origin`: upstream bemble/esphome-bambuddy-reader (read only).
- `fork`: git@github.com:gmarec/esphome-bambuddy-reader.git — push `m5dial` here.

## Layout
- `components/bambu_rc522/`: ESPHome component written from scratch, two readers behind common primitives
  (`detect_`, `auth_`, `read16_`, `write4_`, `end_session_`):
  - `ws1850s`: the Dial's internal MFRC522-compatible chip (I2C `0x28`): WUPA, anticollision CL1/CL2, SELECT,
    MFAuthent key A, READ, NTAG WRITE, HLTA. RX gain 48 dB and TX power forced to max.
  - `pn532`: external module on Grove port A (bus `grove_i2c`, G13 SDA / G15 SCL, `0x24`), in-house I2C driver
    (InListPassiveTarget, InDataExchange).
  - Bambu keys: HKDF-SHA256(UID, salt = Bambu master key, info "RFID-A\0"), 16 keys × 6 bytes.
  - Decoding/sensors/triggers taken from `components/bambu_nfc/` (same YAML keys) + `on_ntag_tag` and `reader`.
  - Diagnostic mode: `set_diag_mode()`; margin = RX-gain steps still answering, reliability = % of successful probes.
- `spool-reader-dial.yaml` + `spool-reader-dial/`: Dial config (mipi_spi GC9A01A, ft5x06 touch at `0x38` with IRQ
  GPIO14, encoder 40/41, button 42, buzzer 3, backlight 9, power hold 46), round LVGL UI 240×240.
  Reader chosen by the `nfc_*` substitutions (PN532 by default).
  - `api-ntag.yaml`: NTAG lookup by UID in BambuBuddy, filament card filled from the spool.
  - `diag.yaml`: signal diagnostic (beep pitch + screen + HA sensors), short press on the idle screen.
  - `pages-triggers.yaml`: `show_filament` script shared by Bambu and NTAG scans.
- Reused unchanged from upstream: `spool-reader/api-bambuddy.yaml`, `spool-reader/ui-fonts.yaml` (still French;
  upstream files, left as-is to ease merges).
- `hardware/handle/`: 3D-printed handle (Dial + PN532), not designed yet.

## Validated on hardware (ESPHome 2026.6.5)
- Build, Wi-Fi, display, touch, Bambu tag reading and BambuBuddy sync with the internal reader.
- Internal reader range is millimetres (tiny antenna) even at max gain/power → switched to an external PN532.

## To validate on hardware
1. PN532: detection, Bambu reading, NTAG; module I2C pull-ups vs the Grove 5 V supply.
2. NTAG lookup flow end to end.
3. Button GPIO42 polarity, encoder scrolling, LVGL buffer 25 % (drop to 12 % if it reboots).

## Next steps
- Make the Dial a SpoolBuddy-compatible device (`/devices/register`, `/devices/{id}/heartbeat`,
  `/nfc/write-tag` → write OpenTag3D bytes with `write4_`, `/nfc/write-result`).

## Commands
- `esphome config spool-reader-dial.yaml`
- `esphome run spool-reader-dial.yaml --device /dev/cu.usbmodem*` (first flash) or `--device 192.168.78.8`
- `esphome logs spool-reader-dial.yaml`

Secrets: `secrets.yaml` (wifi, api key, ota, bambuddy_url, bambuddy_api_key) — never commit.

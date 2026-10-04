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
- `components/spoolbuddy/`: registers with BambuBuddy as a SpoolBuddy device + heartbeat every 15 s
  (offline threshold 30 s), esp_http_client in a FreeRTOS task on core 0. device_id `esphome-spool-reader`
  (same as the shared tag-scanned calls). Heartbeat returns `pending_command` (`tare` handled, others logged)
  and the scale calibration (`tare_offset`, `calibration_factor`, stored by BambuBuddy).
  Scale protocol: weight = (raw - tare) * factor, 5-sample average, stable = spread < 2 g over 1 s,
  `/scale/reading` at most 1/s on a 2 g change, `/scale/update-spool-weight` on request, keep-alive HTTP client.
- `spool-reader-dial/scale.yaml`: HX711 on Grove port B (DT G2 with pull-up, SCK G1), weight on the idle screen,
  "Scale weight" sensor, tare (long press on idle / HA button), `scale_sync_weight` after a known spool is scanned.
- `hardware/station/`: 3D-printed station (scale platter, PN532 under it, Dial on the side), not designed yet.

## Validated on hardware (ESPHome 2026.6.5)
- Build, Wi-Fi, display, touch, Bambu tag reading and BambuBuddy sync with the internal reader.
- SpoolBuddy registration + heartbeat: device listed online in BambuBuddy (2026-10-04).
- Internal reader range is millimetres (tiny antenna) even at max gain/power → switched to an external PN532.

## To validate on hardware
1. PN532: detection, Bambu reading, NTAG; module I2C pull-ups vs the Grove 5 V supply.
2. NTAG lookup flow end to end.
3. Button GPIO42 polarity, encoder scrolling, LVGL buffer 25 % (drop to 12 % if it reboots).

## Next steps
- Handle `write_tag` from the heartbeat: parse `pending_write_payload.ndef_data_hex`, ask for a blank NTAG,
  write it with `write4_` from page 4, report `/nfc/write-result`.

## Commands
- `esphome config spool-reader-dial.yaml`
- `esphome run spool-reader-dial.yaml --device /dev/cu.usbmodem*` (first flash) or `--device 192.168.78.8`
- The maintainer's own Dial uses the French UI: flash `spool-reader-dial-fr.yaml`.
- `esphome logs spool-reader-dial.yaml`

Secrets: `secrets.yaml` (wifi, api key, ota, bambuddy_url, bambuddy_api_key) — never commit.

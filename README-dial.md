# M5Stack Dial adaptation

Branch `m5dial`: the same features as the Waveshare build (Bambu Lab tag reading, Home Assistant sensors,
BambuBuddy sync) on an **M5Stack Dial** (ESP32-S3, round 240×240 touch screen, rotary encoder, button, buzzer),
with an external **PN532** NFC reader plugged into the Dial's Grove port.

## What changes compared to the Waveshare build

| | Waveshare (upstream) | M5Stack Dial |
|---|---|---|
| NFC reader | PN532, SPI | PN532 on Grove port A, I2C `0x24` (internal WS1850S still supported) |
| Component | `bambu_nfc` | `bambu_rc522` (standalone driver for both readers + MIFARE Classic auth) |
| Tags | Bambu Lab | Bambu Lab + NTAG 213/215/216 (identified by UID through BambuBuddy) |
| Display | ST7789V 240×320 touch | GC9A01A round 240×240 touch |
| Navigation | touch buttons | rotary encoder + button + touch |

`spool-reader/api-bambuddy.yaml` and `spool-reader/ui-fonts.yaml` are reused as-is.

## Why an external PN532

The port was first built around the Dial's **internal NFC chip** (WS1850S, MFRC522-compatible, I2C `0x28`), so the
device would need no wiring at all. It works — Bambu tags are read and synced — but its antenna is a small ring around
the screen, and the small tags inside Bambu spools only couple when placed within a few millimetres of the right spot,
even with the receiver gain and transmitter power at their maximum. M5Stack's documentation also recommends
card-sized tags for this reader. That is not usable day to day, so the build now uses an external PN532, whose larger
antenna reads Bambu tags at a comfortable distance and is forgiving about placement.

The internal reader is still supported (see [Reader selection](#reader-selection)), e.g. for card-sized NTAG stickers.

## PN532 wiring

1. Set the PN532 DIP switches to **I2C** (see the module's silkscreen).
2. With the Dial **powered off**, wire Grove port A to the PN532:

   | Grove wire | PN532 pin |
   |---|---|
   | red (5 V) | VCC |
   | black | GND |
   | yellow (G13) | SDA |
   | white (G15) | SCL |

3. Power the Dial on. The reader is only probed at boot: if it is plugged in while the Dial is running, restart it.

> ⚠️ The Grove port supplies **5 V**, while ESP32 pins only accept 3.3 V. Before wiring, check that the module's I2C
> pull-up resistors are not tied to 5 V.

## Reader selection

Three substitutions at the top of `spool-reader-dial.yaml`:

```yaml
# External PN532 on Grove port A (default)
nfc_reader: pn532
nfc_i2c: grove_i2c
nfc_address: "0x24"

# Internal WS1850S
nfc_reader: ws1850s
nfc_i2c: internal_i2c
nfc_address: "0x28"
```

## Usage

1. `secrets.yaml`: same keys as the main README.
2. Flash: `esphome run spool-reader-dial.yaml` (over USB-C the first time, then over Wi-Fi).
3. From the Home Assistant ESPHome add-on: copy `spool-reader-dial.yaml`, `spool-reader/` and `spool-reader-dial/`,
   and point `external_components` to `github://gmarec/esphome-bambuddy-reader/components@m5dial`.

## Controls

- **Scan**: hold the side of the spool against the reader, near the centre hole (that is where Bambu tags sit).
- **Short press**: filament card ⇄ details. On the idle screen: toggles the signal diagnostic.
- **Encoder**: on the filament card, opens the details; on the details, scrolls the list.
- **Long press**: adds the spool to BambuBuddy if it is unknown, otherwise goes back to the idle screen.
- **Touch**: tap the filament card → details, tap the details header → back, "Add" button.

## NTAG tags

Both readers also read **NTAG 213/215/216** tags (7-byte UID). An NTAG is identified by its UID through BambuBuddy:
if it is linked to a spool, the filament card is filled from that spool (material, colour, temperatures, remaining
weight); otherwise the screen shows "unknown tag" with the UID so it can be linked in BambuBuddy.

## SpoolBuddy tags

[SpoolBuddy](https://wiki.bambuddy.cool/spoolbuddy/) is BambuBuddy's own NFC hardware (Raspberry Pi + PN5180).
What it actually does with each tag format (checked in the BambuBuddy source, `spoolbuddy/daemon/` and
`backend/app/services/opentag3d.py`, October 2026):

| Tag | Chip | SpoolBuddy read | SpoolBuddy write | M5Stack Dial |
|---|---|---|---|---|
| Bambu Lab | MIFARE Classic 1K | ✅ decoded (`tray_uuid`, material, colour…) | ❌ (encrypted, proprietary) | ✅ decoded locally |
| OpenTag3D | NTAG 213/215/216 | ⚠️ UID only | ✅ the only format it writes | ⚠️ UID only (write planned) |
| SpoolEase / OpenPrintTag | NTAG | ⚠️ UID only | ❌ | ⚠️ UID only |
| OpenSpool | NTAG | ⚠️ UID only | ❌ | ⚠️ UID only |
| TigerTag | NTAG | ⚠️ UID only | ❌ | ⚠️ UID only |

"UID only" means the tag content is not decoded: BambuBuddy matches the UID against the spool it was linked to.
That is enough in practice, because BambuBuddy holds the spool data and, once a spool is assigned to an AMS slot,
sends the filament settings to the printer (`ams_filament_setting`: type, colour, temperatures, plus the K-factor
calibration via `extrusion_cali_sel`).

### Planned: write tags from the Dial

Make the Dial a SpoolBuddy-compatible device so non-Bambu spools can be tagged from BambuBuddy:

1. The Dial registers as a device (`/api/v1/spoolbuddy/devices/register`) and polls
   `/devices/{id}/heartbeat`.
2. In BambuBuddy, "write tag" on a spool queues a `write_tag` command with the OpenTag3D NDEF bytes, ready to write
   from NTAG page 4 (`/nfc/write-tag`).
3. The Dial asks for a blank NTAG215 sticker, writes the pages (`write4_` in the component) and reports
   `/nfc/write-result`; BambuBuddy then links the tag UID to the spool.
4. Next scans identify the spool by UID, as above.

## Signal diagnostic

Short press on the idle screen (or the "RFID diagnostic" switch in Home Assistant): the Dial beeps higher as the
tag coupling gets stronger, and shows a margin (0–6) and a reliability (%). The screen faces the spool while scanning,
hence the sound. No spool is read or synced while the diagnostic is on.

With the internal reader, the margin is the number of receiver-gain steps at which the tag still answers; with the
PN532 only the reliability is measured.

## Handle (3D model)

A 3D-printed handle for the Dial and the PN532 will live in [`hardware/handle/`](hardware/handle/).

## Colour / density tables

`components/bambu_rc522/sync_tables.sh` regenerates the tables from SpoolmanDB (using the `bambu_nfc` script) and
copies them with the right namespace.

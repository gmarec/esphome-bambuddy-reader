# M5Stack Dial adaptation

Branch `m5dial`: the same features as the Waveshare build (Bambu Lab tag reading, Home Assistant sensors,
BambuBuddy sync) on an **M5Stack Dial** (ESP32-S3, round 240×240 touch screen, rotary encoder, button, buzzer),
with an external **PN532** NFC reader and a **load-cell scale**. The Dial registers with BambuBuddy as a SpoolBuddy
device (NFC + scale).

## Hardware

| Part | Details |
|---|---|
| [M5Stack Dial](https://docs.m5stack.com/en/core/M5Dial) | ESP32-S3FN8, 8 MB flash, no PSRAM; 1.28" round 240×240 GC9A01 display, FT3267 touch, rotary encoder, built-in button, buzzer, BM8563 RTC |
| NFC reader | PN532 module ("NFC V3", red), DIP switches set to **I2C**, address `0x24`, powered at 3.3 V |
| Scale | 5 kg bar load cell + HX711 24-bit ADC module (green "HW-29" board), powered at 3.3 V |
| 3.3 V regulator | AMS1117-3.3 mini module (VIN / OUT / GND, 800 mA max), fed from the Grove 5 V |
| Cables | 2 × Grove (HY2.0-4P) to female Dupont (ports A and B) |
| Power supply | USB-C 5 V, 1 A or more (no battery) |
| External button | push button — wiring and function to be defined |
| Enclosure | 3D-printed station: scale platter, PN532 under it, Dial on the side ([`hardware/station/`](hardware/station/)) |

### Pins used

| Function | GPIO | Notes |
|---|---|---|
| Display SPI | CLK 6, MOSI 5, CS 7, DC 4, RST 8 | GC9A01 |
| Backlight | 9 | LEDC PWM, "Brightness" light in Home Assistant |
| Internal I2C | SDA 11, SCL 12 | touch `0x38` (IRQ 14), RTC `0x51` |
| Grove port A (I2C) | SDA 13 (yellow), SCL 15 (white) | PN532 `0x24`, 100 kHz |
| Grove port B | DT 2 (yellow), SCK 1 (white) | HX711 |
| Encoder | A 40, B 41 | swap them if scrolling goes the wrong way |
| Button | 42 | built-in button |
| Buzzer | 3 | LEDC PWM, RTTTL sounds |
| Power hold | 46 | kept high (only matters on battery) |

### Wiring

Both Grove ports supply **5 V**, but ESP32 pins only accept 3.3 V: the PN532 and the HX711 are powered at **3.3 V**
from the regulator, so their signal lines stay at 3.3 V. Wire everything with the Dial **unplugged**, and check with
a multimeter that the regulator's OUT pin reads 3.3 V before connecting the modules.

```
                       ┌──────────────┐
 Port B red (5 V) ─────┤ VIN          │
 Port B black (GND) ───┤ GND   AMS1117├──── OUT (3.3 V) ──┬── HX711 VCC
                       └──────────────┘                   └── PN532 VCC
```

| From | To |
|---|---|
| Port B, red (5 V) | regulator **VIN** |
| Port B, black (GND) | regulator **GND**, HX711 **GND**, PN532 **GND** |
| regulator **OUT** (3.3 V) | HX711 **VCC**, PN532 **VCC** |
| Port B, yellow (G2) | HX711 **DT** |
| Port B, white (G1) | HX711 **SCK** |
| Port A, yellow (G13) | PN532 **SDA** |
| Port A, white (G15) | PN532 **SCL** |
| Port A, black (GND) | PN532 **GND** (common ground) |
| Port A, red (5 V) | **not connected** |

Load cell to HX711: red → **E+**, black → **E−**, white → **A−**, green → **A+**.

Set the PN532 DIP switches to **I2C** before powering it. The PN532 is only probed at boot: if it is plugged in
while the Dial is running, restart the Dial.

### Power

USB-C 5 V, 1 A or more. The Dial, the PN532 and the HX711 draw roughly 0.5 A at peak (estimate). The Dial can also
be fed 6–36 V DC on its rear terminal; check with a multimeter that the Grove ports then still supply 5 V before
relying on it.

## Why an external PN532

The port was first built around the Dial's **internal NFC chip** (WS1850S, I2C `0x28`) so that no wiring would be
needed. It reads Bambu tags, but its antenna is a small ring around the screen: the small tags inside Bambu spools
only couple within a few millimetres of the right spot, even with the receiver gain and transmitter power at their
maximum (M5Stack also recommends card-sized tags for it). The PN532's larger antenna reads them at a comfortable
distance. The internal reader is still selectable with the `nfc_*` substitutions in `spool-reader-dial.yaml`
(`ws1850s` / `internal_i2c` / `"0x28"`), but it is not the supported setup.

## Usage

1. `secrets.yaml`: same keys as the main README.
2. Flash: `esphome run spool-reader-dial.yaml` (over USB-C the first time, then over Wi-Fi).
3. From the Home Assistant ESPHome add-on: copy `spool-reader-dial.yaml`, `spool-reader/` and `spool-reader-dial/`,
   and point `external_components` to `github://gmarec/esphome-bambuddy-reader/components@m5dial`.

## Language

On-screen text comes from `spool-reader-dial/lang-en.yaml` (default). To build the same firmware in French, flash
`spool-reader-dial-fr.yaml`, which applies `spool-reader-dial/lang-fr.yaml`. Another language only needs a new
`lang-xx.yaml` with the same keys (using characters available in the fonts) and a matching wrapper file.
Home Assistant entity names stay in English.

## Controls

- **Scan**: lay the spool flat on the platter, centred: Bambu tags sit near the hub, right above the PN532.
- **Short press**: filament card ⇄ details. On the idle screen: toggles the signal diagnostic.
- **Encoder**: on the filament card, opens the details; on the details, scrolls the list.
- **Long press**: on the idle screen, tares the scale; on the filament card, adds the spool to BambuBuddy if it is
  unknown, otherwise goes back to the idle screen.
- **Touch**: tap the filament card → details, tap the details header → back, "Add" button.

## Scale

The weight is computed as `(raw - tare) * factor`, with the tare and factor stored by BambuBuddy (SpoolBuddy scale
protocol, `components/spoolbuddy` + `spool-reader-dial/scale.yaml`):

1. **Tare**: empty platter, then long press on the idle screen (or the "Scale tare" button in Home Assistant, or
   "Tare" in BambuBuddy's SpoolBuddy page).
2. **Calibrate** once from BambuBuddy's SpoolBuddy page with a known weight: the Dial reports its raw readings,
   BambuBuddy computes the factor and sends it back with the next heartbeat (within 15 s).
3. The idle screen shows the weight on the platter ("Scale not calibrated" until step 2), and Home Assistant gets a
   "Scale weight" sensor.
4. **Weighing a spool**: when a scanned spool is known to BambuBuddy and the reading is stable (within 2 g over
   1 s, more than 50 g), the Dial sends the weight to BambuBuddy, which updates the spool's used / remaining
   filament (weight on the scale minus the empty spool weight). The remaining weight on the filament card is refreshed
   and the Dial beeps.

Readings are reported to BambuBuddy at most once per second, only when the weight changes by 2 g or more.

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

### SpoolBuddy device

The Dial registers itself with BambuBuddy as a SpoolBuddy device (`components/spoolbuddy`,
`spool-reader-dial/spoolbuddy.yaml`): it shows up in BambuBuddy's SpoolBuddy devices as `spool-reader-dial`
(device id `esphome-spool-reader`, PN532 over I2C, with scale) and sends a heartbeat every 15 s (BambuBuddy marks a
device offline after 30 s). Requests run in a background task, so the UI never freezes. The sync icon on the idle
screen shows the link: green = online, orange = error, grey = connecting.

### Planned: write tags from the Dial

Make the Dial a SpoolBuddy-compatible device so non-Bambu spools can be tagged from BambuBuddy:

1. The Dial registers as a device and polls `/devices/{id}/heartbeat` (done, see above).
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

## Station (3D model)

The 3D-printed station (scale platter, PN532 under its centre, Dial on the side) will live in [`hardware/station/`](hardware/station/).

## Colour / density tables

`components/bambu_rc522/sync_tables.sh` regenerates the tables from SpoolmanDB (using the `bambu_nfc` script) and
copies them with the right namespace.

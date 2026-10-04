# ESPHome Bambu Spool Reader

An ESPHome-based NFC spool reader for Bambu Lab filaments, running on a **Waveshare ESP32-S3 with a 2.8" touch screen**. It reads the NFC tag embedded in Bambu Lab spools and displays the filament information on the screen. It also integrates with [BambuBuddy](https://github.com/piitaya/bambuddy) to track spool inventory and remaining filament weight.

> Special thanks to [@piitaya](https://github.com/piitaya) for his foundational work on the NFC tag decoding and spool matching logic that this project builds upon.

> **M5Stack Dial adaptation** — this branch also runs on an [M5Stack Dial](https://docs.m5stack.com/en/core/M5Dial)
> (round touch screen, rotary encoder, buzzer). The Dial's internal NFC chip was tried first but its range is too short
> for Bambu tags, so the Dial build uses an external PN532 on its Grove port, plus a load-cell scale. It registers
> with BambuBuddy as a SpoolBuddy device (NFC + scale), reads NTAG tags and includes a signal diagnostic.
> See **[README-dial.md](README-dial.md)**.

---

## Features

- Reads Bambu Lab NFC spool tags via a PN532 NFC reader (SPI)
- Displays filament type, color, subtype, temperatures, and weight on the touch screen
- LVGL-powered UI with idle, filament info, and detail pages
- Syncs with BambuBuddy: looks up spools by tag UID and shows remaining weight
- Can add unknown spools to BambuBuddy directly from the device
- Exposes all filament sensors to Home Assistant via the ESPHome API
- Backlight control via Home Assistant
- All the screens are in French, sorry (the M5Stack Dial build is in English)

---

## Hardware

Two builds share the same NFC decoding and BambuBuddy integration:

- **Waveshare build** (upstream): below.
- **M5Stack Dial build**: see [M5Stack Dial build](#m5stack-dial-build) and [README-dial.md](README-dial.md).

### Waveshare build

| Component | Details |
|-----------|---------|
| Microcontroller | Waveshare ESP32-S3 1.69" / 2.8" Touch (ST7789V display, CST328 touch) |
| Display | 240×320 ST7789V via SPI |
| Touch | CST328 via I2C |
| NFC Reader | PN532 module (SPI) |

### Pin Mapping

#### Display SPI
| Signal | GPIO |
|--------|------|
| CLK | GPIO40 |
| MOSI | GPIO45 |
| CS | GPIO42 |
| DC | GPIO41 |
| RST | GPIO39 |
| Backlight | GPIO5 |

#### Touch I2C
| Signal | GPIO |
|--------|------|
| SDA | GPIO1 |
| SCL | GPIO3 |
| INT | GPIO4 |
| RST | GPIO2 |

#### NFC SPI
| Signal | GPIO |
|--------|------|
| CLK | GPIO18 |
| MOSI | GPIO15 |
| MISO | GPIO44 |
| CS | GPIO43 |

### PN532 NFC Reader — Jumper Configuration

The PN532 module must be set to **SPI mode** using its two DIP switches:

| Switch | Position |
|--------|----------|
| 1 | **ON** |
| 2 | **KE** (OFF) |

This selects the SPI interface on the PN532 module. Make sure to set these before powering the board.

### M5Stack Dial build

| Component | Details |
|-----------|---------|
| Microcontroller | [M5Stack Dial](https://docs.m5stack.com/en/core/M5Dial) — ESP32-S3FN8, 8 MB flash, no PSRAM |
| Display | 1.28" round 240×240 GC9A01 via SPI |
| Touch | FT3267 via I2C (`0x38`) |
| Controls | rotary encoder, built-in button, buzzer |
| NFC Reader | PN532 module ("NFC V3", red), **I2C** on Grove port A (`0x24`), powered at 3.3 V |
| Scale | 5 kg bar load cell + HX711 module on Grove port B, powered at 3.3 V |
| 3.3 V regulator | AMS1117-3.3 mini module fed from the Grove 5 V (powers the PN532 and the HX711) |
| Cables | 2 × Grove (HY2.0-4P) to female Dupont |
| Power supply | USB-C 5 V, 1 A or more |
| Enclosure | 3D-printed station: scale platter, PN532 under it, Dial on the side ([`hardware/station/`](hardware/station/)) |

The PN532 DIP switches must be set to **I2C** for this build (not SPI). Pin mapping, wiring and power notes are in
[README-dial.md](README-dial.md#hardware).

---

## Software Setup

### Prerequisites

- [ESPHome](https://esphome.io/) installed (CLI or Home Assistant add-on)
- A running [BambuBuddy](https://github.com/piitaya/bambuddy) instance (optional — the reader works standalone without it)

### Secrets

Create a `secrets.yaml` file alongside `spool-reader.yaml` with the following keys:

```yaml
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"

spool_reader_api_encryption_key: "your_32_byte_base64_key"
spool_reader_ota_password: "your_ota_password"
spool_reader_ap_password: "your_ap_password"

# Optional — BambuBuddy integration
bambuddy_url: "http://your-bambuddy-instance:port"
bambuddy_api_key: "your_bambuddy_api_key"
```

If BambuBuddy is not configured, set `bambuddy_url` to any non-HTTP value (e.g. `disabled`) and the sync icon will remain grey.

### Configuration

Copy `spool-reader.yaml` as your starting point — it already includes all the package references and settings for the Waveshare ESP32-S3:

```yaml
substitutions:
  bambuddy_url: !secret bambuddy_url
  bambuddy_api_key: !secret bambuddy_api_key

esphome:
  name: spool-reader
  friendly_name: spool-reader

esp32:
  board: esp32-s3-devkitc-1
  flash_size: 16MB
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

external_components:
  - source:
      type: git
      url: https://github.com/BluetriX/esphome-CST328-Touch
    components: [cst328]

logger:

api:
  encryption:
    key: !secret spool_reader_api_encryption_key

ota:
  - platform: esphome
    password: !secret spool_reader_ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "Spool-Reader Fallback Hotspot"
    password: !secret spool_reader_ap_password

captive_portal:

packages:
  display: !include spool-reader/hw-display.yaml
  fonts: !include spool-reader/ui-fonts.yaml
  nfc: !include spool-reader/hw-nfc.yaml
  bambuddy: !include spool-reader/api-bambuddy.yaml
  styles: !include spool-reader/ui-styles.yaml
  page_idle: !include spool-reader/page-idle.yaml
  page_filament: !include spool-reader/page-filament.yaml
  page_details: !include spool-reader/page-details.yaml
  triggers: !include spool-reader/pages-triggers.yaml
```

### Flashing

```bash
esphome run spool-reader.yaml
```

---

## Custom Component: `bambu_nfc`

The `components/bambu_nfc/` directory contains the custom ESPHome component that handles:

- SPI communication with the PN532
- Deriving the Bambu Lab NFC authentication keys from the tag UID
- Decoding the proprietary Bambu Lab tag data format
- Resolving color hex codes to human-readable color names
- Looking up material density by filament type

### Keeping the Bambu Database Up to Date

Color names and densities are sourced from [SpoolmanDB](https://github.com/Donkie/SpoolmanDB/blob/main/filaments/bambulab.json). If a scan shows a raw hex code instead of a color name, regenerate the lookup tables:

```bash
cd /homeassistant/esphome/components/bambu_nfc
python3 generate_bambu_db.py
```

This regenerates `bambu_colors.h` and `bambu_densities.h`. Then recompile and reflash the firmware.

---

## Project Structure

```
spool-reader.yaml          # Main ESPHome config
spool-reader/
  hw-display.yaml          # Display & touch hardware config
  hw-nfc.yaml              # NFC hardware & sensor config
  api-bambuddy.yaml        # BambuBuddy HTTP integration
  ui-fonts.yaml            # LVGL font declarations
  ui-styles.yaml           # LVGL styles
  page-idle.yaml           # Idle screen (waiting for tag)
  page-filament.yaml       # Filament info screen
  page-details.yaml        # Detailed filament data screen
  pages-triggers.yaml      # Page navigation triggers
components/
  bambu_nfc/               # Custom ESPHome NFC component

# M5Stack Dial build
spool-reader-dial.yaml     # Main ESPHome config for the Dial
spool-reader-dial/         # Dial hardware, NFC, UI pages, NTAG lookup, signal diagnostic
components/
  bambu_rc522/             # NFC component for the Dial (PN532 I2C or internal WS1850S)
hardware/
  station/                 # 3D-printed station (scale platter, PN532, Dial)
```

---

## Credits

- [@piitaya](https://github.com/piitaya) — foundational work on Bambu Lab NFC tag decoding and spool matching, base for the UI (nearly everything)
- [BluetriX/esphome-CST328-Touch](https://github.com/BluetriX/esphome-CST328-Touch) — CST328 touchscreen ESPHome component
- [SpoolmanDB](https://github.com/Donkie/SpoolmanDB) — Bambu Lab filament database

---

## License

MIT

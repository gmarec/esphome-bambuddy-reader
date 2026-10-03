# Contexte projet — portage M5Stack Dial

Fork de bemble/esphome-bambuddy-reader (ESPHome, lecteur de tags NFC Bambu Lab + synchro BambuBuddy).
Branche `m5dial` : portage sur M5Stack Dial (ESP32-S3 StampS3, sans PSRAM).

## Etat
- `components/bambu_rc522/` : composant ESPHome écrit de zéro pour la WS1850S interne (MFRC522, I2C 0x28).
  Driver MFRC522 minimal (WUPA, anticollision CL1, SELECT, MFAuthent clé A, READ, HLTA).
  Clés : HKDF-SHA256(UID, salt = clé maître Bambu, info "RFID-A\0"), 16 clés × 6 octets.
  Décodage/capteurs/triggers repris de `components/bambu_nfc/` (mêmes clés YAML).
- `spool-reader-dial.yaml` + `spool-reader-dial/` : config Dial (mipi_spi GC9A01A, ft5x06, encodeur 40/41,
  bouton 42, buzzer 3, rétroéclairage 9, power hold 46), UI LVGL ronde 240×240.
- Réutilisés tels quels : `spool-reader/api-bambuddy.yaml`, `spool-reader/ui-fonts.yaml`.
- Option `reader: ws1850s | pn532` (substitutions `nfc_*` de spool-reader-dial.yaml). PN532 : port Grove A
  (bus `grove_i2c`, G13 SDA / G15 SCL, 0x24), driver I2C maison dans le meme composant.
- Tags NTAG (UID 7 octets, SAK 0x00) : trigger `on_ntag_tag` -> `spool-reader-dial/api-ntag.yaml`
  (identification par UID aupres de BambuBuddy, fiche remplie depuis la bobine). Ecriture NTAG : primitive
  `write4_` prete, pas encore branchee (prochaine etape : appareil SpoolBuddy + ecriture OpenTag3D).
- Mode diagnostic signal : `spool-reader-dial/diag.yaml` (appui court depuis l'accueil).

## Valide sur le materiel (ESPHome 2026.6.5)
- Compilation, wifi, ecran, tactile (FT3267 a 0x38, IRQ GPIO14), lecture tags Bambu, synchro BambuBuddy.
- Portee du lecteur interne tres faible (petite antenne) : gain RX 48 dB + puissance TX max appliques,
  insuffisant en usage -> PN532 externe.

## A valider sur le materiel
1. Non-regression lecteur interne apres la refonte PN532/NTAG (compile, pas encore flashe).
2. PN532 : detection, lecture Bambu, NTAG ; niveaux 5 V du module sur le Grove.
3. Sens du bouton GPIO42, defilement encodeur, buffer LVGL 25 %.

## Commandes
- `esphome config spool-reader-dial.yaml`
- `esphome run spool-reader-dial.yaml --device /dev/cu.usbmodem*`
- `esphome logs spool-reader-dial.yaml`

Secrets : `secrets.yaml` (wifi, api key, ota, bambuddy_url, bambuddy_api_key) — ne pas committer.

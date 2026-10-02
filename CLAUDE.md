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
- `esphome config` OK (2026.9.1). Jamais compilé pour la cible ni testé sur le matériel.

## A valider sur le matériel
1. Compilation complète (`esphome compile spool-reader-dial.yaml`).
2. Lecture RFID : logs `bambu_rc522` (auth secteur 0, lecture blocs 1-14).
3. Sens du bouton GPIO42 (`inverted` dans controls.yaml).
4. Mémoire : buffer LVGL 25 % (descendre à 12 % si reboot).
5. Rendu UI sur l'écran rond, défilement encodeur de la page détails.

## Commandes
- `esphome config spool-reader-dial.yaml`
- `esphome run spool-reader-dial.yaml --device /dev/cu.usbmodem*`
- `esphome logs spool-reader-dial.yaml`

Secrets : `secrets.yaml` (wifi, api key, ota, bambuddy_url, bambuddy_api_key) — ne pas committer.

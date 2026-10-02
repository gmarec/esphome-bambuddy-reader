# Portage M5Stack Dial

Branche `m5dial` : même fonctionnement que la version Waveshare (lecture des tags Bambu, capteurs HA, synchro BambuBuddy), sur un **M5Stack Dial** sans aucun câblage.

## Ce qui change

| | Waveshare (upstream) | Dial |
|---|---|---|
| Lecteur | PN532 externe, SPI | WS1850S intégré (MFRC522), I2C 0x28 |
| Composant | `bambu_nfc` | `bambu_rc522` (driver MFRC522 autonome + auth MIFARE Classic) |
| Écran | ST7789V 240×320 tactile | GC9A01A rond 240×240 tactile |
| Navigation | boutons tactiles | encodeur + bouton + tactile |

`api-bambuddy.yaml` et `ui-fonts.yaml` sont réutilisés tels quels.

## Utilisation

1. `secrets.yaml` : mêmes clés que le README principal.
2. Flasher : `esphome run spool-reader-dial.yaml` (USB-C la première fois).
3. Depuis le module ESPHome de HA : copier `spool-reader-dial.yaml` + `spool-reader/` + `spool-reader-dial/`, et pointer `external_components` vers ton fork (`github://<user>/esphome-bambuddy-reader/components@m5dial`).

## Commandes

- **Poser la flasque** contre la face avant (côté écran), près du moyeu.
- **Appui court** : fiche filament ⇄ détails.
- **Encodeur** : sur la fiche, ouvre les détails ; sur les détails, fait défiler la liste.
- **Appui long** : ajoute la bobine dans BambuBuddy si elle est inconnue, sinon retour à l'accueil.
- **Tactile** : tap sur la fiche → détails, tap sur l'en-tête des détails → retour, bouton « Ajouter ».

## Tables couleurs / densités

`components/bambu_rc522/sync_tables.sh` régénère les tables depuis SpoolmanDB (via le script de `bambu_nfc`) et les recopie avec le bon namespace.

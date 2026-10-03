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

- **Poser le côté de la bobine** contre la face avant (côté écran), près du trou central.
- **Appui court** : fiche filament ⇄ détails.
- **Encodeur** : sur la fiche, ouvre les détails ; sur les détails, fait défiler la liste.
- **Appui long** : ajoute la bobine dans BambuBuddy si elle est inconnue, sinon retour à l'accueil.
- **Tactile** : tap sur la fiche → détails, tap sur l'en-tête des détails → retour, bouton « Ajouter ».

## Tables couleurs / densités

`components/bambu_rc522/sync_tables.sh` régénère les tables depuis SpoolmanDB (via le script de `bambu_nfc`) et les recopie avec le bon namespace.

## Lecteur PN532 externe (option)

Le lecteur interne du Dial a une petite antenne : les tags Bambu se lisent à quelques millimètres près.
Un module PN532 (« NFC V3 », rouge) sur le port Grove A lit plus loin et pardonne le placement.

1. Interrupteurs du PN532 en mode **I2C** (voir la sérigraphie du module).
2. Câblage port Grove A → PN532 : rouge → VCC, noir → GND, jaune (G13) → SDA, blanc (G15) → SCL.
   ⚠️ Le Grove fournit du **5 V** : vérifier que les résistances de tirage I2C du module ne remontent pas SDA/SCL à 5 V
   (l'ESP32 n'accepte que 3,3 V).
3. Dans `spool-reader-dial.yaml` :
   ```yaml
   nfc_reader: pn532
   nfc_i2c: grove_i2c
   nfc_address: "0x24"
   ```

Les deux lecteurs lisent les tags Bambu (MIFARE Classic) et les tags **NTAG** (213/215/216).
Un NTAG est identifié par son UID auprès de BambuBuddy : s'il est lié à une bobine, la fiche s'affiche avec les
infos de BambuBuddy ; sinon l'UID s'affiche (« Tag inconnu ») pour pouvoir le lier.

## Diagnostic du signal

Appui court sur le bouton depuis l'accueil (ou interrupteur « Diagnostic RFID » dans HA) : le Dial bipe d'autant
plus aigu que le couplage avec le tag est fort, et affiche une marge (0-6) et une fiabilité (%).
Aucune bobine n'est lue ni synchronisée pendant le diagnostic.

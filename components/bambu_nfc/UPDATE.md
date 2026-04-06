# Mise a jour des tables Bambu Lab (couleurs + densites)

Source: [SpoolmanDB](https://github.com/Donkie/SpoolmanDB/blob/main/filaments/bambulab.json)

## Quand ?

Quand un scan affiche le hex brut au lieu d'un nom de couleur.

## Comment ?

```bash
cd /homeassistant/esphome/components/bambu_nfc
python3 generate_bambu_db.py
```

Genere `bambu_colors.h` et `bambu_densities.h`. Puis recompiler et reflasher.

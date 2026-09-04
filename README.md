# CNC32-TFT app

Réécriture moderne du firmware de l'écran tactile de la **CNC32 / RS-CNC32 de
[maker.fr](https://www.makerfr.com)**. L'écran ESP32 sert d'interface autonome
(DRO + pilotage + usinage) pour une carte **FluidNC** (firmware GRBL pour ESP32),
avec laquelle il dialogue en **UART**.

<img width="332" height="225" alt="Interface CNC32-usinage" src="https://github.com/user-attachments/assets/ee9943aa-3e0b-4242-975e-ada7120bb70e" />
<img width="332" height="225" alt="Interface CNC32-selection" src="https://github.com/user-attachments/assets/1389c321-da1a-404a-ab6e-6feb09a9033c" />
<img width="332" height="225" alt="Interface CNC32-fichiers" src="https://github.com/user-attachments/assets/cb90f99f-abbd-43ce-b72c-19e7fe8a1b52" />

Remplace le firmware d'origine
[mstrens/grbl_controller_esp32](https://github.com/mstrens/grbl_controller_esp32)
en modernisant l'interface (LVGL).

## Matériel ciblé

Écran TFT maker.fr **Version 2 / HSPI (4")** :

| Élément | Détail |
|---|---|
| MCU | ESP32 classique |
| Écran | ST7796 480×320, bus HSPI (27 MHz) |
| Tactile | XPT2046 résistif, bus VSPI séparé |
| Rétroéclairage | GPIO25 (PWM) |
| Carte SD | CS = GPIO5 (bus VSPI partagé avec le tactile) |
| Liaison FluidNC | `Serial2` — RX = GPIO16, TX = GPIO17, 115200 bauds |
| Nunchuk (Wii) | I2C — SDA = GPIO21, SCL = GPIO22 (jog analogique) |
| LED d'état | WS2812 (FastLED), couleur selon l'état machine |

> ⚠️ L'ESP32 classique ne possède pas d'USB-OTG : la liaison avec FluidNC se
> fait obligatoirement en **UART** (connecteur uart0 de la carte GRBL 32 bits),
> pas en USB Host.

Le brochage complet est figé dans [`include/board_config.h`](include/board_config.h).

> ℹ️ **FluidNC** : L'application a été testée et validée pour les versions **3.7.x** de FluidNC (à vérifier si vous utilisez une version plus récente).

## Installation Web Rapide (Recommandé)

Vous pouvez installer ou mettre à jour ce firmware directement depuis votre navigateur (Chrome, Edge, Opera) grâce à la technologie Web Serial, sans avoir à installer PlatformIO :

👉 **[Flasher le firmware depuis le Web](https://christianaubry.github.io/CNC32-TFT/)**

1. Branchez votre écran/ESP32 en USB.
2. Cliquez sur **Connect** sur la page web.
3. Sélectionnez le port COM de l'ESP32 et cliquez sur **Install**.

## Stack logicielle

- **Framework** : Arduino / PlatformIO (partition `huge_app`, pas d'OTA)
- **Graphique** : [LVGL](https://lvgl.io) 8.3 (tas système, `LV_MEM_CUSTOM`)
- **Pilote écran/tactile** : [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- **LED** : [FastLED](https://fastled.io)
- **Persistance** : NVS (`Preferences`) — position de changement d'outil, zéro
  pièce (WCO + masque d'axes), épaisseur palpeur, calibration tactile,
  luminosité LED, sensibilité Nunchuk, couleur d'accent
- **Watchdog** : task WDT 5 s (panic + `esp32_exception_decoder` au moniteur)

## Architecture

```
src/
  main.cpp              Boucle : LVGL + UART + streaming de job + état 5 Hz
                        + LED + Nunchuk (coopératif, mono-tâche)
  display/Display.*     LovyanGFX (ST7796 + XPT2046) ⇆ LVGL, splash PNG,
                        calibration tactile persistée en NVS
  fluidnc/FluidNC.*     Protocole GRBL/FluidNC (état, jog, zéro, homing, probe
                        2 passes, overrides, position outil + zéro pièce NVS,
                        accusés ok/error)
  job/JobRunner.*       Streaming G-code SD → FluidNC à comptage de caractères
                        (fenêtre 120 o / 16 lignes en vol), macro M6
  storage/Storage.*     Montage de la carte SD (VSPI partagé, 4 MHz)
  gcode/GcodeThumb.*    Détection des G-code, miniatures 60×60 8 bpp du parcours
                        (cache SD /thumbs, invalidation taille+mtime+troncature),
                        estimation du temps d'usinage
  ui/ui.*               UI v3 : rail 6 boutons + 7 pages (PILOT, ORIG, FICH,
                        DETAIL, USIN, PROBE, REGL), pool de miniatures,
                        chargement paresseux au scroll
  led/LedControl.*      LED WS2812 d'état (vert/cyan/jaune/rouge pulsé…)
  nunchuk/NunchukCtrl.* Jog analogique Nunchuk (I2C, 20 Hz, contrôle de flux)
include/
  board_config.h        Brochage figé
  lv_conf.h             Configuration LVGL 8.3
```

Séparation nette : `FluidNC` ne connaît rien de l'UI, `ui` ne connaît rien du
transport série, `JobRunner` n'orchestre que le flux SD→FluidNC. Le `main` relie
le tout.

## Interface (UI v3)

Rail de navigation à gauche (62 px), **6 onglets** : PILOT, ORIG, PALP, FICH,
USIN, REGL. Direction visuelle « instrument pro » : fond quasi-noir, **couleur
d'accent configurable** (Bleu/Cyan/Vert/Ambre/Orange/Violet, persistée en NVS,
appliquée après reboot), cibles tactiles généreuses (tactile résistif).

### PILOT — Pilotage

- **DRO compact** X/Y/Z double lecture : **M** (machine) et **P** (pièce,
  `?.??` tant que le zéro pièce n'est pas défini).
- **Croix de jog XY + colonne Z** (cibles 60 px). Appui court = pas incrémental
  (0.01 / 0.1 / 1 / 10 mm) ; **appui long ≥ 1 s = jog continu**, annulé au
  relâchement (`0x85`). Si la machine est référencée, la distance est **bornée
  aux limites machine** (pas de X/Y négatif, pas de Z positif).
- **Homing** (`$H`, coche verte une fois validé) et **Déverrouiller** (actif
  seulement en alarme : jog-cancel + soft-reset + `$X`).

### ORIG — Origines & points

- **Origine pièce** (= point de palpage) : définir ici (`G10 L20 P0`), mise à
  zéro par axe, **aller à l'origine** (Z machine à 0 d'abord, puis Y, puis X).
- **Point de changement d'outil** (coord. machine, persistant NVS) : définir /
  aller (`G53`, Z d'abord, broche stoppée) / effacer.
- Écran **voilé tant que le homing n'a pas été fait** (overlay bloquant).
- Le zéro pièce est **persisté en NVS** et restauré vers GRBL au boot
  (`G10 L2 P1`).

### FICH — Fichiers (carte SD) + DETAIL

- Liste des G-code et dossiers (navigation, tri : dossiers puis fichiers par
  date), **miniature du parcours** + taille + **temps d'usinage estimé**.
- Miniatures **chargées paresseusement** quand le défilement s'arrête (max 4
  simultanées, pool statique), déchargées pendant le scroll.
- Sélection → **vue détaillée** (grande miniature, temps estimé) avec
  **lancement conditionné** : homing + zéro pièce requis (pastilles d'état).
  Au lancement, retour automatique à l'origine pièce si on n'y est pas.

### USIN — Usinage (job en cours)

- Progression (barre + %, offset d'octets lus), ligne `L…/…`, temps écoulé /
  restant (basé sur l'estimation de la miniature, sinon extrapolé), position
  live, aperçu du parcours.
- **Override d'avance** temps réel −10 % / 100 % / +10 % (octets GRBL 0x90…).
- **Pause / Reprendre** (`!` / `~`) et **Stop** (soft-reset, vide le planner).
- Retour automatique à FICH 30 s après la fin du job.

### PALP — Palpage Z

- Épaisseur du palpeur réglable ±0,1 mm (persistée NVS). **Palper Z** lance un
  cycle **2 passes** (`G38.2` rapide 150 mm/min → retrait 2 mm → lente
  30 mm/min), fixe le Z pièce à l'épaisseur puis remonte de 5 mm.

### REGL — Réglages

- Couleur de référence (accent), calibration tactile, intensité LED WS2812,
  sensibilité Nunchuk.

## Changement d'outil (M6)

Quand le job rencontre un `M6`, `JobRunner` injecte une macro : `G53 G0 Z0` →
`M5` → pause 2 s → déplacement au point de changement d'outil (ou X0 Y0
machine) → **pause** (état `WaitToolChange`). Après changement physique,
« Reprendre » ramène la machine à l'origine et bascule sur l'écran PALP
(état `WaitProbe`). Une fois le palpage terminé (`[PRB:…]` détecté), retour à
l'écran USIN ; « Reprendre » relance la broche (`M3` + 3 s) et le flux.

## Jog Nunchuk

Polling I2C à 20 Hz. Jog sur l'axe dominant uniquement (pas de diagonale),
vitesse proportionnelle à la déflexion × sensibilité, petites commandes de
0,1 s de trajet pour garder le planner alimenté (arrêt quasi instantané au
relâchement). Contrôle de flux : max 3 commandes non acquittées. Ré-init
automatique du Nunchuk en cas d'erreur I2C.

## Couche FluidNC (protocole)

- **État** : rapports `?` à 5 Hz, parsing `MPos`/`WPos`/`WCO`/`FS`, déduction
  WPos↔MPos via le WCO.
- **Flags de session** : `isHomed()` (validé au retour `Idle` après `$H`,
  invalidé par alarme / soft-reset) et `isWorkZeroSet()` (X et Y mis à zéro ;
  Z optionnel, via masque d'axes persisté).
- **Palpage** : `probeZ(épaisseur, feedRapide, feedLent, profondeurMax)` +
  détection de fin (`[PRB:…]`).
- **Accusés** : `takeAck(&error)` expose les `ok`/`error:` (streaming +
  contrôle de flux Nunchuk).

## Streaming de jobs (JobRunner)

Streaming **à comptage de caractères** : jusqu'à 16 lignes / 120 octets en vol
(< 128 o du buffer RX GRBL), dépilées au fil des accusés `ok`. Les commentaires
(`;…`, `( … )`) et lignes vides sont retirés. Une erreur GRBL déclenche un
*feed hold* et arrête le job. La progression est estimée sur l'offset d'octets ;
le total de lignes est compté à l'ouverture.

## Miniatures G-code (aperçu du parcours)

Deux passes en streaming sur le fichier (jamais chargé en RAM) : boîte
englobante des coupes puis rastérisation **60×60, 8 bpp** (intensité cumulée par
passage). Seules les avances de coupe (`G1/G2/G3` avec Z ≤ 0) sont tracées.
Cache sous `/thumbs/<chemin-aplati>.thb` avec en-tête (magic, dimensions,
taille et mtime du source, temps estimé) ; invalidation sur incohérence ou
troncature ;
nettoyage des caches orphelins. Le temps d'usinage est estimé pendant la passe 1
(distance / feed, G0 à 2000 mm/min).

> ⚠️ Ce n'est **pas** un rendu de la matière enlevée mais le parcours d'outil
> projeté (vue de dessus).

## Compilation / flash

```bash
pio run                      # compiler
pio run --target upload      # flasher l'ESP32 de l'écran
pio device monitor           # console série (115200, décodeur d'exceptions)
```

## Calibration du tactile

Au premier démarrage (pas de calibration en NVS), la calibration se lance
automatiquement. Relançable depuis **REGL → Calibration tactile** ; le résultat
est persisté en NVS (namespace `display`).

> ℹ️ Les libellés de l'UI sont en français sans accents (les polices Montserrat
> intégrées de LVGL couvrent l'ASCII ; voir note dans `include/lv_conf.h` pour
> compiler une police Latin-1).

---

## Licence

Ce projet est sous licence **MIT**. Voir le fichier [LICENSE](LICENSE) pour plus de détails. Vous êtes libre de l'utiliser, le modifier et le distribuer.

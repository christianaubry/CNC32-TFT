# CNC32-TFT app

Réécriture moderne du firmware de l'écran tactile de la **CNC32 / RS-CNC32 de
[maker.fr](https://www.makerfr.com)**. L'écran ESP32 sert d'interface autonome
(DRO + pilotage + usinage) pour une carte **FluidNC** (firmware GRBL pour ESP32),
avec laquelle il dialogue en **UART**.

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

👉 **[Flasher le firmware depuis le Web](#)** *(Le lien sera ajouté une fois publié sur GitHub Pages)*

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

## État des lieux (analyse du 2026-07-15)

Analyse complète du code orientée **gestion de la RAM** et **robustesse**.

### Points forts (à conserver)

- Boucle coopérative mono-tâche : pas de problème de concurrence, watchdog 5 s
  avec panic + décodeur d'exceptions au moniteur.
- **Pool statique de miniatures** (5 blocs de 7,2 Ko) contre la fragmentation,
  limite stricte de 4 vignettes en liste, déchargement agressif au scroll.
- Fenêtre de streaming bornée à **120 o < 128 o** du buffer GRBL.
- Cache de miniatures avec invalidation robuste (magic + taille + mtime +
  détection de troncature après crash).
- G-code toujours parcouru **en streaming**, jamais chargé en RAM.
- Garde-fou anti-débordement sur la ligne UART reçue (`_rx` > 200 → purge).
- Retours de `malloc` et de création d'objets LVGL vérifiés.

### 🔴 Robustesse — bugs identifiés (par priorité)

1. **Watchdog non nourri dans `JobRunner::countLines()`**
   ([JobRunner.cpp:194](src/job/JobRunner.cpp#L194)). La lecture complète du
   fichier au lancement du job ne fait aucun `esp_task_wdt_reset()`. À
   ~300-400 Ko/s (SD 4 MHz), le WDT de 5 s **provoque un reboot au lancement de
   tout G-code > ~1,5-2 Mo**. Fix : un `esp_task_wdt_reset()` dans la boucle.

2. **Vol d'accusés `ok` entre Nunchuk et JobRunner.** `FluidNC::takeAck()` est
   un compteur global consommé par deux clients
   ([JobRunner.cpp:44](src/job/JobRunner.cpp#L44),
   [NunchukCtrl.cpp:62](src/nunchuk/NunchukCtrl.cpp#L62)). Pendant un job, la
   machine repasse brièvement en `Idle` entre deux mouvements : le Nunchuk avale
   alors les acks du job → `_bytesInFlight` ne redescend plus → **le streaming
   se bloque**. Fix : ne pas consommer d'acks (ni jogger) dans
   `NunchukCtrl::update()` quand `job->isActive()`.

3. **Conflit de broche probable : `LED_PIN 32` = `PIN_TFT_CS 32`**
   ([LedControl.cpp:4](src/led/LedControl.cpp#L4) vs
   [board_config.h:13](include/board_config.h#L13)). Chaque `FastLED.show()`
   (à chaque tour de boucle en état pulsant : Alarm/Sleep/Unknown) bit-bang le
   CS de l'écran. À vérifier contre le câblage réel ; dans tous les cas, la
   broche LED devrait venir de `board_config.h`.

4. **`FluidNC::unlock()` bloque ~1,05 s** (`delay` 50+500+500,
   [FluidNC.cpp:237](src/fluidnc/FluidNC.cpp#L237)) dans un callback LVGL. Le
   buffer RX série (256 o) se remplit en ~22 ms à 115200 bauds : rapports
   d'état et messages de reset perdus, UI gelée. Tolérable après un soft-reset,
   mais une machine à états non bloquante serait plus propre.

5. **Gels d'UI longs dans les callbacks tactiles** : `cb_nav` →
   `gthumb::scanAndGenerate()` et `cb_detail_go` → `start()` → `countLines()`
   s'exécutent dans le handler d'événement — ni rendu, ni tactile, ni
   `fluid.update()` pendant potentiellement des dizaines de secondes (SD pleine
   de nouveaux fichiers). Piste : génération incrémentale depuis `ui_task()`
   (une miniature par tour), l'ossature `THUMB_IDLE/THUMB_SCANNING` existe déjà.

Points secondaires :

- Sur `error:` GRBL en cours de job, `feedHold()` mais **la broche continue de
  tourner** — envoyer aussi `M5` (ou soft-reset comme `stop()`).
- **Pas de détection de liaison morte** : si FluidNC ne répond plus, l'UI
  affiche indéfiniment le dernier état. Ajouter un timeout (> 1 s sans rapport
  → état OFFLINE).
- La restauration du zéro pièce au boot envoie toujours `G10 L2 P1` (= G54)
  même si un autre WCS était actif.
- [ui.cpp:1660](src/ui/ui.cpp#L1660) : style appliqué à `g_lblState` hors du
  garde `if (g_lblState)` (théorique).

### 🟠 RAM — constat et recommandations

Budget global confortable (~300 Ko DRAM) : tampon de rendu LVGL 38,4 Ko
statique, pool miniatures ≤ 36 Ko, widgets LVGL ~40-70 Ko sur le tas, splash
125 Ko en flash. Le vrai risque n'est pas la quantité mais la **fragmentation
du tas sur les longues sessions** (un pendant CNC reste allumé des heures) :

1. **LVGL sur le tas système (`LV_MEM_CUSTOM 1`) + churn massif** :
   `rebuild_file_list()` détruit/recrée ~150 objets à chaque visite de FICH, et
   `ui_update()` appelle `lv_label_set_text()` à 5 Hz sur ~10 labels (LVGL
   réalloue à chaque appel, même texte identique).
   → **Ne réécrire les labels que si le texte a changé** (gain immédiat).
   → Envisager le retour à l'allocateur interne TLSF (`LV_MEM_CUSTOM 0`,
   `LV_MEM_SIZE` 64-80 Ko) pour confiner la fragmentation hors du tas système ;
   valider avec `lv_mem_monitor()`.
   → Instrumenter : logger périodiquement `esp_get_free_heap_size()` et
   `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (la métrique qui révèle
   la fragmentation avant le crash).

2. **Churn de `String` Arduino dans les chemins chauds** :
   - `FluidNC::parseStatusReport()` crée ~10-15 `String` temporaires **par
     rapport, 5 fois par seconde** — à réécrire sur `char*` (comme le parseur
     de `GcodeThumb`, déjà exemplaire).
   - `JobRunner` : `_nextCmd` + `_macroQueue[8]` en `String` = une allocation
     par ligne streamée ; des `char[80]` fixes suffisent.
   - `scanDirRecurse()` : `std::vector<String>` qui grossit avec la SD, au pire
     moment (pendant la génération).

3. **Tampon d'affichage simple + DMA inutile**
   ([Display.cpp:89](src/display/Display.cpp#L89)) : avec un seul tampon,
   `pushPixelsDMA` + `endWrite` est de fait synchrone. **Deux tampons de 20
   lignes** (même empreinte totale) permettraient de dessiner pendant que le
   DMA pousse — fluidité gratuite.

4. **Pile** : `generate()` pose 3,6 Ko de `bits[]` sur la pile, appelé au fond
   d'une chaîne callback LVGL → récursion `scanDirRecurse` (une frame + `File`
   par niveau). Marge faible sur la loopTask (8 Ko). Passer `bits[]` en
   `static` et/ou limiter la profondeur de récursion.

5. **Optimisation possible** : les vignettes 8 bpp sont converties en
   TRUE_COLOR 16 bpp (7,2 Ko/vignette) ; `LV_IMG_CF_INDEXED_8BIT` (palette +
   3,6 Ko) réduirait le pool de ~40 % — utile seulement pour remonter la limite
   de 4 vignettes simultanées.

### Priorités suggérées

1. `esp_task_wdt_reset()` dans `countLines()` — 1 ligne, évite un reboot en
   production.
2. Réserver les acks au JobRunner quand un job est actif — bug de streaming.
3. Vérifier/corriger `LED_PIN 32` vs `PIN_TFT_CS 32`.
4. Labels DRO : n'écrire que si changement — fragmentation à 5 Hz.
5. `M5` sur erreur GRBL + détection de liaison FluidNC morte — sécurité machine.

### Pistes v4

- Arrêt d'urgence permanent en barre haute.
- État OFFLINE si FluidNC muet (timeout de rapport d'état).
- Génération de miniatures incrémentale (non bloquante) depuis `ui_task()`.
- Miniatures « option B » : carte de profondeur Z colorée (pseudo-3D).
- Police Latin-1 pour les accents.

## Licence

Ce projet est sous licence **MIT**. Voir le fichier [LICENSE](LICENSE) pour plus de détails. Vous êtes libre de l'utiliser, le modifier et le distribuer.

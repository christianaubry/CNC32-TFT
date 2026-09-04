# Mise à jour — écran Pilotage (ui.cpp)

Remplace `src/ui/ui.cpp` par le fichier `ui.cpp` fourni. Aucune autre source ni
header n'est modifié (FluidNC, JobRunner, Display, board_config… inchangés).
Seul l'écran **Pilotage** et la logique de jog/pas évoluent ; les écrans
Fichiers / Usinage / Palpage / Réglages sont identiques.

## Changements

1. **DRO compact** — X / Y / Z regroupés sur **une seule bande** (au lieu de 3
   lignes). Par axe : `M` = position machine (gros, blanc), `P` = position pièce
   (petit, bleu). Légende M/P en barre d'état.

2. **Boutons →0** — déplacés en rangée fine sous le DRO (`X→0 / Y→0 / Z→0`),
   plus de bouton →0 par ligne.

3. **Croix de jog** — vraie croix directionnelle X/Y + colonne Z dédiée, cibles
   tactiles 48 px. Lecture du pas actif au centre de la croix.

4. **Molette de pas** — 9 crans : `0.01 · 0.05 · 0.1 · 0.5 · 1 · 5 · 10 · 50 ·
   100 mm`. Valeur active **toujours au centre** ; taper la valeur de gauche /
   droite décale la liste (`cb_step_prev` / `cb_step_next`). « CONT » supprimé.

5. **Jog appui court / appui long** (`cb_jog`) :
   - **appui court** → déplacement d'**un pas** (valeur de la molette) ;
   - **appui long ≥ 1 s** (`JOG_LONG_MS`) → **déplacement continu** : un grand
     jog (`JOG_CONT_DIST_XY` = 1000 mm, `JOG_CONT_DIST_Z` = 200 mm) envoyé puis
     **`jogCancel()` au relâchement**.
   La détection du seuil se fait sur `LV_EVENT_PRESSING` (millis()), sans
   dépendre du long-press LVGL.

6. **Identification visuelle** — tout bouton cliquable porte un **bord bleu +
   texte bleu** ; les zones d'affichage gardent un bord neutre. Le **badge
   d'état** devient un **voyant** (pastille de couleur + texte blanc) au lieu
   d'un cadre bleu qui ressemblait à un bouton.

## Réglages rapides

- Seuil d'appui long : `JOG_LONG_MS` (ms).
- Distances du jog continu : `JOG_CONT_DIST_XY` / `JOG_CONT_DIST_Z` (mm).
- Pas disponibles : tableaux `STEP_VALUES[9]` / `STEP_LABELS[9]`
  (et `STEP_COUNT`). Pas par défaut : `g_stepIndex = 4` (= 1 mm).

> Note polices : les libellés restent en ASCII (« Deverr. », « Zero piece »,
> « piece ») pour les Montserrat intégrées de LVGL, comme dans la version
> d'origine.

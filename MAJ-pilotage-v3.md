# Mise à jour — Pilotage tactile + écran Origines (v3)

Remplace **`src/ui/ui.cpp`** par le fichier fourni. `ui.h` est fourni aussi
(commentaire d'en-tête mis à jour uniquement — **aucun changement d'API**).
Aucune autre source ni header n'est modifié : **FluidNC, JobRunner, Display,
board_config… restent inchangés.**

## Fichiers du paquet

| Fichier | À copier vers | Modifié |
|---|---|---|
| `src/ui/ui.cpp` | `src/ui/ui.cpp` | **oui** (à remplacer) |
| `src/ui/ui.h`   | `src/ui/ui.h`   | commentaire seulement (optionnel) |

> `FluidNC.h` / `FluidNC.cpp` **ne sont pas touchés** : « Aller à l'origine »
> est réalisé dans `ui.cpp` via `g_fluid->sendLine(...)`, en n'utilisant que
> l'API publique existante (`zeroAll`, `gotoToolChangePos`, `setToolChangePos`,
> `clearToolChangePos`, `zeroAxis`, `sendLine`).

## Changements

### 1. Écran Pilotage — épuré pour le doigt
- **Croix de jog XY** en cibles **60 px** (X-/X+/Y-/Y+), lecture du pas au
  centre. Colonne **Z** dédiée (Z+/Z-) en deux grandes cibles de 93 px.
- **Molette de pas** agrandie : `< 1 >`, valeur active au centre, flèches
  cliquables (34 px). L'appui court/long du jog est inchangé.
- **Homing** et **Déverrouiller** en boutons pleine largeur.
- Les boutons de définition de point (« Zéro pièce », « Pos. outil ») ont
  **quitté cet écran** → déplacés vers l'écran **ORIG**.

### 2. Nouvel écran ORIG « Origines & points »
Ajouté au rail entre **PILOT** et **FICH**. Deux cartes :

- **Origine pièce (= point de palpage / Z0)**
  - `Définir ici` → `zeroAll()` (X=Y=Z=0 dans le WCS).
  - `Aller à l'origine` → `sendLine`: `G90` puis `G0 Z5` (sécurité,
    `ORIGIN_SAFE_Z`) puis `G0 X0 Y0`. **On ne descend pas jusqu'à Z0** (palpeur
    / matière) : on s'arrête au-dessus. Garde-fou : origine requise.
  - `X→0 / Y→0 / Z→0` → `zeroAxis(i)`.
- **Point de changement d'outil (coord. machine, NVS)**
  - `Définir ici` → `setToolChangePos()` (capture MPos + persiste).
  - `Aller au chgt. outil` → `gotoToolChangePos()` (G53, Z d'abord).
  - `Effacer le point` → `clearToolChangePos()`.

Les pastilles d'état (vert = défini) et les coordonnées se mettent à jour à
l'entrée de l'écran et après chaque action (`refresh_orig()`).

### 3. Écran Réglages — allégé
- Section **Broche** supprimée (régime fixe).
- Carte **Position chgt. outil** supprimée (déplacée vers ORIG) ; une note
  renvoie vers ORIG.
- Conservé : Calibration tactile, Vitesses de jog.

### 4. Écran Palpage — allégé
- Section **Vitesse de palpage** supprimée (vitesses fixes 150 puis 30 mm/min,
  toujours appliquées par `probeZ`). Conservé : Épaisseur palpeur, Dernier
  palpage, Palper Z.

## Réglages rapides (en haut de `ui.cpp`)
- `ORIGIN_SAFE_Z` (mm) — hauteur de sécurité au-dessus de l'origine avant la
  traverse XY de « Aller à l'origine ». Défaut : `5.0`.
- `JOG_LONG_MS`, `JOG_CONT_DIST_XY/Z`, `STEP_VALUES[9]` — inchangés.

## À vérifier sur machine
- **« Aller à l'origine »** suppose Z0 = haut de la pièce (comme le palpage).
  Si ta convention diffère, ajuste `ORIGIN_SAFE_Z` ou la séquence dans
  `cb_goto_origin()`.

> Note polices : libellés en ASCII (« Definir ici », « Deverrouiller »,
> « Origine piece », « chgt. outil »…) pour les Montserrat intégrées de LVGL,
> comme dans la version d'origine.

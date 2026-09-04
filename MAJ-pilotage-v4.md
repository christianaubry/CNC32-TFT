# Mise à jour — Pilotage / Origines (v4)

Reprise des ajustements visuels validés sur les maquettes ORIG + PILOT.

## Fichiers du paquet

| Fichier | À copier vers | Modifié |
|---|---|---|
| `src/ui/ui.cpp`       | `src/ui/ui.cpp`       | **oui** (à remplacer) |
| `include/lv_conf.h`   | `include/lv_conf.h`   | **oui** (police 22 px activée) |

> Aucune autre source n'est touchée. API FluidNC / JobRunner inchangée.

## Changements

### 1. `lv_conf.h` — police 22 px
- Ajout de `#define LV_FONT_MONTSERRAT_22 1` (utilisée par Homing et la valeur
  centrale de la molette de pas). Nouvelle macro `F22` dans `ui.cpp`.

### 2. Écran ORIG — hiérarchie des actions inversée
Dans les **deux** cartes, l'action de déplacement passe **en haut** (bouton
principal, bordure bleue) et **« Définir ici »** passe **en dessous**, plus
discret (hauteur réduite 50 → 40 px, fond `C_CARD2`, bordure neutre `C_LINE`,
texte `C_SUB`, police 14).

- **Origine pièce** : `Aller a l'origine piece` (haut) puis `Definir ici` (bas).
  Le libellé du bouton « aller » est renommé **« Aller a l'origine piece »**.
- **Point chgt. outil** : `Aller au chgt. outil` (haut) puis `Definir ici` (bas).

> Les callbacks restent identiques : `cb_goto_origin` / `cb_zeroall`,
> `cb_tcp_goto` / `cb_tcp_set`.

### 3. Écran PILOT — typographie agrandie (lisibilité au doigt)
- **Rail de navigation** : libellés 12 → **14 px** (impacte tous les écrans).
- **DRO** : lettres d'axe X/Y/Z 18 → **24 px**.
- **Croix de jog XY + colonne Z** : 20 → **28 px**.
- **Pas actif au centre de la croix** : 18 → **24 px**.
- **Valeur de la molette de pas** : 16 → **22 px**.
- **Homing** : 16 → **22 px** ; **Déverrouiller** : 16 → **20 px**.
- Légende `machine` / `piece` : décalée de 342 → **346 px** (respiration).
- **DRO — précision** : les 6 valeurs (M et P, X/Y/Z) passent de 3 → **2 décimales** (centièmes) : `%.3f` → `%.2f`, placeholders `?.???` → `?.??`.

## ⚠️ Note « gras » (font-weight 700)
Les maquettes affichaient le rail en **gras**. Les polices Montserrat intégrées
de LVGL sont **mono-graisse** (regular) : il n'existe pas de variante bold sans
embarquer un binaire de police dédié. Le rail est donc passé en **14 px regular**
(taille augmentée, sans gras). Si le gras est souhaité, il faut générer et
ajouter une police Montserrat SemiBold/Bold via `lv_font_conv` — me le dire.

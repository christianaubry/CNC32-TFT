# Patch — Couleur de référence sélectionnable (écran REGL)

Fichier modifié : `src/ui/ui.cpp` (remplace l'existant).

## Ce que ça ajoute
- **Sélecteur de couleur** dans REGL (carte « Couleur de référence », `lv_dropdown`).
- 6 références : Bleu (défaut), Cyan, Vert, Ambre, Orange, Violet.
- **Sauvegarde immédiate** à la sélection dans NVS : namespace `"ui"`, clé `accent_idx`
  (même mécanisme que `led_bright` / `nunchuk_sens`).
- **Redémarrage automatique** après sauvegarde (`ESP.restart()`).

## Modifications détaillées
1. **Palette** — `C_ACC`, `C_ACC_BG`, `C_ACC_TX` passent de `#define` à variables
   `lv_color_t` (recalculables). `C_ACC_BG` est dérivé par `lv_color_mix` (~16 %
   d'accent sur le fond) ; `C_ACC_TX` reste un sombre lisible.
   **Bleus auparavant codés en dur, désormais dérivés de l'accent :**
   `C_THUMB` (miniatures de parcours) = accent éclairci.
   **L'axe Z (`C_Z`) reste un bleu fixe** : convention X rouge / Y vert / Z bleu.
   Autres couleurs sémantiques conservées : OK vert, WARN ambre, ERR rouge.
2. **`apply_accent(idx)`** + table `g_accentPalette[]` / `g_accentIdx`.
3. **`ui_init()`** — lecture de `accent_idx` (défaut Orange) et `apply_accent()` **avant**
   `build_rail()` / `build_*()`, pour que rail, logo et boutons prennent la couleur.
4. **`build_settings()` (REGL)** — carte « Couleur de référence » + dropdown stylé sombre,
   **placée en haut**. Les 4 cartes (Couleur 48-102, Calibration 108-148, LED 154-210,
   Nunchuk 216-272) sont recompactées pour **tenir dans 320 px sans ascenseur** ; le
   défilement vertical a été supprimé.
5. **Palette de fonds graphite neutre** — `C_BG/RAIL/CARD/CARD2/BORDER/LINE` ne sont plus
   bleu-gris froids mais neutres (`0x111113`…`0x34343A`), pour ne pas jurer avec l'accent.
6. **Bleus résiduels supprimés (3 sources que les `#define` ne couvraient pas) :**
   - **Thème LVGL par défaut** — réinitialisé dans `ui_init()` via `lv_theme_default_init`
     (primaire = accent, secondaire = gris neutre, mode sombre). C'est lui qui colorait
     en **bleu** les sliders, scrollbars et surbrillances, indépendamment de la palette.
   - **Sliders REGL** — `LV_PART_INDICATOR` et `LV_PART_KNOB` forcés sur l'accent,
     piste sur `C_LINE`.
   - **Gris de texte** — `C_TXT/C_MUTE/C_SUB` étaient des bleu-gris (texte du rail) ;
     neutralisés en gris purs (`0xF0F0F2 / 0x6E6E73 / 0xA8A8AE`).

## Pourquoi un redémarrage
`C_ACC` est lue à la **construction** de chaque page (rail, boutons Homing / Palper /
Lancer, badges). Les widgets déjà créés ne sont pas re-stylés à la volée ; le reboot
reconstruit l'UI avec la nouvelle valeur lue depuis NVS. La sélection est persistée
avant le reboot, donc elle survit au redémarrage.

## Couleur par défaut
`ACCENT_DEFAULT = 4` (**Orange**) — appliqué au tout premier boot (clé NVS absente).
Pour changer le défaut, modifiez cette constante (0=Bleu, 1=Cyan, 2=Vert, 3=Ambre,
4=Orange, 5=Violet).

⚠️ Si la clé `accent_idx` existe déjà en NVS (essai précédent), c'est elle qui prime :
le nouveau défaut ne s'applique pas tant que vous n'effacez pas la NVS **ou** ne
choisissez pas une couleur dans REGL.

## Application
Copier `src/ui/ui.cpp` par-dessus celui de votre projet, recompiler et flasher.
Aucune autre dépendance (`Preferences.h`, `lvgl.h`, `Arduino.h` déjà inclus).

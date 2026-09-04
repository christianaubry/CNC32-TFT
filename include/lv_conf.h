// lv_conf.h — configuration LVGL 8.3 pour CNC32-TFT app.
// Seules les options surchargées sont définies ; le reste reprend les valeurs
// par défaut de lv_conf_internal.h.
#pragma once

#include <stdint.h>

// --- Couleur ----------------------------------------------------------------
#define LV_COLOR_DEPTH    16
// L'inversion d'octets est gérée côté LovyanGFX (setSwapBytes), pas par LVGL.
#define LV_COLOR_16_SWAP  0

// --- Mémoire ----------------------------------------------------------------
#define LV_MEM_CUSTOM     1
// Utilisera le tas (heap) système via malloc/free, évitant l'OOM sur les listes longues.
// L'ancienne limite statique est désactivée.
// #define LV_MEM_SIZE       (48U * 1024U)

// --- Base de temps (utilise millis() d'Arduino) -----------------------------
#define LV_TICK_CUSTOM              1
#define LV_TICK_CUSTOM_INCLUDE      "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// Rafraîchissement / cadence d'entrée
#define LV_DISP_DEF_REFR_PERIOD  20
#define LV_INDEV_DEF_READ_PERIOD 20

// --- Polices ----------------------------------------------------------------
// L'UI v2 utilise une échelle typographique plus riche (rail, DRO, titres).
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_MONTSERRAT_40  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

// Les libellés contiennent des caractères accentués (é, è, à…). Les polices
// Montserrat intégrées de LVGL couvrent l'ASCII : pour un rendu correct des
// accents, activer la plage Latin-1 supplémentaire à la compilation de la
// police (lv_font_conv) ou fournir une police personnalisée. À défaut, les
// libellés restent lisibles en ASCII (les accents peuvent manquer).

// --- Widgets / fonctionnalités utilisées ------------------------------------
#define LV_USE_CANVAS    1   // rendu des miniatures G-code (1 bpp -> image)
#define LV_USE_BAR       1   // barre de progression d'usinage

// --- Divers -----------------------------------------------------------------
#define LV_USE_LOG       0
#define LV_USE_PERF_MONITOR 0

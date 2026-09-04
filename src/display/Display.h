// Display.h — initialisation de l'écran (LovyanGFX) et pont vers LVGL.
#pragma once

#include <lvgl.h>

// Initialise le panneau ST7796, le tactile XPT2046 et enregistre les pilotes
// d'affichage et d'entrée LVGL. À appeler une fois après lv_init() interne.
void display_init();

// Règle la luminosité du rétroéclairage (0..255).
void display_set_brightness(uint8_t value);

// Lance la calibration tactile interactive de LovyanGFX et imprime les valeurs
// brutes sur le port série (à reporter dans board_config.h si besoin).
void display_calibrate();

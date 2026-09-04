// board_config.h — brochage figé de l'écran TFT maker.fr Version 2 / HSPI (4")
// MCU : ESP32 classique — voir mémoire projet "hardware-spec".
// Source de vérité : firmware d'origine mstrens/grbl_controller_esp32.
#pragma once

// --- Écran ST7796 480x320 sur bus HSPI -------------------------------------
#define TFT_HOST       HSPI_HOST
#define TFT_SPI_FREQ   27000000
#define PIN_TFT_SCLK   14
#define PIN_TFT_MOSI   13
#define PIN_TFT_MISO   12
#define PIN_TFT_DC     27
#define PIN_TFT_CS     32
#define PIN_TFT_RST    33
#define PIN_TFT_BL     25   // rétroéclairage (TFT_LED)

#define TFT_PANEL_W    320  // dimensions natives du panneau (portrait)
#define TFT_PANEL_H    480
#define TFT_ROTATION   3    // paysage 480x320
#define SCREEN_W       480
#define SCREEN_H       320

// --- Tactile XPT2046 (résistif) sur bus VSPI séparé -------------------------
#define TOUCH_HOST     VSPI_HOST
#define TOUCH_SPI_FREQ 1000000
#define PIN_TOUCH_SCLK 18
#define PIN_TOUCH_MOSI 23
#define PIN_TOUCH_MISO 19
#define PIN_TOUCH_CS   26
#define PIN_TOUCH_IRQ  -1

// Valeurs de calibration brutes par défaut (à affiner via display_calibrate()).
#define TOUCH_RAW_XMIN 3900
#define TOUCH_RAW_XMAX 300
#define TOUCH_RAW_YMIN 200
#define TOUCH_RAW_YMAX 3700

// --- Carte SD (bus VSPI partagé avec le tactile) ----------------------------
#define PIN_SD_CS      5

// --- Port Nunchuk (I2C) -----------------------------------------------------
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22

// --- Liaison série vers la carte FluidNC (connecteur uart0 GRBL 32 bits) ----
#define FLUIDNC_SERIAL   Serial2
#define PIN_FLUIDNC_RX   16   // RX de l'ESP32 <- TX de FluidNC
#define PIN_FLUIDNC_TX   17   // TX de l'ESP32 -> RX de FluidNC
#define FLUIDNC_BAUD     115200

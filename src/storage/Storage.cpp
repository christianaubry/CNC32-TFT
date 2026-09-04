// Storage.cpp — montage SD sur le bus VSPI partagé.
#include "Storage.h"

#include <SPI.h>
#include "board_config.h"

static SPIClass s_sdSpi(VSPI);
static bool     s_ready = false;

bool storage_begin() {
  // Mêmes pins que le tactile (bus VSPI), CS dédié à la SD.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);

  s_sdSpi.begin(PIN_TOUCH_SCLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_SD_CS);
  // 4 MHz : max safe speed for shared buses or level-shifter SD modules
  s_ready = SD.begin(PIN_SD_CS, s_sdSpi, 4000000);
  return s_ready;
}

bool storage_ready() {
  return s_ready;
}

bool storage_refresh() {
  SD.end();
  // On tente de ré-initialiser avec la même fréquence de 4 MHz
  s_ready = SD.begin(PIN_SD_CS, s_sdSpi, 4000000);
  return s_ready;
}

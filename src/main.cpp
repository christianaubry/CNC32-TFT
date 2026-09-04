// main.cpp — CNC32-TFT app : interface ESP32 pilotant FluidNC (GRBL) en UART.
//
// Boucle : LVGL (tâches + rendu) -> lecture UART FluidNC -> streaming d'un job
// G-code éventuel -> requête d'état périodique -> rafraîchissement de l'UI.
#include <Arduino.h>
#include <lvgl.h>
#include <esp_task_wdt.h>

#include "board_config.h"
#include "display/Display.h"
#include "fluidnc/FluidNC.h"
#include "gcode/GcodeThumb.h"
#include "job/JobRunner.h"
#include "storage/Storage.h"
#include "ui/ui.h"
#include "led/LedControl.h"
#include "nunchuk/NunchukCtrl.h"

static FluidNC   fluid;
static JobRunner job;
static NunchukCtrl nunchuk;
static uint32_t  lastStatusReq = 0;
static const uint32_t STATUS_PERIOD_MS = 200;  // 5 Hz de rafraîchissement DRO

void setup() {
  Serial.begin(115200);
  Serial.println(F("CNC32-TFT app — démarrage"));

  // Watchdog 5s
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = 5000,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  esp_task_wdt_init(&twdt_config);
#else
  esp_task_wdt_init(5, true);
#endif
  esp_task_wdt_add(NULL);

  // Carte SD : on initialise la SD en premier pour que
  // la librairie Arduino SPI prenne la main sur le bus VSPI. LovyanGFX
  // (display_init) s'y greffera ensuite en mode partagé (bus_shared = true).
  if (!storage_begin()) {
    Serial.println(F("SD: absente ou non montee"));
  }

  display_init();              // LovyanGFX + LVGL
  fluid.begin(FLUIDNC_SERIAL); // UART + relecture position outil persistée (NVS)
  job.begin(&fluid);           // streaming de jobs
  
  led_init(255);               // Init LED (UI will override brightness from preferences)
  
  ui_init(&fluid, &job);       // rail + 5 écrans
  nunchuk.begin();             // I2C Nunchuk
  
  fluid.requestStatus();       // premier rapport au plus tôt
}

void loop() {
  lv_timer_handler();          // moteur LVGL (rendu + entrées tactiles)
  fluid.update();              // consomme l'UART entrant (état + accusés)
  job.pump();                  // streaming du job en cours (send-response)
  esp_task_wdt_reset();        // Pet the watchdog

  const uint32_t now = millis();
  if (now - lastStatusReq >= STATUS_PERIOD_MS) {
    fluid.requestStatus();
    lastStatusReq = now;
  }

  if (fluid.hasNewStatus()) {
    ui_update(fluid.status());
    led_update_state(fluid.status().state);
  }
  ui_task();                   // progression / temps d'usinage
  led_tick();                  // animation LED
  nunchuk.update(&fluid);      // polling nunchuk

  delay(5);
}

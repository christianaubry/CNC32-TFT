// ui.h — interface LVGL v3 pour le controleur CNC32-TFT (480x320 paysage).
//
// Six ecrans accessibles par un rail de navigation a gauche :
//   Pilotage  — DRO double (machine + piece), croix de jog XY + Z tactile
//               (cibles 60 px), molette de pas, homing, deverrouillage.
//   Origines  — definition + deplacement de l'origine piece (= point de
//               palpage) et du point de changement d'outil.
//   Fichiers  — liste SD avec miniatures du parcours d'outil ; apercu detaille
//               et lancement d'usinage conditionne (homing + zero piece).
//   Usinage   — progression du job, override d'avance, pause/stop.
//   Palpage   — palpage Z (zero hauteur outil sur palpeur).
//   Reglages  — calibration tactile, vitesses de jog.
#pragma once

#include "fluidnc/FluidNC.h"
#include "job/JobRunner.h"

// Construit l'interface. `fluid` et `job` doivent rester valides toute la duree.
void ui_init(FluidNC* fluid, JobRunner* job);

// Met a jour l'affichage temps reel (DRO, etat, feed/broche) depuis un rapport.
void ui_update(const MachineStatus& st);

// A appeler souvent depuis loop() : rafraichit l'ecran d'usinage (progression,
// temps, position) tant qu'un job est actif.
void ui_task();

extern int g_nunchukSensitivity;

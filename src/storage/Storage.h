// Storage.h — montage de la carte SD (bus VSPI partagé avec le tactile).
//
// La SD et le tactile XPT2046 partagent le même bus VSPI (SCLK=18, MISO=19,
// MOSI=23), chacun avec son propre CS — exactement comme le firmware d'origine
// mstrens. Les transactions étant sérialisées dans la boucle principale, il
// suffit de monter la SD sur un SPIClass(VSPI) dédié.
#pragma once

#include <SD.h>

// Monte la carte SD. À appeler une fois au setup(), après display_init()
// (le bus VSPI est alors déjà câblé par LovyanGFX). Renvoie true si l'initialisation a réussi (carte présente et lisible).
bool storage_begin();

// true si la carte SD est considérée comme initialisée.
bool storage_ready();

// Force la libération du bus et relance storage_begin() (détection d'insertion/retrait).
bool storage_refresh();

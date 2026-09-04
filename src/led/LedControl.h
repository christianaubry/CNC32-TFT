#pragma once

#include <stdint.h>
#include "fluidnc/FluidNC.h"

// Initialize the LED with a default brightness (0-255)
void led_init(uint8_t defaultBrightness);

// Set the current brightness of the LED (0-255)
void led_set_brightness(uint8_t brightness);

// Update the current state of the machine for the LED
void led_update_state(MachineState state);

// Call this function in the main loop to handle animations (pulsing)
void led_tick();

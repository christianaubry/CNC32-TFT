#include "LedControl.h"
#include <FastLED.h>

#define LED_PIN     32
#define NUM_LEDS    1

CRGB leds[NUM_LEDS];

static MachineState currentState = MachineState::Unknown;
static uint8_t currentBrightness = 255;

void led_init(uint8_t defaultBrightness) {
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    led_set_brightness(defaultBrightness);
}

void led_set_brightness(uint8_t brightness) {
    currentBrightness = brightness;
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
}

void led_update_state(MachineState state) {
    if (currentState != state) {
        currentState = state;
    }
}

void led_tick() {
    CRGB baseColor = CRGB::Black;
    bool pulsing = false;
    uint8_t bpm = 0;
    uint8_t minDim = 0;
    uint8_t maxDim = 255;
    bool isWhite = false;

    switch (currentState) {
        case MachineState::Idle:
        case MachineState::Check:
            baseColor = CRGB::Green;
            break;
        case MachineState::Run:
        case MachineState::Jog:
            baseColor = CRGB::Cyan; // Cyan for job running
            break;
        case MachineState::Hold:
        case MachineState::Door:
            baseColor = CRGB::Yellow;
            break;
        case MachineState::Alarm:
            baseColor = CRGB::Red;
            pulsing = true;
            bpm = 60; // Fast pulse
            minDim = 20;
            maxDim = 255;
            break;
        case MachineState::Home:
            baseColor = CRGB::Magenta;
            break;
        case MachineState::Sleep:
        case MachineState::Unknown:
            baseColor = CRGB::White;
            pulsing = true;
            isWhite = true;
            bpm = 15; // Slow pulse
            minDim = 5;
            maxDim = 80; // Dim white
            break;
    }

    if (pulsing) {
        uint8_t beat = beatsin8(bpm, minDim, maxDim);
        
        if (isWhite) {
            leds[0] = CRGB(beat, beat, beat);
        } else {
            leds[0] = baseColor;
            leds[0].nscale8(beat);
        }
        FastLED.show();
    } else {
        // Static color
        if (leds[0] != baseColor) {
            leds[0] = baseColor;
            FastLED.show();
        }
    }
}

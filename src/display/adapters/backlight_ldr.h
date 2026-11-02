// backlight_ldr — the device half of backlight: samples the light sensor (LDR
// on GPIO34), runs backlight's pure decision and ramps the PWM backlight
// (GPIO21) through LovyanGFX. Hardware adapter, not covered by Unity
// (CLAUDE.md "Testing"). Also defines backlightDecision(),
// backlightHandleTouch() and backlightStatus() declared in backlight.h.
#pragma once

#include "display/adapters/lgfx/LGFX_CYD.hpp"
#include "display/core/backlight.h"

// After tft.init(): ADC attenuation, backlight to full.
void backlightBegin(LGFX &tft);
// Every loop(): samples the sensor, decides, ramps the PWM.
void backlightLoop(LGFX &tft, const Config &cfg, bool serviceScreen);
// A service screen (OTA progress...) is about to be drawn from outside
// loop(): full brightness now.
void backlightFullNow(LGFX &tft);

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Backlight duty wrapper. On the ESP32 this module owned a LEDC channel; on
// the Presto the PWM lives in the ST7701 driver, so begin() is a no-op kept
// for symmetry with upstream's main.
namespace backlight {

void begin();
void set(uint8_t duty);  // 0..255

}  // namespace backlight

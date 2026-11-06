// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "hw/backlight.h"

#include "hw/panel.h"

namespace backlight {

void begin() {}  // PWM is attached by the ST7701 driver

void set(uint8_t duty) { panel::setBacklight(duty); }

}  // namespace backlight

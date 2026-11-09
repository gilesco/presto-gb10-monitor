// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Screen dimensions (upstream defines these in board_pins.h for the Guition
// board; the Presto carries the same 480x480 glass).
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 480;

// ST7701 panel + backlight on the Presto, via Pimoroni's PIO scanout driver
// (drivers/st7701 in the pimoroni/presto repo). The scanout is IRQ-driven
// from core 1 — begin() blocks until core 1 has initialised the panel.
namespace panel {

bool begin();  // false if the panel could not be started

// The 480x480 RGB565 framebuffer, in internal SRAM. Pixels must be written
// BYTE-SWAPPED (see hw/screen.cpp and the M0 finding in the repo README);
// screen::present() handles that for the grid renderer.
uint16_t* framebuffer();

// Backlight PWM duty, 0 (off) to 255.
void setBacklight(uint8_t duty);

// Parks the core-1 scanout loop for a flash write (XIP stalls both cores).
// True when core 1 is parked in RAM-only code — flash may be programmed.
// False: it did not ack in time; skip the write and retry later.
bool pauseScanout();

// Unparks after pauseScanout() returned true. Always pair the two.
void resumeScanout();

}  // namespace panel

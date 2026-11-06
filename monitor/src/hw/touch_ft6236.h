// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Minimal polled FT6236 driver for the Presto (upstream boards use a GT911;
// the Presto's touch is a FocalTech FT6236 at I²C 0x48, ported from the
// touch.py module in pimoroni/presto). Polled ~30 Hz; INT is active-low
// while a finger is down.
namespace touch {

struct Point {
  bool fresh;  // the controller had a new report (false: nothing new, keep last state)
  bool down;   // finger on the panel (only meaningful when fresh)
  uint16_t x, y;
};

struct Info {
  uint8_t addr;        // 0x48
  char product[5];     // "FT62"
  uint16_t max_x, max_y;
};

bool begin(Info* info);  // false if no FT6236 answers
Point read();            // call at ~30 Hz

}  // namespace touch

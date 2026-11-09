// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Colour roles and themes (SPEC.md §6). Grid cells store a Role; the theme
// maps it to RGB565 at blit time, so switching theme is a full repaint with
// no re-layout. The themes are btop's Default + bundled themes
// (https://github.com/aristocratos/btop, Apache-2.0; by aristocratos and the
// theme authors), converted by tools/themes/import_btop.py. The default,
// "SYSTEM-D", is btop's built-in Default palette. Arduino-free.
namespace theme {

enum Role : uint8_t {
  BG,
  BORDER,   // box 1 (btop cpu_box); also the default box colour
  TITLE,
  LABEL,
  VALUE,
  DIM,
  GOOD,
  WARN,
  BAD,
  DECODE,   // TG numbers, sparklines, graphs
  PREFILL,  // PP numbers, graphs
  BOX2,     // btop mem_box
  BOX3,     // btop net_box
  BOX4,     // btop proc_box
  BRAND,    // "SYSTEM-D": the same red in every theme (user request)
  // Server state, the same in every theme (upstream v0.1.4):
  STATE_OK,    // OK IDLE / BUSY / WARM: green
  STATE_WAIT,  // OK RESTART, CONNECTING, STALE: orange
  STATE_BAD,   // DOWN, NO WIFI, UNKNOWN: red
  ROLE_COUNT
};

struct Theme {
  const char* name;
  uint16_t rgb565[ROLE_COUNT];
};

// btop's built-in Default palette, renamed; used on first boot and for an
// unknown saved name.
constexpr const char* DEFAULT_NAME = "SYSTEM-D";

int count();
const Theme& get(int index);           // wraps around
int find(const char* name);            // index; DEFAULT_NAME's if unknown

constexpr uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) |
                    ((rgb & 0xFF) >> 3));
}

constexpr uint16_t BRAND_RED = rgb565(0xFF3B30);  // fixed, not themed
// Server state, fixed across themes (upstream v0.1.4): OK green, in-progress
// orange, no-data red — the same colours whatever the theme.
constexpr uint16_t STATE_GREEN = rgb565(0x34C759);
constexpr uint16_t STATE_ORANGE = rgb565(0xFF9500);
constexpr uint16_t STATE_RED = BRAND_RED;

}  // namespace theme

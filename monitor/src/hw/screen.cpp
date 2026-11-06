// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "hw/screen.h"

#include <string.h>

#include "gfx/raster.h"
#include "hardware/psram.h"
#include "hw/panel.h"

namespace screen {

namespace {
constexpr int BAND_PIXELS = raster::CELL_H * SCREEN_W;  // one cell row: 480×24
static_assert(GRID_ROWS * raster::CELL_H == SCREEN_H, "grid must fill the panel");

uint16_t* g_fb = nullptr;
Grid g_shown;
bool g_full = true;
// Dirty cells of a cell row are rasterised here, then the span from the
// first to the last dirty column is swapped and copied line by line into
// the framebuffer. Staging only — the scanout never reads it — so it lives
// in PSRAM to keep SRAM for the framebuffer and the network stack.
__attribute__((section(".psram_uninitialised.band"), aligned(4)))
uint16_t g_band[BAND_PIXELS];
}  // namespace

void begin() {
  g_fb = panel::framebuffer();
  g_full = true;
}

void invalidate() { g_full = true; }

int present(const Grid& next, const theme::Theme& t) {
  if (!g_fb) return 0;
  int painted = 0;
  for (int r = 0; r < GRID_ROWS; r++) {
    int first = -1, last = -1;
    for (int c = 0; c < GRID_COLS; c++) {
      if (!g_full && next.at(c, r) == g_shown.at(c, r)) continue;
      // Every cell in [first, last] is copied, so render the clean cells in
      // between too; they're already on screen, this just fills the buffer.
      for (int k = last < 0 ? c : last + 1; k <= c; k++)
        raster::cell(next.at(k, r), t, g_band + k * raster::CELL_W, SCREEN_W);
      if (first < 0) first = c;
      last = c;
      painted++;
    }
    if (first < 0) continue;

    const int x0 = first * raster::CELL_W;
    const int span = (last - first + 1) * raster::CELL_W;
    // The Presto framebuffer wants byte-swapped RGB565 (M0 hardware finding);
    // the raster emits standard little-endian RGB565, so swap during the copy.
    for (int y = 0; y < raster::CELL_H; y++) {
      const uint16_t* src = g_band + y * SCREEN_W + x0;
      uint16_t* dst = g_fb + r * BAND_PIXELS + y * SCREEN_W + x0;
      for (int k = 0; k < span; k++) dst[k] = __builtin_bswap16(src[k]);
    }
  }
  g_shown = next;
  g_full = false;
  return painted;
}

}  // namespace screen

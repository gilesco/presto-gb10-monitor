// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "raster.h"

#include "font.h"
#include "platform.h"

namespace raster {

FAST_CODE void cell(const Cell& c, const theme::Theme& t, uint16_t* dst, int stride) {
  const uint16_t fg = t.rgb565[c.fg];
  const uint16_t bg = t.rgb565[c.bg];

  if (c.attr & ATTR_BIG) {
    // Pixel-double the bold glyph (20 → 40 px) and centre it in the 2-cell
    // block (48 px): each quadrant shows a 5×10 region of the glyph, padded
    // with blank rows above (top quadrants) or below (bottom quadrants).
    const uint16_t* g = font::rows(c.ch, true);
    const uint8_t q = bigQuadrant(c.attr);
    const bool top = (q >> 1) == 0;
    const int sx = (q & 1) * (CELL_W / 2), sy = top ? 0 : 10;
    const int pad = CELL_H - 20;  // 4 px: the block's spare 8 px split top/bottom
    for (int y = 0; y < CELL_H; y++) {
      const int gy = top ? y - pad : y;  // row within this quadrant's 20 px of glyph
      const uint16_t bits = (gy >= 0 && gy < 20) ? g[sy + gy / 2] : 0;
      uint16_t* p = dst + y * stride;
      for (int x = 0; x < CELL_W; x++) p[x] = (bits >> (15 - (sx + x / 2))) & 1 ? fg : bg;
    }
    return;
  }

  if (c.attr & ATTR_PIXBARS) {
    // Three bars (3, 3, 4 px wide) filled up from the bottom of the cell.
    const int h[3] = {pixBar(c.ch, 0), pixBar(c.ch, 1), pixBar(c.ch, 2)};
    for (int y = 0; y < CELL_H; y++) {
      uint16_t* p = dst + y * stride;
      const int from_bottom = CELL_H - y;
      for (int x = 0; x < CELL_W; x++) p[x] = from_bottom <= h[x < 3 ? 0 : x < 6 ? 1 : 2] ? fg : bg;
    }
    return;
  }

  // The 20-row glyph sits centred in the taller cell. Padding rows repeat the
  // glyph's first/last row: blank for text, but a continuous stroke for box
  // drawing (│ ┤ ├ …) and blocks, so frame lines and bars still join.
  // Braille is stretched over the full cell instead: its dots don't touch
  // the glyph edges, so padding would leave a gap between graph rows.
  const uint16_t* g = font::rows(c.ch, c.attr & ATTR_BOLD);
  const bool stretch = c.ch >= 0x2800 && c.ch <= 0x28FF;
  for (int y = 0; y < CELL_H; y++) {
    int gy = stretch ? y * 20 / CELL_H : y - PAD_TOP;
    gy = gy < 0 ? 0 : gy > 19 ? 19 : gy;
    const uint16_t bits = g[gy];
    uint16_t* p = dst + y * stride;
    for (int x = 0; x < CELL_W; x++) p[x] = (bits >> (15 - x)) & 1 ? fg : bg;
  }
}

}  // namespace raster

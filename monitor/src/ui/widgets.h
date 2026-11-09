// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#include "gfx/grid.h"
#include "gfx/theme.h"

// The visual language of SPEC.md §7: btop-style boxes, │ … │ rows, bars,
// block sparklines and braille graphs. Pages only compose these.
// Arduino-free.
namespace widgets {

constexpr int CONTENT_COL = 2;               // first column inside "│ "
constexpr int CONTENT_END = GRID_COLS - 2;   // exclusive; " │" follows

// btop-style boxes (SPEC.md §7): every section is its own box with rounded
// corners and its labels in notches on the border:
//   ╭─┐Throughput┌──────────────┐120s┌─╮      top edge
//   │ …                                 │      boxRow()
//   ╰─┘≡└─┘poll 2s · ok 0.4s ago└──┘‹ 1/2 ›└─╯   bottom edge
// edgeStart() draws the corner and one "─" and returns the next column;
// notch() draws ┐text┌ (top) or ┘text└ (bottom) there and returns the column
// after it; edgeEnd() fills with "─" to the optional right-hand notch and the
// far corner. edgeTail() is how many columns edgeEnd needs.
enum class Edge : uint8_t { TOP, BOTTOM };

int edgeStart(Grid& g, int row, Edge e, uint8_t box);
int notch(Grid& g, int row, int col, Edge e, uint8_t box, const char* text, uint8_t role,
          uint8_t attr = ATTR_NONE);
void edgeEnd(Grid& g, int row, int col, Edge e, uint8_t box, const char* right = nullptr,
             uint8_t right_role = theme::DIM, uint8_t right_attr = ATTR_NONE);
int edgeTail(const char* right);
int notchCol(const char* right);  // column where edgeEnd puts the right label's text

// Common case: "╭─┐Title┌────┐right┌─╮" with a bold title.
void boxTop(Grid& g, int row, uint8_t box, const char* title, const char* right = nullptr,
            uint8_t right_role = theme::DIM);
void boxBottom(Grid& g, int row, uint8_t box);

// "│" at both edges of a content row, in the box's colour.
void boxRow(Grid& g, int row, uint8_t box = theme::BORDER);

// Writes consecutive coloured segments along a row, clipped at max_col.
class Writer {
 public:
  Writer(Grid& g, int row, int col, int max_col = CONTENT_END)
      : g_(g), row_(row), col_(col), max_(max_col) {}
  Writer& put(const char* utf8, uint8_t role, uint8_t attr = ATTR_NONE) {
    col_ = g_.text(col_, row_, utf8, role, attr, max_);
    return *this;
  }
  Writer& to(int col) {
    col_ = col;
    return *this;
  }
  int col() const { return col_; }

 private:
  Grid& g_;
  int row_, col_, max_;
};

// "█████░░░░░" bar, filled part in `role`, rest dim. NaN → all dim.
void bar(Grid& g, int col, int row, int width, float pct, uint8_t role);

// Bar sparkline at pixel resolution: `cells` columns × `rows` rows, one
// ~3 px bar per sample (PIXBARS_PER_CELL per cell, 24 px of height per row),
// so data (oldest first) must hold cells × PIXBARS_PER_CELL samples. Bars
// aren't pooled, so the shape scrolls steadily instead of regrouping every
// poll. Scaled to the window max; 0 is a 1 px baseline, NaN a gap (empty).
// Returns the scale, NaN if the window is all gaps.
float sparkline(Grid& g, int col, int row, int rows, int cells, const float* data, uint8_t role);

// Braille area graph: `cols` columns × `rows` rows, 2 samples per column,
// 4 levels per row; data holds cols × 2 samples, oldest first. Values are
// scaled to vmax (clamped). NaN samples leave their dot column empty.
void braille(Grid& g, int col, int row, int rows, int cols, const float* data, float vmax,
             uint8_t role);

// Threshold colour for KV / context fill (SPEC.md §5).
uint8_t fillRole(float pct);

}  // namespace widgets

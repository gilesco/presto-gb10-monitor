// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "grid.h"

uint32_t utf8Next(const char*& s) {
  const unsigned char* p = (const unsigned char*)s;
  uint32_t c = *p;
  if (c == 0) return 0;
  int extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
  c &= extra == 0 ? 0x7F : extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
  for (int i = 1; i <= extra; i++) {
    if ((p[i] & 0xC0) != 0x80) {  // malformed: consume one byte, emit '?'
      s += 1;
      return '?';
    }
    c = (c << 6) | (p[i] & 0x3F);
  }
  s += 1 + extra;
  return c;
}

int utf8Columns(const char* s) {
  int n = 0;
  while (utf8Next(s)) n++;
  return n;
}

void Grid::clear() {
  for (auto& row : cells_)
    for (auto& c : row) c = Cell{};
}

void Grid::put(int col, int row, uint32_t ch, uint8_t fg, uint8_t attr) {
  if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return;
  Cell& c = cells_[row][col];
  c.ch = ch > 0xFFFF ? '?' : (uint16_t)ch;
  c.fg = fg;
  c.bg = theme::BG;
  c.attr = attr;
}

int Grid::text(int col, int row, const char* s, uint8_t fg, uint8_t attr, int max_col) {
  if (max_col > GRID_COLS) max_col = GRID_COLS;
  while (uint32_t ch = utf8Next(s)) {
    if (col >= max_col) break;
    put(col++, row, ch, fg, attr);
  }
  return col;
}

int Grid::textRight(int end_col, int row, const char* s, uint8_t fg, uint8_t attr) {
  int start = end_col - utf8Columns(s);
  text(start, row, s, fg, attr, end_col);
  return start;
}

int Grid::big(int col, int row, const char* s, uint8_t fg) {
  for (; *s; s++) {
    for (uint8_t q = 0; q < 4; q++) put(col + (q & 1), row + (q >> 1), (uint8_t)*s, fg, bigAttr(q));
    col += 2;
  }
  return col;
}

void Grid::fill(int col, int row, int count, uint32_t ch, uint8_t fg) {
  for (int i = 0; i < count; i++) put(col + i, row, ch, fg);
}

namespace {
void appendUtf8(std::string& out, uint32_t c) {
  if (c < 0x80) {
    out += (char)c;
  } else if (c < 0x800) {
    out += (char)(0xC0 | (c >> 6));
    out += (char)(0x80 | (c & 0x3F));
  } else {
    out += (char)(0xE0 | (c >> 12));
    out += (char)(0x80 | ((c >> 6) & 0x3F));
    out += (char)(0x80 | (c & 0x3F));
  }
}
}  // namespace

std::string Grid::dump() const {
  std::string out;
  for (int r = 0; r < GRID_ROWS; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      const Cell& cell = cells_[r][c];
      if (cell.attr & ATTR_BIG) {
        uint8_t q = bigQuadrant(cell.attr);
        if (q == 0) appendUtf8(out, cell.ch == ' ' ? 0x3000 : cell.ch + 0xFEE0);
        else if (q >= 2) out += ' ';
        continue;
      }
      if (cell.attr & ATTR_PIXBARS) {  // the block element for the tallest bar
        int h = 0;
        for (int i = 0; i < PIXBARS_PER_CELL; i++) h = pixBar(cell.ch, i) > h ? pixBar(cell.ch, i) : h;
        appendUtf8(out, h == 0 ? ' ' : 0x2580 + (h * 8 + 23) / 24);  // ▁ … █
        continue;
      }
      appendUtf8(out, cell.ch);
    }
    out += '\n';
  }
  return out;
}

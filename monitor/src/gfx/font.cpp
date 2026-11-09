// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "font.h"

#include "font_ter10x20.h"
#include "platform.h"

namespace font {

namespace {

FAST_CODE int find(const uint16_t* cps, int n, uint32_t cp) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (cps[mid] == cp) return mid;
    if (cps[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

}  // namespace

bool has(uint32_t cp, bool bold) {
  using namespace font_data;
  return bold ? find(BOLD_CODEPOINTS, BOLD_COUNT, cp) >= 0
              : find(REGULAR_CODEPOINTS, REGULAR_COUNT, cp) >= 0;
}

FAST_CODE const uint16_t* rows(uint32_t cp, bool bold) {
  using namespace font_data;
  if (bold) {
    int i = find(BOLD_CODEPOINTS, BOLD_COUNT, cp);
    if (i >= 0) return BOLD_ROWS[i];
  }
  int i = find(REGULAR_CODEPOINTS, REGULAR_COUNT, cp);
  if (i < 0) i = find(REGULAR_CODEPOINTS, REGULAR_COUNT, '?');
  return REGULAR_ROWS[i];
}

}  // namespace font

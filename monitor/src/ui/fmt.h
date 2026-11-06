// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <math.h>
#include <stddef.h>

// Number formatting for the screens (SPEC.md §5 "Number format").
// Every function returns "no data" for NaN. Arduino-free, no heap.
namespace fmt {

struct Str {
  char s[24];
  operator const char*() const { return s; }
};

constexpr const char* NO_DATA = "no data";

inline bool missing(float v) { return isnan(v); }

Str speed(float v);       // 100.9 · 1,331 · 120K · 1.20M
Str rate(float v);        // speed + "/s" (bare "no data" when missing)
Str speedShort(float v);  // sparkline scale: 91.2 · 6.8K · 1.2M
Str pct(float v);         // 31.4%
Str tokens(float v);      // 0 · 950 · 329K · 1.0M
Str dur(float s);         // 312ms · 1.85s · 12.4s
Str durShort(float s);    // 1.2s (stream table)
Str count(float v);       // 42

}  // namespace fmt

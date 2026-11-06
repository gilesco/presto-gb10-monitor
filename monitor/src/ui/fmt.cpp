// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "fmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fmt {

namespace {
Str noData() {
  Str r;
  strcpy(r.s, NO_DATA);
  return r;
}
}  // namespace

Str speed(float v) {
  if (missing(v)) return noData();
  Str r;
  float a = fabsf(v);
  if (a < 999.95f) {
    snprintf(r.s, sizeof r.s, "%.1f", v);
  } else if (a < 99999.5f) {
    long n = lroundf(v);
    snprintf(r.s, sizeof r.s, "%ld,%03ld", n / 1000, labs(n % 1000));
  } else if (a < 999500.0f) {
    snprintf(r.s, sizeof r.s, "%.0fK", v / 1e3f);
  } else {
    snprintf(r.s, sizeof r.s, "%.2fM", v / 1e6f);
  }
  return r;
}

Str rate(float v) {
  Str r = speed(v);
  if (!missing(v)) strncat(r.s, "/s", sizeof r.s - strlen(r.s) - 1);
  return r;
}

Str speedShort(float v) {
  if (missing(v)) return noData();
  Str r;
  float a = fabsf(v);
  if (a < 999.95f) snprintf(r.s, sizeof r.s, "%.1f", v);
  else if (a < 999950.0f) snprintf(r.s, sizeof r.s, "%.1fK", v / 1e3f);
  else snprintf(r.s, sizeof r.s, "%.1fM", v / 1e6f);
  return r;
}

Str pct(float v) {
  if (missing(v)) return noData();
  Str r;
  snprintf(r.s, sizeof r.s, "%.1f%%", v);
  return r;
}

Str tokens(float v) {
  if (missing(v)) return noData();
  Str r;
  float a = fabsf(v);
  if (a < 999.5f) snprintf(r.s, sizeof r.s, "%.0f", v);
  else if (a < 999500.0f) snprintf(r.s, sizeof r.s, "%.0fK", v / 1e3f);
  else snprintf(r.s, sizeof r.s, "%.1fM", v / 1e6f);
  return r;
}

Str dur(float s) {
  if (missing(s)) return noData();
  Str r;
  if (s < 0.9995f) snprintf(r.s, sizeof r.s, "%.0fms", s * 1e3f);
  else if (s < 9.995f) snprintf(r.s, sizeof r.s, "%.2fs", s);
  else snprintf(r.s, sizeof r.s, "%.1fs", s);
  return r;
}

Str durShort(float s) {
  if (missing(s)) return noData();
  Str r;
  if (s < 0.9995f) snprintf(r.s, sizeof r.s, "%.0fms", s * 1e3f);
  else snprintf(r.s, sizeof r.s, "%.1fs", s);
  return r;
}

Str count(float v) {
  if (missing(v)) return noData();
  Str r;
  snprintf(r.s, sizeof r.s, "%.0f", v);
  return r;
}

}  // namespace fmt

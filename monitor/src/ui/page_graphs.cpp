// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Graphs page, SPEC.md §7.2.
#include <math.h>
#include <stdio.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {

struct Stats {
  float now = NAN, avg = NAN, max = NAN, min = NAN;
};

// events_only: per-request series (TensorFold PP), where 0 means "no
// request finished in that poll"; stats then cover finished requests only and
// "now" is the latest one.
Stats stats(const float* d, bool events_only = false) {
  Stats s;
  s.now = d[RING_LEN - 1];
  if (events_only) {
    s.now = NAN;
    for (int i = RING_LEN - 1; i >= 0; i--)
      if (!isnan(d[i]) && d[i] > 0) { s.now = d[i]; break; }
  }
  float sum = 0;
  int n = 0;
  for (int i = 0; i < RING_LEN; i++) {
    if (isnan(d[i]) || (events_only && d[i] <= 0)) continue;
    sum += d[i];
    n++;
    if (isnan(s.max) || d[i] > s.max) s.max = d[i];
    if (isnan(s.min) || d[i] < s.min) s.min = d[i];
  }
  if (n) s.avg = sum / n;
  return s;
}

using Formatter = fmt::Str (*)(float);

// "now  100.9" — label padded to 4, then the value right-aligned in 5 columns.
void statLine(Grid& g, int row, const char* label, float v, Formatter f, uint8_t role) {
  char lab[8], buf[32];
  fmt::Str s = f(v);
  snprintf(lab, sizeof lab, "%-4s", label);  // "now ", "last" line up
  snprintf(buf, sizeof buf, "%5s", s.s);
  Writer(g, row, 33).put(lab, theme::LABEL).put(" ", theme::BG).put(buf, isnan(v) ? (uint8_t)theme::DIM : role);
}

struct GraphSpec {
  const char* title;
  uint8_t box;
  const float* data;
  int rows;
  float floor;      // minimum y-scale, so idle noise doesn't fill the box
  bool fixed_100;   // KV: always 0–100 %
  Formatter fmt;
  uint8_t role;
  bool full_stats;   // now/avg/max/min, else now/max
  bool events_only;  // per-request series: stats over finished requests, "last"
};

// "120s", or "5m" for longer windows (60 samples × the poll interval).
void spanLabel(char* out, size_t n, unsigned s) {
  if (s <= 120 || s % 60) snprintf(out, n, "%us", s);
  else snprintf(out, n, "%um", s / 60);
}

// Box top, `rows` graph rows; the caller draws the bottom edge.
int graph(Grid& g, int row, const GraphSpec& s, unsigned span_s) {
  char span[12];
  spanLabel(span, sizeof span, span_s);
  boxTop(g, row++, s.box, s.title, span);
  Stats st = stats(s.data, s.events_only);
  float vmax = s.fixed_100 ? 100.0f : fmaxf(isnan(st.max) ? 0.0f : st.max, s.floor);
  for (int r = 0; r < s.rows; r++) boxRow(g, row + r, s.box);
  bool empty = true;
  for (int i = 0; i < RING_LEN; i++) empty &= isnan(s.data[i]);
  if (empty) {  // empty window: one centred "no data", no stats
    g.text((GRID_COLS - 7) / 2, row + (s.rows - 1) / 2, fmt::NO_DATA, theme::DIM);
    return row + s.rows;
  }
  braille(g, CONTENT_COL, row, s.rows, 30, s.data, vmax, s.role);
  // Stats down the right, as many as the graph has rows for.
  struct Line { const char* label; float v; };
  const Line full[] = {{s.events_only ? "last" : "now", st.now}, {"avg", st.avg}, {"max", st.max}, {"min", st.min}};
  const Line brief[] = {{"now", st.now}, {"max", st.max}};
  const Line* lines = s.full_stats ? full : brief;
  const int n = s.full_stats ? 4 : 2;
  for (int i = 0; i < n && i < s.rows; i++) statLine(g, row + i, lines[i].label, lines[i].v, s.fmt, s.role);
  return row + s.rows;
}

}  // namespace

// Layout (SPEC.md §7.2): header box, then TG (6 rows), PP (2) and KV (2)
// boxes; the KV box's bottom edge is the footer.
void graphs(Grid& g, const View& v) {
  g.clear();
  int row = chrome(g, v);
  const unsigned span = v.ring_span_s;
  row = graph(g, row, {"TG live tok/s", theme::BOX2, v.tg_hist, 6, 10, false, fmt::speed, theme::DECODE, true, false}, span);
  boxBottom(g, row++, theme::BOX2);
  row = graph(g, row, {v.pp_per_request ? "PP per request tok/s" : "PP live tok/s", theme::BOX3, v.pp_hist,
                       2, 100, false, fmt::speed, theme::PREFILL, true, v.pp_per_request}, span);
  boxBottom(g, row++, theme::BOX3);
  row = graph(g, row, {"KV %", theme::BOX4, v.kv_hist, 2, 0, true, fmt::pct, theme::GOOD, false, false}, span);
  footer(g, v, "tap: overview", theme::BOX4);
}

}  // namespace pages

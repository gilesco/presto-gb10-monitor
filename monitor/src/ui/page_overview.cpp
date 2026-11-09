// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Overview page, SPEC.md §7.1.
#include <math.h>
#include <stdio.h>

#include <initializer_list>
#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {

constexpr const char* SEP = " \xC2\xB7 ";  // " · "

uint8_t mtpRole(float pct) {
  if (isnan(pct)) return theme::DIM;
  return pct >= 70 ? theme::GOOD : pct >= 50 ? theme::WARN : theme::BAD;
}

uint8_t valueRole(float v, uint8_t role) { return isnan(v) ? (uint8_t)theme::DIM : role; }

bool allMissing(std::initializer_list<float> vs) {
  for (float v : vs)
    if (!isnan(v)) return false;
  return true;
}

// SPEC.md §5: when every value in a row is missing, one "no data" replaces
// them all (after an optional leading label). Returns true if it did.
bool noDataRow(Grid& g, int row, std::initializer_list<float> vs, const char* label = nullptr,
               uint8_t label_role = theme::LABEL, uint8_t label_attr = ATTR_NONE) {
  if (!allMissing(vs)) return false;
  Writer w(g, row, CONTENT_COL);
  if (label) w.put(label, label_role, label_attr);
  w.put(fmt::NO_DATA, theme::DIM);
  return true;
}

// Rows 4–5: "TG <hero> tok/s <2-row sparkline>" / "live".
void throughputHero(Grid& g, int row, const View& v) {
  boxRow(g, row, theme::BOX2);
  boxRow(g, row + 1, theme::BOX2);
  g.text(CONTENT_COL, row, "TG", theme::DECODE, ATTR_BOLD);
  if (isnan(v.tg_live)) {
    g.text(5, row, fmt::NO_DATA, theme::DIM);
  } else {
    char hero[sizeof(fmt::Str)];
    snprintf(hero, sizeof hero, "%5s", fmt::speed(v.tg_live).s);
    g.big(5, row, hero, theme::DECODE);
  }
  g.text(16, row, "tok/s", theme::DIM);
  g.text(17, row + 1, "live", theme::DIM);
  static_assert(20 * PIXBARS_PER_CELL == RING_LEN, "one bar per sample");
  sparkline(g, 22, row, 2, 20, v.tg_hist, theme::DECODE);
}

void streamRow(Grid& g, int row, const StreamView& s) {
  // Columns (absolute): id 3 · state 6 · ctx ends 22 · tokens ends 29 ·
  // TTFT ends 36 · TG ends 44.
  boxRow(g, row, theme::BOX3);
  char id[4];
  snprintf(id, sizeof id, "%u", s.id);
  g.text(3, row, id, theme::VALUE);

  const char* state = nullptr;
  uint8_t role = theme::DIM;
  switch (s.state) {
    case StreamState::DECODING: state = "decoding"; role = theme::GOOD; break;
    case StreamState::PREFILL: state = "prefill"; role = theme::WARN; break;
    case StreamState::IDLE: state = "idle"; role = theme::DIM; break;
    case StreamState::DONE: state = "done"; role = theme::LABEL; break;
    case StreamState::NO_DATA: break;
  }
  if (state) g.text(6, row, state, role);

  g.textRight(22, row, fmt::pct(s.ctx_pct), valueRole(s.ctx_pct, fillRole(s.ctx_pct)));
  g.textRight(29, row, fmt::tokens(s.tokens), valueRole(s.tokens, theme::VALUE));

  if (s.state == StreamState::IDLE) return;  // nothing to measure
  const bool ttft = !isnan(s.ttft_s), tg = !isnan(s.tg);
  if (!ttft && !tg) {
    g.text(31, row, fmt::NO_DATA, theme::DIM);  // one "no data" spans both
    return;
  }
  g.textRight(36, row, fmt::durShort(s.ttft_s), valueRole(s.ttft_s, theme::VALUE));
  g.textRight(44, row, fmt::rate(s.tg), valueRole(s.tg, theme::DECODE));
}

}  // namespace

// Layout (SPEC.md §7.1), four btop boxes in 20 rows:
//   0–3   header box (chrome)
//   4–10  Throughput: hero ×2, PP, run/wait + TG avg/max, KV
//   11–16 Streams: column labels in the top edge, 4 stream rows
//   17–19 Averages: one row; its bottom edge is the footer
void overview(Grid& g, const View& v) {
  g.clear();
  chrome(g, v);

  // Throughput (box 2)
  const uint8_t thr_box = theme::BOX2;
  boxTop(g, 4, thr_box, "Throughput");
  throughputHero(g, 5, v);

  boxRow(g, 7, thr_box);
  if (v.pp_per_request) {
    // "PP last 3,354/s · prefilling 12s"
    Writer w(g, 7, CONTENT_COL);
    w.put("PP ", theme::PREFILL, ATTR_BOLD).put("last ", theme::LABEL);
    w.put(fmt::rate(v.pp_last), valueRole(v.pp_last, theme::PREFILL));
    if (!isnan(v.prefilling_s)) {
      char s[24];
      snprintf(s, sizeof s, "prefilling %.0fs", v.prefilling_s);
      w.put(SEP, theme::DIM).put(s, theme::WARN);
    }
  } else if (!noDataRow(g, 7, {v.pp_live, v.pp_avg, v.pp_eng}, "PP ", theme::PREFILL, ATTR_BOLD)) {
    Writer(g, 7, CONTENT_COL)
        .put("PP ", theme::PREFILL, ATTR_BOLD)
        .put(fmt::rate(v.pp_live), valueRole(v.pp_live, theme::PREFILL))
        .put("  avg ", theme::LABEL)
        .put(fmt::speed(v.pp_avg), valueRole(v.pp_avg, theme::PREFILL));
  }

  // "run 2/4 · wait 1 · TG avg 95.1 · max 104.2": queue, then live TG since
  // the monitor started / the server restarted (average over generating
  // polls, and peak). Each pair is all-or-nothing, so each collapses to one
  // "no data".
  boxRow(g, 8, thr_box);
  {
    Writer w(g, 8, CONTENT_COL);
    if (isnan(v.running)) {
      w.put("run ", theme::LABEL).put(fmt::NO_DATA, theme::DIM);
    } else {
      char run[24];
      if (isnan(v.max_batch)) snprintf(run, sizeof run, "%.0f", v.running);
      else snprintf(run, sizeof run, "%.0f/%.0f", v.running, v.max_batch);
      w.put("run ", theme::LABEL).put(run, theme::VALUE).put(SEP, theme::DIM);
      w.put("wait ", theme::LABEL).put(fmt::count(v.waiting), valueRole(v.waiting, theme::VALUE));
    }
    w.put(SEP, theme::DIM).put("TG ", theme::DECODE, ATTR_BOLD);
    if (isnan(v.tg_avg)) {
      w.put(fmt::NO_DATA, theme::DIM);
    } else {
      w.put("avg ", theme::LABEL).put(fmt::speed(v.tg_avg), theme::DECODE).put(SEP, theme::DIM);
      w.put("max ", theme::LABEL).put(fmt::speed(v.tg_max), valueRole(v.tg_max, theme::DECODE));
    }
  }

  // KV cache (was its own "Cache" box; folded in to fit btop boxes in 20 rows):
  // "KV 0.0% ░░░░░░ · peak 12.6% · 0/1.0M tok". KV is freed when a request
  // ends, so the live value is usually 0; the peak is the highest reading in
  // the history window (the KV graph's, RING_LEN polls), so a request that
  // just finished still shows how full it got (user request).
  boxRow(g, 9, thr_box);
  {
    float peak = NAN;
    for (int i = 0; i < RING_LEN; i++)
      if (!isnan(v.kv_hist[i]) && (isnan(peak) || v.kv_hist[i] > peak)) peak = v.kv_hist[i];
    const fmt::Str now = fmt::pct(v.kv_pct), pk = fmt::pct(peak);
    char tok[2 * sizeof(fmt::Str)] = "";
    if (!isnan(v.kv_tokens) && !isnan(v.kv_capacity))
      snprintf(tok, sizeof tok, "%s/%s", fmt::tokens(v.kv_tokens).s, fmt::tokens(v.kv_capacity).s);
    // The bar takes whatever is left, so worst-case values (100.0%, 1.0M/1.0M) fit.
    const int text = 3 + utf8Columns(now.s) + 1 + (isnan(peak) ? 0 : 3 + 5 + utf8Columns(pk.s)) + 3 +
                     (tok[0] ? utf8Columns(tok) + 4 : utf8Columns(fmt::NO_DATA));
    int bar_w = CONTENT_END - CONTENT_COL - text;
    bar_w = bar_w < 3 ? 3 : bar_w > 16 ? 16 : bar_w;

    Writer w(g, 9, CONTENT_COL);
    w.put("KV ", theme::LABEL).put(now, fillRole(v.kv_pct)).put(" ", theme::BG);
    bar(g, w.col(), 9, bar_w, v.kv_pct, fillRole(v.kv_pct));
    w.to(w.col() + bar_w);
    if (!isnan(peak)) w.put(SEP, theme::DIM).put("peak ", theme::LABEL).put(pk, fillRole(peak));
    w.put(SEP, theme::DIM);
    if (tok[0]) w.put(tok, theme::VALUE).put(" tok", theme::DIM);
    else w.put(fmt::NO_DATA, theme::DIM);
  }
  boxBottom(g, 10, thr_box);

  // Streams (box 3): column labels sit in the top edge, over their columns.
  const uint8_t str_box = theme::BOX3;
  boxTop(g, 11, str_box, "Streams");
  g.textRight(22, 11, "ctx", theme::DIM);
  g.textRight(29, 11, "tokens", theme::DIM);
  g.textRight(36, 11, "TTFT", theme::DIM);
  g.textRight(44, 11, "TG", theme::DIM);
  for (int r = 12; r <= 15; r++) boxRow(g, r, str_box);
  if (v.n_streams == 0) g.text(3, 12, fmt::NO_DATA, theme::DIM);
  for (int i = 0; i < v.n_streams && i < 4; i++) streamRow(g, 12 + i, v.streams[i]);
  boxBottom(g, 16, str_box);

  // Averages (box 4); its bottom edge is the footer.
  const uint8_t avg_box = theme::BOX4;
  boxTop(g, 17, avg_box, "Averages");
  boxRow(g, 18, avg_box);
  if (!noDataRow(g, 18, {v.ttft_cum, v.mtp_pct, v.cache_hit_pct}))
    Writer(g, 18, CONTENT_COL)
        .put("TTFT ", theme::LABEL)
        .put(fmt::dur(v.ttft_cum), valueRole(v.ttft_cum, theme::VALUE))
        .put(SEP, theme::DIM)
        .put("MTP ", theme::LABEL)
        .put(fmt::pct(v.mtp_pct), mtpRole(v.mtp_pct))
        .put(SEP, theme::DIM)
        .put("cache-hit ", theme::LABEL)
        .put(fmt::pct(v.cache_hit_pct), valueRole(v.cache_hit_pct, theme::VALUE));
  footer(g, v, "tap: graphs", avg_box);
}

}  // namespace pages

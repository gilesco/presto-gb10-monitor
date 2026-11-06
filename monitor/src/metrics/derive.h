// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#include "sample.h"
#include "server_info.h"
#include "ui/view.h"

// Turns successive Samples into the View the pages draw (SPEC.md §5):
// rates from deltas between polls, first-vs-last window averages, 120 s
// rings, restart detection and the server state. Arduino-free.
class Deriver {
 public:
  static constexpr int WINDOW = 15;               // samples (~30 s at 2 s)
  static constexpr uint32_t RESTART_SHOW_MS = 30000;
  // A finished stream keeps its last active row, marked done, this long
  // (user request: rows vanished too quickly to read).
  static constexpr uint32_t STREAM_LINGER_MS = 10000;

  // A successful /metrics poll. Returns true if it detected an engine
  // restart (a counter went down), so the caller can re-fetch info.
  bool onSample(const Sample& s, const Identity& id, const ServerInfo& info);

  // A failed poll: pushes a gap into the rings and counts towards DOWN.
  void onFailure(uint32_t t_ms, const char* reason);

  // Fills everything except the header (clock, IP, Wi-Fi bars), which the
  // caller owns. retry_ms: time until the next attempt while failing.
  void fill(View& v, uint32_t now_ms, uint32_t poll_ms, WifiLink link, uint32_t retry_ms) const;

  int consecutiveFailures() const { return failures_; }

 private:
  const Sample& at(int i) const;  // 0 = oldest in window
  void pushRings(float tg, float pp, float kv);
  // Stream rows from the latest poll (and the one before, for TG), in slot
  // order; returns how many.
  int liveStreams(StreamView rows[MAX_STREAMS], float ctx_window) const;

  Sample win_[WINDOW];
  int n_ = 0, head_ = 0;  // head_ = next write slot
  float tg_ring_[RING_LEN], pp_ring_[RING_LEN], kv_ring_[RING_LEN];
  int ring_head_ = 0;
  bool rings_init_ = false;

  Identity id_;
  ServerInfo info_;
  bool ever_ok_ = false;
  uint32_t last_ok_ms_ = 0;
  int failures_ = 0;
  char reason_[24] = "";
  bool restarted_ = false;
  uint32_t restart_ms_ = 0;

  // Live TG since boot / the last restart: peak, and the sum + count of the
  // polls where it was generating (TG > 0), for the "since start" average.
  float tg_peak_ = NAN;
  double tg_sum_ = 0;
  uint32_t tg_busy_polls_ = 0;

  // Per slot: the last row while a request was active, and when it ended.
  StreamView last_active_[SAMPLE_STREAMS];
  bool was_active_[SAMPLE_STREAMS] = {};
  bool lingering_[SAMPLE_STREAMS] = {};
  uint32_t ended_ms_[SAMPLE_STREAMS] = {};

  // Per-request prefill (see View::pp_per_request)
  float last_pp_eng_ = NAN;
  bool prefilling_ = false;
  bool decode_live_ = false;  // TF 1.2.3: the scheduler gauge is moving
  uint32_t prefill_since_ms_ = 0;
};

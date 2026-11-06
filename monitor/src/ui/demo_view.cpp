// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "demo_view.h"

#include <math.h>
#include <string.h>

View demoView() {
  View v;
  strcpy(v.clock, "11-02-26 12:47:33 PM");
  strcpy(v.server, "gpu-box");
  strcpy(v.server_ip, "192.168.1.50");
  strcpy(v.ip, "192.168.1.42");
  v.wifi_bars = 3;
  strcpy(v.engine, "TensorFold");
  strcpy(v.model, "deepseek-v4.1-flash-1017");
  v.state = ServerState::BUSY;

  // 120 s rings: idle for the first 18 s, a missed-poll gap at 76–82 s.
  for (int i = 0; i < RING_LEN; i++) {
    const bool idle = i <= 8, gap = i >= 38 && i <= 41;
    v.tg_hist[i] = gap ? NAN : idle ? 0.0f : 85.0f + 15.0f * sinf(i / 4.0f) + (float)(i % 5);
    // TensorFold PP: prefill speed of each request at the poll it finished.
    v.pp_hist[i] = gap ? NAN : i == 14 ? 6800.0f : i == 28 ? 3000.0f : i == 44 ? 3354.0f : 0.0f;
    v.kv_hist[i] = gap ? NAN : idle ? 0.0f : 31.4f * i / (RING_LEN - 1);
  }

  v.tg_live = v.tg_hist[RING_LEN - 1];
  v.tg_avg = 95.1f;
  v.tg_eng = 91.2f;
  v.tg_max = 104.2f;
  v.pp_live = 1331.0f;
  v.pp_avg = 1290.0f;
  v.pp_eng = 6802.0f;
  v.pp_per_request = true;  // TensorFold
  v.pp_last = 3354.0f;
  v.prefilling_s = 12.0f;
  v.running = 2;
  v.max_batch = 4;
  v.waiting = 1;

  v.kv_pct = 31.4f;
  v.ctx_window = 1048576.0f;
  v.kv_capacity = v.ctx_window;
  v.kv_tokens = v.kv_pct / 100.0f * v.kv_capacity;

  v.n_streams = 4;
  v.streams[0] = {0, StreamState::DECODING, 31.4f, 0.314f * 1048576.0f, 1.2f, 62.7f};
  v.streams[1] = {1, StreamState::PREFILL, 2.0f, 21000.0f, NAN, NAN};
  v.streams[2] = {2, StreamState::IDLE, 0.0f, 0.0f, NAN, NAN};
  v.streams[3] = {3, StreamState::IDLE, 0.0f, 0.0f, NAN, NAN};

  v.ttft_cum = 1.85f;
  v.ttft_live = 1.42f;
  v.lat_cum = 8.16f;
  v.n_requests = 42;
  v.mtp_pct = 48.9f;
  v.mtp_accepted = 9175;
  v.mtp_drafted = 15175;
  v.cache_hit_pct = 75.4f;

  strcpy(v.status, "poll 2s \xC2\xB7 ok 0.4s ago");
  return v;
}

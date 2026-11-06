// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <math.h>
#include <stdint.h>

// One poll's raw values from /metrics (SPEC.md §4.2), before any deltas.
// NaN = the metric wasn't in the response. Arduino-free.

constexpr int SAMPLE_STREAMS = 8;

enum class Engine : uint8_t { UNKNOWN, TENSORFOLD, VLLM };

// Fields that combine across label sets (SPEC.md §4.2 "Combine series").
enum Field : uint8_t {
  F_RUNNING,
  F_WAITING,
  F_PROMPT_TOKENS,     // counter
  F_GEN_TOKENS,        // counter (TF: finished requests; vLLM: live)
  F_GEN_RUNNING,       // gauge, TF only: tokens held by live streams
  F_PREFILL_S,         // hist sum, finished requests
  F_DECODE_S,          // hist sum, finished requests
  F_TTFT_SUM,
  F_TTFT_COUNT,
  F_E2E_SUM,
  F_E2E_COUNT,
  F_DRAFT_TOKENS,      // spec decode / MTP
  F_ACCEPTED_TOKENS,
  F_DRAFTS,            // vLLM only (τ)
  F_CACHED_TOKENS,     // prompt_tokens_cached_total (vLLM), else TF's F_HEALTH_CACHED
  F_PREEMPTIONS,
  F_KV_POOL,           // max over pools / unlabeled series, ratio 0–1
  F_KV_POOL_TOKENS,    // vLLM: cache_config_info{kv_cache_size_tokens}
  F_REQ_PROMPT,        // vLLM: request_prompt_tokens_sum (finished requests)
  F_REQ_GEN,           // vLLM: request_generation_tokens_sum (finished requests)
  F_REQ_COMPUTED,      // vLLM: request_prefill_kv_computed_tokens_sum (finished, cache hits excluded)
  // TensorFold builds that mirror /health into /metrics as tensorfold_health:*
  // (seen 2026-11-03; they dropped generation_tokens_running and
  // request_prefill_time_seconds).
  F_GEN_LIVE,          // completion_tokens_total: running replies included
  F_HEALTH_PREFILL_S,  // prefill_seconds_total, finished requests; fills F_PREFILL_S
  F_HEALTH_CACHED,     // cached_tokens_total, finished requests; fills F_CACHED_TOKENS
  F_SCHED_DECODE,      // TF 1.2.3: scheduler_tokens_total{mode="decode"} — LIVE
                       // generated tokens (finish-timed counters freeze during
                       // decode on this build; this gauge doesn't). Resets per
                       // request, so deltas go negative at request boundaries.
  F_COUNT
};

enum class StreamPhase : int8_t { NONE = -1, IDLE, PREFILLING, DECODING };

struct StreamSample {
  bool present = false;    // seen any per-stream series for this slot
  float kv = NAN;          // ratio 0–1 (kv_cache_usage_perc{stream})
  StreamPhase phase = StreamPhase::NONE;  // App. A stream_state
  float gen_tokens = NAN;  // App. A
  float ttft_s = NAN;      // App. A (NaN until first token)
  float start_s = NAN;     // App. A (Unix time the request started)
};

struct Sample {
  uint32_t t_ms = 0;  // millis() when the response finished
  float v[F_COUNT];
  StreamSample streams[SAMPLE_STREAMS];
  char model_name[48] = "";  // vLLM's model_name label, first seen

  // Recognised metric lines per prefix, for engine identification.
  uint16_t n_tensorfold = 0;
  uint16_t n_vllm = 0;

  Sample() {
    for (float& x : v) x = NAN;
  }
  float get(Field f) const { return v[f]; }
  bool has(Field f) const { return !isnan(v[f]); }
};

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "prom.h"

#include <stdlib.h>
#include <string.h>

namespace {

enum class Combine : uint8_t { SUM, MAX, STREAM_KV, STREAM_STATE, STREAM_GEN, STREAM_TTFT, STREAM_START, KV_POOL_SIZE, SCHED_DECODE };

struct Entry {
  const char* suffix;
  Field field;
  Combine combine;
};

// Suffixes after "tensorfold:" or "vllm:" (SPEC.md §4.2). TensorFold also
// exports mirrors (requests_running, request_decode_seconds_*, mtp_*); only
// the vLLM-style names are listed so nothing is counted twice.
const Entry TABLE[] = {
    {"num_requests_running", F_RUNNING, Combine::SUM},
    {"num_requests_waiting", F_WAITING, Combine::SUM},
    {"prompt_tokens_total", F_PROMPT_TOKENS, Combine::SUM},
    {"generation_tokens_total", F_GEN_TOKENS, Combine::SUM},
    {"generation_tokens_running", F_GEN_RUNNING, Combine::SUM},
    {"request_prefill_time_seconds_sum", F_PREFILL_S, Combine::SUM},
    {"request_decode_time_seconds_sum", F_DECODE_S, Combine::SUM},
    {"time_to_first_token_seconds_sum", F_TTFT_SUM, Combine::SUM},
    {"time_to_first_token_seconds_count", F_TTFT_COUNT, Combine::SUM},
    {"e2e_request_latency_seconds_sum", F_E2E_SUM, Combine::SUM},
    {"e2e_request_latency_seconds_count", F_E2E_COUNT, Combine::SUM},
    {"spec_decode_num_draft_tokens_total", F_DRAFT_TOKENS, Combine::SUM},
    {"spec_decode_num_accepted_tokens_total", F_ACCEPTED_TOKENS, Combine::SUM},
    {"spec_decode_num_drafts_total", F_DRAFTS, Combine::SUM},
    {"prompt_tokens_cached_total", F_CACHED_TOKENS, Combine::SUM},
    {"request_prompt_tokens_sum", F_REQ_PROMPT, Combine::SUM},       // vLLM
    {"request_generation_tokens_sum", F_REQ_GEN, Combine::SUM},      // vLLM
    {"request_prefill_kv_computed_tokens_sum", F_REQ_COMPUTED, Combine::SUM},  // vLLM
    {"cache_config_info", F_KV_POOL_TOKENS, Combine::KV_POOL_SIZE},  // vLLM, label
    {"preemptions_total", F_PREEMPTIONS, Combine::SUM},      // TensorFold
    {"num_preemptions_total", F_PREEMPTIONS, Combine::SUM},  // vLLM
    {"kv_cache_usage_ratio", F_KV_POOL, Combine::MAX},
    {"gpu_cache_usage_perc", F_KV_POOL, Combine::MAX},       // older vLLM
    {"kv_cache_usage_perc", F_KV_POOL, Combine::STREAM_KV},  // per stream on TF
    {"stream_state", F_COUNT, Combine::STREAM_STATE},        // App. A
    {"stream_generation_tokens", F_COUNT, Combine::STREAM_GEN},
    {"stream_ttft_seconds", F_COUNT, Combine::STREAM_TTFT},
    {"stream_request_start_seconds", F_COUNT, Combine::STREAM_START},
    {"scheduler_tokens_total", F_SCHED_DECODE, Combine::SCHED_DECODE},  // TF 1.2.3
};

// Suffixes after "tensorfold_health:", the /health counters some TensorFold
// builds also export. Only the ones that stand in for metrics those builds
// dropped are read.
const Entry HEALTH_TABLE[] = {
    {"completion_tokens_total", F_GEN_LIVE, Combine::SUM},
    {"prefill_seconds_total", F_HEALTH_PREFILL_S, Combine::SUM},
    {"cached_tokens_total", F_HEALTH_CACHED, Combine::SUM},
};

template <size_t N>
const Entry* find(const Entry (&table)[N], const char* suffix) {
  for (const Entry& e : table)
    if (strcmp(e.suffix, suffix) == 0) return &e;
  return nullptr;
}

bool endsWith(const char* s, const char* tail) {
  size_t n = strlen(s), m = strlen(tail);
  return n >= m && strcmp(s + n - m, tail) == 0;
}

int streamSlot(const char* labels) {
  char buf[8];
  if (!promLabel(labels, "stream", buf, sizeof buf)) return -1;
  char* end;
  long v = strtol(buf, &end, 10);
  return (*end == '\0' && v >= 0 && v < SAMPLE_STREAMS) ? (int)v : -1;
}

void addTo(float& dst, float v) { dst = isnan(dst) ? v : dst + v; }
void keepMax(float& dst, float v) { dst = (isnan(dst) || v > dst) ? v : dst; }

}  // namespace

bool promLabel(const char* labels, const char* key, char* out, size_t out_len) {
  out[0] = '\0';
  const size_t klen = strlen(key);
  const char* p = labels;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    const char* name = p;
    while (*p && *p != '=') p++;
    if (*p != '=') return false;
    const bool match = (size_t)(p - name) == klen && strncmp(name, key, klen) == 0;
    p++;
    if (*p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
      if (*p == '\\' && p[1]) p++;
      if (match && n + 1 < out_len) out[n++] = *p;
      p++;
    }
    if (match) {
      out[n] = '\0';
      return true;
    }
    if (*p == '"') p++;
  }
  return false;
}

void PromParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = data[i];
    if (c == '\n') {
      if (overflow_) overlong_++;
      else parseLine(line_, len_);
      len_ = 0;
      overflow_ = false;
    } else if (!overflow_) {
      if (len_ + 1 < sizeof line_) line_[len_++] = c;
      else overflow_ = true;  // skip the rest of this line
    }
  }
}

void PromParser::finish() {
  if (len_ && !overflow_) parseLine(line_, len_);
  len_ = 0;
  overflow_ = false;
  // A build without the histogram or cached counter gets /health's.
  if (isnan(s_.v[F_PREFILL_S])) s_.v[F_PREFILL_S] = s_.v[F_HEALTH_PREFILL_S];
  if (isnan(s_.v[F_CACHED_TOKENS])) s_.v[F_CACHED_TOKENS] = s_.v[F_HEALTH_CACHED];
}

void PromParser::parseLine(char* line, size_t n) {
  line[n] = '\0';
  if (n && line[n - 1] == '\r') line[--n] = '\0';
  char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0' || *p == '#') return;
  lines_++;

  // name[{labels}] value [timestamp]
  char* name = p;
  while (*p && *p != '{' && *p != ' ' && *p != '\t') p++;
  char* labels = nullptr;
  if (*p == '{') {
    *p++ = '\0';
    labels = p;
    bool quoted = false;
    for (; *p; p++) {
      if (*p == '\\' && quoted && p[1]) { p++; continue; }
      if (*p == '"') quoted = !quoted;
      else if (*p == '}' && !quoted) break;
    }
    if (*p != '}') return;  // malformed
    *p++ = '\0';
  } else if (*p) {
    *p++ = '\0';
  }
  while (*p == ' ' || *p == '\t') p++;
  char* end;
  const double value = strtod(p, &end);
  if (end == p) return;

  const char* suffix;
  bool tf, health = false;
  if (strncmp(name, "tensorfold:", 11) == 0) { suffix = name + 11; tf = true; }
  else if (strncmp(name, "tensorfold_health:", 18) == 0) { suffix = name + 18; tf = health = true; }
  else if (strncmp(name, "vllm:", 5) == 0) { suffix = name + 5; tf = false; }
  else return;
  if (endsWith(suffix, "_bucket") || endsWith(suffix, "_created")) return;

  if (!tf && labels && !s_.model_name[0])
    promLabel(labels, "model_name", s_.model_name, sizeof s_.model_name);

  const Entry* e = health ? find(HEALTH_TABLE, suffix) : find(TABLE, suffix);
  if (!e) return;
  matched_++;
  (tf ? s_.n_tensorfold : s_.n_vllm)++;

  const float v = (float)value;
  const int slot = labels ? streamSlot(labels) : -1;
  switch (e->combine) {
    case Combine::SUM:
      if (!isnan(v) && !isinf(v)) addTo(s_.v[e->field], v);
      break;
    case Combine::MAX:
      if (!isnan(v) && !isinf(v)) keepMax(s_.v[e->field], v);
      break;
    case Combine::STREAM_KV:
      if (isnan(v) || isinf(v)) break;
      if (slot >= 0) {
        s_.streams[slot].present = true;
        s_.streams[slot].kv = v;
      } else {
        keepMax(s_.v[F_KV_POOL], v);  // vLLM: one unlabeled pool series
      }
      break;
    case Combine::STREAM_STATE: {
      if (slot < 0 || v != 1.0f) break;
      char st[16];
      promLabel(labels, "state", st, sizeof st);
      StreamPhase ph = strcmp(st, "decoding") == 0     ? StreamPhase::DECODING
                       : strcmp(st, "prefilling") == 0 ? StreamPhase::PREFILLING
                       : strcmp(st, "idle") == 0       ? StreamPhase::IDLE
                                                       : StreamPhase::NONE;
      s_.streams[slot].present = true;
      s_.streams[slot].phase = ph;
      break;
    }
    case Combine::STREAM_GEN:
      if (slot >= 0 && !isnan(v)) { s_.streams[slot].present = true; s_.streams[slot].gen_tokens = v; }
      break;
    case Combine::STREAM_TTFT:  // NaN = no first token yet: stays NaN
      if (slot >= 0) { s_.streams[slot].present = true; if (!isnan(v)) s_.streams[slot].ttft_s = v; }
      break;
    case Combine::KV_POOL_SIZE: {
      char buf[24];
      if (labels && promLabel(labels, "kv_cache_size_tokens", buf, sizeof buf)) {
        char* end;
        double tokens = strtod(buf, &end);
        if (end != buf && tokens > 0) s_.v[F_KV_POOL_TOKENS] = (float)tokens;
      }
      break;
    }
    case Combine::STREAM_START:
      if (slot >= 0 && !isnan(v)) { s_.streams[slot].present = true; s_.streams[slot].start_s = v; }
      break;
    case Combine::SCHED_DECODE:
      // scheduler_tokens_total{mode="decode"}: only the decode-mode series,
      // not the prefill_compute/prefill_cache siblings of the same name.
      if (!isnan(v) && !isinf(v) && labels) {
        char mode[16];
        if (promLabel(labels, "mode", mode, sizeof mode) && strcmp(mode, "decode") == 0)
          s_.v[F_SCHED_DECODE] = v;
      }
      break;
  }
}

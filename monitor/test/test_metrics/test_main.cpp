// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// /metrics parsing, /health + /v1/models, engine identification (SPEC.md
// §4) and the derivation into a View (§5), against the dgx042 capture.
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "gfx/grid.h"
#include "metrics/derive.h"
#include "metrics/prom.h"
#include "metrics/server_info.h"
#include "ui/pages.h"

static std::string readFixtureIn(const char* dir, const char* name) {
  std::string rel = std::string("test/fixtures/") + dir + "/" + name;
  FILE* f = fopen(rel.c_str(), "rb");
  if (!f) {  // fall back to a path relative to this source file
    std::string here = __FILE__;
    rel = here.substr(0, here.rfind('/')) + "/../fixtures/" + dir + "/" + name;
    f = fopen(rel.c_str(), "rb");
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(f, name);
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}
static std::string readFixture(const char* name) { return readFixtureIn("tensorfold-1.1.8-idle", name); }
static std::string readVllm(const char* name) { return readFixtureIn("vllm-0.33.0rc1-idle", name); }

static Sample parse(const std::string& body, size_t chunk = 4096) {
  Sample s;
  PromParser p(s);
  for (size_t i = 0; i < body.size(); i += chunk) p.feed(body.data() + i, std::min(chunk, body.size() - i));
  p.finish();
  return s;
}

static ServerInfo dgxInfo() {
  ServerInfo info;
  std::string h = readFixture("health.json"), m = readFixture("v1_models.json");
  TEST_ASSERT_TRUE(parseHealth(h.data(), h.size(), info));
  TEST_ASSERT_TRUE(parseModels(m.data(), m.size(), info));
  return info;
}

void setUp() {}
void tearDown() {}

// --- parser ---------------------------------------------------------------

static void test_fixture_values() {
  Sample s = parse(readFixture("metrics.txt"));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_RUNNING));  // not doubled by the requests_running mirror
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_WAITING));
  TEST_ASSERT_EQUAL_FLOAT(763467, s.get(F_PROMPT_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(15361, s.get(F_GEN_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_GEN_RUNNING));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 89.046108, s.get(F_PREFILL_S));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 249.521992, s.get(F_DECODE_S));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 93.039729, s.get(F_TTFT_SUM));
  TEST_ASSERT_EQUAL_FLOAT(42, s.get(F_TTFT_COUNT));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 342.561721, s.get(F_E2E_SUM));
  TEST_ASSERT_EQUAL_FLOAT(42, s.get(F_E2E_COUNT));
  TEST_ASSERT_EQUAL_FLOAT(15175, s.get(F_DRAFT_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(9175, s.get(F_ACCEPTED_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_PREEMPTIONS));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_KV_POOL));
  TEST_ASSERT_TRUE(std::isnan(s.get(F_CACHED_TOKENS)));  // not exported by 1.1.8
  TEST_ASSERT_TRUE(std::isnan(s.get(F_DRAFTS)));         // vLLM only
  TEST_ASSERT_TRUE(s.streams[0].present);
  TEST_ASSERT_EQUAL_FLOAT(0, s.streams[0].kv);
  TEST_ASSERT_FALSE(s.streams[1].present);
  TEST_ASSERT_TRUE(s.n_tensorfold > 10);
  TEST_ASSERT_EQUAL(0, s.n_vllm);
}

static void test_byte_by_byte_feed_matches() {
  std::string body = readFixture("metrics.txt");
  Sample a = parse(body), b = parse(body, 1);
  for (int f = 0; f < F_COUNT; f++) {
    if (std::isnan(a.v[f])) TEST_ASSERT_TRUE(std::isnan(b.v[f]));
    else TEST_ASSERT_EQUAL_FLOAT(a.v[f], b.v[f]);
  }
  TEST_ASSERT_EQUAL(a.n_tensorfold, b.n_tensorfold);
}

static void test_overlong_line_skipped() {
  std::string body = "tensorfold:prompt_tokens_total{x=\"" + std::string(3000, 'a') + "\"} 5\n"
                     "tensorfold:generation_tokens_total 7\n";
  Sample s;
  PromParser p(s);
  p.feed(body.data(), body.size());
  p.finish();
  TEST_ASSERT_EQUAL(1, p.overlong());
  TEST_ASSERT_TRUE(std::isnan(s.get(F_PROMPT_TOKENS)));
  TEST_ASSERT_EQUAL_FLOAT(7, s.get(F_GEN_TOKENS));
}

static void test_appendix_a_streams() {
  const char* body =
      "tensorfold:stream_state{stream=\"0\",state=\"decoding\"} 1\n"
      "tensorfold:stream_state{stream=\"1\",state=\"prefilling\"} 1\n"
      "tensorfold:stream_generation_tokens{stream=\"0\"} 1874\n"
      "tensorfold:stream_ttft_seconds{stream=\"0\"} 1.214\n"
      "tensorfold:stream_ttft_seconds{stream=\"1\"} NaN\n"
      "tensorfold:stream_request_start_seconds{stream=\"0\"} 1793630811.42\n"
      "tensorfold:prompt_tokens_cached_total 4321\n";
  Sample s = parse(body);
  TEST_ASSERT_EQUAL(StreamPhase::DECODING, s.streams[0].phase);
  TEST_ASSERT_EQUAL(StreamPhase::PREFILLING, s.streams[1].phase);
  TEST_ASSERT_EQUAL_FLOAT(1874, s.streams[0].gen_tokens);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 1.214, s.streams[0].ttft_s);
  TEST_ASSERT_TRUE(std::isnan(s.streams[1].ttft_s));
  TEST_ASSERT_EQUAL_FLOAT(4321, s.get(F_CACHED_TOKENS));
}

static void test_labels() {
  char out[16];
  TEST_ASSERT_TRUE(promLabel("stream=\"3\",state=\"idle\"", "state", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("idle", out);
  TEST_ASSERT_TRUE(promLabel("a=\"x\\\"y\",b=\"2\"", "b", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("2", out);
  TEST_ASSERT_FALSE(promLabel("a=\"1\"", "stream", out, sizeof out));
}

// --- /health, /v1/models, identification ----------------------------------

static void test_health_and_models() {
  ServerInfo info = dgxInfo();
  TEST_ASSERT_TRUE(info.health_ok);
  TEST_ASSERT_FALSE(info.warming);
  TEST_ASSERT_EQUAL_FLOAT(4, info.max_batch);
  TEST_ASSERT_EQUAL_STRING("1.1.8", info.version);
  TEST_ASSERT_EQUAL_STRING("deepseek-v4.1-flash-1017", info.model);
  TEST_ASSERT_EQUAL_FLOAT(1048576, info.context_length);
  ServerInfo bad;
  TEST_ASSERT_FALSE(parseHealth("<html>", 6, bad));
  TEST_ASSERT_FALSE(parseModels("{}", 2, bad));
}

static void test_identify_tensorfold() {
  Identity id = identify(parse(readFixture("metrics.txt")), "TensorFold", dgxInfo());
  TEST_ASSERT_EQUAL(Engine::TENSORFOLD, id.engine);
  TEST_ASSERT_TRUE(id.confirmed);
  id = identify(parse(readFixture("metrics.txt")), "uvicorn", ServerInfo{});
  TEST_ASSERT_EQUAL(Engine::TENSORFOLD, id.engine);
  TEST_ASSERT_FALSE(id.confirmed);
}

static void test_identify_vllm() {
  const char* body =
      "# HELP vllm:num_requests_running Number of requests in model execution batches.\n"
      "vllm:num_requests_running{engine=\"0\",model_name=\"glm-5.3\"} 3.0\n"
      "vllm:num_requests_waiting{engine=\"0\",model_name=\"glm-5.3\"} 1.0\n"
      "vllm:kv_cache_usage_perc{engine=\"0\",model_name=\"glm-5.3\"} 0.116\n"
      "vllm:prompt_tokens_total{engine=\"0\",model_name=\"glm-5.3\"} 1000.0\n"
      "vllm:time_to_first_token_seconds_bucket{le=\"0.1\",model_name=\"glm-5.3\"} 2.0\n"
      "process_resident_memory_bytes 1.2e9\n";
  Sample s = parse(body);
  TEST_ASSERT_EQUAL(Engine::VLLM, identify(s, "uvicorn", ServerInfo{}).engine);
  TEST_ASSERT_EQUAL_STRING("glm-5.3", s.model_name);
  TEST_ASSERT_EQUAL_FLOAT(3, s.get(F_RUNNING));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.116, s.get(F_KV_POOL));
}

static void test_identify_unknown() {
  const char* html = "<!doctype html><html><body>404 Not Found</body></html>\n";
  TEST_ASSERT_EQUAL(Engine::UNKNOWN, identify(parse(html), "nginx", ServerInfo{}).engine);
  TEST_ASSERT_EQUAL(Engine::UNKNOWN, identify(parse(""), nullptr, ServerInfo{}).engine);
  const char* node = "node_cpu_seconds_total{cpu=\"0\"} 12\n";  // another exporter
  TEST_ASSERT_EQUAL(Engine::UNKNOWN, identify(parse(node), nullptr, ServerInfo{}).engine);
}

// --- derivation -----------------------------------------------------------

struct Rig {
  Deriver d;
  Sample base;
  ServerInfo info;
  Identity id;
  Rig() {
    base = parse(readFixture("metrics.txt"));
    info = dgxInfo();
    id = identify(base, "TensorFold", info);
  }
  // A sample t_ms after the base with counters advanced.
  Sample at(uint32_t t_ms, float gen, float gen_running, float prompt, float decode_s,
            float prefill_s, float running = 0) {
    Sample s = base;
    s.t_ms = t_ms;
    s.v[F_GEN_TOKENS] += gen;
    s.v[F_GEN_RUNNING] = gen_running;
    s.v[F_PROMPT_TOKENS] += prompt;
    s.v[F_DECODE_S] += decode_s;
    s.v[F_PREFILL_S] += prefill_s;
    s.v[F_RUNNING] = running;
    return s;
  }
  View view(uint32_t now, bool wifi = true, uint32_t retry = 0) {
    View v;
    d.fill(v, now, 2000, wifi ? WifiLink::UP : WifiLink::LOST, retry);
    return v;
  }
};

static void test_rates() {
  Rig r;
  r.d.onSample(r.at(10000, 0, 0, 0, 0, 0), r.id, r.info);
  View v = r.view(10000);
  TEST_ASSERT_TRUE(std::isnan(v.tg_live));  // one sample: no rates yet
  TEST_ASSERT_EQUAL(ServerState::IDLE, v.state);

  r.d.onSample(r.at(12000, 200, 20, 3000, 2.0f, 0.5f, 1), r.id, r.info);
  v = r.view(12400);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 110, v.tg_live);  // (200 + 20 in flight) / 2 s
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 100, v.tg_eng);   // 200 / 2 s of decode
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 1500, v.pp_live);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 6000, v.pp_eng);
  TEST_ASSERT_EQUAL(ServerState::BUSY, v.state);
  TEST_ASSERT_EQUAL_STRING("poll 2s \xC2\xB7 ok 0.4s ago", v.status);
  TEST_ASSERT_EQUAL_FLOAT(4, v.max_batch);
  TEST_ASSERT_EQUAL_FLOAT(1048576, v.ctx_window);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 60.46, v.mtp_pct);       // 9175 / 15175
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 93.039729 / 42, v.ttft_cum);
  TEST_ASSERT_TRUE(std::isnan(v.cache_hit_pct));          // not exported by 1.1.8
  TEST_ASSERT_EQUAL_STRING("TensorFold", v.engine);
  TEST_ASSERT_EQUAL_STRING("deepseek-v4.1-flash-1017", v.model);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 110, v.tg_hist[RING_LEN - 1]);  // newest ring slot
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 110, v.tg_max);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 110, v.tg_avg);  // one generating poll so far

  // A slower poll doesn't lower the peak; the average covers both.
  r.d.onSample(r.at(14000, 250, 10, 3000, 3.0f, 0.5f, 1), r.id, r.info);
  v = r.view(14000);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 20, v.tg_live);  // (250 + 10) − (200 + 20) = 40 / 2 s
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 110, v.tg_max);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 65, v.tg_avg);   // (110 + 20) / 2

  // Idle polls don't drag the average down.
  r.d.onSample(r.at(16000, 250, 10, 3000, 3.0f, 0.5f, 0), r.id, r.info);
  v = r.view(16000);
  TEST_ASSERT_EQUAL_FLOAT(0, v.tg_live);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 65, v.tg_avg);
}

static void test_idle_wall_rate_is_zero_engine_rate_no_data() {
  Rig r;
  r.d.onSample(r.at(0, 0, 0, 0, 0, 0), r.id, r.info);
  r.d.onSample(r.at(2000, 0, 0, 0, 0, 0), r.id, r.info);
  View v = r.view(2000);
  TEST_ASSERT_EQUAL_FLOAT(0, v.tg_live);       // a real idle 0.0
  TEST_ASSERT_TRUE(std::isnan(v.tg_eng));      // no decode time advanced
}

static void test_restart_detected() {
  Rig r;
  r.d.onSample(r.at(0, 500, 0, 5000, 5, 1), r.id, r.info);
  r.d.onSample(r.at(2000, 600, 0, 6000, 6, 1.2f), r.id, r.info);
  Sample fresh = r.at(4000, 0, 0, 0, 0, 0);
  fresh.v[F_GEN_TOKENS] = 10;  // counters went back to near zero
  fresh.v[F_PROMPT_TOKENS] = 100;
  TEST_ASSERT_TRUE(r.d.onSample(fresh, r.id, r.info));
  View v = r.view(5000);
  TEST_ASSERT_EQUAL(ServerState::RESTART, v.state);
  TEST_ASSERT_TRUE(std::isnan(v.tg_live));  // new baseline
  TEST_ASSERT_EQUAL_STRING("restarted 1s ago", v.status);
  TEST_ASSERT_TRUE(std::isnan(v.tg_max));  // peak and average reset with the server
  TEST_ASSERT_TRUE(std::isnan(v.tg_avg));
  v = r.view(4000 + Deriver::RESTART_SHOW_MS + 1000);
  TEST_ASSERT_NOT_EQUAL(ServerState::RESTART, v.state);
}

static void test_failures_stale_down_nowifi() {
  Rig r;
  r.d.onSample(r.at(0, 0, 0, 0, 0, 0), r.id, r.info);
  r.d.onFailure(2000, "timeout");
  View v = r.view(7000);
  TEST_ASSERT_EQUAL(ServerState::STALE, v.state);
  TEST_ASSERT_EQUAL_STRING("stale 7s \xC2\xB7 timeout", v.status);
  r.d.onFailure(4000, "timeout");
  r.d.onFailure(6000, "timeout");
  v = r.view(8000, true, 8000);
  TEST_ASSERT_EQUAL(ServerState::DOWN, v.state);
  TEST_ASSERT_EQUAL_STRING("timeout \xC2\xB7 retry 8s", v.status);
  TEST_ASSERT_TRUE(std::isnan(v.tg_hist[RING_LEN - 1]));  // failures are gaps
  v = r.view(8000, false);
  TEST_ASSERT_EQUAL(ServerState::NO_WIFI, v.state);
  TEST_ASSERT_EQUAL_STRING("wifi lost \xC2\xB7 retry", v.status);
}

static void test_wifi_connecting_at_boot() {
  // Right after boot the link isn't up yet: that's "connecting", not an error.
  Deriver d;
  View v;
  d.fill(v, 1000, 2000, WifiLink::JOINING, 0);
  TEST_ASSERT_EQUAL(ServerState::CONNECTING, v.state);
  TEST_ASSERT_EQUAL_STRING("wifi connecting\xE2\x80\xA6", v.status);
  Grid g;
  pages::overview(g, v);
  TEST_ASSERT_TRUE(g.dump().find("CONNECTING") != std::string::npos);
  // Never joined within the grace period: an error, with a hint.
  d.fill(v, 30000, 2000, WifiLink::CANT_JOIN, 0);
  TEST_ASSERT_EQUAL(ServerState::NO_WIFI, v.state);
  TEST_ASSERT_EQUAL_STRING("check SSID / password", v.status);
}

static void test_not_an_engine() {
  Rig r;
  Sample s;  // a response with nothing recognisable
  s.t_ms = 1000;
  r.d.onSample(s, identify(s, "nginx", ServerInfo{}), ServerInfo{});
  View v = r.view(1000);
  TEST_ASSERT_EQUAL(ServerState::UNKNOWN, v.state);
  TEST_ASSERT_EQUAL_STRING("not TensorFold or vLLM", v.status);
  TEST_ASSERT_EQUAL_STRING("???", v.engine);
}

static void test_finished_stream_lingers() {
  // dgx042 exports only kv_cache_usage_perc{stream}: a stream is active while
  // it holds cache. When it frees it, the row stays (marked done) for 10 s.
  Rig r;
  Sample s = r.at(0, 0, 0, 0, 0, 0, 1);
  s.streams[0].kv = 0.126f;
  r.d.onSample(s, r.id, r.info);
  View v = r.view(500);
  TEST_ASSERT_EQUAL(StreamState::NO_DATA, v.streams[0].state);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 12.6, v.streams[0].ctx_pct);

  Sample done = r.at(2000, 0, 0, 0, 0, 0, 0);
  done.streams[0].kv = 0;
  r.d.onSample(done, r.id, r.info);
  v = r.view(2000 + Deriver::STREAM_LINGER_MS - 1);
  TEST_ASSERT_EQUAL(StreamState::DONE, v.streams[0].state);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 12.6, v.streams[0].ctx_pct);  // the last active values

  v = r.view(2000 + Deriver::STREAM_LINGER_MS);
  TEST_ASSERT_EQUAL(StreamState::NO_DATA, v.streams[0].state);  // back to the live row
  TEST_ASSERT_EQUAL_FLOAT(0, v.streams[0].ctx_pct);

  // A new request in the slot ends the linger at once.
  Sample again = r.at(4000, 0, 0, 0, 0, 0, 1);
  again.streams[0].kv = 0.02f;
  r.d.onSample(again, r.id, r.info);
  v = r.view(4500);
  TEST_ASSERT_EQUAL(StreamState::NO_DATA, v.streams[0].state);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0, v.streams[0].ctx_pct);
}

static void test_prefilling_and_per_request_pp() {
  // From the 2026-11-01 load test: a 127K-token prompt prefilled for ~38 s
  // with prompt_tokens_total only advancing when the request finished.
  Rig r;
  Sample idle = r.at(0, 0, 0, 0, 0, 0);
  r.d.onSample(idle, r.id, r.info);
  for (uint32_t t = 2000; t <= 38000; t += 2000) {
    Sample s = r.at(t, 0, 0, 0, 0, 0, 1);
    s.v[F_KV_POOL] = 0.119f;
    r.d.onSample(s, r.id, r.info);
  }
  View v = r.view(38500);
  TEST_ASSERT_TRUE(v.pp_per_request);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 36.5, v.prefilling_s);  // since the first prefill poll (t=2 s)
  TEST_ASSERT_TRUE(std::isnan(v.pp_last));                // nothing finished yet
  TEST_ASSERT_EQUAL_FLOAT(0, v.pp_hist[RING_LEN - 1]);    // no completion this poll

  Sample done = r.at(40000, 900, 0, 127000, 18, 38);  // finished: prompt + prefill time land
  r.d.onSample(done, r.id, r.info);
  v = r.view(40000);
  TEST_ASSERT_TRUE(std::isnan(v.prefilling_s));
  TEST_ASSERT_FLOAT_WITHIN(1, 3342.1, v.pp_last);                 // 127000 / 38 s
  TEST_ASSERT_FLOAT_WITHIN(1, 3342.1, v.pp_hist[RING_LEN - 1]);   // per-request point
  Grid g;
  pages::overview(g, v);
  TEST_ASSERT_TRUE(g.dump().find("PP last 3,342/s") != std::string::npos);
}

// --- TensorFold with tensorfold_health:* (dgx042 capture, 2026-11-03) ------
// This build dropped generation_tokens_running and
// request_prefill_time_seconds, mirrors /health into /metrics, and serves the
// {"ok": true, "backend": "tensorfold"} /health with no version.

static std::string readTfNew(const char* name) { return readFixtureIn("tensorfold-2026-11-03-idle", name); }

static ServerInfo tfNewInfo() {
  ServerInfo info;
  std::string h = readTfNew("health.json"), m = readTfNew("v1_models.json");
  TEST_ASSERT_TRUE(parseHealth(h.data(), h.size(), info));
  TEST_ASSERT_TRUE(parseModels(m.data(), m.size(), info));
  return info;
}

static void test_tf_health_build_values() {
  Sample s = parse(readTfNew("metrics.txt"));
  TEST_ASSERT_FALSE(s.has(F_GEN_RUNNING));                      // dropped
  TEST_ASSERT_EQUAL_FLOAT(2530, s.get(F_GEN_LIVE));             // tensorfold_health:completion_tokens_total
  TEST_ASSERT_EQUAL_FLOAT(2530, s.get(F_GEN_TOKENS));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 30.6395, s.get(F_PREFILL_S));  // from prefill_seconds_total
  TEST_ASSERT_EQUAL_FLOAT(106816, s.get(F_CACHED_TOKENS));      // from cached_tokens_total
  TEST_ASSERT_EQUAL_FLOAT(157669, s.get(F_PROMPT_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_RUNNING));

  ServerInfo info = tfNewInfo();
  TEST_ASSERT_TRUE(info.health_ok);
  TEST_ASSERT_TRUE(info.tf_backend);
  TEST_ASSERT_EQUAL_STRING("", info.version);  // not reported
  TEST_ASSERT_EQUAL_FLOAT(4, info.max_batch);  // streams.max
  TEST_ASSERT_EQUAL_FLOAT(1048576, info.context_length);  // /health; /v1/models no longer has it
  TEST_ASSERT_EQUAL_STRING("glm5.3-flash", info.model);

  Identity id = identify(s, "BaseHTTP/0.6 Python/3.12.3", info);
  TEST_ASSERT_EQUAL(Engine::TENSORFOLD, id.engine);
  TEST_ASSERT_TRUE(id.confirmed);  // by backend "tensorfold"

  // 1.1.8's /health format still parses the same way.
  ServerInfo old = dgxInfo();
  TEST_ASSERT_TRUE(old.health_ok);
  TEST_ASSERT_FALSE(old.tf_backend);  // 1.1.8 says "cuda"
  TEST_ASSERT_EQUAL_STRING("1.1.8", old.version);
}

static void test_tf_health_build_live_tg_and_pp() {
  Sample base = parse(readTfNew("metrics.txt"));
  ServerInfo info = tfNewInfo();
  Identity id = identify(base, "BaseHTTP/0.6 Python/3.12.3", info);
  Deriver d;
  auto at = [&](uint32_t t, float live, float finished, float prompt, float prefill_s, float running, float kv,
                float cached = 0) {
    Sample s = base;
    s.v[F_CACHED_TOKENS] += cached;
    s.t_ms = t;
    s.v[F_GEN_LIVE] += live;
    s.v[F_GEN_TOKENS] += finished;
    s.v[F_PROMPT_TOKENS] += prompt;
    s.v[F_PREFILL_S] += prefill_s;
    s.v[F_RUNNING] = running;
    s.v[F_KV_POOL] = kv;
    return s;
  };
  auto view = [&](uint32_t now) {
    View v;
    d.fill(v, now, 2000, WifiLink::UP, 0);
    return v;
  };
  d.onSample(at(0, 0, 0, 0, 0, 0, 0), id, info);
  d.onSample(at(2000, 0, 0, 0, 0, 1, 0.02f), id, info);  // prefill: nothing generated yet
  View v = view(2000);
  TEST_ASSERT_FALSE(std::isnan(v.prefilling_s));  // "prefilling" shown

  // Decoding: the reply's tokens count while it runs, though no request finished.
  d.onSample(at(4000, 100, 0, 0, 0, 1, 0.03f), id, info);
  v = view(4000);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 50, v.tg_live);
  TEST_ASSERT_TRUE(std::isnan(v.prefilling_s));  // not mistaken for a prefill
  TEST_ASSERT_EQUAL(ServerState::BUSY, v.state);

  // Finished: the finished counter catches up (no jump in TG), and the prompt
  // and prefill time land, so PP last updates.
  d.onSample(at(6000, 200, 200, 8000, 2.0f, 0, 0), id, info);
  v = view(6000);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 50, v.tg_live);
  TEST_ASSERT_FLOAT_WITHIN(1, 4000, v.pp_last);  // 8000 / 2 s
  TEST_ASSERT_EQUAL_FLOAT(4, v.max_batch);
  TEST_ASSERT_EQUAL_FLOAT(1048576, v.ctx_window);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 106816.0f / 165669 * 100, v.cache_hit_pct);
  TEST_ASSERT_EQUAL_STRING("glm5.3-flash", v.model);

  // Cached prompt tokens take no prefill time: the request seen live on
  // dgx042 had 50,953 prompt tokens, 50,112 of them cached, and 0.691 s of
  // prefill, so PP is 841 / 0.691 s, not 50,953 / 0.691 s (73,749).
  d.onSample(at(8000, 300, 300, 8000 + 50953, 2.691f, 0, 0, 50112), id, info);
  v = view(8000);
  TEST_ASSERT_FLOAT_WITHIN(1, 1217, v.pp_last);
}

// --- vLLM (dick6000 capture, 2026-11-01) ---------------------------------

static ServerInfo vllmInfo() {
  ServerInfo info;
  std::string h = readVllm("health.txt"), m = readVllm("v1_models.json"), ver = readVllm("version.json");
  TEST_ASSERT_FALSE(parseHealth(h.data(), h.size(), info));  // vLLM's /health is an empty 200
  TEST_ASSERT_TRUE(parseModels(m.data(), m.size(), info));
  TEST_ASSERT_TRUE(parseVersion(ver.data(), ver.size(), info));
  return info;
}

static void test_vllm_fixture_values() {
  Sample s = parse(readVllm("metrics.txt"));
  TEST_ASSERT_EQUAL_STRING("qwen3.8-flash-next", s.model_name);
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_RUNNING));
  TEST_ASSERT_EQUAL_FLOAT(98189253, s.get(F_PROMPT_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(812177, s.get(F_GEN_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(82323200, s.get(F_CACHED_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(355241, s.get(F_KV_POOL_TOKENS));   // from the 1.1 KB cache_config_info line
  TEST_ASSERT_EQUAL_FLOAT(98189253, s.get(F_REQ_PROMPT));
  TEST_ASSERT_EQUAL_FLOAT(812177, s.get(F_REQ_GEN));
  TEST_ASSERT_EQUAL_FLOAT(15866053, s.get(F_REQ_COMPUTED));  // prompt − cached
  TEST_ASSERT_EQUAL_FLOAT(953736, s.get(F_DRAFT_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(494477, s.get(F_ACCEPTED_TOKENS));
  TEST_ASSERT_EQUAL_FLOAT(317912, s.get(F_DRAFTS));
  TEST_ASSERT_EQUAL_FLOAT(81, s.get(F_PREEMPTIONS));
  TEST_ASSERT_EQUAL_FLOAT(830, s.get(F_TTFT_COUNT));
  TEST_ASSERT_EQUAL_FLOAT(0, s.get(F_KV_POOL));
  TEST_ASSERT_TRUE(std::isnan(s.get(F_GEN_RUNNING)));  // TensorFold only
  for (const StreamSample& st : s.streams) TEST_ASSERT_FALSE(st.present);
  TEST_ASSERT_EQUAL(0, s.n_tensorfold);
  TEST_ASSERT_TRUE(s.n_vllm > 10);  // lines the suffix table recognises
}

static void test_vllm_identified_and_confirmed() {
  ServerInfo info = vllmInfo();
  TEST_ASSERT_EQUAL_STRING("0.33.0rc1.dev229+g3966924d5.d20261029", info.version);
  TEST_ASSERT_EQUAL_STRING("qwen3.8-flash-next", info.model);
  TEST_ASSERT_EQUAL_FLOAT(262144, info.context_length);  // max_model_len
  Identity id = identify(parse(readVllm("metrics.txt")), "uvicorn", info);
  TEST_ASSERT_EQUAL(Engine::VLLM, id.engine);
  TEST_ASSERT_TRUE(id.confirmed);
  TEST_ASSERT_FALSE(identify(parse(readVllm("metrics.txt")), "uvicorn", ServerInfo{}).confirmed);
}

static void test_vllm_derived_view() {
  ServerInfo info = vllmInfo();
  Sample a = parse(readVllm("metrics.txt"));
  Identity id = identify(a, "uvicorn", info);
  a.t_ms = 0;
  Sample b = a;
  b.t_ms = 2000;
  b.v[F_GEN_TOKENS] += 120;           // live counter moves while decoding
  b.v[F_REQ_GEN] += 400;              // a request finished: its tokens …
  b.v[F_DECODE_S] += 5;               // … and its decode time
  b.v[F_REQ_PROMPT] += 20000;
  b.v[F_PROMPT_TOKENS] += 20000;
  b.v[F_REQ_COMPUTED] += 5000;        // 15,000 of the prompt came from the cache
  b.v[F_PREFILL_S] += 2.5f;
  b.v[F_RUNNING] = 1;
  b.v[F_KV_POOL] = 0.25f;
  Deriver d;
  d.onSample(a, id, info);
  d.onSample(b, id, info);
  View v;
  d.fill(v, 2000, 2000, WifiLink::UP, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 60, v.tg_live);    // 120 live tokens / 2 s
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 80, v.tg_eng);     // 400 finished tokens / 5 s decode
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 2000, v.pp_last);  // 5000 computed / 2.5 s prefill
  TEST_ASSERT_TRUE(v.pp_per_request);
  TEST_ASSERT_FLOAT_WITHIN(1, 88810, v.kv_tokens);  // 25 % of the 355,241-token pool
  TEST_ASSERT_EQUAL_FLOAT(355241, v.kv_capacity);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 83.84, v.cache_hit_pct);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 51.85, v.mtp_pct);
  TEST_ASSERT_EQUAL(0, v.n_streams);
  Grid g;
  pages::overview(g, v);
  std::string dump = g.dump();
  printf("\n--- vLLM overview ---\n%s", dump.c_str());
  TEST_ASSERT_TRUE(dump.find("vLLM \xC2\xB7 qwen3.8-flash-next") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("89K/355K tok") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("cache-hit 83.8%") != std::string::npos);
  TEST_ASSERT_TRUE(dump.find("nan") == std::string::npos);
}

static void test_footer_shows_server_position() {
  View v;
  v.server_index = 1;
  v.server_count = 2;
  Grid g;
  pages::overview(g, v);
  TEST_ASSERT_TRUE(g.dump().find("\xE2\x94\x98" "2/2" "\xE2\x94\x94") != std::string::npos);  // ┘2/2└, no ‹ ›
}

static void test_derived_view_renders_cleanly() {
  Rig r;
  r.d.onSample(r.at(0, 0, 0, 0, 0, 0), r.id, r.info);
  r.d.onSample(r.at(2000, 180, 0, 0, 1.9f, 0, 1), r.id, r.info);
  View v = r.view(2100);
  Grid g;
  for (auto p : {pages::Page::OVERVIEW, pages::Page::GRAPHS}) {
    pages::draw(p, g, v);
    std::string d = g.dump();
    TEST_ASSERT_TRUE(d.find("nan") == std::string::npos);
    TEST_ASSERT_TRUE(d.find("TensorFold") != std::string::npos);
  }
  pages::draw(pages::Page::OVERVIEW, g, v);
  printf("\n--- live-derived overview ---\n%s", g.dump().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fixture_values);
  RUN_TEST(test_byte_by_byte_feed_matches);
  RUN_TEST(test_overlong_line_skipped);
  RUN_TEST(test_appendix_a_streams);
  RUN_TEST(test_labels);
  RUN_TEST(test_health_and_models);
  RUN_TEST(test_identify_tensorfold);
  RUN_TEST(test_identify_vllm);
  RUN_TEST(test_identify_unknown);
  RUN_TEST(test_rates);
  RUN_TEST(test_idle_wall_rate_is_zero_engine_rate_no_data);
  RUN_TEST(test_restart_detected);
  RUN_TEST(test_failures_stale_down_nowifi);
  RUN_TEST(test_not_an_engine);
  RUN_TEST(test_prefilling_and_per_request_pp);
  RUN_TEST(test_tf_health_build_values);
  RUN_TEST(test_tf_health_build_live_tg_and_pp);
  RUN_TEST(test_vllm_fixture_values);
  RUN_TEST(test_vllm_identified_and_confirmed);
  RUN_TEST(test_vllm_derived_view);
  RUN_TEST(test_footer_shows_server_position);
  RUN_TEST(test_derived_view_renders_cleanly);
  RUN_TEST(test_finished_stream_lingers);
  RUN_TEST(test_wifi_connecting_at_boot);
  return UNITY_END();
}

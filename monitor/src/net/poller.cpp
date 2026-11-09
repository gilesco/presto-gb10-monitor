// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Cooperative port of upstream's poller. Upstream ran one FreeRTOS task per
// server with a blocking HTTP client; here a single async HTTP request is in
// flight at a time and task() steps a per-server state machine from the main
// loop (core 0). Phase order, engine detection, backoff, generation counters
// and serial-log formats mirror the original task() loop.
//
// Memory: the per-server Deriver history and the shared scratch live in
// PSRAM (8 MB) via __uninitialized_psram — SRAM is nearly full with the
// 450 KB framebuffer. These are touched on 2 s polls, so QSPI latency is
// irrelevant. lwIP's pools are in PSRAM too (board header).

#include "net/poller.h"

#include <math.h>
#include "serial_log.h"
#include <new>
#include <stdio.h>
#include <string.h>

#include "hardware/psram.h"
#include "metrics/derive.h"
#include "metrics/prom.h"
#include "metrics/server_info.h"
#include "net/http_get.h"
#include "net/wifi.h"
#include "pico/stdlib.h"
#include "pico/time.h"

namespace poller {

namespace {

constexpr uint32_t INFO_EVERY_MS = 30000;
constexpr uint32_t BACKOFF_MS[] = {2000, 4000, 9000};  // SPEC.md §8: 2, 4, then every 9 s (upstream v0.1.4)
constexpr int MAX_SLOTS = 4;

struct Timing {
  uint32_t poll_ms, connect_ms, read_ms;
} g_timing;

// Collects a small JSON body. 4096: TensorFold's /health grew past 2 KB
// (upstream v0.1.2 fix) — a truncated body parses fine but loses tail fields.
struct Buffer {
  char data[4096];
  size_t len = 0;
};
void toBuffer(const char* d, size_t n, void* ctx) {
  Buffer* b = (Buffer*)ctx;
  size_t room = sizeof b->data - 1 - b->len;
  if (n > room) n = room;
  memcpy(b->data + b->len, d, n);
  b->len += n;
}
void toParser(const char* d, size_t n, void* ctx) { ((PromParser*)ctx)->feed(d, n); }

struct Slot {
  int index = 0;
  ServerCfg cfg{};
  char label[24] = "";
  Deriver deriver;
  uint32_t retry_at = 0;
  char ip[16] = "";
  // task-only state
  enum class Phase : uint8_t { WAIT, HEALTH, VERSION, MODELS, METRICS } phase = Phase::WAIT;
  uint32_t next_action = 0;  // earliest next attempt (ms)
  uint32_t cycle_start = 0;
  bool need_models = true;
  bool need_info = true;
  int info_fails = 0;   // consecutive failed info/models fetches (give-up guard)
  int start_fails = 0;  // consecutive http::start() failures (stuck-engine guard)
  uint32_t last_info = 0;
  Engine engine = Engine::UNKNOWN;
  ServerInfo info;
};

// One request in flight at a time, so the JSON buffer, sample, parser and
// log view are shared scratch.
Slot g_slots[MAX_SLOTS] __uninitialized_psram("slots");
int g_count = 0;
int g_preferred = 0;   // the server the user wants (tap left/right); 0 = 8000
int g_inflight = -1;   // the slot owning the in-flight request, -1 = none
uint32_t g_generation[MAX_SLOTS];
Buffer g_json __uninitialized_psram("scratch");
Sample g_sample __uninitialized_psram("scratch");
View g_log_view __uninitialized_psram("scratch");
// PromParser holds a reference, so it can't be reassigned per poll —
// placement-new it instead (its own counters reset each request).
alignas(PromParser) static char g_parser_storage[sizeof(PromParser)] __uninitialized_psram("scratch");
PromParser& g_parser = *new (g_parser_storage) PromParser(g_sample);

uint32_t ms() { return to_ms_since_boot(get_absolute_time()); }

// The slot to service right now: the in-flight request's owner first, then
// the preferred server — unless it's failing, in which case the first healthy
// alternative takes over (failover), and the preferred one is retried when
// the alternative fails too.
int activeSlot() {
  if (g_inflight >= 0) return g_inflight;
  if (g_count <= 1) return 0;
  Slot& p = g_slots[g_preferred];
  if (p.deriver.consecutiveFailures() == 0) return g_preferred;
  for (int i = 0; i < g_count; i++) {
    if (i != g_preferred && g_slots[i].deriver.consecutiveFailures() == 0) return i;
  }
  return g_preferred;  // everything failing: keep retrying the preferred one
}

void startJson(Slot& s, Slot::Phase phase, const char* path,
               bool (*parse)(const char*, size_t, ServerInfo&)) {
  (void)parse;  // consumed in process(); kept in the signature for symmetry
  g_json.len = 0;
  // Single-flight engine: when a request is already in flight, leave the
  // slot in WAIT — the next task() pass retries. Setting the phase here
  // would make this slot consume the OTHER slot's response.
  if (http::start(s.cfg.host, s.cfg.port, path, toBuffer, &g_json, g_timing.connect_ms,
                  g_timing.read_ms)) {
    s.start_fails = 0;
    s.phase = phase;
    g_inflight = s.index;
  } else {
    // start() only fails when the engine isn't IDLE; with no request in
    // flight that means a wedged engine. Log the first failure, force a
    // reset after a streak — the engine must never wedge silently again.
    if (g_inflight < 0 && ++s.start_fails == 1)
      LOGF("[%s] http start failed (engine state %d)\n", s.label, http::state());
    if (g_inflight < 0 && s.start_fails >= 5) {
      http::reset();
      s.start_fails = 0;
    }
  }
}

void startMetrics(Slot& s) {
  g_sample = Sample();
  g_parser.~PromParser();
  new (g_parser_storage) PromParser(g_sample);
  if (http::start(s.cfg.host, s.cfg.port, "/metrics", toParser, &g_parser, g_timing.connect_ms,
                  g_timing.read_ms)) {
    s.start_fails = 0;
    s.phase = Slot::Phase::METRICS;
    g_inflight = s.index;
  } else {
    if (g_inflight < 0 && ++s.start_fails == 1)
      LOGF("[%s] http start failed (engine state %d)\n", s.label, http::state());
    if (g_inflight < 0 && s.start_fails >= 5) {
      http::reset();
      s.start_fails = 0;
    }
  }
}

// One line of derived values per poll, for checking the screen against the
// server (and the Wi-Fi bars against the real signal).
void logValues(Slot& s) {
  s.deriver.fill(g_log_view, ms(), g_timing.poll_ms, WifiLink::UP, 0);
  LOGF("[%s]   tg %.1f (eng %.1f) pp last %.1f%s run %.0f wait %.0f kv %.1f%% "
         "ttft %.2fs mtp %.1f%% rssi %d dBm\n",
         s.label, g_log_view.tg_live, g_log_view.tg_eng, g_log_view.pp_last,
         isnan(g_log_view.prefilling_s) ? "" : " PREFILLING", g_log_view.running,
         g_log_view.waiting, g_log_view.kv_pct, g_log_view.ttft_cum, g_log_view.mtp_pct,
         net::rssi());
}

void backToWait(Slot& s, uint32_t wait_ms) {
  s.phase = Slot::Phase::WAIT;
  s.next_action = ms() + wait_ms;
}

// Handles a completed request according to the slot's phase.
void process(Slot& s, http::Result& r) {
  if (r.ip[0]) snprintf(s.ip, sizeof s.ip, "%s", r.ip);
  const uint32_t now = ms();

  switch (s.phase) {
    case Slot::Phase::HEALTH: {
      s.last_info = s.cycle_start;
      const bool ok = r.err == http::Err::OK && parseHealth(g_json.data, g_json.len, s.info);
      LOGF("[%s] %-11s %s, %u B, %u ms\n", s.label, "/health", ok ? "ok" : "failed",
             (unsigned)r.bytes, (unsigned)r.ms);
      stdio_flush();
      if (ok) {
        s.info_fails = 0;
        s.need_info = false;
        char ctx[12], streams[12];
        if (isfinite(s.info.context_length)) snprintf(ctx, sizeof ctx, "%.0f", (double)s.info.context_length);
        else snprintf(ctx, sizeof ctx, "?");
        if (isfinite(s.info.max_batch)) snprintf(streams, sizeof streams, "%.0f", (double)s.info.max_batch);
        else snprintf(streams, sizeof streams, "?");
        LOGF("[%s] identity: %s, ctx %s, streams %s\n", s.label,
               s.info.version[0] ? s.info.version
                                 : (s.info.tf_backend ? "TensorFold (backend)" : "unconfirmed"),
               ctx, streams);
        backToWait(s, 0);
      } else if (s.engine != Engine::TENSORFOLD) {
        startJson(s, Slot::Phase::VERSION, "/version", parseVersion);
      } else {
        // A down server used to loop /health forever without reaching
        // /metrics, so its deriver never counted a failure — failover then
        // saw it as healthy and the preferred server starved. Give up on
        // info after 3 and fall through; the next INFO_EVERY_MS cycle
        // retries it when the server is back.
        if (++s.info_fails >= 3) {
          s.need_info = false;
          LOGF("[%s] info endpoints failing (%d), continuing to /metrics\n", s.label,
                 s.info_fails);
        }
        backToWait(s, 0);
      }
      return;
    }
    case Slot::Phase::VERSION: {
      s.last_info = s.cycle_start;
      const bool ok = r.err == http::Err::OK && parseVersion(g_json.data, g_json.len, s.info);
      LOGF("[%s] %-11s %s, %u B, %u ms\n", s.label, "/version", ok ? "ok" : "failed",
             (unsigned)r.bytes, (unsigned)r.ms);
      if (ok) {
        s.info_fails = 0;
        s.need_info = false;
      } else if (++s.info_fails >= 3) {  // same give-up as /health
        s.need_info = false;
        LOGF("[%s] info endpoints failing (%d), continuing to /metrics\n", s.label, s.info_fails);
      }
      backToWait(s, 0);
      return;
    }
    case Slot::Phase::MODELS: {
      const bool ok = r.err == http::Err::OK && parseModels(g_json.data, g_json.len, s.info);
      LOGF("[%s] %-11s %s, %u B, %u ms\n", s.label, "/v1/models", ok ? "ok" : "failed",
             (unsigned)r.bytes, (unsigned)r.ms);
      if (ok) {
        s.info_fails = 0;
        s.need_models = false;
        if (s.info.model[0]) LOGF("[%s] model: %s\n", s.label, s.info.model);
      } else if (++s.info_fails >= 3) {  // same give-up: /metrics must still run
        s.need_models = false;
        LOGF("[%s] info endpoints failing (%d), continuing to /metrics\n", s.label, s.info_fails);
      }
      backToWait(s, 0);
      return;
    }
    case Slot::Phase::METRICS: {
      s.start_fails = 0;  // a completed request proves the engine started
      uint32_t wait = g_timing.poll_ms;
      if (r.err == http::Err::OK) {
        g_parser.finish();
        g_sample.t_ms = now;
        Identity id = identify(g_sample, r.server, s.info);
        if (id.engine != s.engine && id.engine != Engine::UNKNOWN) {
          s.engine = id.engine;  // re-read the engine's own info endpoint
          s.need_info = true;
        }
        const bool restart = s.deriver.onSample(g_sample, id, s.info);
        if (restart) s.need_models = s.need_info = true;
        LOGF("[%s] /metrics    ok, %u B, %u ms, %s%s, %u lines (%u used)%s\n", s.label,
               (unsigned)r.bytes, (unsigned)r.ms, engineName(id.engine),
               id.confirmed ? "" : " (unconfirmed)", (unsigned)g_parser.lines(),
               (unsigned)g_parser.recognised(), restart ? ", RESTART" : "");
        logValues(s);
      } else {
        g_sample.t_ms = now;
        char why[32];
        http::describe(r, s.cfg.host, why, sizeof why);
        s.deriver.onFailure(g_sample.t_ms, why);
        const int fails = s.deriver.consecutiveFailures();
        if (fails >= 3) {
          // Back off 2, 4 s, then retry every 9 s (upstream v0.1.4: the old
          // 8→16→30 s tail delayed recovery long after the server returned).
          static_assert(sizeof BACKOFF_MS / sizeof BACKOFF_MS[0] >= 3,
                        "backoff table needs its first three entries");
          const int i = fails - 3;
          wait = BACKOFF_MS[i < 2 ? i : 2];
          s.need_models = s.need_info = true;  // re-read everything when it comes back
        }
        LOGF("[%s] /metrics    FAILED: %s (%u ms), retry in %u ms\n", s.label, why,
               (unsigned)r.ms, (unsigned)wait);
      }
      s.retry_at = s.cycle_start + wait;
      g_generation[s.index]++;
      backToWait(s, wait);
      return;
    }
    case Slot::Phase::WAIT:
      return;
  }
}

}  // namespace

void begin(const ServerCfg* servers, int count, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms) {
  g_timing.poll_ms = poll_ms;
  g_timing.connect_ms = connect_ms;
  g_timing.read_ms = read_ms;
  g_count = count < MAX_SLOTS ? count : MAX_SLOTS;
  g_preferred = 0;
  g_inflight = -1;
  for (int i = 0; i < g_count; i++) {
    Slot& s = g_slots[i];
    s.index = i;
    s.cfg = servers[i];
    s.phase = Slot::Phase::WAIT;
    s.next_action = 0;
    s.retry_at = 0;
    s.need_models = s.need_info = true;
    s.info_fails = s.start_fails = 0;
    s.engine = Engine::UNKNOWN;
    s.ip[0] = '\0';
    if (s.cfg.name) {
      snprintf(s.label, sizeof s.label, "%s", s.cfg.name);
    } else {
      const char* dot = strchr(s.cfg.host, '.');
      snprintf(s.label, sizeof s.label, "%.*s", dot ? (int)(dot - s.cfg.host) : 23, s.cfg.host);
    }
    LOGF("  [%d] %s:%u\n", i, s.cfg.host, (unsigned)s.cfg.port);
  }
}

void task() {
  if (!g_count) return;
  http::task();  // deadline enforcement for the in-flight request
  Slot& s = g_slots[activeSlot()];
  const uint32_t now = ms();

  // 30 s heartbeat: several poller states are silent by design (WiFi down or
  // DHCP pending, http start failures, info-phase cycles). This line makes
  // any of them diagnosable from the serial log alone. Slot fields: phase
  // (w wait, h health, v version, m models, r metrics), deriver failures (f),
  // info failures (i), start failures (s).
  static uint32_t hb_at = 0;
  if (now - hb_at >= 30000) {
    hb_at = now;
    LOGF("[hb] cfg=%d rssi=%d http=%d inflight=%d", (int)net::configured(), net::rssi(),
           http::state(), g_inflight);
    for (int i = 0; i < g_count; i++) {
      const Slot& sl = g_slots[i];
      static const char PHASE[] = "whvmr";
      const int pi = sl.phase == Slot::Phase::WAIT      ? 0
                     : sl.phase == Slot::Phase::HEALTH  ? 1
                     : sl.phase == Slot::Phase::VERSION ? 2
                     : sl.phase == Slot::Phase::MODELS  ? 3
                                                        : 4;
      LOGF(" | %d:%c f%d i%d s%d", i, PHASE[pi], sl.deriver.consecutiveFailures(), sl.info_fails,
             sl.start_fails);
    }
    LOGF("\n");
  }

  switch (s.phase) {
    case Slot::Phase::WAIT: {
      if (!net::configured()) {
        // Down, or DHCP not bound yet: connecting against a half-ready link
        // at boot must not count as a server failure (it used to mark the
        // preferred server failed and trigger the failover starvation).
        // count generations while down so the UI keeps refreshing (upstream
        // did this on each 500 ms task tick)
        if ((int32_t)(now - s.next_action) >= 0) {
          g_generation[s.index]++;
          s.next_action = now + 500;
        }
        break;
      }
      if ((int32_t)(now - s.next_action) < 0) break;
      s.cycle_start = now;
      if (s.need_info || now - s.last_info >= INFO_EVERY_MS) {
        if (s.engine != Engine::VLLM) {
          startJson(s, Slot::Phase::HEALTH, "/health", parseHealth);
          break;
        }
        if (s.engine != Engine::TENSORFOLD) {
          startJson(s, Slot::Phase::VERSION, "/version", parseVersion);
          break;
        }
        s.need_info = false;
        s.last_info = now;
      }
      if (s.need_models) {
        startJson(s, Slot::Phase::MODELS, "/v1/models", parseModels);
        break;
      }
      startMetrics(s);
      break;
    }
    case Slot::Phase::HEALTH:
    case Slot::Phase::VERSION:
    case Slot::Phase::MODELS:
    case Slot::Phase::METRICS: {
      http::Result r;
      if (http::poll(r)) {
        process(s, r);
        g_inflight = -1;
      }
      break;
    }
  }
}

int count() { return g_count; }

void setPreferred(int index) {
  if (index >= 0 && index < MAX_SLOTS) g_preferred = index;
}

int active() { return activeSlot(); }

bool busy() { return g_inflight >= 0; }

void setPollMs(uint32_t ms_) { g_timing.poll_ms = ms_; }

uint32_t pollMs() { return g_timing.poll_ms; }

void snapshot(int index, View& v, WifiLink link) {
  if (index < 0 || index >= g_count) return;
  Slot& s = g_slots[index];
  const uint32_t now = ms();
  const uint32_t retry = (int32_t)(s.retry_at - now) > 0 ? s.retry_at - now : 0;
  s.deriver.fill(v, now, g_timing.poll_ms, link, retry);
  memcpy(v.server_ip, s.ip, sizeof v.server_ip);
}

uint32_t generation(int index) {
  return (index >= 0 && index < g_count) ? g_generation[index] : 0;
}

}  // namespace poller

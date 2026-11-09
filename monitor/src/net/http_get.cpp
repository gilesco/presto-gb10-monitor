// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Async HTTP/1.0 GET over lwIP's raw TCP API. Callbacks arrive from the
// CYW43 async-context worker (IRQ context on core 0), so they only append
// bytes and set flags — logging and decisions happen in task()/poll(),
// which run from the main loop. Semantics (errors, deadlines, header
// handling, describe() strings) mirror the ESP32 original.

#include "net/http_get.h"

#include <cstdio>
#include "serial_log.h"
#include <cstdlib>
#include <cstring>
#include <strings.h>

// Header/body split (host-testable; see http_split.cpp for the segmentation
// hazard this factoring fixes).
#include "net/http_split.h"

#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "pico/stdlib.h"
#include "pico/time.h"

namespace http {

namespace {

constexpr uint32_t MAX_TOTAL_MS = 5000;  // hard cap per request (upstream)

enum class State : uint8_t { IDLE, DNS, TCP, HEADERS, BODY, DONE };

State g_state = State::IDLE;
tcp_pcb* g_pcb = nullptr;

char g_host[64];
uint16_t g_port = 0;
char g_path[64];
Sink g_sink = nullptr;
void* g_ctx = nullptr;
uint32_t g_connect_ms = 800, g_read_ms = 1500;
uint32_t g_start_ms = 0, g_activity_ms = 0;

Result g_result;
bool g_result_valid = false;

ip_addr_t g_resolved;
char g_req[256];

// Header accumulation is gone: splitHeaders() (net/http_split.h) does the
// header/body split on a caller-provided copy, so no buffer ever holds both
// once the segment straddles the boundary.
long g_content_length = -1;

uint32_t ms() { return to_ms_since_boot(get_absolute_time()); }

// M2 breadcrumbs: set to 1 to trace the HTTP engine's steps (start, DNS,
// connect, finish). Off for normal running; output is flushed per line so it
// survives panics.
#ifndef HTTP_TRACE_ENABLED
#define HTTP_TRACE_ENABLED 0
#endif
#if HTTP_TRACE_ENABLED
#define HTTP_TRACE(...)                     \
    do {                                    \
        LOGF("[http] " __VA_ARGS__);      \
        stdio_flush();                      \
    } while (0)
#else
#define HTTP_TRACE(...) (void)0
#endif

void finish(Err err) {
  if (g_state == State::DONE) return;  // e.g. err callback after our own abort
  HTTP_TRACE("finish err=%d state=%d\n", (int)err, (int)g_state);
  g_result.err = err;
  g_result.ms = ms() - g_start_ms;
  g_result_valid = true;
  g_state = State::DONE;
  // pcb is freed by lwIP in the err path; close it here on graceful paths.
  if (g_pcb) {
    tcp_recv(g_pcb, nullptr);
    tcp_err(g_pcb, nullptr);
    if (tcp_close(g_pcb) != ERR_OK) tcp_abort(g_pcb);
    g_pcb = nullptr;
  }
}

void abort_and_finish(Err err) {
  if (g_state == State::DONE) return;
  finish(err);          // marks DONE and clears callbacks
  if (g_pcb) {
    tcp_abort(g_pcb);   // err callback fires; guarded by DONE
    g_pcb = nullptr;
  }
}

// Streams received bytes through the header-accumulation state machine.
void process_bytes(const char* data, size_t len) {
  if (g_state == State::HEADERS) {
    // TensorFold 1.2.3 writes headers and body start in one TCP segment, so
    // the CRLFCRLF marker can sit mid-buffer; splitHeaders() finds it there,
    // parses the headers on a copy (an in-place strtok_r once rewrote the
    // \r of leading body lines to NUL and silently ate the first fields),
    // and reports where the body starts.
    static char hdr_copy[2048];
    int status;
    const char* server;
    long content_length;
    size_t body_off;
    if (!splitHeaders(data, len, hdr_copy, sizeof hdr_copy, &status, &server,
                      &content_length, &body_off))
      return;  // headers still incomplete
    g_result.status = status;
    snprintf(g_result.server, sizeof g_result.server, "%s", server);
    g_content_length = content_length;
    if (status != 200) {
      abort_and_finish(Err::STATUS);
      return;
    }
    g_state = State::BODY;
    if (body_off < len) {
      const size_t body = len - body_off;
      g_sink(data + body_off, body, g_ctx);
      g_result.bytes += body;
    }
    return;
  }
  // BODY
  g_sink(data, len, g_ctx);
  g_result.bytes += len;
  if (g_content_length >= 0 && (long)g_result.bytes >= g_content_length) {
    finish(Err::OK);
  }
}

err_t recv_cb(void*, tcp_pcb* pcb, pbuf* p, err_t err) {
  if (err != ERR_OK || !pcb) {
    abort_and_finish(Err::BAD_RESPONSE);
    return ERR_OK;
  }
  if (!p) {  // FIN: server closed — body complete (HTTP/1.0)
    if (g_state == State::BODY || g_state == State::HEADERS) {
      // Upstream: disconnect before a full header block → bad response.
      finish(g_state == State::BODY ? Err::OK : Err::BAD_RESPONSE);
    }
    return ERR_OK;
  }
  g_activity_ms = ms();
  for (pbuf* q = p; q && g_state != State::DONE; q = q->next) {
    process_bytes((const char*)q->payload, q->len);
  }
  const uint16_t plen = p->tot_len;
  tcp_recved(pcb, plen);
  pbuf_free(p);
  return ERR_OK;
}

err_t connected_cb(void*, tcp_pcb* pcb, err_t err) {
  if (err != ERR_OK || !pcb) {
    abort_and_finish(Err::CONNECT);
    return ERR_OK;
  }
  const int n = snprintf(g_req, sizeof g_req,
                         "GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: system-d-monitor\r\n"
                         "Accept: */*\r\nConnection: close\r\n\r\n",
                         g_path, g_host, (unsigned)g_port);
  g_state = State::HEADERS;
  g_content_length = -1;
  HTTP_TRACE("tcp connected\n");
  tcp_write(pcb, g_req, (uint16_t)n, TCP_WRITE_FLAG_COPY);
  tcp_output(pcb);
  g_activity_ms = ms();
  return ERR_OK;
}

void err_cb(void*, err_t err) {
  // pcb is already freed by lwIP when the error callback runs.
  g_pcb = nullptr;
  if (g_state == State::DONE) return;
  HTTP_TRACE("tcp err %d in state %d\n", (int)err, (int)g_state);  // Before the connection was established: "refused"; mid-response: bad.
  abort_and_finish(g_state == State::TCP ? Err::CONNECT : Err::BAD_RESPONSE);
}

void dns_cb(const char*, const ip_addr_t* addr, void*) {
  if (g_state != State::DNS) return;  // timed out / stale callback
  if (!addr) {
    HTTP_TRACE("dns failed\n");
    finish(Err::DNS);
    return;
  }
  g_resolved = *addr;
  snprintf(g_result.ip, sizeof g_result.ip, "%s", ipaddr_ntoa(&g_resolved));
  HTTP_TRACE("dns -> %s\n", g_result.ip);
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!pcb) {
    finish(Err::CONNECT);
    return;
  }
  g_pcb = pcb;
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, recv_cb);
  tcp_err(pcb, err_cb);
  g_state = State::TCP;
  const err_t e = tcp_connect(pcb, &g_resolved, g_port, connected_cb);
  if (e != ERR_OK) {
    abort_and_finish(Err::CONNECT);
  }
}

}  // namespace

bool start(const char* host, uint16_t port, const char* path, Sink sink, void* ctx,
           uint32_t connect_ms, uint32_t read_ms) {
  if (g_state != State::IDLE) return false;
  HTTP_TRACE("start %s:%u%s\n", host, (unsigned)port, path);
  memset(&g_result, 0, sizeof g_result);
  g_result_valid = false;
  g_sink = sink;
  g_ctx = ctx;
  g_connect_ms = connect_ms;
  g_read_ms = read_ms;
  snprintf(g_host, sizeof g_host, "%s", host);
  g_port = port;
  snprintf(g_path, sizeof g_path, "%s", path);
  g_start_ms = g_activity_ms = ms();
  g_content_length = -1;
  g_state = State::DNS;
  ip_addr_t addr;
  const err_t e = dns_gethostbyname(host, &addr, dns_cb, nullptr);
  if (e == ERR_OK) {
    dns_cb(host, &addr, nullptr);  // numeric or cached: connect immediately
  } else if (e != ERR_INPROGRESS) {
    finish(Err::DNS);
  }
  return true;
}

bool busy() { return g_state != State::IDLE; }

bool poll(Result& r) {
  if (g_state != State::DONE || !g_result_valid) return false;
  r = g_result;
  g_state = State::IDLE;
  return true;
}

void task() {
  if (g_state == State::IDLE || g_state == State::DONE) return;
  const uint32_t now = ms();
  const uint32_t hard = g_start_ms + MAX_TOTAL_MS;
  if (g_state == State::DNS || g_state == State::TCP) {
    if ((int32_t)(now - (g_start_ms + g_connect_ms)) >= 0 || (int32_t)(now - hard) >= 0) {
      abort_and_finish(Err::TIMEOUT);
    }
    return;
  }
  // HEADERS/BODY: no data for read_ms, or past the hard cap.
  if ((int32_t)(now - (g_activity_ms + g_read_ms)) >= 0 || (int32_t)(now - hard) >= 0) {
    abort_and_finish(Err::TIMEOUT);
  }
}

void reset() {
  // Every state either reaches DONE via the deadlines above or is already
  // IDLE — a state that persists without an owner is a bug. Abort loudly and
  // return to IDLE so the poller can carry on.
  if (g_state != State::IDLE) {
    LOGF("http: forced reset from state %d\n", (int)g_state);
    stdio_flush();
  }
  if (g_pcb) {
    tcp_recv(g_pcb, nullptr);
    tcp_err(g_pcb, nullptr);
    tcp_abort(g_pcb);
    g_pcb = nullptr;
  }
  g_state = State::IDLE;
  g_result_valid = false;
}

int state() { return (int)g_state; }

void describe(const Result& r, const char* host, char* out, size_t len) {
  switch (r.err) {
    case Err::OK: snprintf(out, len, "ok"); break;
    case Err::DNS: snprintf(out, len, "dns %s", host); break;
    case Err::CONNECT: snprintf(out, len, "refused"); break;
    case Err::TIMEOUT: snprintf(out, len, "timeout"); break;
    case Err::STATUS: snprintf(out, len, "http %d", r.status); break;
    case Err::BAD_RESPONSE: snprintf(out, len, "bad response"); break;
  }
}

}  // namespace http

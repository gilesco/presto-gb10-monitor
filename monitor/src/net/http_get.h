// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

// Minimal HTTP/1.0 GET (SPEC.md §4.2): HTTP/1.0 + Connection: close means no
// chunked encoding, and the body streams straight to a callback.
//
// Ported from the ESP32 original (blocking Arduino WiFiClient) to lwIP's raw
// API as a single-request async engine: the UI loop on core 0 must never
// stall on the network, so start() kicks a request off, task() enforces the
// deadlines (called from the main loop), and poll() reports completion.
// One request in flight at a time — the poller serialises its servers.
namespace http {

enum class Err : uint8_t { OK, DNS, CONNECT, TIMEOUT, STATUS, BAD_RESPONSE };

struct Result {
  Err err = Err::OK;
  int status = 0;        // HTTP status code
  char server[24] = "";  // "Server:" response header
  char ip[16] = "";      // the address the host name resolved to
  size_t bytes = 0;      // body bytes delivered
  uint32_t ms = 0;       // total time
};

using Sink = void (*)(const char* data, size_t len, void* ctx);

// Kicks off a request (DNS + connect + GET). Returns false if another
// request is still in flight (its result hasn't been collected).
bool start(const char* host, uint16_t port, const char* path, Sink sink, void* ctx,
           uint32_t connect_ms, uint32_t read_ms);

bool busy();

// True when the request finished (any outcome); fills r exactly once.
bool poll(Result& r);

// Deadline checks; call from the main loop.
void task();

// Recovery hatch: abort any in-flight request and return to IDLE. For the
// poller's stuck-engine guard — the engine must never stay wedged.
void reset();

// Raw engine state, for the poller's heartbeat log (0=IDLE … 5=DONE).
int state();

// Short reason for the footer: "dns gpu-box.lan", "refused", "timeout", "http 503", …
void describe(const Result& r, const char* host, char* out, size_t len);

}  // namespace http

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#include "app_config.h"
#include "ui/view.h"

// One background task per server (core 0, SPEC.md §10 "Tasks"): /metrics
// every poll_ms; /health + /v1/models on connect, every 30 s and after a
// restart (vLLM: /version instead of /health); exponential backoff while a
// server is down. Servers poll independently, so a dead one never delays
// another.
namespace poller {

void begin(const ServerCfg* servers, int count, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms);

// Steps the cooperative poller state machine (upstream ran one FreeRTOS task
// per server; here everything is non-blocking on core 0). Call every loop.
// Port deviation from upstream's header, documented in the repo README.
void task();

int count();

// Change the poll interval at runtime (Settings); takes effect on each
// task's next cycle.
void setPollMs(uint32_t ms);
uint32_t pollMs();

// Fills v (except the header fields, which the caller sets) from server
// `index`'s latest data. Safe to call from the UI task.
void snapshot(int index, View& v, WifiLink link);

// Failover: the preferred server (user taps; default 0) is polled while it
// answers; when it fails, the other one takes over, and the poller switches
// back as soon as the preferred one answers again. Port deviation from
// upstream (which ran one poller task per server), documented in the README.
void setPreferred(int index);

// The server currently being polled — the UI shows this one's data.
int active();

// True while an HTTP request is in flight. Flash writes stall both cores
// (XIP pause), so callers should wait for an idle engine.
bool busy();

// Incremented after every poll attempt of server `index` (and while Wi-Fi
// is down); redraw when the shown server's generation changes.
uint32_t generation(int index);

}  // namespace poller

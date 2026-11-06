// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ui/view.h"

namespace net {

// Station mode with auto-reconnect, plus SNTP for the header clock.
// An empty SSID leaves Wi-Fi off.
void begin(const char* ssid, const char* pass, const char* tz);

bool up();

// up() plus DHCP actually bound. The poller waits for this before its first
// request: a boot-time connect against a half-ready link must not count as a
// server failure (that's what used to trigger the failover starvation).
bool configured();

// Join maintenance: re-issues the join while the link is down. Called from
// link() and, from the main loop directly — redraws pause during screen
// sleep, but rejoining must not.
void service();

// Link state for the header: tells "still connecting after boot" apart from
// "can't join" and "lost". JOIN_GRACE_MS: how long the first connection may
// take before it counts as failed.
constexpr uint32_t JOIN_GRACE_MS = 20000;
WifiLink link();

// Header info: IP ("" when down) and signal bars 0–4 (-1 when down);
// RSSI ≥ −55/−67/−75/−85 dBm → 4/3/2/1 bars (SPEC.md §7.1).
void info(char* ip, size_t ip_len, int8_t* bars);

// Raw signal strength in dBm (0 when down).
int rssi();

// Date and 12-hour time, "11-02-26 2:47:33 PM" (MM-DD-YY, user request), or
// "" until SNTP has set the time.
void clock(char* out, size_t len);

}  // namespace net

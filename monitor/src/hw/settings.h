// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Settings persistence for the Presto port (upstream used ESP32 NVS via
// Preferences). One checksummed record in the LAST flash sector, written
// with the core-1 scanout parked via panel::pauseScanout() (XIP stalls both
// cores during erase/program). Writes are debounced: a burst of taps costs
// a single sector erase.
#pragma once

#include <stdint.h>

namespace settings {

struct Values {
  uint8_t theme;       // theme index (fixed compile-time list)
  uint8_t page;        // dashboard page: 0 = overview, 1 = graphs
  uint8_t server;      // preferred server index (tap left/right)
  uint8_t brightness;  // percent
  uint32_t poll_ms;
};

// Reads the record from flash. True when a valid record was found (fields in
// `out`); false means defaults should apply (blank, old, or corrupt sector).
bool load(Values& out);

// Marks new values dirty. The flash write is debounced and happens from
// service(); nothing is written here.
void store(const Values& v);

// Call from the main loop. net_idle: pass !poller::busy() — a flash write
// pauses the whole chip (XIP off for both cores), so it must never start
// while an HTTP request is in flight.
void service(bool net_idle);

// DIAGNOSTIC: raw write with no core-1 coordination — only safe before the
// scanout/WiFi exist (boot time). Used to bisect the flash-write hang.
void writeNowForTest(const Values& v, uint32_t offset, int mode);

// DIAGNOSTIC: print flash chip identity/status registers (pure reads).
void printFlashRegs();

}  // namespace settings

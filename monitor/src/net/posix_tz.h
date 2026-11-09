// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Minimal POSIX TZ string evaluator (e.g. "GMT0BST,M3.5.0/1,M10.5.0" for
// Europe/London). The ESP32 original used ESP-IDF's setenv()/localtime();
// the Pico SDK's libc can't be relied on for TZ rules, so this implements
// exactly what the config needs: M-format DST rules and second-level offsets.
// Pure C++, no platform includes (host-testable).
//
// POSIX offset sign convention: the offset is ADDED to local time to get UTC
// (i.e. "PST8" = UTC-8). Local = UTC - effective_offset.

namespace posix_tz {

constexpr size_t ABBR_LEN = 6;

struct Rule {
  bool valid = false;
  uint8_t month = 0;  // 1-12
  uint8_t week = 0;   // 1-5, 5 = last <day> of the month
  uint8_t day = 0;    // 0 = Sunday .. 6
  int32_t time_s = 2 * 3600;
};

struct Rules {
  char std_abbr[ABBR_LEN] = "";
  char dst_abbr[ABBR_LEN] = "";
  int32_t std_off_s = 0;  // seconds added to local-std to get UTC
  int32_t dst_off_s = 0;  // same for DST; valid only when has_dst
  bool has_dst = false;
  Rule start;  // into DST
  Rule end;    // back to STD
};

// Parses a POSIX TZ string. Returns false (and leaves *out untouched-ish)
// if it can't be parsed; the caller keeps working with UTC then.
bool parse(const char* tz, Rules& out);

// UTC epoch seconds -> broken-down local time. Returns false when the rules
// failed to parse (caller falls back to UTC).
bool to_local(const Rules& r, uint64_t utc_s, struct tm* out);

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm).
void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d);

}  // namespace posix_tz

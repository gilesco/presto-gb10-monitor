// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "sample.h"

// Streaming Prometheus text parser (SPEC.md §4.2). Feed the HTTP body in any
// chunk sizes; it keeps one line (2 KB) at a time, so /metrics (14 KB on
// TensorFold, 67 KB on vLLM) never needs to be buffered. Arduino-free.
class PromParser {
 public:
  explicit PromParser(Sample& out) : s_(out) {}

  void feed(const char* data, size_t len);
  void finish();  // parses a final line that has no trailing newline

  uint32_t lines() const { return lines_; }          // non-comment lines seen
  uint32_t recognised() const { return matched_; }   // lines that filled a field
  uint32_t overlong() const { return overlong_; }    // lines skipped (> 2 KB)

 private:
  void parseLine(char* line, size_t n);

  Sample& s_;
  char line_[2048];  // vLLM's cache_config_info line is ~1.1 KB
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t lines_ = 0, matched_ = 0, overlong_ = 0;
};

// Finds key="value" in a Prometheus label string (without the braces).
// Returns false if absent; out is always NUL-terminated.
bool promLabel(const char* labels, const char* key, char* out, size_t out_len);

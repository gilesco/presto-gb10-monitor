// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sample.h"

// Slow-changing facts about the server: /health and /v1/models (TensorFold,
// SPEC.md §4.1) and engine identification (§4.3). Arduino-free.

struct ServerInfo {
  // /health (TensorFold)
  // Two formats: 1.1.8 {"status": "ok", "version", "max_batch_size", …};
  // later builds {"ok": true, "backend": "tensorfold", "streams": {"max"},
  // "context_length", …} with no version.
  bool health_ok = false;     // status == "ok", or ok == true
  bool warming = false;
  bool tf_backend = false;    // "backend": "tensorfold"
  float max_batch = NAN;
  char version[48] = "";      // TensorFold: /health.version; vLLM: /version
  // /v1/models
  char model[48] = "";
  float context_length = NAN;
};

// Return false if the body isn't the expected JSON (fields stay unchanged).
bool parseHealth(const char* json, size_t len, ServerInfo& info);
bool parseModels(const char* json, size_t len, ServerInfo& info);
bool parseVersion(const char* json, size_t len, ServerInfo& info);  // vLLM GET /version

struct Identity {
  Engine engine = Engine::UNKNOWN;
  bool confirmed = false;  // a second signal agrees (SPEC.md §4.3 step 2)
};

// Step 1: the prefix with the most recognised /metrics names wins; a tie or
// none → UNKNOWN. Step 2: TensorFold is confirmed by a "Server: TensorFold"
// header, a /health with a version, or one that says backend "tensorfold"; vLLM by a GET /version that parsed.
Identity identify(const Sample& s, const char* server_header, const ServerInfo& info);

const char* engineName(Engine e);  // "TensorFold", "vLLM", "???"

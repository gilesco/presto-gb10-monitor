// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "server_info.h"

#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>

namespace {
void copy(char* dst, size_t n, const char* src) {
  if (!src) return;
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}
}  // namespace

bool parseHealth(const char* json, size_t len, ServerInfo& info) {
  JsonDocument filter;
  filter["status"] = true;
  filter["warming"] = true;
  filter["max_batch_size"] = true;
  filter["version"] = true;
  filter["model"] = true;
  // Newer TensorFold deployments (seen 2026-11) answer a different health
  // schema: {"ok": true, "backend": ..., "streams": {"max": N}, ...
  // "context_length": N} with no status/version/model fields.
  filter["ok"] = true;
  filter["backend"] = true;
  filter["context_length"] = true;
  filter["streams"]["max"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  if (!doc.is<JsonObject>()) return false;
  if (doc["status"].is<const char*>()) {
    info.health_ok = strcmp(doc["status"].as<const char*>(), "ok") == 0;
  } else if (doc["ok"].is<bool>()) {
    info.health_ok = doc["ok"].as<bool>();
  } else {
    return false;  // neither schema
  }
  info.warming = doc["warming"] | false;
  info.tf_backend = doc["backend"].is<const char*>() && strcmp(doc["backend"].as<const char*>(), "tensorfold") == 0;
  if (doc["max_batch_size"].is<float>()) info.max_batch = doc["max_batch_size"].as<float>();
  else if (doc["streams"]["max"].is<float>()) info.max_batch = doc["streams"]["max"].as<float>();
  if (doc["context_length"].is<float>()) info.context_length = doc["context_length"].as<float>();
  copy(info.version, sizeof info.version, doc["version"] | (const char*)nullptr);
  if (!info.model[0]) copy(info.model, sizeof info.model, doc["model"] | (const char*)nullptr);
  return true;
}

bool parseModels(const char* json, size_t len, ServerInfo& info) {
  JsonDocument filter;
  filter["data"][0]["id"] = true;
  filter["data"][0]["context_length"] = true;
  filter["data"][0]["max_model_len"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  JsonObject m = doc["data"][0];
  if (m.isNull() || !m["id"].is<const char*>()) return false;
  copy(info.model, sizeof info.model, m["id"].as<const char*>());
  if (m["context_length"].is<float>()) info.context_length = m["context_length"].as<float>();
  else if (m["max_model_len"].is<float>()) info.context_length = m["max_model_len"].as<float>();
  return true;
}

bool parseVersion(const char* json, size_t len, ServerInfo& info) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if (!doc["version"].is<const char*>()) return false;
  copy(info.version, sizeof info.version, doc["version"].as<const char*>());
  return true;
}

Identity identify(const Sample& s, const char* server_header, const ServerInfo& info) {
  Identity id;
  if (s.n_tensorfold > s.n_vllm) id.engine = Engine::TENSORFOLD;
  else if (s.n_vllm > s.n_tensorfold) id.engine = Engine::VLLM;
  else return id;  // tie or nothing recognised
  if (id.engine == Engine::TENSORFOLD)
    id.confirmed = (server_header && strcasecmp(server_header, "TensorFold") == 0) || info.version[0] || info.tf_backend;
  else
    id.confirmed = info.version[0] != '\0';  // from /version
  return id;
}

const char* engineName(Engine e) {
  switch (e) {
    case Engine::TENSORFOLD: return "TensorFold";
    case Engine::VLLM: return "vLLM";
    default: return "???";
  }
}

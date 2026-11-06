// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Pulls in include/config.h, or the example (Wi-Fi off) if it's missing.
#if __has_include("config.h")
#include "config.h"
#define CONFIG_PRESENT 1
#else
#warning "include/config.h not found: copy include/config.example.h and fill it in. Building with Wi-Fi disabled."
#include "config.example.h"
#define CONFIG_PRESENT 0
#endif

// One monitored server. name == nullptr → SERVER host up to the first dot.
struct ServerCfg {
  const char* name;
  const char* host;
  uint16_t port;
};

#ifdef SERVERS
static const ServerCfg kServers[] = SERVERS;
#else  // older single-server config: SERVER_HOST / SERVER_PORT [/ SERVER_NAME]
#ifdef SERVER_NAME
static const ServerCfg kServers[] = {{SERVER_NAME, SERVER_HOST, SERVER_PORT}};
#else
static const ServerCfg kServers[] = {{nullptr, SERVER_HOST, SERVER_PORT}};
#endif
#endif
constexpr int kServerCount = sizeof kServers / sizeof kServers[0];
static_assert(kServerCount >= 1 && kServerCount <= 4, "SERVERS: 1 to 4 entries");

#ifndef BRIGHTNESS_PCT
#define BRIGHTNESS_PCT 100  // first-boot backlight; then set in Settings
#endif

#ifndef ROTATE_S
#define ROTATE_S 0  // auto-advance to the next server every N s (0 = off)
#endif

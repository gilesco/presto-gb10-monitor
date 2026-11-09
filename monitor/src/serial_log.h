// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once

#include "app_config.h"

// Serial log (USB CDC, 115200), only when SERIAL_LOG is 1 in config.h —
// upstream v0.1.6's release-silent default. When off, the arguments aren't
// evaluated and nothing is compiled in (also skips stdio_init_all()).
#if SERIAL_LOG
#define LOGF(...) printf(__VA_ARGS__)
#define LOGLN(s) puts(s)
#else
#define LOGF(...) ((void)0)
#define LOGLN(s) ((void)0)
#endif

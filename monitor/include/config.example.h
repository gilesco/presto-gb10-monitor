// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
// SYSTEM-D Monitor configuration (SPEC.md §10).
//
// Copy this file to include/config.h and fill it in. config.h is gitignored,
// so your Wi-Fi password never ends up in the repo. Without a config.h the
// firmware still builds (from this file) and shows NO WIFI.

#define WIFI_SSID   ""          // 2.4 GHz network (the ESP32-S3 has no 5 GHz)
#define WIFI_PASS   ""
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"   // header clock; POSIX TZ string

// Servers to monitor, 1–4: { name, host, port }. name nullptr = host up to
// the first dot. Use a host name your router's DNS knows (e.g. "gpu-box.lan")
// or an IP: the ESP32 has no DNS search domain, so a bare "gpu-box" won't
// resolve. Engines are detected automatically (vLLM or TensorFold).
#define SERVERS {                                            \
    {nullptr, "gpu-box.lan", 8000},     /* e.g. vLLM (default port 8000) */ \
}
#define ROTATE_S    0             // auto-advance to the next server every N s (0 = off)

#define POLL_MS     2000          // /metrics every 2 s
#define CONNECT_MS  800
#define READ_MS     1500
#define BRIGHTNESS_PCT 100        // first-boot backlight, % (then set in Settings)

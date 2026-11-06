// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
// Placement attributes for the hot rendering path. On the ESP32-S3, flash
// and PSRAM share one bus, and the LCD DMA keeps PSRAM busy; font data or
// raster code served from flash stalls on every cache miss (≈45 cycles per
// pixel measured). FAST_DATA puts tables in internal DRAM, FAST_CODE puts
// functions in IRAM. Both are no-ops on the host.
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define FAST_DATA DRAM_ATTR
#define FAST_CODE IRAM_ATTR
#else
#define FAST_DATA
#define FAST_CODE
#endif

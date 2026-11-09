// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "hw/panel.h"

#include <cstring>

#include "drivers/st7701/st7701.hpp"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/time.h"

using namespace pimoroni;

namespace {

constexpr uint32_t W = SCREEN_W;
constexpr uint32_t H = SCREEN_H;

// 480x480 RGB565 = 450 KB, in internal SRAM: the PIO scanout reads it line
// by line, exactly as the Presto's MicroPython firmware does (presto.ld
// places the MicroPython framebuffer the same way). The 8 MB PSRAM is left
// to lwIP and buffers.
uint16_t fb[W * H] __attribute__((aligned(4)));

ST7701* presto = nullptr;

// Flash-write handshake with core 0 (hw/settings.cpp): flash erase/program
// stalls XIP on BOTH cores, so core 1 must sit in RAM-only code while it
// happens. The Pimoroni scanout ISRs and wait_for_vsync are already
// __not_in_flash_func (the driver is built to survive flash writes — the
// display keeps scanning the SRAM framebuffer throughout), so the only
// flash-resident code on core 1 is this loop: the pause spin below is
// RAM-resident, and core 0 starts the write only after g_paused is raised.
// (The SDK's multicore_lockout FIFO-IRQ victim deadlocked here instead.)
volatile bool g_pause_req = false;
volatile bool g_paused = false;

void __not_in_flash_func(pause_spin)() {
  __dmb();
  g_paused = true;
  // No interrupts while parked: the scanout ISRs are RAM-resident, but the
  // per-frame path calls out-of-line SDK functions (dma_channel_abort in
  // start_frame_xfer) that live in FLASH — an XIP access mid-write wedges
  // the QSPI port and kills the chip. With IRQs off the scanout stops (the
  // panel blanks for the ~0.4 s write) and core 0's __sev() wakes the WFE.
  const uint32_t ints = save_and_disable_interrupts();
  while (g_pause_req) __wfe();
  restore_interrupts_from_disabled(ints);
  __dmb();
  g_paused = false;
  __sev();
}

// The scanout is IRQ-driven from whichever core called init(); the Presto's
// MicroPython module runs it on core 1, so we do the same and leave core 0
// to the application.
void core1_display() {
  presto->init();
  multicore_fifo_push_blocking(0);  // panel init done
  while (true) {
    if (g_pause_req) pause_spin();
    presto->wait_for_vsync();
  }
}

}  // namespace

namespace panel {

uint16_t* framebuffer() { return fb; }

bool begin() {
  presto = new ST7701(W, H, ROTATE_0,
      SPIPins{spi1, PRESTO_LCD_CS_PIN, PRESTO_LCD_CLK_PIN, PRESTO_LCD_DAT_PIN,
              PIN_UNUSED, (uint)-1 /* no DC pin on the RGB interface */,
              PRESTO_BACKLIGHT_PIN},
      fb, nullptr /* no palette */, PRESTO_LCD_D0_PIN);

  memset(fb, 0, sizeof fb);
  multicore_launch_core1(core1_display);
  multicore_fifo_pop_blocking();  // wait for panel init on core 1
  return true;
}

void setBacklight(uint8_t duty) {
  if (presto) presto->set_backlight(duty);
}

bool pauseScanout() {
  g_pause_req = true;
  __dmb();
  const uint64_t deadline = to_us_since_boot(get_absolute_time()) + 200000;  // 200 ms
  while (!g_paused) {
    if (to_us_since_boot(get_absolute_time()) > deadline) {  // core 1 not answering
      g_pause_req = false;
      __sev();
      return false;
    }
    tight_loop_contents();
  }
  return true;
}

void resumeScanout() {
  g_pause_req = false;
  __dmb();
  __sev();
}

}  // namespace panel

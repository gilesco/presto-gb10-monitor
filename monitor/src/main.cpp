// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// SYSTEM-D Monitor ported to the Pimoroni Presto (RP2350), from the Guition
// ESP32-S3-4848S040 original. Touch and page behaviour is upstream's:
//
//   tap left third / right third   previous / next server (with > 1 server)
//   tap centre                     overview ↔ graphs (anywhere with 1 server)
//   tap bottom-left corner (≡)     settings: refresh rate, brightness, theme
//   hold 2.2 s anywhere            screen off (backlight); a tap turns it back on
//
// M1: the view comes from demoView() (upstream's static sample data) and the
// poller/net modules are stubs; M2 swaps in the real poller. The display
// scanout runs on core 1 (Pimoroni's ST7701 driver), everything app-side
// runs here on core 0. Settings persistence (flash) lands in M3.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/structs/watchdog.h"
#include "hardware/ticks.h"
#include "hardware/watchdog.h"

#include "app_config.h"
#include "gfx/grid.h"
#include "gfx/theme.h"
#include "hw/backlight.h"
#include "hw/panel.h"
#include "hw/screen.h"
#include "hw/settings.h"
#include "hw/touch_ft6236.h"
#include "net/poller.h"
#include "net/wifi.h"
#include "ui/demo_view.h"
#include "ui/pages.h"

#define DEMO 0  // M2: the real poller feeds the view (DEMO 1 was the M1 sample data)

// Diagnostic: store a record automatically 8 s after boot, exercising the
// whole flash write path without needing a tap. Keep 0 for normal running.
#define SETTINGS_SELFTEST 0

// Diagnostic: run the identical flash write at boot, before core 1 / WiFi /
// the scanout exist. Proven to survive with the direct-mode program; keep 0
// for normal running (it writes flash every boot).
#define SETTINGS_BOOT_WRITE_TEST 0

// Bisection knobs for the boot-write test: which sector, and which ops
// (0 = erase+program, 1 = erase only, 2 = program only, 3 = erase + one
// status read, 4 = register reads only).
#define SETTINGS_TEST_OFFSET 0xFF000
#define SETTINGS_TEST_MODE 0

// Settings persistence to flash. The write path is complete and ENABLED:
// the page program goes through our own QMI direct-mode transaction that
// waits for WIP before re-enabling XIP (the SDK's flash_range_program /
// flash_do_cmd re-enable XIP while the chip is still programming, and the
// first fetch from flash then stalls the bus — see the README "Settings
// persistence" section for the full story).
#define SETTINGS_PERSIST 1

namespace {

constexpr uint32_t TOUCH_POLL_MS = 33;  // ~30 Hz
constexpr uint32_t TAP_MAX_MS = 500;    // SPEC.md §9
constexpr uint32_t HOLD_MS = 700;
// Hold this long: screen off. 2.2 s on the board; the README says "3 s" so a
// user who counts to three is sure to get there (user request).
constexpr uint32_t SLEEP_HOLD_MS = 2200;
constexpr int TAP_MAX_MOVE = 24;
constexpr uint32_t ROTATE_PAUSE_MS = 60000;  // a touch pauses auto-rotate (SPEC.md §9)

Grid grid;
View view;
pages::Page page = pages::Page::OVERVIEW;
pages::Page back_to = pages::Page::OVERVIEW;  // where "done" in Settings returns
int theme_index = 0;
int list_page = 0;  // settings: page of the theme list
uint8_t brightness_pct = BRIGHTNESS_PCT;
int server = 0;  // index into kServers
uint32_t last_rotate = 0, last_touch_ms = 0;
bool touch_ok = false;
// Screen sleep: only the backlight goes off. Polling keeps running (M2+), so
// history and averages are current on wake; redraws pause meanwhile.
bool asleep = false;

// Gesture state
bool pressed = false;
bool hold_fired = false;
bool swallow = false;  // this press woke or slept the screen: it does nothing else
uint32_t press_ms = 0;
int press_x = 0, press_y = 0, last_x = 0, last_y = 0;

uint32_t ms() { return to_ms_since_boot(get_absolute_time()); }

// Header label: the configured name, or the host up to the first dot.
void serverName(const ServerCfg& s, char* out, size_t len) {
  if (s.name) {
    snprintf(out, len, "%s", s.name);
    return;
  }
  const char* dot = strchr(s.host, '.');
  snprintf(out, len, "%.*s", dot ? (int)(dot - s.host) : (int)strlen(s.host), s.host);
}

void refreshView() {
#if DEMO
  view = demoView();
  serverName(kServers[server], view.server, sizeof view.server);
  view.server_index = (uint8_t)server;
  view.server_count = (uint8_t)kServerCount;
  snprintf(view.status, sizeof view.status, "demo data \xC2\xB7 wifi lands in M2");
#else
  net::clock(view.clock, sizeof view.clock);
  // The header shows the server the user selected, not the one the failover
  // logic is polling right now — otherwise a down selection can never be
  // viewed (the display snapped back to whoever was healthy on every redraw).
  // The poller keeps the healthy server live regardless of the selection.
  serverName(kServers[server], view.server, sizeof view.server);
  net::info(view.ip, sizeof view.ip, &view.wifi_bars);
  view.server_index = (uint8_t)server;
  view.server_count = (uint8_t)kServerCount;
  poller::snapshot(server, view, net::link());
  if (!CONFIG_PRESENT) snprintf(view.status, sizeof view.status, "no include/config.h");
#endif
}

pages::Settings settingsState() { return {poller::pollMs(), brightness_pct, theme_index, list_page}; }

// Dashboard page to persist: while the settings menu is open, `page` holds
// SETTINGS, so the page underneath (where "done" returns to) is what's saved.
// Upstream saved the raw page value here, which booted into GRAPHS after a
// settings visit; this is the corrected behaviour.
uint8_t dashPage() {
  const pages::Page p = page == pages::Page::SETTINGS ? back_to : page;
  return p == pages::Page::GRAPHS ? 1 : 0;
}

// Snapshot everything the user can set. Debounced; the write happens in the
// main loop via settings::service() when the engine request is done.
// Gated off while the flash-program platform issue stands (README); settings
// apply for the session.
void persist() {
#if SETTINGS_PERSIST
  settings::store({(uint8_t)theme_index, dashPage(), (uint8_t)server, brightness_pct,
                   poller::pollMs()});
#else
  (void)0;
#endif
}

void redraw() {
  if (page == pages::Page::SETTINGS) {
    pages::settings(grid, settingsState());
  } else {
    refreshView();
    pages::draw(page, grid, view);
  }
  screen::present(grid, theme::get(theme_index));
}

void openSettings() {
  back_to = page;
  page = pages::Page::SETTINGS;
  list_page = theme_index / pages::THEMES_PER_PAGE;
  redraw();
}

void setTheme(int index) {
  theme_index = index;
  persist();
  printf("theme -> %s\n", theme::get(theme_index).name);
  screen::invalidate();
  redraw();
}

void stepPoll(int dir) {
  int i = 0;
  while (i < pages::POLL_OPTION_COUNT - 1 && pages::POLL_OPTIONS_MS[i] < poller::pollMs()) i++;
  i += dir;
  if (i < 0 || i >= pages::POLL_OPTION_COUNT) return;
  poller::setPollMs(pages::POLL_OPTIONS_MS[i]);
  persist();
  printf("poll -> %u ms\n", (unsigned)pages::POLL_OPTIONS_MS[i]);
  redraw();
}

void stepBrightness(int dir) {
  int i = 0;
  while (i < pages::BRIGHTNESS_OPTION_COUNT - 1 && pages::BRIGHTNESS_OPTIONS_PCT[i] < brightness_pct) i++;
  i += dir;
  if (i < 0 || i >= pages::BRIGHTNESS_OPTION_COUNT) return;
  brightness_pct = pages::BRIGHTNESS_OPTIONS_PCT[i];
  backlight::set(pages::brightnessDuty(brightness_pct));
  persist();
  printf("brightness -> %u%% (duty %u)\n", brightness_pct, pages::brightnessDuty(brightness_pct));
  redraw();
}

void onSettingsTap(int x, int y) {
  pages::SettingsHit h = pages::settingsHit(x, y, settingsState());
  static const char* const KIND[] = {"none",  "refresh -", "refresh +", "brightness -", "brightness +",
                                     "theme", "page -",    "page +",    "done"};
  printf("  -> %s%s\n", KIND[h.kind], h.kind == pages::SettingsHit::THEME ? theme::get(h.theme).name : "");
  switch (h.kind) {
    case pages::SettingsHit::POLL_PREV: return stepPoll(-1);
    case pages::SettingsHit::POLL_NEXT: return stepPoll(+1);
    case pages::SettingsHit::BRIGHT_PREV: return stepBrightness(-1);
    case pages::SettingsHit::BRIGHT_NEXT: return stepBrightness(+1);
    case pages::SettingsHit::THEME: return setTheme(h.theme);
    case pages::SettingsHit::PAGE_PREV:
      if (list_page > 0) { list_page--; redraw(); }
      return;
    case pages::SettingsHit::PAGE_NEXT:
      if (list_page + 1 < pages::themePages()) { list_page++; redraw(); }
      return;
    case pages::SettingsHit::DONE:
      page = back_to;
      return redraw();
    case pages::SettingsHit::NONE: return;
  }
}

void showServer(int index, bool remember = true) {
  server = (index + kServerCount) % kServerCount;
  poller::setPreferred(server);  // fail back to the user's choice when it recovers
  if (remember) persist();  // auto-rotate doesn't rewrite flash every pass
  printf("server -> %d (%s:%u)\n", server, kServers[server].host, (unsigned)kServers[server].port);
  redraw();
}

void onTap(int x, int y) {
  printf("tap x=%d y=%d (col %d, row %d) on %s\n", x, y, x / 10, y / 24,
         page == pages::Page::SETTINGS ? "settings" : "dashboard");
  if (page == pages::Page::SETTINGS) return onSettingsTap(x, y);
  if (pages::isSettingsCorner(x, y)) return openSettings();
  if (kServerCount > 1 && x < SCREEN_W / 3) return showServer(server - 1);
  if (kServerCount > 1 && x >= SCREEN_W * 2 / 3) return showServer(server + 1);
  page = page == pages::Page::OVERVIEW ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  persist();
  redraw();
}

void sleepScreen() {
  asleep = true;
  backlight::set(0);
  printf("screen -> off\n");
}

void wakeScreen() {
  asleep = false;
  redraw();  // catch up first, so the backlight comes on over a fresh frame
  backlight::set(pages::brightnessDuty(brightness_pct));
  printf("screen -> on\n");
}

void pollTouch(uint32_t now) {
  touch::Point p = touch::read();
  if (p.fresh && p.down) {
    last_touch_ms = now;
    if (!pressed) {
      pressed = true;
      hold_fired = false;
      press_ms = now;
      press_x = p.x;
      press_y = p.y;
      swallow = asleep;
      if (asleep) wakeScreen();  // on touch-down, so it feels immediate
    }
    last_x = p.x;
    last_y = p.y;
  } else if (p.fresh && pressed) {  // released
    pressed = false;
    const int moved = abs(last_x - press_x) + abs(last_y - press_y);
    if (!swallow && !hold_fired && now - press_ms <= TAP_MAX_MS && moved < TAP_MAX_MOVE)
      onTap(press_x, press_y);
  }
  if (pressed && !hold_fired && now - press_ms >= HOLD_MS) hold_fired = true;  // a hold isn't a tap
  // Sleep fires while the finger is still down, so it's clear it worked;
  // lifting that finger doesn't wake it. A drag doesn't count.
  if (pressed && !swallow && now - press_ms >= SLEEP_HOLD_MS &&
      abs(last_x - press_x) + abs(last_y - press_y) < TAP_MAX_MOVE) {
    swallow = true;
    sleepScreen();
  }
}

}  // namespace

int main() {
  stdio_init_all();
  sleep_ms(2000);  // give USB CDC a moment to enumerate before first prints
  printf("\n\nsystem-d monitor (presto port) \xE2\x80\x94 %d server(s), poll %u ms%s\n", kServerCount,
         (unsigned)POLL_MS, CONFIG_PRESENT ? "" : " (no config.h: Wi-Fi off)");
  printf("wd: tick_running=%d last_reboot_was_wd=%d\n", (int)tick_is_running(TICK_WATCHDOG),
         (int)watchdog_caused_reboot());
  watchdog_hw->scratch[5]++;  // survives watchdog resets: boot counter
  printf("boot #%lu\n", (unsigned long)watchdog_hw->scratch[5]);
#if SETTINGS_BOOT_WRITE_TEST
  {
    printf("st: BOOT-TIME write test\n"); stdio_flush();
    // Where the previous life died (watchdog scratch survives the reset):
    // 0 none · 1 irqs off · 2 PSRAM parked · 3 erased · 4 programmed · 5
    // restored — plus the WP#/HOLD# pad registers before/after exit_xip
    // (scratch[0..3], mode 6's breadcrumbs).
    printf("st: previous life died at step %lu\n", (unsigned long)watchdog_hw->scratch[4]);
    printf("st: pads WP# %08lx->%08lx HOLD# %08lx->%08lx | WIP %02lx after %lu reads | readback %08lx | jedec %06lx\n",
           (unsigned long)watchdog_hw->scratch[0], (unsigned long)watchdog_hw->scratch[2],
           (unsigned long)watchdog_hw->scratch[1], (unsigned long)watchdog_hw->scratch[3],
           ((unsigned long)watchdog_hw->scratch[7] >> 24) & 0xFF,
           (unsigned long)watchdog_hw->scratch[7] & 0xFFFF,
           (unsigned long)watchdog_hw->scratch[6],
           (unsigned long)watchdog_hw->scratch[6] & 0xFFFFFF);
    stdio_flush();
    // Arm the watchdog NOW: if the write hangs, the reset cycle interval in
    // dmesg timestamps the death (~5 s cycle = the op itself; no cycle = op
    // is fine and the earlier kills were runtime interactions).
    watchdog_enable(3000, 1);
    settings::Values v = {};
    v.theme = 0;
    v.page = 0;
    v.server = 0;
    v.brightness = 100;
    v.poll_ms = POLL_MS;
    settings::writeNowForTest(v, SETTINGS_TEST_OFFSET, SETTINGS_TEST_MODE);
    printf("st: BOOT-TIME write survived\n"); stdio_flush();
    // Boot continues without the net (the runtime write path arms its own).
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
  }
#endif
  for (int i = 0; i < kServerCount; i++)
    printf("  [%d] %s:%u\n", i, kServers[i].host, (unsigned)kServers[i].port);

  // WiFi first: the join must be issued before the display scanout starts on
  // core 1 (Pimoroni's own Presto module insists on this ordering). It's
  // non-blocking; link status is polled from the main loop.
  net::begin(WIFI_SSID, WIFI_PASS, TZ_POSIX);
  printf("[boot] net\n"); stdio_flush();

  if (!panel::begin()) {
    printf("FATAL panel begin failed\n");
    return 1;
  }
  screen::begin();
  printf("[boot] panel\n"); stdio_flush();

  // Restore persisted settings (M3). Fields are clamped independently: a
  // record from a firmware with different option lists must not wedge the
  // poll navigation (it searches those lists).
  settings::Values saved = {};
  const bool have_saved = settings::load(saved);
  bool poll_ok = false, bright_ok = false;
  for (int i = 0; i < pages::POLL_OPTION_COUNT; i++)
    poll_ok |= saved.poll_ms == pages::POLL_OPTIONS_MS[i];
  for (int i = 0; i < pages::BRIGHTNESS_OPTION_COUNT; i++)
    bright_ok |= saved.brightness == pages::BRIGHTNESS_OPTIONS_PCT[i];
  if (have_saved) {
    if (saved.theme < theme::count()) theme_index = saved.theme;
    if (bright_ok) brightness_pct = saved.brightness;
    page = saved.page == 1 ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
    server = saved.server % kServerCount;
    printf("settings: restored theme=%s page=%d server=%d bright=%u%% poll=%u ms\n",
           theme::get(theme_index).name, (int)page, server, brightness_pct,
           (unsigned)saved.poll_ms);
    if (!poll_ok) printf("settings: poll %u ms not an option any more, keeping %u ms\n",
                         (unsigned)saved.poll_ms, (unsigned)POLL_MS);
  } else {
    printf("settings: none in flash, defaults\n");
  }

  touch::Info tinfo;
  touch_ok = touch::begin(&tinfo);
  if (touch_ok)
    printf("touch ok addr=0x%02x product=%s max=%ux%u\n", tinfo.addr, tinfo.product,
           tinfo.max_x, tinfo.max_y);
  else
    printf("touch not found\n");
  printf("[boot] touch\n"); stdio_flush();

  poller::begin(kServers, kServerCount, POLL_MS, CONNECT_MS, READ_MS);
  if (have_saved && poll_ok) poller::setPollMs(saved.poll_ms);
  poller::setPreferred(server);
  printf("[boot] poller\n"); stdio_flush();

  redraw();
  printf("[boot] first redraw\n"); stdio_flush();
  backlight::begin();
  backlight::set(pages::brightnessDuty(brightness_pct));
  printf("[boot] loop start\n"); stdio_flush();

  while (true) {
    static uint32_t last_touch = 0, last_tick = 0, seen_gen = 0;
    const uint32_t now = ms();

    poller::task();  // network runs even with the screen off
    net::service();  // rejoin maintenance: redraws pause during sleep, rejoining must not
    settings::service(!poller::busy());  // debounced flash write when idle
    watchdog_update();  // settings writes arm it; a hang anywhere reboots the board
#if SETTINGS_SELFTEST
    static bool selftest_done = false;
    if (!selftest_done && now >= 8000) {
      selftest_done = true;
      printf("[st] self-test write\n"); stdio_flush();
      persist();
    }
#endif
#if SETTINGS_BOOT_WRITE_TEST
    static uint32_t next_regs = 6000;
    if (now >= next_regs) {  // diagnostic build: print continuously
      next_regs += 5000;
      settings::printFlashRegs();
    }
#endif
    if (touch_ok && now - last_touch >= TOUCH_POLL_MS) {
      last_touch = now;
      pollTouch(now);
    }
    if (asleep) {  // nothing visible: skip auto-rotate and redraws
      sleep_ms(1);
      continue;
    }
    if (ROTATE_S > 0 && kServerCount > 1 && now - last_rotate >= ROTATE_S * 1000u &&
        now - last_touch_ms >= ROTATE_PAUSE_MS) {
      last_rotate = now;
      showServer(server + 1, false);  // screensaver: don't persist each pass
    }
    const uint32_t gen = poller::generation(poller::active());
    if (page == pages::Page::SETTINGS) {
      // static screen: only redraws on touch
    } else if (gen != seen_gen || now - last_tick >= 1000) {
      seen_gen = gen;
      last_tick = now;
      redraw();
    }
    sleep_ms(1);
  }
}

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "hw/settings.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hw/panel.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/time.h"

namespace settings {

namespace {

// The record sits in the LAST flash sector. The firmware (~1 MB including the
// CYW43 blob) and whatever the SDK places behind it can never reach 16 MB,
// and reflashing the app doesn't erase it: a UF2 only writes its own range.
// One sector erase per save; settings change by hand, so wear is a non-issue
// (100k cycles is decades of taps).
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

constexpr uint32_t MAGIC = 0x53594d44u;  // "SYMD"
constexpr uint8_t VERSION = 1;

// Everything past the header is the saved settings; crc covers those bytes.
struct Record {
  uint32_t magic;
  uint8_t version;
  uint8_t theme;
  uint8_t page;
  uint8_t server;
  uint8_t brightness;
  uint32_t poll_ms;
  uint32_t crc;  // over all bytes before this field
};

static_assert(sizeof(Record) <= 32, "record must fit the short page program");
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "record must fit one flash page");

constexpr uint32_t DEBOUNCE_MS = 2000;

Values g_pending;
bool g_dirty = false;
uint32_t g_dirty_at = 0;  // when the debounced write fires

const Record* flashRecord() {
  return reinterpret_cast<const Record*>(XIP_BASE + SETTINGS_FLASH_OFFSET);
}

uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & -(int32_t)(crc & 1));
  }
  return ~crc;
}

bool valid(const Record& r) {
  return r.magic == MAGIC && r.version == VERSION &&
         r.crc == crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, crc));
}

}  // namespace

bool load(Values& out) {
  const Record r = *flashRecord();
  if (!valid(r)) return false;
  out.theme = r.theme;
  out.page = r.page;
  out.server = r.server;
  out.brightness = r.brightness;
  out.poll_ms = r.poll_ms;
  return true;
}

void store(const Values& v) {
  g_pending = v;
  if (!g_dirty) g_dirty_at = to_ms_since_boot(get_absolute_time()) + DEBOUNCE_MS;
  g_dirty = true;
}

namespace {

Record makeRecord(const Values& v) {
  Record r = {};
  r.magic = MAGIC;
  r.version = VERSION;
  r.theme = v.theme;
  r.page = v.page;
  r.server = v.server;
  r.brightness = v.brightness;
  r.poll_ms = v.poll_ms;
  r.crc = crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, crc));
  return r;
}

// (The old flash_do_cmd-based page program was removed: it hung because the
// SDK's XIP re-enable runs while the chip is still programming — the direct
// transaction below waits for WIP first.)

}  // namespace

void printFlashRegs() {
  const uint32_t ints = save_and_disable_interrupts();
  uint8_t id_tx[1] = {0x9F};
  uint8_t id_rx[4];
  flash_do_cmd(id_tx, id_rx, sizeof id_rx);
  uint8_t s1_tx[2] = {0x05, 0x00};
  uint8_t s1_rx[2];
  flash_do_cmd(s1_tx, s1_rx, sizeof s1_tx);
  uint8_t s3_tx[2] = {0x15, 0x00};
  uint8_t s3_rx[2];
  flash_do_cmd(s3_tx, s3_rx, sizeof s3_tx);
  restore_interrupts(ints);
  printf("st: JEDEC id = %02x %02x %02x | SR1=0x%02x SR3=0x%02x\n", id_rx[1], id_rx[2],
         id_rx[3], s1_rx[1], s3_rx[1]);
  printf("st: devinfo cs0=%d cs1=%d cs1_gpio=%u | m1.timing=%08lx rfmt=%08lx wcmd=%08lx\n",
         (int)flash_devinfo_get_cs_size(0), (int)flash_devinfo_get_cs_size(1),
         flash_devinfo_get_cs_gpio(1),
         (unsigned long)qmi_hw->m[1].timing, (unsigned long)qmi_hw->m[1].rfmt,
         (unsigned long)qmi_hw->m[1].wcmd);
  stdio_flush();
}

// Program up to 256 bytes of a page. Runs ENTIRELY from RAM: the SDK's
// flash_do_cmd works for the transaction itself, but on this board its
// return into flash-resident caller code happens while the chip is still
// programming (WIP) — that first XIP fetch during a program/erase-busy
// window is what wedged the original write path (the ROM's erase path waits
// for completion internally, which is why erase alone always worked). Here
// every call returns RAM-to-RAM and we poll WIP to clear before the first
// fetch from flash. See the README "Settings persistence" section.
void __no_inline_not_in_flash_func(programPageSafe)(uint32_t off, const uint8_t* data,
                                                    uint32_t count) {
  uint8_t wren_tx[1] = {0x06};  // Write Enable
  uint8_t wren_rx[1];
  flash_do_cmd(wren_tx, wren_rx, sizeof wren_tx);

  uint8_t tx[4 + FLASH_PAGE_SIZE];
  tx[0] = 0x02;  // Page Program, 3-byte address
  tx[1] = (off >> 16) & 0xFF;
  tx[2] = (off >> 8) & 0xFF;
  tx[3] = off & 0xFF;
  memcpy(&tx[4], data, count);
  uint8_t rx[4 + FLASH_PAGE_SIZE];
  flash_do_cmd(tx, rx, 4u + count);

  // Wait for the write to complete (Read Status 0x05, WIP = bit 0) — bounded,
  // and the watchdog net in the caller catches a wedged chip regardless.
  uint8_t st_tx[2] = {0x05, 0x00};
  uint8_t st_rx[2] = {0, 0xFF};
  for (uint32_t i = 0; i < 100000; i++) {
    flash_do_cmd(st_tx, st_rx, sizeof st_tx);
    if (!(st_rx[1] & 0x01)) return;
  }
}

void writeNowForTest(const Values& v, uint32_t offset, int mode) {
  const Record r = makeRecord(v);
  // The bisection harness, kept for regression checks: mode 0 = erase +
  // program, 1 = erase only, 2 = program only, 3 = erase + one status read,
  // 4 = register reads only. All programs go through the working direct
  // transaction. Step breadcrumbs live in watchdog scratch[4] and survive a
  // wedge; no stdio inside the disabled-interrupt window (the USB IRQ is
  // off, so a flush there would block forever).
  const uint32_t ints = save_and_disable_interrupts();
  watchdog_hw->scratch[4] = 1;  // interrupts off
  if (mode == 4) {
    // Register reads only (identity + status): the proven-safe transaction.
    printFlashRegs();
    restore_interrupts(ints);
    return;
  }
  if (mode != 2) {
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    watchdog_hw->scratch[4] = 3;  // erase survived
  }
  if (mode != 1 && mode != 3) {
    programPageSafe(offset, reinterpret_cast<const uint8_t*>(&r), 32);
    watchdog_hw->scratch[4] = 4;  // program survived
  }
  watchdog_hw->scratch[4] = 5;  // restored, op complete
  restore_interrupts(ints);
  restore_interrupts(ints);
  printf("st: BOOT-TIME write done (off=0x%lx mode=%d)\n", (unsigned long)offset, mode);
  stdio_flush();
}

void service(bool net_idle) {
  if (!g_dirty) return;
  if (!net_idle) return;  // never stall the chip mid-request
  const uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((int32_t)(now - g_dirty_at) < 0) return;  // still debouncing

  // Park the scanout core first; if it doesn't ack, keep the write pending
  // (stays dirty; retried on a later service() pass).
  printf("st: pausing\n"); stdio_flush();
  if (!panel::pauseScanout()) {
    printf("st: scanout didn't pause, deferring write\n");
    stdio_flush();
    return;
  }
  printf("st: paused\n"); stdio_flush();

  // Committed from here: a failed write simply reverts to the old record on
  // next boot (CRC mismatch → defaults).
  g_dirty = false;

  // Watchdog net: the write stalls XIP and interrupts; if anything in the
  // erase/program path wedges, the board reboots instead of bricking. 2 s is
  // comfortably above the W25Q128 worst-case sector erase (400 ms). The main
  // loop keeps it fed the rest of the time.
  watchdog_enable(2000, 1);
  const Record r = makeRecord(g_pending);
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
  programPageSafe(SETTINGS_FLASH_OFFSET, reinterpret_cast<const uint8_t*>(&r), 32);
  restore_interrupts(ints);
  panel::resumeScanout();
  // Direct evidence of what landed: a readback of the record through XIP
  // (flash_do_cmd flushed the cache on its way out).
  const Record* chk = reinterpret_cast<const Record*>(XIP_BASE + SETTINGS_FLASH_OFFSET);
  printf("st: wrote | flash magic %08lx (want %08lx)\n", (unsigned long)chk->magic,
         (unsigned long)MAGIC);
  stdio_flush();

  printf("settings: saved (flash stalled %u ms)\n",
         (unsigned)(to_ms_since_boot(get_absolute_time()) - now));
  stdio_flush();
}

}  // namespace settings

# system-d-monitor — Pimoroni Presto port

A port of [dicksondickson/system-d-monitor](https://github.com/dicksondickson/system-d-monitor)
(a desk dashboard for vLLM / TensorFold LLM inference servers) from the
Guition ESP32-S3-4848S040 to the Pimoroni Presto (RP2350), in C++ on the
Pico SDK.

Upstream is MIT © Dickson Chow; this port keeps that licence and reuses
upstream's `src/gfx`, `src/ui`, `src/metrics`, host tests, Terminus font and
btop themes. The ESP32-specific `src/hw` and `src/net` layers are replaced
with Presto equivalents.

## Provenance

- **Upstream project**: [dicksondickson/system-d-monitor](https://github.com/dicksondickson/system-d-monitor)
  (MIT) — the original SYSTEM-D Monitor firmware for the Guition
  ESP32-S3-4848S040. This repository is a port of it; the metrics parsing,
  UI, themes and test suite here are upstream's, and upstream's `SPEC.md`
  documents the protocol and derivation rules this port follows.
- **Board reference**: upstream's
  [`docs/board-guide.md`](https://github.com/dicksondickson/system-d-monitor/blob/main/docs/board-guide.md)
  is the complete hardware reference for the original board (GPIO map,
  ST7701 display init and timings, GT911 touch, toolchain paths, known
  gotchas). The original firmware's bring-up followed it, and this port's
  bring-up leaned on it before diverging to the Presto's own hardware
  (FT6236 touch, PIO scanout, CYW43 WiFi — mapped in the table below).
- **Fixtures**: the engine captures under `monitor/test/fixtures/` come from
  upstream (TensorFold 1.1.8, vLLM 0.33.0rc1, and the later TensorFold build
  of 2026-11-03), with the tests that exercise them.
- **Third-party**: Terminus font (OFL), btop themes (see
  `monitor/third_party/`).

## Status

| Milestone | Scope | State |
|---|---|---|
| M0 | Panel bring-up: 480×480 ST7701 scanout, backlight, pixel-format test | **done** |
| M1 | FT6236 touch + upstream demo UI + host tests | **done** |
| M2 | WiFi (CYW43/lwIP), HTTP polling, live monitoring, failover | **done** |
| M3 | Settings persistence, docs, failover soak | **done** |
| M4 | Upstream v0.1.1 parity (later TensorFold build), persistence wall fixed, poller resilience | **done** |

M1 verified on hardware: all gestures work (centre toggle, left/right server
switching with the ‹1/2› footer, ≡ settings with live themes/brightness,
hold 2.2 s to sleep), and upstream's host test suite passes against the
ported gfx/ui/metrics layers (36 tests, 0 failures). M4 verified on hardware
against production: live TG/PP on the later TensorFold build, settings
persisting across reboot and reflash.

## Hardware mapping (upstream → Presto)

| Upstream (ESP32-S3) | Presto (RP2350) |
|---|---|
| Arduino_GFX ST7701 RGB panel + bounce buffers | `pimoroni/presto` `drivers/st7701` PIO scanout, core 1 |
| GT911 touch @ 0x5D | **FT6236** touch @ 0x48, I2C1 SDA 30 / SCL 31 |
| LEDC backlight 1 kHz on GPIO38 | driver PWM on GPIO45 |
| ESP-NOW WiFi / lwIP | CYW43 via `pico_cyw43_arch` + lwIP (in PSRAM) |
| NVS settings | last flash sector + `multicore_lockout` (see M3 notes) |
| FreeRTOS poller tasks | cooperative poller on core 0 |

## Pixel encoding (M0 finding)

The Presto's 480×480 RGB565 framebuffer is scanned out by PIO with DMA
byte-swap enabled; the net effect is that **the framebuffer stores
byte-swapped RGB565** (verified with colour-bar test patterns 2026-11-03:
standard encoding renders red as blue / green as red and turns grey ramps
into green/magenta banding; byte-swapped encoding is correct). The port
swaps pixels at the band-blit (`monitor/src/hw/screen.cpp`), keeping
upstream's theme/raster code and host tests pristine.

The framebuffer lives in internal SRAM (450 KB of 512 KB), exactly as the
Presto's MicroPython firmware does; the 8 MB PSRAM holds lwIP pools and
buffers.

## Network (M2 findings)

- **Join order**: the WiFi join must be issued before the display scanout
  starts on core 1 (Pimoroni's own Presto module insists on this); the boot
  sequence in `main.cpp` does net → panel → touch → poller.
- **Country code**: `CYW43_COUNTRY_UK` (set in `net/wifi.cpp`). The SDK's
  WORLDWIDE default stalls association on EU channels 12/13.
- **Arch**: `pico_cyw43_arch_lwip_poll`, driven by `cyw43_arch_poll()` from
  the main loop (`net::service()`). The threadsafe-background variant proved
  unreliable alongside the display scanout (joins stalled at LINK_JOIN).
- **lwIP timeouts**: `MEMP_NUM_SYS_TIMEOUT 8` in `net/lwipopts.h`. The
  default of 3 is exhausted by DHCP×2 + DNS + SNTP on this stack; the first
  HTTP attempt then asserts (`sys_timeout … pool MEMP_SYS_TIMEOUT is empty`)
  and panics with no visible output.
- **Configuration** lives in `monitor/include/config.h` (gitignored — copy
  `config.example.h`): WiFi credentials, POSIX TZ, server list, poll/connect/
  read timings, and an optional `WIFI_GATEWAY` (applied if the AP's DHCP
  offer carries no router option — off-subnet destinations then fail at the
  ARP layer with `ERR_RTE`).
- **Servers on another subnet**: if your engines are reached across subnets,
  put numeric IPs in `config.h` (lwIP's resolver has no search-domain logic
  and DNS proved flaky here — DNS resolution is kept off the critical path;
  the on-screen labels still show the configured names) and raise
  `CONNECT_MS` — 2000 (upstream 800) covered a slower cross-subnet path.
- **Engine compatibility**: `parseHealth` (`metrics/server_info.cpp`) accepts
  both upstream TensorFold 1.1.8 (`"status": "ok"`, `model`, `version`) and
  the deployed newer schema (`"ok": true`, `streams.max`, `context_length`,
  no version string); `backend: "tensorfold"` in the latter confirms the
  engine's identity. Upstream commit `83683b6` (2026-11-03, ported here)
  handles the same build's `/metrics` dialect: it dropped
  `generation_tokens_running` and `request_prefill_time_seconds_*` and
  mirrors `/health` into `/metrics` as `tensorfold_health:*`. The parser
  reads `tensorfold_health:completion_tokens_total` for live TG (running
  replies included), and `prefill_seconds_total` / `cached_tokens_total`
  stand in for the dropped histogram and cache counter. PP counts only
  *computed* prompt tokens (vLLM's `request_prefill_kv_computed_tokens_sum`,
  or `prompt − cached` on TensorFold), since cache hits take no prefill
  time — the 50,953-prompt / 50,112-cached / 0.69 s request in upstream's
  production capture reads 1,217 tok/s, not 73,749. Fixture
  `test/fixtures/tensorfold-2026-11-03-idle/`; tests `test_tf_health_build_*`.
- **Engine counter caveat** (resolved for the deployed build): 1.1.8-style
  `/metrics` tick `generation_tokens_total` once per *completed* response,
  so TG shows one spike per reply instead of a live rate; the newer build's
  `tensorfold_health:completion_tokens_total` moves live and is used instead.
  A build exporting only finished-request counters without the health mirror
  would still need an engine-side counter fix (preferred) or an app-side
  estimate. Per-stream data likewise depends on the build: 1.1.8 exported
  `kv_cache_usage_perc` only as an aggregate; the 2026-11-03 build exports
  it per stream again.

### Failover semantics (deviation from upstream)

Upstream ran one poller task per server, all servers polled always. The port
polls **one server at a time** (single-flight HTTP engine, so a stall on one
host can't starve the other): the *preferred* server (tap left/right to set;
default 8000) is polled while it answers; when it fails the other takes over
and is retried until it fails too; when the preferred answers again the
poller switches back automatically. Tapping left/right sets the preferred
server **and** the one displayed: the header shows your selection with its
own state (a down selection shows its `connection refused · retry Ns` page
while the poller keeps the healthy server live).

Three resilience guards keep the single-flight engine from wedging silently
(all observed live before they were fixed):

- **Info-phase give-up**: a server that's down at the TCP level used to cycle
  `/health`→`/version` forever; those failures never counted, so a failing
  *preferred* server was starved of retries. After 3 consecutive failed
  info/models fetches the slot falls through to `/metrics` (whose failure
  the deriver counts) and retries info on the next 30 s cycle.
- **HTTP engine recovery**: `http::start()` only fails when the engine isn't
  idle; a streak of 5 failures with nothing in flight force-resets the engine
  and logs it.
- **DHCP before polling**: the poller waits for a bound lease
  (`net::configured()`), so boot-time connects against a half-ready link
  can't mark servers failed. A `[hb]` heartbeat every 30 s logs net/HTTP/slot
  state, which is how any future stall becomes diagnosable from the serial
  log alone. Join maintenance runs from the main loop even while the screen
  is asleep (redraws pause; rejoining must not), and the boot-time
  `wifi: ip/gw/nm` line snapshots each address into its own buffer —
  `ipaddr_ntoa()` returns one static buffer, so all three used to print the
  netmask.

## Settings persistence (M3 — implemented and enabled)

Upstream used ESP32 NVS (`Preferences`). The port keeps one checksummed
record (`magic`/`version`/CRC-32) in the **last flash sector**
(`PICO_FLASH_SIZE_BYTES - 4096`; the ~1 MB firmware can never reach it, and
reflashing a UF2 doesn't touch it), validated against the option lists on
load, written with the core-1 scanout parked via a RAM-resident pause
handshake (`hw/panel.cpp` `pauseScanout`), debounced 2 s, deferred until no
HTTP request is in flight, with a 2 s watchdog net so a wedged write reboots
the board instead of bricking it. **Enabled** (`SETTINGS_PERSIST 1`).

**The platform wall, found and fixed:** on this board the SDK's flash
*page-program* wedged the whole chip — reproducibly at boot, before core 1 /
WiFi / the scanout exist. The original bisection matrix:

| op | result |
|---|---|
| `flash_range_erase` (ROM) | **works**, any offset |
| `flash_range_program` (ROM) | hangs, any offset |
| `flash_do_cmd` Read Status (2 B) | works |
| raw `0x02` page program via `flash_do_cmd` | hangs |

Chip and platform verified normal (Winbond W25Q128JV, JEDEC `ef 40 18`,
3-byte addressing, no protection bits, factory OTP `FLASH_DEVINFO`, stock
`boot2_w25q080`, pico-sdk 2.3.1 with the
[#1982/#2082](https://github.com/raspberrypi/pico-sdk/issues/1983) QMI-window
fix). Hypotheses tested and **refuted on hardware** (watchdog-scratch
breadcrumbs per boot step): PSRAM bus contention (parking CS1 GPIO 47 high
through the whole op changed nothing) and stripped QSPI pad config
(`pads_qspi` registers are identical across the ROM's `exit_xip`).

The actual cause: the transaction was never the problem — **the return was**.
`flash_do_cmd(0x02)` completes and re-enables XIP, then returns into
*flash-resident caller code* while the chip is still programming (WIP); that
first XIP fetch during the busy window stalls the bus. The ROM's erase path
waits for completion internally, which is why erase always worked and made
the write look chip-specific. The fix (`hw/settings.cpp`
`programPageSafe`): the program wrapper is RAM-resident
(`__no_inline_not_in_flash_func`), every call returns RAM-to-RAM, and it
polls WIP clear via `flash_do_cmd(0x05)` before the first fetch from flash.
Verified end-to-end on the board: boot-context write, runtime writes
(scanout parked, 58-76 ms stall), and persistence across reboot *and*
reflash.

To pick this up later: nothing — it works. If a future SDK changes
`flash_do_cmd`'s tail, the same RAM-resident + WIP-wait shape applies.
Upstream quirk already fixed in the port: upstream saved the raw page value,
so visiting settings then power-cycling booted into GRAPHS; the port
persists the dashboard page *under* the menu.

## Layout

```
deps/        pinned dependency clones (not in git; see pins below)
monitor/     the CMake firmware project
```

### Pinned dependencies

| Repo | Branch | Commit |
|---|---|---|
| raspberrypi/pico-sdk | master | `079c6f3` |
| pimoroni/pimoroni-pico | feature/picovector2-and-layers | `4ae7372` |
| pimoroni/presto | main | `b96a8cb` |

(`pico-sdk` submodules in use: `lib/tinyusb`, `lib/lwip`.)

## Build

```sh
cd monitor
cmake -B build -DPICO_SDK_PATH=$PWD/../deps/pico-sdk \
      -DPIMORONI_PICO_PATH=$PWD/../deps/pimoroni-pico \
      -DPIMORONI_PRESTO_PATH=$PWD/../deps/presto
cmake --build build -j
# → build/monitor.uf2
```

Toolchain: `cmake`, `gcc-arm-none-eabi`, `build-essential`, `libusb-1.0-0-dev`
(apt). `mpremote` for remote BOOTSEL (`pip install --user mpremote`).

## Flash

From MicroPython: remote BOOTSEL without button-pressing
(`mpremote connect /dev/ttyACM0 exec "import machine; machine.bootloader()"`),
then copy the UF2 to the mounted `RP2350` drive (`sudo cp ... && sudo sync`).

From this firmware (SDK binaries expose picotool's boot interface — no
button-pressing needed): `picotool load -f -x monitor/build/monitor.uf2`.
The picotool fetched into `monitor/build/_deps` is built **without USB
support** (it can only inspect files); a USB-capable build lives one command
away and is needed for `load` to reach the device:

```sh
cmake -S monitor/build/_deps/picotool-src -B deps/picotool \
      -DPICO_SDK_PATH=$PWD/deps/pico-sdk && cmake --build deps/picotool -j
sudo deps/picotool/picotool load -f -x monitor/build/monitor.uf2
```

(sudo: the picoboot USB node is root-owned; the CDC serial node is world-
readable, so the serial log needs no privileges. If a firmware is stuck in a
reboot loop, `load -f` can lose the race with the watchdog — hold **BOOT**,
tap **RESET**, and load without `-f`.)

Otherwise, hold **BOOT**, tap **RESET**, and copy the UF2 to the drive that
appears. A firmware UF2 does not touch the MicroPython filesystem — the
portfolio survives and can be restored by re-flashing
`presto-v2.0.0-micropython.uf2`.

To restore the MicroPython portfolio: flash
`presto-v2.0.0-micropython.uf2` (kept in `~/Projects/presto-project/`).

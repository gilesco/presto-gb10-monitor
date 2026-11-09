# Changelog

- M5.1 - 2026-11-09 - /metrics parse bug fixed: the HTTP engine tokenized the header buffer in place, and once
  TensorFold writes headers and body start in one TCP segment that pass rewrote the `\r` of the first body lines
  to NUL — 185 value lines / 19 recognised became 178 / 17, and PP (`prompt_tokens_total`) read no data on this
  engine. The split now happens on a copy (`net/http_split.{h,cpp}`, host-tested); verified live: the board
  parses all fields and PP latches at the request-finish boundary.
- M5 - 2026-11-09 - Header clock fixed (the TZ evaluator ignored each DST rule's
  month — the board ran BST two weeks past the fall-back and showed 16:04 at
  15:04) and the date is now unambiguous UK style `09-Nov-26` (`fmt::date`).
  Ported from upstream v0.1.2–v0.1.6: fixed state colours in every theme
  (green OK, orange in-progress, red down) including the Settings preview and
  the Wi-Fi bars; DHCP `HOSTNAME`; `/health` reads TensorFold's larger body
  without truncating; down servers retry 2 s, 4 s, then every 9 s; silent
  release firmware (`SERIAL_LOG`, `LOGF()` compiles out); warning-free builds
  in logged and silent modes.
- M4 - 2026-11-06 - Upstream v0.1.1 parity (later TensorFold build: `tensorfold_health:*`,
  live TG, computed-tokens PP, `backend` identity — verified on hardware against
  production); settings persistence enabled (the flash page-program wedge was
  the SDK's XIP re-enable running while the chip was still programming — the
  program now runs RAM-resident and waits for WIP); poller resilience (info-phase
  give-up so a down failover slot can't starve the preferred server, HTTP engine
  forced reset, DHCP-before-polling gate, 30 s heartbeat); truthful `wifi: ip/gw/nm`
  log (`ipaddr_ntoa` shares one static buffer).
- M3 - 2026-11-03 - Settings persistence written + gated off at a platform wall; docs
- M2 - 2026-11-03 - WiFi + live monitoring
- M1 - 2026-11-03 - Touch + upstream demo UI on the Presto
- M0 - 2026-11-03 - Panel bring-up on the Presto

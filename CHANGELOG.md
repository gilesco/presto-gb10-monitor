# Changelog

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

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Wi-Fi + SNTP on the Presto's CYW43 (Pico W-class radio), replacing the
// ESP32 WiFi.h implementation. Station mode with retry; the link state
// machine matches upstream's semantics (JOINING / CANT_JOIN / LOST). The
// header clock comes from SNTP (started once the link is up) evaluated
// through a POSIX TZ string (net/posix_tz.cpp).

#include "net/wifi.h"

#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "lwip/apps/sntp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "net/posix_tz.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/time.h"

namespace net {

namespace {

bool g_enabled = false;
bool g_ever_up = false;
bool g_sntp_started = false;
uint32_t g_begin_ms = 0;
uint32_t g_retry_at_ms = 0;
char g_ssid[33] = "";
char g_pass[65] = "";

// SNTP epoch, and the boot-time it was set at (for interpolation).
volatile uint32_t g_epoch_sec = 0;
volatile uint32_t g_epoch_ms = 0;

posix_tz::Rules g_tz;

uint32_t ms() { return to_ms_since_boot(get_absolute_time()); }

constexpr uint32_t RETRY_MS = 10000;  // re-issue the join while down

// Applies WIFI_GATEWAY once, if DHCP bound an address but no usable router.
static void checkGateway() {
  static bool checked = false;
  if (checked || !g_enabled) return;
  struct netif* n = netif_default;
  // dhcp_supplied_address: only trust the fields once DHCP has actually
  // bound — mid-negotiation the netif fields hold transient values.
  if (!n || !netif_is_up(n) || !dhcp_supplied_address(n)) return;
  checked = true;
  const ip4_addr_t* gw = netif_ip4_gw(n);
  // Bogus router offers happen (seen on this AP: gw == netmask ==
  // 255.255.255.0): treat them like no gateway at all so the configured
  // fallback applies — off-subnet destinations would otherwise fail at ARP.
  const bool bogus = ip4_addr_isany_val(*gw) || ip4_addr_eq(gw, netif_ip4_netmask(n)) ||
                     ip4_addr_isbroadcast(gw, n);
  if (bogus) {
    ip4_addr_t fallback;
    if (ipaddr_aton(WIFI_GATEWAY, &fallback)) {
      netif_set_gw(n, &fallback);
      printf("wifi: DHCP gateway %s unusable, set %s\n", ipaddr_ntoa(gw), WIFI_GATEWAY);
    } else {
      printf("wifi: DHCP gateway %s unusable, WIFI_GATEWAY unparseable\n", ipaddr_ntoa(gw));
    }
  } else {
    // ipaddr_ntoa() returns ONE static buffer: snapshot each address before
    // printing, or all three %s show the last-converted value.
    char ip_s[16], gw_s[16], nm_s[16];
    snprintf(ip_s, sizeof ip_s, "%s", ip4addr_ntoa(netif_ip4_addr(n)));
    snprintf(gw_s, sizeof gw_s, "%s", ip4addr_ntoa(gw));
    snprintf(nm_s, sizeof nm_s, "%s", ip4addr_ntoa(netif_ip4_netmask(n)));
    printf("wifi: ip %s gw %s nm %s\n", ip_s, gw_s, nm_s);
  }
  stdio_flush();
}

// M2 diagnostics: set to 1 to trace every firmware event (auth/link/psk_sup
// + reasons) and every received Ethernet frame. Off for normal running.
#define WIFI_EVENT_TRACE 0

void maintainJoin() {
  if (!g_enabled) return;
  const uint32_t now = ms();
  const int status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
  static int last_status = -99;
  if (status != last_status) {  // transition log: idle → join → up (or failure)
    printf("wifi: link status %d at %u ms\n", status, now);
    last_status = status;
  }
  static uint32_t last_diag = 0;  // M2 diagnostics: join state machine + host-wake line
#if WIFI_EVENT_TRACE
  if (now - last_diag >= 3000) {
    last_diag = now;
    const ip4_addr_t* a = netif_default ? netif_ip4_addr(netif_default) : nullptr;
    printf("wifi: diag join_state=0x%04x host_wake=%d tcpip_status=%d ip=%s dhcp=%d\n",
           cyw43_state.wifi_join_state, (int)gpio_get(CYW43_PIN_WL_HOST_WAKE),
           cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA),
           a ? ip4addr_ntoa(a) : "(no netif)",
           (int)dhcp_supplied_address(netif_default));
    stdio_flush();
  }
#else
  (void)last_diag;
#endif
  if (up()) {
    g_retry_at_ms = 0;
    return;
  }
  if ((int32_t)(now - g_retry_at_ms) < 0) return;
  g_retry_at_ms = now + RETRY_MS;
  // JOIN/NOIP: a join attempt is still in flight; retry only on terminal states.
  if (status == CYW43_LINK_JOIN || status == CYW43_LINK_NOIP) return;
  printf("wifi: (re)joining \"%s\" (link status %d)\n", g_ssid, status);
  cyw43_arch_wifi_connect_async(g_ssid, g_pass[0] ? g_pass : nullptr, CYW43_AUTH_WPA2_AES_PSK);
}

void startSntp() {
  if (g_sntp_started) return;
  g_sntp_started = true;
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, (char*)"pool.ntp.org");
  sntp_setservername(1, (char*)"time.cloudflare.com");
  sntp_init();
  printf("wifi: sntp started\n");
}

}  // namespace

// SNTP_SET_SYSTEM_TIME hook (lwipopts.h), C linkage, called from lwIP.
extern "C" void sysd_sntp_set_epoch(unsigned long sec) {
  g_epoch_sec = (uint32_t)sec;
  g_epoch_ms = to_ms_since_boot(get_absolute_time());
}

void begin(const char* ssid, const char* pass, const char* tz) {
  if (!ssid || !ssid[0]) {
    printf("wifi: no SSID configured (include/config.h), staying offline\n");
    return;
  }
  snprintf(g_ssid, sizeof g_ssid, "%s", ssid);
  snprintf(g_pass, sizeof g_pass, "%s", pass ? pass : "");
  if (!posix_tz::parse(tz, g_tz)) printf("wifi: could not parse TZ \"%s\"; clock stays UTC-less\n", tz);

  // UK country code: the WORLDWIDE default leaves channels 12-13 passive-only,
  // and association then stalls forever on routers parked there.
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_UK)) {
    printf("wifi: cyw43_arch_init failed\n");
    return;
  }
  cyw43_arch_enable_sta_mode();
#if WIFI_EVENT_TRACE
  // Dump every firmware event: invaluable for bring-up, far too chatty to run.
  cyw43_state.trace_flags |= CYW43_TRACE_ASYNC_EV;
#endif
  g_enabled = true;
  g_begin_ms = ms();
  // Issue the join immediately: MicroPython's Presto module brings Wi-Fi up
  // *before* the display scanout starts ("WiFi - must happen before Presto
  // bringup"), and joining after the panel is live stalls at LINK_JOIN.
  g_retry_at_ms = ms() + RETRY_MS;  // service() takes over retries from here
  cyw43_arch_wifi_connect_async(g_ssid, g_pass[0] ? g_pass : nullptr, CYW43_AUTH_WPA2_AES_PSK);
  printf("wifi: connecting to \"%s\"\n", ssid);
}

bool up() {
  // The poll arch does all Wi-Fi/lwIP processing here, synchronously: no
  // IRQ worker. Called from the main loop (~1 kHz) and on every redraw.
  if (g_enabled) cyw43_arch_poll();
  // NOTE: cyw43_wifi_link_status() never reports UP for the STA interface —
  // it only maps the join state machine, which resets to ACTIVE on success.
  // The authoritative signal (netif up + link up + IP acquired) is here:
  return g_enabled && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

bool configured() {
  return up() && netif_default && dhcp_supplied_address(netif_default);
}

void service() { maintainJoin(); }

WifiLink link() {
  if (!g_enabled) return WifiLink::CANT_JOIN;
  maintainJoin();
  checkGateway();
  if (up()) {
    g_ever_up = true;
    startSntp();
    return WifiLink::UP;
  }
  if (g_ever_up) return WifiLink::LOST;
  return ms() - g_begin_ms < JOIN_GRACE_MS ? WifiLink::JOINING : WifiLink::CANT_JOIN;
}

void info(char* ip, size_t ip_len, int8_t* bars) {
  if (!up()) {
    ip[0] = '\0';
    *bars = -1;
    return;
  }
  const ip4_addr_t* a = netif_ip4_addr(netif_default);
  snprintf(ip, ip_len, "%s", a ? ip4addr_ntoa(a) : "");
  int32_t rssi = 0;
  cyw43_wifi_get_rssi(&cyw43_state, &rssi);
  *bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}

int rssi() {
  int32_t r = 0;
  if (up()) cyw43_wifi_get_rssi(&cyw43_state, &r);
  return (int)r;
}

void clock(char* out, size_t len) {
  out[0] = '\0';
  if (!g_enabled || !g_epoch_sec) return;
  const uint64_t now = ms();
  uint64_t utc = g_epoch_sec;
  if (now > g_epoch_ms) utc += (now - g_epoch_ms) / 1000;
  // Reject nonsense (SNTP not set yet / pre-2024 board clock)
  if (utc < 1704067200ull || utc > 4102444800ull) return;
  struct tm t;
  if (!posix_tz::to_local(g_tz, utc, &t)) return;
  char date[12], time[16];
  snprintf(date, sizeof date, "%02d-%02d-%02d", t.tm_mon + 1, t.tm_mday, t.tm_year % 100);
  int hour12 = t.tm_hour % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(time, sizeof time, "%d:%02d:%02d %s", hour12, t.tm_min, t.tm_sec,
           t.tm_hour < 12 ? "AM" : "PM");
  snprintf(out, len, "%s %s", date, time);
}

}  // namespace net

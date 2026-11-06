// lwIP configuration for the SYSTEM-D Monitor Presto port.
// Based on raspberrypi/pico-examples' lwipopts_examples_common.h
// (BSD-3-Clause), extended with SNTP and a few more PCBs.
//
// This file must be reachable as "lwipopts.h" from every lwIP compilation
// unit (the SDK compiles lwIP sources as INTERFACE sources of the final
// executable, so a project include dir reaches them).
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_TCP_PCB            4
// DHCP uses 2 (coarse + fine), DNS and SNTP take 1 each; the default of 3
// exhausted the pool and asserted on the first HTTP attempt.
#define MEMP_NUM_SYS_TIMEOUT        8
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// SNTP for the header clock. The epoch lands in net (wifi.cpp) via
// SNTP_SET_SYSTEM_TIME; SNTP_SERVER_DNS lets us name pool.ntp.org etc.
#define SNTP_SERVER_DNS             1
#define SNTP_MAX_SERVERS            2
#ifdef __cplusplus
extern "C" {
#endif
void sysd_sntp_set_epoch(unsigned long sec);  // defined in net/wifi.cpp
#ifdef __cplusplus
}
#endif
#define SNTP_SET_SYSTEM_TIME(sec) sysd_sntp_set_epoch((unsigned long)(sec))

// lwIP assertions normally call panic(x), whose message dies in the USB CDC
// buffer when the firmware hangs (panic never flushes). Override: print the
// assertion, FLUSH it out, then park with a distinctive marker so the serial
// log tells us exactly which lwIP invariant broke.
#ifndef LWIP_PLATFORM_ASSERT
#include <stdio.h>
#include "pico/stdlib.h"
#define LWIP_PLATFORM_ASSERT(x)                                  \
    do {                                                         \
        printf("\n[LWIP ASSERT] %s\n", x ? (const char*)(x) : "(nil)"); \
        stdio_flush();                                           \
        printf("[LWIP ASSERT] halting\n");                       \
        stdio_flush();                                           \
        for (;;) { tight_loop_contents(); }                      \
    } while (0)
#endif

// Put lwIP's static pools (memp pools, RAM heap — ~37 KB) in PSRAM, like the
// Presto's MicroPython board config does. Without this they land in .bss and
// overflow the 512 KB SRAM alongside the 450 KB framebuffer. The macro is
// expanded later (in lwIP sources, after arch.h defines
// LWIP_MEM_ALIGN_BUFFER), so referencing it here is fine.
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    __attribute__((section(".psram_uninitialised.lwip"), aligned(4))) uint8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)]

#endif  // _LWIPOPTS_H

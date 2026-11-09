// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Golden text dumps of both pages rendered from demoView() (SPEC.md §7).
// After an intentional layout change, run `pio test -e native -v`, check the
// printed dump, and paste it here.
#pragma once

static const char* GOLDEN_OVERVIEW = R"GOLDEN(╭─┐SYSTEM-D┌────┐09-Nov-26 12:47:33 PM┌─┐⣀⣤⣶⣿┌─╮
│ gpu-box · 192.168.1.50               OK BUSY │
│ TensorFold · deepseek-v4.1-flash-1017        │
╰──────────────────────────────────────────────╯
╭─┐Throughput┌─────────────────────────────────╮
│ TG １０１．３ tok/s    █▆▄▄▅▆███▇ ▄▄▆███     │
│                live ▁▁▁██████████ ██████     │
│ PP last 3,354/s · prefilling 12s             │
│ run 2/4 · wait 1 · TG avg 95.1 · max 104.2   │
│ KV 31.4% ██░░░░ · peak 31.4% · 329K/1.0M tok │
╰──────────────────────────────────────────────╯
╭─┐Streams┌────────ctx─tokens───TTFT──────TG───╮
│  0  decoding   31.4%   329K   1.2s  62.7/s   │
│  1  prefill     2.0%    21K  no data         │
│  2  idle        0.0%      0                  │
│  3  idle        0.0%      0                  │
╰──────────────────────────────────────────────╯
╭─┐Averages┌───────────────────────────────────╮
│ TTFT 1.85s · MTP 48.9% · cache-hit 75.4%     │
╰─┘≡└─┘poll 2s · ok 0.4s ago└────┘tap: graphs└─╯
)GOLDEN";

static const char* GOLDEN_GRAPHS = R"GOLDEN(╭─┐SYSTEM-D┌────┐09-Nov-26 12:47:33 PM┌─┐⣀⣤⣶⣿┌─╮
│ gpu-box · 192.168.1.50               OK BUSY │
│ TensorFold · deepseek-v4.1-flash-1017        │
╰──────────────────────────────────────────────╯
╭─┐TG live tok/s┌───────────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⢸⣤⡀⠀⠀⠀⠀⠀⠀⣠⣾⣾⣿⣧⣄⠀⠀⠀⠀⠀⠀⢀⣴⣷⣿⣿ now  101.3    │
│ ⠀⠀⠀⠀⢸⣿⣿⣧⣄⣀⣀⣴⣿⣿⣿⣿⣿⣿⣿⠀⠀⣀⣀⣠⣾⣿⣿⣿⣿⣿ avg   73.7    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿ max  102.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿ min    0.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
╰──────────────────────────────────────────────╯
╭─┐PP per request tok/s┌────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀ last 3,354    │
│ ⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀ avg  4,385    │
╰──────────────────────────────────────────────╯
╭─┐KV %┌────────────────────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀ now  31.4%    │
│ ⠀⠀⠀⠀⢀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣤⠀⠀⣤⣤⣤⣤⣤⣤⣤⣤⣴ max  31.4%    │
╰─┘≡└─┘poll 2s · ok 0.4s ago└──┘tap: overview└─╯
)GOLDEN";

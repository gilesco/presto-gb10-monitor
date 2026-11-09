// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

#include <stdio.h>

namespace pages {

using namespace widgets;
using theme::Role;

namespace {

struct StateLabel {
  const char* text;
  uint8_t role;
};

StateLabel stateLabel(ServerState s) {
  // Fixed colours in every theme (upstream v0.1.4): OK green, in progress /
  // waiting orange, no data getting through red.
  switch (s) {
    case ServerState::IDLE: return {"OK IDLE", theme::STATE_OK};
    case ServerState::BUSY: return {"OK BUSY", theme::STATE_OK};
    case ServerState::WARMING: return {"OK WARM", theme::STATE_OK};
    case ServerState::RESTART: return {"OK RESTART", theme::STATE_WAIT};  // in progress
    case ServerState::STALE: return {"STALE", theme::STATE_WAIT};
    case ServerState::DOWN: return {"DOWN", theme::STATE_BAD};
    case ServerState::NO_WIFI: return {"NO WIFI", theme::STATE_BAD};
    case ServerState::CONNECTING: return {"CONNECTING", theme::STATE_WAIT};
    default: return {"UNKNOWN", theme::STATE_BAD};
  }
}

// Copies at most `cols` codepoints of src into dst, ending with "…" if cut.
void truncate(char* dst, size_t n, const char* src, int cols) {
  if (utf8Columns(src) <= cols) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
    return;
  }
  const char* p = src;
  for (int i = 0; i < cols - 1 && utf8Next(p);) i++;
  size_t len = (size_t)(p - src);
  if (len > n - 4) len = n - 4;
  memcpy(dst, src, len);
  strcpy(dst + len, "\xE2\x80\xA6");  // …
}

}  // namespace

// Rows 1–4, one btop box (SPEC.md §7):
//   ╭─┐SYSTEM-D┌──────┐11-02-26 7:42:10 PM┌─┐⣀⣤⣶⣿┌─╮
//   │ gpu-box · 192.168.1.50            OK IDLE │
//   │ TensorFold · deepseek-v4.1-flash-1017     │
//   ╰───────────────────────────────────────────╯
int chrome(Grid& g, const View& v) {
  // Wi-Fi strength as rising braille bars, like the braille graphs.
  static const uint32_t BARS[4] = {0x28C0, 0x28E4, 0x28F6, 0x28FF};  // ⣀⣤⣶⣿
  const uint8_t box = theme::BORDER;
  const char* bars = "\xE2\xA3\x80\xE2\xA3\xA4\xE2\xA3\xB6\xE2\xA3\xBF";

  // Top edge: "SYSTEM-D" and the date + time in two notches with the border
  // between them as the divider (btop style), Wi-Fi bars in the right one.
  // The date + time sits one "─" left of the Wi-Fi notch, so the divider
  // takes the rest: 5–6 columns (user request: more space after SYSTEM-D).
  int col = edgeStart(g, 0, Edge::TOP, box);
  col = notch(g, 0, col, Edge::TOP, box, "SYSTEM-D", theme::BRAND, ATTR_BOLD);  // red in every theme
  if (v.clock[0]) {
    const int bars_notch = GRID_COLS - 1 - (utf8Columns(bars) + 3);  // its ┐
    const int clock_notch = bars_notch - 1 - (utf8Columns(v.clock) + 2);
    for (; col < clock_notch; col++) g.put(col, 0, 0x2500, box);  // ─ divider
    col = notch(g, 0, col, Edge::TOP, box, v.clock, theme::VALUE);
  }
  edgeEnd(g, 0, col, Edge::TOP, box, bars, theme::DIM);
  const int bars_col = notchCol(bars);
  // Lit bars take the fixed state colours (upstream v0.1.4): 3–4 bars green,
  // 2 orange, 1 red — like the server state, the same in every theme.
  const uint8_t lit = v.wifi_bars >= 3 ? theme::STATE_OK
                      : v.wifi_bars == 2 ? theme::STATE_WAIT : theme::STATE_BAD;
  for (int i = 0; i < 4; i++) g.put(bars_col + i, 0, BARS[i], i < v.wifi_bars ? lit : theme::DIM);

  // Server · IP, state right-aligned.
  StateLabel st = stateLabel(v.state);
  boxRow(g, 1, box);
  const int state_col = g.textRight(CONTENT_END, 1, st.text, st.role, ATTR_BOLD);
  Writer(g, 1, CONTENT_COL, state_col - 1)
      .put(v.server[0] ? v.server : fmt::NO_DATA, v.server[0] ? theme::TITLE : theme::DIM, ATTR_BOLD)
      .put(" \xC2\xB7 ", theme::DIM)
      .put(v.server_ip[0] ? v.server_ip : fmt::NO_DATA, v.server_ip[0] ? theme::VALUE : theme::DIM);

  // Engine · model, with the whole row for the model name.
  boxRow(g, 2, box);
  const char* engine = v.engine[0] ? v.engine : "???";
  Writer w(g, 2, CONTENT_COL);
  w.put(engine, theme::TITLE, ATTR_BOLD);
  if (v.model[0]) {
    char model[sizeof v.model + 4];
    truncate(model, sizeof model, v.model, CONTENT_END - w.col() - 3);
    w.put(" \xC2\xB7 ", theme::DIM).put(model, theme::VALUE);
  }
  boxBottom(g, 3, box);
  return 4;
}

// The last box's bottom edge: "╰─┘≡└─┘poll 2s · ok 0.4s ago└──┘tap: graphs└─╯".
// The ≡ notch is the settings button (tap the bottom-left corner).
void footer(Grid& g, const View& v, const char* hint, uint8_t box) {
  const int row = GRID_ROWS - 1;
  char pos[16];
  if (v.server_count > 1) {
    // Which server of how many. No ‹ › here: they aren't buttons (tap the
    // left / right third of the screen to switch), and a ‹ in the right
    // third would point the wrong way.
    snprintf(pos, sizeof pos, "%u/%u", v.server_index + 1u, (unsigned)v.server_count);
    hint = pos;
  }
  int col = edgeStart(g, row, Edge::BOTTOM, box);
  col = notch(g, row, col, Edge::BOTTOM, box, "\xE2\x89\xA1", theme::TITLE, ATTR_BOLD);  // ≡
  g.put(col++, row, 0x2500, box);
  // Status, clipped so the right-hand notch always fits.
  char status[sizeof v.status + 4];
  truncate(status, sizeof status, v.status[0] ? v.status : fmt::NO_DATA,
           GRID_COLS - edgeTail(hint) - col - 3);
  col = notch(g, row, col, Edge::BOTTOM, box, status, theme::DIM);
  edgeEnd(g, row, col, Edge::BOTTOM, box, hint, theme::DIM);
}

}  // namespace pages

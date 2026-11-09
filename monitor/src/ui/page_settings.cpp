// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Settings screen (SPEC.md §7.3): refresh rate, brightness and theme picker.
// Opened by tapping the ≡ notch in the bottom-left corner of the dashboard.
//
//   ╭─┐Settings┌─────────────────────────────────╮
//   │ Refresh     ‹         2 s          ›        │   tap left / right half
//   │ Brightness  ‹        100%          ›        │   tap left / right half
//   │ TG 101.3 · PP 3,354/s · KV ████░░ · OK IDLE │   live preview
//   ╰─────────────────────────────────────────────╯
//   ╭─┐Themes┌───────────────────────────┐1/2┌─╮
//   │ ▶SYSTEM-D             everforest-dark-medium │   12 rows × 2 columns
//   │ …                                            │
//   │ ‹ prev           done            next ›      │
//   ╰─┘≡└─┘tap a theme to apply└───────────────────╯
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {
constexpr int REFRESH_ROW = 1, BRIGHT_ROW = 2, PREVIEW_ROW = 3;
constexpr int LIST_TOP = 6;     // first theme row
constexpr int LIST_ROWS = 12;
constexpr int NAV_ROW = 18;
static_assert(LIST_TOP + LIST_ROWS == NAV_ROW && THEMES_PER_PAGE == 2 * LIST_ROWS, "settings layout");
constexpr int COL_W = 22;       // per theme column
constexpr int ARROW_PREV_COL = 14;  // ‹, left half (x 140)
constexpr int ARROW_NEXT_COL = 33;  // ›, right half (x 330)
const char* SEP = " \xC2\xB7 ";

// "▶name": the marker sits in the gap before the name, so names get 21 columns.
void themeCell(Grid& g, int row, int col, int index, bool current) {
  const theme::Theme& t = theme::get(index);
  char name[32];
  const int room = COL_W - 1;
  if ((int)strlen(t.name) > room) snprintf(name, sizeof name, "%.*s\xE2\x80\xA6", room - 1, t.name);
  else snprintf(name, sizeof name, "%s", t.name);
  if (current) g.put(col, row, 0x25B6, theme::TITLE);  // ▶
  g.text(col + 1, row, name, current ? theme::TITLE : theme::VALUE, current ? ATTR_BOLD : ATTR_NONE);
}

// "Label  ‹  value  ›": ‹ sits in the left half and › in the right half,
// matching the tap zones (settingsHit): left half = less, right half = more.
void stepper(Grid& g, int row, const char* label, const char* value) {
  g.text(CONTENT_COL, row, label, theme::LABEL);
  g.put(ARROW_PREV_COL, row, 0x2039, theme::TITLE, ATTR_BOLD);  // ‹
  g.text(GRID_COLS / 2 - utf8Columns(value) / 2, row, value, theme::VALUE, ATTR_BOLD);
  g.put(ARROW_NEXT_COL, row, 0x203A, theme::TITLE, ATTR_BOLD);  // ›
}
}  // namespace

const uint32_t POLL_OPTIONS_MS[POLL_OPTION_COUNT] = {1000, 2000, 3000, 5000, 10000, 30000};
// 5 % steps from 60 %: below that the panel is too dark to read (user).
const uint8_t BRIGHTNESS_OPTIONS_PCT[BRIGHTNESS_OPTION_COUNT] = {60, 65, 70, 75, 80, 85, 90, 95, 100};

uint8_t brightnessDuty(int pct) {
  if (pct <= 0) return 0;
  if (pct >= 100) return 255;
  // CIE 1976: L* → relative luminance Y.
  const double l = pct, y = l > 8 ? pow((l + 16) / 116, 3) : l / 903.3;
  const long duty = lround(y * 255);
  return (uint8_t)(duty < 1 ? 1 : duty);
}

int themePages() { return (theme::count() + THEMES_PER_PAGE - 1) / THEMES_PER_PAGE; }

void settings(Grid& g, const Settings& s) {
  g.clear();
  const uint8_t box = theme::BORDER, list_box = theme::BOX2;

  boxTop(g, 0, box, "Settings");
  char value[16];
  boxRow(g, REFRESH_ROW, box);
  snprintf(value, sizeof value, "%u s", (unsigned)(s.poll_ms / 1000));
  stepper(g, REFRESH_ROW, "Refresh", value);
  boxRow(g, BRIGHT_ROW, box);
  snprintf(value, sizeof value, "%u%%", (unsigned)s.brightness_pct);
  stepper(g, BRIGHT_ROW, "Brightness", value);
  // Live preview of the current theme's roles.
  boxRow(g, PREVIEW_ROW, box);
  {
    // "TG 101.3 · PP 3,354 · KV ██░░ · OK BUSY DOWN" (exactly 44 columns)
    // The state words use the fixed state colours, like the chrome
    // (upstream v0.1.4): OK and BUSY green, DOWN red — in every theme.
    Writer w(g, PREVIEW_ROW, CONTENT_COL);
    w.put("TG ", theme::DECODE, ATTR_BOLD).put("101.3", theme::DECODE).put(SEP, theme::DIM);
    w.put("PP ", theme::PREFILL, ATTR_BOLD).put("3,354", theme::PREFILL).put(SEP, theme::DIM);
    w.put("KV ", theme::LABEL);
    bar(g, w.col(), PREVIEW_ROW, 4, 50, theme::GOOD);
    w.to(w.col() + 4).put(SEP, theme::DIM).put("OK", theme::STATE_OK, ATTR_BOLD)
        .put(" ", theme::BG).put("BUSY", theme::STATE_OK, ATTR_BOLD).put(" ", theme::BG)
        .put("DOWN", theme::STATE_BAD, ATTR_BOLD);
  }
  boxBottom(g, PREVIEW_ROW + 1, box);

  char pages_label[24];
  snprintf(pages_label, sizeof pages_label, "%d/%d", s.list_page + 1, themePages());
  boxTop(g, LIST_TOP - 1, list_box, "Themes", pages_label);
  const int first = s.list_page * THEMES_PER_PAGE;
  for (int r = 0; r < LIST_ROWS; r++) {
    boxRow(g, LIST_TOP + r, list_box);
    for (int c = 0; c < 2; c++) {
      const int i = first + c * LIST_ROWS + r;
      if (i < theme::count()) themeCell(g, LIST_TOP + r, CONTENT_COL + c * COL_W, i, i == s.theme);
    }
  }
  boxRow(g, NAV_ROW, list_box);
  const bool has_prev = s.list_page > 0, has_next = s.list_page + 1 < themePages();
  g.text(CONTENT_COL, NAV_ROW, "\xE2\x80\xB9 prev", has_prev ? theme::TITLE : theme::DIM, ATTR_BOLD);
  g.text(21, NAV_ROW, "done", theme::TITLE, ATTR_BOLD);
  g.textRight(CONTENT_END, NAV_ROW, "next \xE2\x80\xBA", has_next ? theme::TITLE : theme::DIM, ATTR_BOLD);

  int col = edgeStart(g, GRID_ROWS - 1, Edge::BOTTOM, list_box);
  col = notch(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box, "\xE2\x89\xA1", theme::TITLE, ATTR_BOLD);
  g.put(col++, GRID_ROWS - 1, 0x2500, list_box);
  col = notch(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box, "tap a theme to apply", theme::DIM);
  edgeEnd(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box);
}

SettingsHit settingsHit(int x, int y, const Settings& s) {
  const int row = y / CELL_PX_H, w = GRID_PX_W;
  // The Settings box is split between the two steppers so both are easy to
  // hit: top edge + Refresh (48 px), Brightness + preview + bottom edge
  // (72 px). Left half = ‹, right half = ›.
  if (row <= REFRESH_ROW) return {x < w / 2 ? SettingsHit::POLL_PREV : SettingsHit::POLL_NEXT, 0};
  if (row < LIST_TOP - 1) return {x < w / 2 ? SettingsHit::BRIGHT_PREV : SettingsHit::BRIGHT_NEXT, 0};
  if (row >= LIST_TOP && row < LIST_TOP + LIST_ROWS) {
    const int i = s.list_page * THEMES_PER_PAGE + (x >= w / 2 ? LIST_ROWS : 0) + (row - LIST_TOP);
    if (i < theme::count()) return {SettingsHit::THEME, i};
    return {SettingsHit::NONE, 0};
  }
  // Nav: the nav row and the bottom edge below it (48 px), so a tap that
  // lands a little low still counts. The ≡ corner closes Settings, as it
  // opened them.
  if (row == NAV_ROW || row == GRID_ROWS - 1) {
    if (row == GRID_ROWS - 1 && x < w / 4) return {SettingsHit::DONE, 0};
    if (x < w / 3) return {SettingsHit::PAGE_PREV, 0};
    if (x >= w * 2 / 3) return {SettingsHit::PAGE_NEXT, 0};
    return {SettingsHit::DONE, 0};
  }
  return {SettingsHit::NONE, 0};
}

}  // namespace pages

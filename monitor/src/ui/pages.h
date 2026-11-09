// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include "gfx/grid.h"
#include "view.h"

// The two screens of SPEC.md §7. Each clears the grid and draws the whole
// page from the View. Arduino-free.
namespace pages {

enum class Page : uint8_t { OVERVIEW, GRAPHS, SETTINGS };

void overview(Grid& g, const View& v);
void graphs(Grid& g, const View& v);

// Settings screen (page_settings.cpp): refresh rate, brightness, theme picker.
struct Settings {
  uint32_t poll_ms;
  uint8_t brightness_pct;
  int theme;      // current theme index
  int list_page;  // which page of the theme list is shown
};
constexpr int THEMES_PER_PAGE = 24;  // 12 rows × 2 columns
constexpr int POLL_OPTION_COUNT = 6;
extern const uint32_t POLL_OPTIONS_MS[POLL_OPTION_COUNT];  // 1, 2, 3, 5, 10, 30 s
constexpr int BRIGHTNESS_OPTION_COUNT = 9;
extern const uint8_t BRIGHTNESS_OPTIONS_PCT[BRIGHTNESS_OPTION_COUNT];  // 60 … 100 % in 5 % steps
// Backlight PWM duty (0–255) for a perceived brightness in percent: the
// percentage is CIE L* lightness, so 50 % looks half as bright (duty 47).
uint8_t brightnessDuty(int pct);
int themePages();
void settings(Grid& g, const Settings& s);

// Touch → action on the settings screen (pixel coordinates).
constexpr int CELL_PX_H = 24, GRID_PX_W = 480;
struct SettingsHit {
  enum Kind : uint8_t { NONE, POLL_PREV, POLL_NEXT, BRIGHT_PREV, BRIGHT_NEXT, THEME, PAGE_PREV, PAGE_NEXT, DONE } kind;
  int theme;
};
SettingsHit settingsHit(int x, int y, const Settings& s);

// True for a tap on the ≡ notch area (bottom-left corner) of the dashboard.
inline bool isSettingsCorner(int x, int y) { return y / CELL_PX_H == GRID_ROWS - 1 && x < GRID_PX_W / 4; }

inline void draw(Page p, Grid& g, const View& v) {
  p == Page::GRAPHS ? graphs(g, v) : overview(g, v);
}

// Shared chrome (chrome.cpp), SPEC.md §7. chrome() draws the header box
// (rows 1–4) and returns the first free row; footer() draws the bottom edge
// of the last box (row 20) with the ≡ settings notch, the status and the hint.
int chrome(Grid& g, const View& v);
void footer(Grid& g, const View& v, const char* hint, uint8_t box);

}  // namespace pages

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Renders both pages from the demo view and compares the text dump with the
// golden files (SPEC.md §7 layout). On mismatch the actual dump is printed.
#include <unity.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "golden.h"
#include "gfx/grid.h"
#include "gfx/raster.h"
#include "ui/demo_view.h"
#include "gfx/theme.h"
#include "ui/pages.h"
#include "ui/widgets.h"

static std::string render(pages::Page p, const View& v = demoView()) {
  Grid g;
  pages::draw(p, g, v);
  return g.dump();
}

// Display width of a UTF-8 line: fullwidth forms (the 2× hero) count as 2.
static int displayCols(const std::string& line) {
  int cols = 0;
  const char* s = line.c_str();
  while (uint32_t c = utf8Next(s)) cols += (c >= 0xFF01 && c <= 0xFF5E) || c == 0x3000 ? 2 : 1;
  return cols;
}

static void expectPage(const char* golden, const std::string& actual, const char* name) {
  if (actual != golden) printf("\n--- actual %s ---\n%s", name, actual.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(golden, actual.c_str(), name);
}

void setUp() {}
void tearDown() {}

static void test_overview_matches_golden() {
  expectPage(GOLDEN_OVERVIEW, render(pages::Page::OVERVIEW), "overview");
}

static void test_graphs_matches_golden() {
  expectPage(GOLDEN_GRAPHS, render(pages::Page::GRAPHS), "graphs");
}

static void test_every_row_is_48_columns() {
  for (auto p : {pages::Page::OVERVIEW, pages::Page::GRAPHS}) {
    std::istringstream in(render(p));
    std::string line;
    int rows = 0;
    while (std::getline(in, line)) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GRID_COLS, displayCols(line), line.c_str());
      rows++;
    }
    TEST_ASSERT_EQUAL_INT(GRID_ROWS, rows);
  }
}

// Row 10 of the dump (the KV row) as a string.
static std::string kvRow(const View& v) {
  std::istringstream in(render(pages::Page::OVERVIEW, v));
  std::string line;
  for (int r = 0; r <= 9 && std::getline(in, line); r++) {}
  return line;
}

static void test_kv_peak_over_window() {
  View v = demoView();
  for (int i = 0; i < RING_LEN; i++) v.kv_hist[i] = 0;
  v.kv_hist[20] = 12.6f;  // a request that has since finished
  v.kv_pct = 0;
  v.kv_tokens = 0;
  std::string row = kvRow(v);
  TEST_ASSERT_TRUE_MESSAGE(row.find("KV 0.0%") != std::string::npos, row.c_str());
  TEST_ASSERT_TRUE_MESSAGE(row.find("peak 12.6%") != std::string::npos, row.c_str());
  // Worst case still fits in the box: the bar gives way.
  for (int i = 0; i < RING_LEN; i++) v.kv_hist[i] = 100;
  v.kv_pct = 100;
  v.kv_tokens = v.kv_capacity = 1.0e6f;
  row = kvRow(v);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GRID_COLS, displayCols(row), row.c_str());
  TEST_ASSERT_TRUE_MESSAGE(row.find("peak 100.0% \xC2\xB7 1.0M/1.0M tok \xE2\x94\x82") != std::string::npos, row.c_str());
}

static void test_empty_view_says_no_data() {
  View v;  // nothing received yet
  std::string ov = render(pages::Page::OVERVIEW, v);
  TEST_ASSERT_TRUE(ov.find("no data") != std::string::npos);
  TEST_ASSERT_TRUE(ov.find("UNKNOWN") != std::string::npos);
  TEST_ASSERT_TRUE(ov.find("nan") == std::string::npos);  // never a raw NaN
  std::string gp = render(pages::Page::GRAPHS, v);
  TEST_ASSERT_TRUE(gp.find("no data") != std::string::npos);
  TEST_ASSERT_TRUE(gp.find("nan") == std::string::npos);
}

static void test_settings_screen() {
  Grid g;
  const int nord = theme::find("nord");
  pages::Settings s{2000, 80, nord, nord / pages::THEMES_PER_PAGE};
  pages::settings(g, s);
  std::string d = g.dump();
  printf("\n--- settings ---\n%s", d.c_str());
  std::istringstream in(d);
  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(GRID_COLS, displayCols(line), line.c_str());
    rows++;
  }
  TEST_ASSERT_EQUAL_INT(GRID_ROWS, rows);
  TEST_ASSERT_TRUE(d.find("2 s") != std::string::npos);
  // ‹ in the left half, › in the right half of the refresh row
  TEST_ASSERT_EQUAL_UINT16(0x2039, g.at(14, 1).ch);
  TEST_ASSERT_EQUAL_UINT16(0x203A, g.at(33, 1).ch);
  // … and of the brightness row
  TEST_ASSERT_TRUE(d.find("Brightness") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("80%") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT16(0x2039, g.at(14, 2).ch);
  TEST_ASSERT_EQUAL_UINT16(0x203A, g.at(33, 2).ch);
  TEST_ASSERT_TRUE(d.find("\xE2\x96\xB6nord") != std::string::npos);  // ▶ marks the current theme
}

static void test_pixel_sparkline() {
  // One bar per sample at pixel resolution: 0 → 1 px baseline, NaN → gap,
  // the max → the full 48 px over two rows.
  float d[6] = {NAN, 0, 10, 20, 40, 80};
  Grid g;
  widgets::sparkline(g, 0, 0, 2, 2, d, theme::DECODE);
  TEST_ASSERT_EQUAL_UINT8(ATTR_PIXBARS, g.at(0, 1).attr);
  TEST_ASSERT_EQUAL_INT(0, pixBar(g.at(0, 1).ch, 0));   // gap
  TEST_ASSERT_EQUAL_INT(1, pixBar(g.at(0, 1).ch, 1));   // baseline
  TEST_ASSERT_EQUAL_INT(6, pixBar(g.at(0, 1).ch, 2));   // 10/80 × 48
  TEST_ASSERT_EQUAL_INT(24, pixBar(g.at(1, 1).ch, 1));  // 40/80 × 48 fills the bottom row …
  TEST_ASSERT_EQUAL_INT(0, pixBar(g.at(1, 0).ch, 1));   // … exactly
  TEST_ASSERT_EQUAL_INT(24, pixBar(g.at(1, 0).ch, 2));  // max: top row full too
  // Raster: bars are 3, 3, 4 px wide and fill up from the cell bottom.
  const theme::Theme& t = theme::get(0);
  uint16_t px[raster::CELL_H * raster::CELL_W];
  raster::cell(g.at(0, 1), t, px, raster::CELL_W);
  const uint16_t fg = t.rgb565[theme::DECODE], bg = t.rgb565[theme::BG];
  const int last = (raster::CELL_H - 1) * raster::CELL_W;
  TEST_ASSERT_EQUAL_HEX16(bg, px[last + 0]);                           // gap
  TEST_ASSERT_EQUAL_HEX16(fg, px[last + 3]);                           // baseline …
  TEST_ASSERT_EQUAL_HEX16(bg, px[last - raster::CELL_W + 3]);          // … 1 px high
  TEST_ASSERT_EQUAL_HEX16(fg, px[(raster::CELL_H - 6) * raster::CELL_W + 9]);  // 6 px bar, top
  TEST_ASSERT_EQUAL_HEX16(bg, px[(raster::CELL_H - 7) * raster::CELL_W + 9]);
}

static void test_brightness_duty() {
  TEST_ASSERT_EQUAL_UINT8(0, pages::brightnessDuty(0));
  TEST_ASSERT_EQUAL_UINT8(255, pages::brightnessDuty(100));
  TEST_ASSERT_EQUAL_UINT8(47, pages::brightnessDuty(50));  // perceptual: half as bright, not half duty
  int prev = 0;
  for (int i = 0; i < pages::BRIGHTNESS_OPTION_COUNT; i++) {
    const int duty = pages::brightnessDuty(pages::BRIGHTNESS_OPTIONS_PCT[i]);
    TEST_ASSERT_GREATER_THAN_INT(prev, duty);  // every step is brighter, and the lowest isn't off
    prev = duty;
  }
}

static void test_brand_is_red_in_every_theme() {
  Grid g;
  pages::overview(g, demoView());
  TEST_ASSERT_EQUAL_UINT8(theme::BRAND, g.at(3, 0).fg);  // the "S" of SYSTEM-D
  for (int i = 0; i < theme::count(); i++)
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(theme::BRAND_RED, theme::get(i).rgb565[theme::BRAND], theme::get(i).name);
}

static void test_themes_and_hits() {
  TEST_ASSERT_EQUAL_INT(42, theme::count());  // btop Default + 41 bundled
  TEST_ASSERT_EQUAL_STRING("SYSTEM-D", theme::get(0).name);  // btop's Default, renamed
  TEST_ASSERT_TRUE(theme::find("tokyo-night") > 0);
  // Unknown names (first boot, the removed EngineMetrics theme, the old
  // "Default" name) → SYSTEM-D.
  TEST_ASSERT_EQUAL_INT(0, theme::find("no-such-theme"));
  TEST_ASSERT_EQUAL_INT(0, theme::find("EngineMetrics"));
  TEST_ASSERT_EQUAL_INT(0, theme::find("Default"));
  TEST_ASSERT_EQUAL_INT(0, theme::find("System-D"));  // before it was capitalized
  TEST_ASSERT_EQUAL_INT(2, pages::themePages());

  pages::Settings s{2000, 100, 0, 0};
  using H = pages::SettingsHit;
  // The visible ‹ and › must fall in their own zones (this was wrong: both
  // sat in the left half, so tapping › lowered the rate).
  TEST_ASSERT_EQUAL(H::POLL_PREV, pages::settingsHit(14 * 10 + 5, 30, s).kind);  // on ‹
  TEST_ASSERT_EQUAL(H::POLL_NEXT, pages::settingsHit(33 * 10 + 5, 30, s).kind);  // on ›
  TEST_ASSERT_EQUAL(H::POLL_PREV, pages::settingsHit(50, 5, s).kind);    // top edge, left
  TEST_ASSERT_EQUAL(H::POLL_NEXT, pages::settingsHit(400, 47, s).kind);  // … right, bottom of the row
  TEST_ASSERT_EQUAL(H::BRIGHT_PREV, pages::settingsHit(14 * 10 + 5, 2 * 24 + 10, s).kind);  // on ‹
  TEST_ASSERT_EQUAL(H::BRIGHT_NEXT, pages::settingsHit(33 * 10 + 5, 2 * 24 + 10, s).kind);  // on ›
  TEST_ASSERT_EQUAL(H::BRIGHT_PREV, pages::settingsHit(50, 3 * 24 + 5, s).kind);   // preview row
  TEST_ASSERT_EQUAL(H::BRIGHT_NEXT, pages::settingsHit(400, 4 * 24 + 20, s).kind);  // box bottom edge
  TEST_ASSERT_EQUAL(H::NONE, pages::settingsHit(30, 5 * 24 + 5, s).kind);         // Themes top edge
  H h = pages::settingsHit(30, 6 * 24 + 5, s);                            // first list row, left column
  TEST_ASSERT_EQUAL(H::THEME, h.kind);
  TEST_ASSERT_EQUAL_INT(0, h.theme);
  TEST_ASSERT_EQUAL_INT(12 + 2, pages::settingsHit(300, 8 * 24, s).theme);  // right column, 3rd row
  s.list_page = 1;
  TEST_ASSERT_EQUAL_INT(24, pages::settingsHit(30, 6 * 24, s).theme);
  TEST_ASSERT_EQUAL(H::NONE, pages::settingsHit(300, 17 * 24, s).kind);   // past the last theme
  TEST_ASSERT_EQUAL(H::PAGE_PREV, pages::settingsHit(20, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::DONE, pages::settingsHit(240, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::PAGE_NEXT, pages::settingsHit(470, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::PAGE_NEXT, pages::settingsHit(442, 475, s).kind);   // a little low: bottom edge
  TEST_ASSERT_EQUAL(H::DONE, pages::settingsHit(27, 468, s).kind);         // ≡ corner
  TEST_ASSERT_EQUAL(H::PAGE_PREV, pages::settingsHit(140, 470, s).kind);   // bottom edge, left third
  TEST_ASSERT_TRUE(pages::isSettingsCorner(20, 470));
  TEST_ASSERT_FALSE(pages::isSettingsCorner(20, 440));   // row above the footer
  TEST_ASSERT_FALSE(pages::isSettingsCorner(300, 470));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_overview_matches_golden);
  RUN_TEST(test_graphs_matches_golden);
  RUN_TEST(test_every_row_is_48_columns);
  RUN_TEST(test_empty_view_says_no_data);
  RUN_TEST(test_kv_peak_over_window);
  RUN_TEST(test_settings_screen);
  RUN_TEST(test_brightness_duty);
  RUN_TEST(test_pixel_sparkline);
  RUN_TEST(test_themes_and_hits);
  RUN_TEST(test_brand_is_red_in_every_theme);
  return UNITY_END();
}

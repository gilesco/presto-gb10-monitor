// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// POSIX TZ evaluation (net/posix_tz.cpp): the Europe/London and US strings
// against their known 2026 transition instants, the southern-hemisphere wrap,
// and the clock string the header renders.
#include <unity.h>

#include <string.h>

#include "net/posix_tz.h"
#include "ui/fmt.h"

void setUp() {}
void tearDown() {}

static void check_local(const char* what, const posix_tz::Rules& r, unsigned long utc,
                        int y, int mon, int dom, int h, int m) {
  struct tm t;
  TEST_ASSERT_TRUE_MESSAGE(posix_tz::to_local(r, utc, &t), what);
  char got[64], want[64];
  snprintf(got, sizeof got, "%04d-%02d-%02d %02d:%02d", t.tm_year + 1900, t.tm_mon + 1,
           t.tm_mday, t.tm_hour, t.tm_min);
  snprintf(want, sizeof want, "%04d-%02d-%02d %02d:%02d", y, mon, dom, h, m);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, what);
}

static void test_london_2026() {
  posix_tz::Rules r;
  TEST_ASSERT_TRUE(posix_tz::parse("GMT0BST,M3.5.0/1,M10.5.0", r));
  TEST_ASSERT_EQUAL_STRING("GMT", r.std_abbr);
  TEST_ASSERT_EQUAL_STRING("BST", r.dst_abbr);
  TEST_ASSERT_EQUAL(0, r.std_off_s);
  TEST_ASSERT_EQUAL(-3600, r.dst_off_s);  // POSIX offsets run west-positive

  // BST began 2026-03-29 01:00 UTC (clocks 00:59 GMT -> 02:00 BST).
  check_local("before spring forward", r, 1774745940ul, 2026, 3, 29, 0, 59);
  check_local("after spring forward", r, 1774746060ul, 2026, 3, 29, 2, 1);
  // Midsummer: UTC+1.
  check_local("midsummer", r, 1782907200ul, 2026, 7, 1, 13, 0);
  // BST ended 2026-10-25 01:00 UTC (clocks 01:59 BST -> 01:00 GMT).
  check_local("before fall back", r, 1792889940ul, 2026, 10, 25, 1, 59);
  check_local("after fall back", r, 1792890060ul, 2026, 10, 25, 1, 1);
  // THE REGRESSION: autumn after the change must be GMT, not BST. The board
  // showed 4:04 pm at 15:04 UTC because DST never ended.
  check_local("autumn is GMT (regression)", r, 1794236640ul, 2026, 11, 9, 15, 4);
}

static void test_us_nonzero_offset() {
  // Non-zero std offset: also covers the transition-instant sign (the rule
  // time is local wall clock; UTC = local + offset, POSIX offsets west-pos).
  posix_tz::Rules r;
  TEST_ASSERT_TRUE(posix_tz::parse("EST5EDT,M3.2.0,M11.1.0", r));
  TEST_ASSERT_EQUAL(18000, r.std_off_s);   // EST = UTC-5, so +18000 westwards
  TEST_ASSERT_EQUAL(14400, r.dst_off_s);   // EDT = UTC-4
  // DST began 2026-03-08 02:00 EST = 07:00 UTC (verified with date(1)).
  check_local("US before spring forward", r, 1772953140ul, 2026, 3, 8, 1, 59);
  check_local("US after spring forward", r, 1772953260ul, 2026, 3, 8, 3, 1);
  // DST ended 2026-11-01 02:00 EDT = 06:00 UTC.
  check_local("US before fall back", r, 1793512740ul, 2026, 11, 1, 1, 59);
  check_local("US after fall back", r, 1793512860ul, 2026, 11, 1, 1, 1);
}

static void test_southern_hemisphere() {
  // Sydney: DST starts first Sunday of October, ends first Sunday of April —
  // start_utc > end_utc within a year, exercising the wrap branch.
  posix_tz::Rules r;
  TEST_ASSERT_TRUE(posix_tz::parse("AEST-10AEDT,M10.1.0,M4.1.0", r));
  TEST_ASSERT_EQUAL(-36000, r.std_off_s);  // AEST = UTC+10
  TEST_ASSERT_EQUAL(-39600, r.dst_off_s);  // AEDT = UTC+11
  // January 2026 is DST: 00:59 UTC -> 11:59 AEDT.
  check_local("Sydney summer is AEDT", r, 1767229140ul, 2026, 1, 1, 11, 59);
  // July is standard: 00:59 UTC -> 10:59 AEST.
  check_local("Sydney winter is AEST", r, 1782867540ul, 2026, 7, 1, 10, 59);
}

static void test_no_dst_and_leap_day() {
  posix_tz::Rules r;
  TEST_ASSERT_TRUE(posix_tz::parse("UTC0", r));
  TEST_ASSERT_FALSE(r.has_dst);
  // Leap day exists and the day-of-week math knows it: 2024-02-29 was a
  // Thursday (tm_wday 4).
  struct tm t;
  TEST_ASSERT_TRUE(posix_tz::to_local(r, 1709164800ul, &t));
  TEST_ASSERT_EQUAL(4, t.tm_wday);
}

static void test_date_format() {
  // UK order with an obvious three-letter month.
  TEST_ASSERT_EQUAL_STRING("09-Nov-26", fmt::date(2026, 11, 9));
  TEST_ASSERT_EQUAL_STRING("01-Jan-00", fmt::date(2000, 1, 1));
  TEST_ASSERT_EQUAL_STRING("29-Feb-24", fmt::date(2024, 2, 29));
  TEST_ASSERT_EQUAL_STRING("01-Jan-26", fmt::date(2126, 1, 1));  // year%100
  TEST_ASSERT_EQUAL_STRING("no data", fmt::date(2026, 0, 9));    // junk guards
  TEST_ASSERT_EQUAL_STRING("no data", fmt::date(2026, 13, 9));
  TEST_ASSERT_EQUAL_STRING("no data", fmt::date(2026, 11, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_london_2026);
  RUN_TEST(test_us_nonzero_offset);
  RUN_TEST(test_southern_hemisphere);
  RUN_TEST(test_no_dst_and_leap_day);
  RUN_TEST(test_date_format);
  return UNITY_END();
}

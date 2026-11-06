// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Number formatting (SPEC.md §5 "Number format").
#include <unity.h>

#include <math.h>

#include "ui/fmt.h"

void setUp() {}
void tearDown() {}

static void test_speed() {
  TEST_ASSERT_EQUAL_STRING("0.0", fmt::speed(0));
  TEST_ASSERT_EQUAL_STRING("100.9", fmt::speed(100.94f));
  TEST_ASSERT_EQUAL_STRING("999.9", fmt::speed(999.9f));
  TEST_ASSERT_EQUAL_STRING("1,000", fmt::speed(999.96f));
  TEST_ASSERT_EQUAL_STRING("1,331", fmt::speed(1331.2f));
  TEST_ASSERT_EQUAL_STRING("99,999", fmt::speed(99999.0f));
  TEST_ASSERT_EQUAL_STRING("120K", fmt::speed(120400.0f));
  TEST_ASSERT_EQUAL_STRING("1.20M", fmt::speed(1.2e6f));
  TEST_ASSERT_EQUAL_STRING("no data", fmt::speed(NAN));
}

static void test_rate_has_no_suffix_when_missing() {
  TEST_ASSERT_EQUAL_STRING("1,331/s", fmt::rate(1331));
  TEST_ASSERT_EQUAL_STRING("no data", fmt::rate(NAN));
}

static void test_short_pct_tokens() {
  TEST_ASSERT_EQUAL_STRING("6.8K", fmt::speedShort(6800));
  TEST_ASSERT_EQUAL_STRING("91.2", fmt::speedShort(91.2f));
  TEST_ASSERT_EQUAL_STRING("31.4%", fmt::pct(31.4f));
  TEST_ASSERT_EQUAL_STRING("0", fmt::tokens(0));
  TEST_ASSERT_EQUAL_STRING("21K", fmt::tokens(21000));
  TEST_ASSERT_EQUAL_STRING("329K", fmt::tokens(329253));
  TEST_ASSERT_EQUAL_STRING("1.0M", fmt::tokens(1048576));
}

static void test_durations() {
  TEST_ASSERT_EQUAL_STRING("312ms", fmt::dur(0.312f));
  TEST_ASSERT_EQUAL_STRING("1.85s", fmt::dur(1.85f));
  TEST_ASSERT_EQUAL_STRING("12.4s", fmt::dur(12.4f));
  TEST_ASSERT_EQUAL_STRING("1.2s", fmt::durShort(1.214f));
  TEST_ASSERT_EQUAL_STRING("no data", fmt::dur(NAN));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_speed);
  RUN_TEST(test_rate_has_no_suffix_when_missing);
  RUN_TEST(test_short_pct_tokens);
  RUN_TEST(test_durations);
  return UNITY_END();
}

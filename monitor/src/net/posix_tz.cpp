// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "net/posix_tz.h"

#include <ctype.h>
#include <string.h>

namespace posix_tz {

namespace {

bool digits_to_int(const char*& p, int& out) {
  int v = 0, n = 0;
  while (isdigit((unsigned char)*p)) {
    v = v * 10 + (*p - '0');
    p++;
    n++;
  }
  out = v;
  return n > 0;
}

// [+-]hh[:mm[:ss]] → seconds (signed, POSIX convention baked in by caller)
bool parse_offset(const char*& p, int32_t& out_s) {
  int sign = 1;
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  int h;
  if (!digits_to_int(p, h)) return false;
  int m = 0, s = 0;
  if (*p == ':') {
    p++;
    if (!digits_to_int(p, m)) return false;
    if (*p == ':') {
      p++;
      if (!digits_to_int(p, s)) return false;
    }
  }
  out_s = sign * (h * 3600 + m * 60 + s);
  return true;
}

bool parse_name(const char*& p, char* out) {
  int n = 0;
  while (isalpha((unsigned char)*p) && n < (int)ABBR_LEN - 1) out[n++] = *p++;
  out[n] = '\0';
  return n >= 3;  // POSIX requires at least 3 characters
}

// Mm.w.d[/time]
bool parse_rule(const char*& p, Rule& r) {
  if (*p != 'M') return false;
  p++;
  int m, w, d;
  if (!digits_to_int(p, m) || m < 1 || m > 12) return false;
  if (*p != '.') return false;
  p++;
  if (!digits_to_int(p, w) || w < 1 || w > 5) return false;
  if (*p != '.') return false;
  p++;
  if (!digits_to_int(p, d) || d > 6) return false;
  r.month = (uint8_t)m;
  r.week = (uint8_t)w;
  r.day = (uint8_t)d;
  r.time_s = 2 * 3600;
  if (*p == '/') {
    p++;
    if (*p == '-') {
      // "-time": 23:59:59ish of the previous day (POSIX 2017); we accept the
      // form and treat small values as same-day.
      p++;
      int32_t v;
      if (!parse_offset(p, v)) return false;
      r.time_s = 86400 - v;
    } else {
      int32_t v;
      if (!parse_offset(p, v)) return false;
      r.time_s = v;
    }
  }
  r.valid = true;
  return true;
}

int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// The day of month for the w-th <day> of month (w=5 → last one).
unsigned rule_day_of_month(int year, const Rule& r) {
  const unsigned first_wday = (unsigned)((days_from_civil(year, r.month, 1) + 4) % 7);
  if (r.week == 5) {  // last <day> of the month
    unsigned next_month = r.month + 1;
    int y2 = year;
    if (next_month > 12) {
      next_month = 1;
      y2++;
    }
    const unsigned last_dom =
        (unsigned)(days_from_civil(y2, next_month, 1) - days_from_civil(year, r.month, 1));
    const unsigned last_wday = (unsigned)((days_from_civil(year, r.month, last_dom) + 4) % 7);
    return last_dom - (last_wday - r.day + 7) % 7;
  }
  return 1 + (r.day - first_wday + 7) % 7 + (r.week - 1) * 7;
}

}  // namespace

void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
  const unsigned mm = mp + (mp < 10 ? 3 : -9);
  *y = (int)(yy + (mm <= 2));
  *m = mm;
  *d = dd;
}

bool parse(const char* tz, Rules& out) {
  if (!tz || !tz[0]) return false;
  Rules r;
  const char* p = tz;
  if (!parse_name(p, r.std_abbr)) return false;
  if (!parse_offset(p, r.std_off_s)) return false;

  if (*p && *p != ',') {
    if (!parse_name(p, r.dst_abbr)) return false;
    r.has_dst = true;
    if (*p && *p != ',') {
      if (!parse_offset(p, r.dst_off_s)) return false;
    } else {
      r.dst_off_s = r.std_off_s - 3600;  // default: one hour east of std
    }
    if (*p != ',') return false;
  }
  if (r.has_dst) {
    p++;
    if (!parse_rule(p, r.start)) return false;
    if (*p != ',') return false;
    p++;
    if (!parse_rule(p, r.end)) return false;
    if (*p != '\0') return false;
  } else if (*p != '\0') {
    return false;
  }
  out = r;
  return true;
}

bool to_local(const Rules& r, uint64_t utc_s, struct tm* out) {
  if (!out) return false;
  int y;
  unsigned mo, da;
  civil_from_days((int64_t)(utc_s / 86400), &y, &mo, &da);

  bool dst = false;
  if (r.has_dst) {
    // Transition instants: POSIX Mm.w.d rules name a day in a month — build
    // each one from its actual month/day, not as a day-of-year off Jan 1.
    // The rule time is local wall clock of the *receiving* zone: start is
    // standard time, end is DST time. Local = utc − offset (POSIX offsets run
    // west-positive), so utc = local + offset; the receiving zone's offset
    // is std_off_s for the start, dst_off_s for the end.
    const int64_t start_local_s =
        days_from_civil(y, r.start.month, rule_day_of_month(y, r.start)) * 86400 +
        r.start.time_s;  // time may exceed 24h (POSIX.1-2017): spills whole days
    const int64_t end_local_s =
        days_from_civil(y, r.end.month, rule_day_of_month(y, r.end)) * 86400 +
        r.end.time_s;
    const int64_t start_utc = start_local_s + r.std_off_s;
    const int64_t end_utc = end_local_s + r.dst_off_s;
    const int64_t t = (int64_t)utc_s;
    dst = start_utc <= end_utc ? (t >= start_utc && t < end_utc)
                               : (t >= start_utc || t < end_utc);  // southern hemisphere
  }
  const int64_t local = (int64_t)utc_s - (dst ? r.dst_off_s : r.std_off_s);
  const int64_t days = local / 86400;
  int64_t rem = local % 86400;
  if (rem < 0) {
    rem += 86400;
  }
  int yy;
  unsigned mm2, dd2;
  civil_from_days(days, &yy, &mm2, &dd2);
  memset(out, 0, sizeof *out);
  out->tm_year = yy - 1900;
  out->tm_mon = (int)mm2 - 1;
  out->tm_mday = (int)dd2;
  out->tm_hour = (int)(rem / 3600);
  out->tm_min = (int)((rem / 60) % 60);
  out->tm_sec = (int)(rem % 60);
  out->tm_wday = (int)((days + 4) % 7);  // 1970-01-01 was a Thursday
  return true;
}

}  // namespace posix_tz

// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Polled FT6236 touch driver for the Presto, ported from the touch.py module
// in pimoroni/presto (boards/presto, MIT). Register protocol: write the
// 1-byte register pointer 0x00 with a repeated start, then read 15 bytes:
//   [0] device mode  [1] gesture  [2] TD_STATUS (touch count, low nibble)
//   [3+i*6] touch i: XH XL YH YL (XH[7:6] = event, [3:0] = x MSBs), weight,
//            misc
// Event codes: 0 = DOWN, 1 = UP, 2 = CONTACT, 3 = NONE. Coordinates are
// already in panel pixels at full res.

#include "hw/touch_ft6236.h"

#include <cstdio>
#include <cstring>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hw/panel.h"
#include "pico/stdlib.h"

namespace touch {

namespace {
constexpr uint8_t ADDR = 0x48;
constexpr uint SDA = PRESTO_TOUCH_SDA_PIN;  // 30
constexpr uint SCL = PRESTO_TOUCH_SCL_PIN;  // 31
constexpr uint INT = PRESTO_TOUCH_INT_PIN;  // 32, active low

constexpr int I2C_MAX_ERRORS = 10;     // then the driver gives up (touch_ok stays, reads go stale)
constexpr int I2C_BACKOFF_POLLS = 20;  // skip polls after an error; a wedged bus costs a
                                       // 50 ms I²C timeout per attempt, which would stall
                                       // the frame loop (same reasoning as touch.py)

bool inited = false;
int i2c_errors = 0;
int i2c_skip = 0;
bool last_down = false;
uint16_t last_x = SCREEN_W / 2, last_y = SCREEN_H / 2;

bool read_block(uint8_t* buf) {
  const uint8_t reg = 0x00;
  absolute_time_t until = make_timeout_time_ms(50);
  if (i2c_write_blocking_until(i2c1, ADDR, &reg, 1, true /* repeated start */, until) < 0)
    return false;
  until = make_timeout_time_ms(50);
  return i2c_read_blocking_until(i2c1, ADDR, buf, 15, false, until) == 15;
}

// A controller interrupted mid-transaction can hold SDA low, wedging the
// bus. Clocking SCL lets it finish so it releases the line (touch.py
// _recover_i2c).
void recover_i2c() {
  gpio_set_function(SCL, GPIO_FUNC_SIO);
  gpio_set_dir(SCL, GPIO_OUT);
  gpio_put(SCL, 1);
  gpio_set_function(SDA, GPIO_FUNC_SIO);
  gpio_set_dir(SDA, GPIO_IN);
  gpio_pull_up(SDA);
  for (int i = 0; i < 9; i++) {
    if (gpio_get(SDA)) break;
    sleep_us(5);
    gpio_put(SCL, 0);
    sleep_us(5);
    gpio_put(SCL, 1);
  }
  i2c_init(i2c1, 400000);
  gpio_set_function(SDA, GPIO_FUNC_I2C);
  gpio_set_function(SCL, GPIO_FUNC_I2C);
  gpio_pull_up(SDA);
  gpio_pull_up(SCL);
}

}  // namespace

bool begin(Info* info) {
  i2c_init(i2c1, 400000);
  gpio_set_function(SDA, GPIO_FUNC_I2C);
  gpio_pull_up(SDA);
  gpio_set_function(SCL, GPIO_FUNC_I2C);
  gpio_pull_up(SCL);
  gpio_init(INT);
  gpio_set_dir(INT, GPIO_IN);
  gpio_pull_up(INT);

  uint8_t buf[15];
  inited = read_block(buf);
  if (info) {
    memset(info, 0, sizeof *info);
    if (inited) {
      info->addr = ADDR;
      snprintf(info->product, sizeof info->product, "FT62");
      info->max_x = SCREEN_W;
      info->max_y = SCREEN_H;
    }
  }
  if (!inited) printf("touch: no FT6236 at 0x%02x\n", ADDR);
  return inited;
}

Point read() {
  Point p{false, false, last_x, last_y};
  if (!inited) return p;
  if (i2c_skip) {
    i2c_skip--;
    return p;
  }
  // Read when INT is low (touch active) or a finger was down at the last
  // read (its release may not assert INT again).
  if (gpio_get(INT) && !last_down) return p;

  uint8_t d[15];
  if (!read_block(d)) {
    if (++i2c_errors >= I2C_MAX_ERRORS) {
      printf("touch: giving up after %d I2C errors\n", i2c_errors);
      inited = false;
      return p;
    }
    if (i2c_errors > 1) recover_i2c();
    i2c_skip = I2C_BACKOFF_POLLS;
    return p;
  }
  i2c_errors = 0;

  const int touches = d[2] & 0x0F;
  bool down = false;
  uint16_t x = last_x, y = last_y;
  if (touches >= 1) {
    const uint8_t* t = d + 3;  // first touch point
    const uint8_t e = t[0] >> 6;
    x = (uint16_t)(((t[0] & 0x0F) << 8) | t[1]);
    y = (uint16_t)(((t[2] & 0x0F) << 8) | t[3]);
    down = e != 0x01 && e != 0x03;  // not UP, not NONE
  }
  last_down = down;
  last_x = x;
  last_y = y;
  p.fresh = true;
  p.down = down;
  p.x = x;
  p.y = y;
  return p;
}

}  // namespace touch

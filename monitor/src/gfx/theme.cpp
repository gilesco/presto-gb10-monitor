// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "theme.h"

#include <string.h>

#include "themes_btop.h"

namespace theme {

int count() { return BTOP_COUNT; }

const Theme& get(int index) {
  index %= count();
  if (index < 0) index += count();
  return BTOP_THEMES[index];
}

int find(const char* name) {
  for (int i = 0; i < count(); i++)
    if (name && strcmp(get(i).name, name) == 0) return i;
  // Unknown name (first boot, or a theme that no longer exists): default.
  for (int i = 0; i < count(); i++)
    if (strcmp(get(i).name, DEFAULT_NAME) == 0) return i;
  return 0;
}

}  // namespace theme

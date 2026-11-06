// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include "gfx/grid.h"
#include "gfx/theme.h"

// Puts a Grid on the panel: diffs it against what's shown and rasterises
// only the changed cells, then copies the dirty spans into the Presto
// framebuffer, byte-swapping pixels (the Presto's PIO scanout wants
// byte-swapped RGB565 — verified on hardware, see the repo README).
namespace screen {

void begin();

// Returns the number of cells repainted.
int present(const Grid& next, const theme::Theme& t);

// Forces the next present() to repaint everything (e.g. after a theme change).
void invalidate();

}  // namespace screen

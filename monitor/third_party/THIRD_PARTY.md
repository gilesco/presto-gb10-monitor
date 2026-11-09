# Third-party components

SYSTEM-D Monitor itself is MIT-licensed (`LICENSE`). It includes or uses:

| Component | Where | Licence |
|---|---|---|
| [Terminus Font](https://terminus-font.sourceforge.net/) 4.49.1 (10×20 regular + bold) | vendored BDFs in `third_party/terminus/`, converted into `src/gfx/font_ter10x20.h` | SIL Open Font License 1.1 (`third_party/terminus/OFL.TXT`) |
| [btop](https://github.com/aristocratos/btop) colour themes by aristocratos and the theme authors (named in 33 of the 41 files; kept unchanged) (built-in Default, shown as "SYSTEM-D", + 41 bundled `.theme` files) | vendored in `third_party/btop/`, converted into `src/gfx/themes_btop.h` | Apache License 2.0 (`third_party/btop/LICENSE`); source commit in `third_party/btop/SOURCE.txt` |
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) 1.6.9 | fetched by PlatformIO (`lib_deps`) | BSD |
| [ArduinoJson](https://arduinojson.org/) 7.4.3 | fetched by PlatformIO (`lib_deps`) | MIT |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) / ESP-IDF | fetched by PlatformIO (`platform = espressif32@8.0.0`) | Apache 2.0 / LGPL 2.1 (see upstream) |

## Inspiration

[btop](https://github.com/aristocratos/btop),
[EngineMetrics](https://github.com/0rand/EngineMetrics) and
[nv-monitor](https://github.com/wentbackward/nv-monitor). No code is copied
from them.

The vendor's board download pack (datasheets, demos, Windows tools) is not
included in this repository. See `docs/board-guide.md` for where to get it.

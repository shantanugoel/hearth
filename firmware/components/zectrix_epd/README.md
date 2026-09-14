# zectrix_epd

An ESP-IDF 5.4+ component for the Zectrix 4.2-inch SSD2683 e-paper display.
The component has a single public C header, `include/zectrix_epd.h`, and does
not depend on LVGL.

Copy this directory to `components/zectrix_epd` in a third-party project. The
default pin configuration targets the Zectrix ESP32-S3 e-paper board and can
be overridden through `zectrix_epd_config_t`.

The API provides explicit panel power-on/power-off, full 1bpp refresh, partial
1bpp refresh and full 4bpp/16-gray refresh. See the standalone showcase's
`docs/EPD_API.md` for lifecycle, pixel formats and integration examples.

Copyright (c) 2026 Zectrix Lab. Licensed under MIT.

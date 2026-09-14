# Third-party notices

Hearth is MIT licensed. The following third-party software retains its own
license.

## Zectrix Lab NOTE4 board support

`firmware/components/zectrix_epd` and `firmware/components/zectrix_board` are
copied from [zectrix-note4-epd-demo](https://github.com/itopinion/zectrix-note4-epd-demo),
MIT License, Copyright (c) 2026 Zectrix Lab. The license text is at
`licenses/ZECTRIX_MIT_LICENSE`.

The 1bpp canvas in `firmware/components/hearth_gfx` is trimmed from the same
project's `zectrix_demo_ui` (also MIT).

## TRMNL16 Regular

The embedded ASCII bitmap table in
`firmware/components/hearth_gfx/font/zectrix_ascii_font_8x16.h` was rasterized
from TRMNL16 Regular by Heavyweight Digital Type Foundry. The font is licensed
under the SIL Open Font License, Version 1.1, included at
`licenses/TRMNL_FONT_LICENSE.txt`.

## Espressif ESP-IDF

ESP-IDF is a build dependency and is not copied into this project.

## Espressif esp_codec_dev

The audio codec abstraction is obtained through ESP-IDF Component Manager as
`espressif/esp_codec_dev`. Its license file is delivered with the downloaded
component in `firmware/managed_components/espressif__esp_codec_dev/LICENSE`.

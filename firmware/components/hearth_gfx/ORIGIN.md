1bpp canvas and TRMNL16 ASCII bitmap, trimmed from
https://github.com/itopinion/zectrix-note4-epd-demo (`zectrix_demo_ui`).

- Canvas code: MIT, Copyright 2026 Zectrix Lab (see licenses/ZECTRIX_MIT_LICENSE).
- `font/zectrix_ascii_font_8x16.h` was rasterized from TRMNL16 Regular by
  Heavyweight Digital Type Foundry (SIL OFL 1.1, licenses/TRMNL_FONT_LICENSE.txt).

The canvas header does not include the EPD driver so the same translation
units build on the host simulator.

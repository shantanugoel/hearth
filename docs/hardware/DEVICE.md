# This NOTE4 unit

Recorded from USB Serial/JTAG before any Hearth image was written.

The chip was **not** running the stock ZECTRIX beta demo. The as-found
image is BEACON (zectrix-note4-agent-mission-control): ESP-IDF v6.1
bootloader compiled 2026-09-07 17:34:01. Restore returns the board to
that image, not to the original demo.

| Field | Value |
|---|---|
| Product | ZECTRIX NOTE4 Developer Kit (monochrome), PCB V1.0 |
| Host serial | `/dev/ttyACM0` |
| USB | Espressif USB Serial/JTAG |
| Chip | ESP32-S3 (QFN56) revision v0.2 |
| Features | Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240 MHz, embedded PSRAM 8 MB (AP_3v3) |
| Crystal | 40 MHz |
| MAC | `28:84:85:32:07:e0` |
| Flash | 16 MB, manufacturer `46`, device `4018`, quad, 3.3 V (eFuse) |
| Secure Boot | Disabled |
| Flash encryption | Disabled |
| Factory dump | `backups/factory-flash-16MiB.bin` (gitignored) |
| Dump size | 16777216 bytes (16 MiB) |
| Dump SHA-256 | `365e8caebaa02d47f3067a93436cef830ff12075691aa64ea7680a6340615a47` |
| Mirror | `~/.local/share/hearth/factory-flash-16MiB.bin` |

Do not flash NOTE4C images on this unit. Restore playbook: [FACTORY_FLASH.md](../FACTORY_FLASH.md).

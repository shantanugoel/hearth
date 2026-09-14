# Factory flash dump and restore

The NOTE4 Developer Kit ships with beta demonstration firmware. Hearth replaces
that image. Before the first custom flash, dump the entire 16 MiB SPI flash,
checksum it, and keep a second copy off the working tree.

This playbook is **read-only against the chip** unless you pass the restore
safety latch. Do not restore-write as part of ordinary development.

## What is stored

| Artifact | Location | Git |
|---|---|---|
| 16 MiB dump | `backups/factory-flash-16MiB.bin` | gitignored |
| SHA-256 sidecar | `backups/factory-flash-16MiB.bin.sha256` | gitignored |
| Mirror copy | `~/.local/share/hearth/factory-flash-16MiB.bin` | outside the repo |

The dump includes bootloader, partition table, NVS, and the as-found firmware.
It is unique to this unit only in the sense that NVS may hold Wi-Fi or other
runtime state. The chip MAC is recorded in [hardware/DEVICE.md](hardware/DEVICE.md).

Known-good dump of this unit, taken 2026-09-14 before any Hearth image:

```
365e8caebaa02d47f3067a93436cef830ff12075691aa64ea7680a6340615a47  factory-flash-16MiB.bin
```

The bootloader in this dump identifies as ESP-IDF v6.1, compiled
2026-09-07 17:34:01 — BEACON, not the stock ZECTRIX beta demo. Restore
puts that image back.

Do **not** commit the `.bin`. It is a 16 MiB device image.

## Prerequisites

- NOTE4 on USB-C data (this machine: `/dev/ttyACM0`).
- User in the `uucp` group (or otherwise able to open the serial device).
- `esptool` from the ESP-IDF v6.1 EIM venv. The script finds it at
  `~/.espressif/tools/python/v6.1/venv/bin/esptool`. Override with `ESPTOOL=`.

If the port does not appear, hold the front BOOT / Confirm button, tap the
recessed RESET pinhole, then release BOOT.

## Commands

All commands default to this unit's MAC (`28:84:85:32:07:e0`) and refuse to
run against a different chip. Disable that check only with `--expect-mac ''`.

Identify:

```bash
python3 tools/note4_flash.py info
```

Dump (creates the gitignored 16 MiB file and a SHA-256 sidecar; also copies
to `~/.local/share/hearth/`):

```bash
python3 tools/note4_flash.py dump
```

Verify the file against the chip without writing:

```bash
python3 tools/note4_flash.py verify
```

Restore dry-run (checksum + chip + MAC, no `write-flash`):

```bash
python3 tools/note4_flash.py restore --dry-run
```

Restore write — **destructive**, only when you mean to put the factory image
back. The extra flag is required:

```bash
python3 tools/note4_flash.py restore --i-know-this-overwrites-the-device
```

After a restore write the script runs `verify-flash` itself.

## Direct esptool equivalents

These are the same operations if you are not using the wrapper. Replace
`ESPTOOL` with the binary from the IDF venv.

```bash
ESPTOOL=~/.espressif/tools/python/v6.1/venv/bin/esptool
PORT=/dev/ttyACM0

$ESPTOOL --chip esp32s3 --port $PORT flash-id

$ESPTOOL --chip esp32s3 --port $PORT --baud 921600 \
  read-flash 0x0 0x1000000 backups/factory-flash-16MiB.bin

sha256sum backups/factory-flash-16MiB.bin

$ESPTOOL --chip esp32s3 --port $PORT --baud 921600 \
  verify-flash --diff 0x0 backups/factory-flash-16MiB.bin

$ESPTOOL --chip esp32s3 --port $PORT --baud 921600 \
  write-flash --flash-mode keep --flash-freq keep --flash-size 16MB \
  0x0 backups/factory-flash-16MiB.bin
```

`verify-flash` is the integrity check. `sha256sum` of the local file is the
archive check. Both are required before you trust a dump.

## Safety

- Restore is opt-in. `restore` without `--i-know-this-overwrites-the-device`
  exits 1 and writes nothing.
- `--dry-run` never calls `write-flash`.
- The script refuses a dump or restore if `flash-id` does not report 16 MB, or
  if the MAC does not match `docs/hardware/DEVICE.md`.
- Flash encryption is disabled on this unit. A dump is plaintext firmware,
  not an encrypted blob.
- Do not flash NOTE4C images onto this board.

## After Hearth is installed

Keep the dump. Later steps replace the running image. The restore command
above is the way back to the as-found factory firmware.

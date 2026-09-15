# Hearth

Household working memory on a ZECTRIX NOTE4. Speak at the fridge; a dedicated
Hermes profile files what you said onto a kitchen poster. The e-paper holds
what the family needs to see without unlocking a phone.

This repository is the firmware, hub, and tools. The product plan is
[docs/PLAN.md](docs/PLAN.md).

## Status

- Step 0: factory flash dump and restore playbook. The 16 MiB as-found image is **not** in git. See [docs/FACTORY_FLASH.md](docs/FACTORY_FLASH.md).
- Step 1: ESP-IDF bring-up (display, buttons, power-hold, Wi-Fi scan, sleep). See [docs/BRINGUP.md](docs/BRINGUP.md).
- Step 2: hold-OK voice clip → hub STT → transcript on **Heard**. See [docs/VOICE.md](docs/VOICE.md).
- Step 3: `hearth` profile files a hub board; **Today** / **Buy** posters. See [docs/BOARD.md](docs/BOARD.md).
- Step 4: **Menu**, unified **Notes** (Do + Pack), owners-from-speech, Pulse.
  Telegram deferred. See [docs/BOARD.md](docs/BOARD.md).
- Step 5: fast local command routing, weather/agenda refresh, idle return,
  quiet visual acknowledgements, and the final high-contrast poster system.
- Kitchen posters: labeled top tabs, **Notes**, async filing on a bottom status bar,
  speaker alarm. Firmware `v0.6.0-hearth`.

## Hub

```bash
python3 -m hub --host 0.0.0.0 --port 8790
```

Copy `.env.example` to `.env` (gitignored). That file holds the local STT URL
and Open-Meteo coords (`HEARTH_LAT` / `HEARTH_LON`). The sample is Whitefield,
Bengaluru. The NOTE4 posts `http://<hub>:8790/v1/utterance`.
Install the kitchen profile files with `./tools/install_hearth_profile.sh`.

## Hardware

Monochrome NOTE4 Developer Kit, PCB V1.0, ESP32-S3 N16R8. Do not flash NOTE4C
images. Device identity for this unit: [docs/hardware/DEVICE.md](docs/hardware/DEVICE.md).

## Simulator

```bash
make -C sim run
```

## Firmware

```bash
./tools/idf.sh set-target esp32s3
./tools/idf.sh build
./tools/idf.sh -p /dev/ttyACM0 flash monitor
```

Flashing replaces the as-found image. Restore is documented in
[docs/FACTORY_FLASH.md](docs/FACTORY_FLASH.md).

## Factory dump

```bash
python3 tools/note4_flash.py info
python3 tools/note4_flash.py dump
python3 tools/note4_flash.py verify
python3 tools/note4_flash.py restore --dry-run
```

Restore-write is destructive and requires
`--i-know-this-overwrites-the-device`. Do not run it unless you intend to put
the factory image back.

## License

MIT. See [LICENSE](LICENSE).

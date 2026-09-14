# Board, Today, Buy

Step 3 of [PLAN.md](PLAN.md): the first family-useful day.

The hub owns a JSON board. After STT it asks the `hearth` Hermes profile
to file the utterance. The NOTE4 paints **Today** and **Buy** from a flat
poster JSON. Hub-rendered 16-gray bitmaps stay for step 5.

## What it does

| Screen | Job |
|---|---|
| Today | Date, weather, tonight's meal, then pack/do/buy lines |
| Buy | Open shopping list |
| Radio | STA status, scan list; short OK rescans |
| Power | Battery and the 3 s DOWN shutdown |

- **Hold OK:** listen until release, cap 12 s. Overlay `listening` then `sending`.
- After a turn the poster shows one ack line (`Added oat milk to Buy.`).
- **UP / DOWN:** wrap Today → Buy → Radio → Power.
- Completions are first-class (`we got eggs` ticks the Buy row).

## Board

Canonical store: `~/.local/share/hearth/board.json` (gitignored live file).
Hermes chat memory is not the source of truth.

```
GET  /v1/board
POST /v1/board/apply   {"ops":[...], "source":"fridge"}
GET  /v1/poster        flat keys the firmware can parse
POST /v1/utterance      STT, then file, then poster fields + ack
```

Apply ops: `add` / `complete` on `buy|do|pack`, and `set_menu`.

Weather is a hub Open-Meteo one-liner (`HEARTH_LAT` / `HEARTH_LON`).
If those are unset, Today shows `weather unknown`.

## Hermes

Profile alias `hearth` on `hermes-incus`. Install SOUL + skill:

```bash
./tools/install_hearth_profile.sh
```

The hub files with a fresh oneshot each time (`hearth --yolo --skills
hearth-board -z '…'`). The board is the source of truth, so a shared Hermes
session is not required. `--continue family-kitchen` is reserved for later
once that session exists.

## Hub

```bash
python3 -m hub --host 0.0.0.0 --port 8790
```

`--no-hermes` transcribes only. `--mock-hermes` files with a tiny heuristic
(for tests, not the kitchen).

`POST /v1/utterance` still takes 16 kHz PCM16 WAV. The JSON now also carries
`ack`, `date`, `weather`, `meal`, `n_buy` / `n_do` / `n_pack`, `t0`…`t5`,
`b0`…`b7`.

## Simulator

```bash
make -C sim run
```

![Today](img/01-today.png)
![Buy](img/02-buy.png)
![Radio](img/03-radio.png)
![Power](img/04-power.png)
![Listen](img/05-listen.png)

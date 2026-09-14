# Board, Today, Buy, Menu, Notes

Kitchen posters on the fridge. The hub owns JSON. Hermes files speech.
The NOTE4 paints from a flat poster. 16-gray hub bitmaps stay for later.

## What it does

| Screen | Job |
|---|---|
| Today | Date + weather icon, tonight's meal, Buy/Notes peeks, next alarm |
| Buy | Open shopping list, owner suffix when named |
| Menu | Mon–Sun dinners, today marked |
| Notes | Chores, bags, leftover thoughts |
| Pulse | Battery, Wi-Fi, last heard phrase, hub URL, alarm |

- Left spine is icon tabs (house, basket, plate, notes, radio).
- **Hold OK:** listen until release, cap 12 s. Overlay `listening` then `sending`.
- After a turn the poster shows one ack line, then Hermes files in the background.
- **UP / DOWN:** wrap Today → Buy → Menu → Notes → Pulse. ~20 s idle snaps to Today.
- Completions (`we got eggs`) and deletions (`take eggs off`) are first-class.
- Named people become an owner suffix. Unnamed items stay household-owned.
- **Alarm:** “set an alarm for 7 for school” stores `07:00`. The NOTE4 chimes on the ES8311 at that minute. Short OK dismisses.

## Board

Canonical store: `~/.local/share/hearth/board.json` (gitignored live file).
Hermes chat memory is not the source of truth.

```
GET  /v1/board
POST /v1/board/apply   {"ops":[...], "source":"fridge", "ack":"..."}
GET  /v1/poster        flat keys the firmware can parse
POST /v1/utterance      STT immediately, Hermes files in the background
```

Apply ops: `add` / `complete` / `delete` on `buy|notes`, plus `set_menu`,
`set_alarm`, `clear_alarm`. Old `do`/`pack` list names still file into Notes.

Weather is a hub Open-Meteo one-liner plus a `wx` token (`sun` / `cloud` /
`rain` / `storm` / `snow` / `fog`) for the icon. Coords live in gitignored
`.env` (`HEARTH_LAT` / `HEARTH_LON`).

## Hermes

The NOTE4 talks only to the hub. Hermes stays behind SSH on `hermes-incus`.
That keeps STT, weather, and the board on one LAN service, and it keeps
plain `hermes` / Telegram from becoming the fridge.

Profile alias `hearth` on `hermes-incus`. Install SOUL + skill:

```bash
./tools/install_hearth_profile.sh
```

The hub files with a fresh oneshot each time (`hearth --yolo --skills
hearth-board -z '…'`). `POST /v1/utterance` returns the transcript as soon as
STT finishes (`pending=1`). The device polls `/v1/poster` until Hermes
writes the ack.

## Hub

```bash
python3 -m hub --host 0.0.0.0 --port 8790
```

`--no-hermes` transcribes only. `--mock-hermes` files with a tiny heuristic
(for tests, not the kitchen).

`POST /v1/utterance` still takes 16 kHz PCM16 WAV. The JSON also carries
`ack`, `pending`, `date`, `weather`, `wx`, `meal`, `n_buy` / `n_notes`,
`b0`…`b7`, `n0`…`n7`, `m0`…`m6`, `alarm`, `ahh`, `amm`.

## Simulator

```bash
make -C sim run
```

![Today](img/01-today.png)
![Buy](img/02-buy.png)
![Menu](img/03-menu.png)
![Notes](img/04-notes.png)
![Pulse](img/05-pulse.png)
![Listen](img/06-listen.png)

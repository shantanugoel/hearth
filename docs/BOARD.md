# Board, Today, Buy, Menu, Notes

Kitchen posters on the fridge. The hub owns JSON. Its command router files
simple speech; Hermes files requests that need language interpretation.
The NOTE4 paints from a flat poster. The final UI is high-contrast 1-bit so
text stays crisp and status changes can use fast partial refreshes.

## What it does

| Screen | Job |
|---|---|
| Today | Date + weather icon, tonight's meal, Buy/Notes peeks, next alarm |
| Buy | Open shopping list, owner suffix when named |
| Menu | Mon–Sun dinners, today marked |
| Notes | Chores, bags, leftover thoughts |
| Pulse | Battery, Wi-Fi, last heard phrase, hub status, alarm |

- A labeled top rail uses house **Today**, `$` **Buy**, fork-and-knife **Menu**,
  paper **Notes**, and signal **Pulse**. The active poster is reversed in black.
- **Hold OK:** listen until release, cap 12 s. A bottom status bar says
  `listening`, then `sending`, then `filing`. The poster stays on screen.
- After STT the fridge returns to buttons. Hermes files in the background;
  the bar stays until `/v1/poster` says `pending=0`, then shows the ack for
  15 seconds. A new recording cannot start while that filing is active.
- **UP / DOWN:** wrap Today → Buy → Menu → Notes → Pulse while filing.
  ~20 s idle snaps to Today (the status bar stays if still filing).
- Simple additions, completions, deletions, dinner changes, and alarms take a
  deterministic fast path on the hub. `remove oat milk`, `delete oat milk`, and
  `take oat milk off the shopping list` never fall through to an add. Mixed or
  ambiguous speech still goes to Hermes.
- Named people become an owner suffix. Unnamed items stay household-owned.
- **Alarm:** “set an alarm for 7 for school” stores `07:00`. The NOTE4 chimes on the ES8311 at that minute. Short OK dismisses.

## Board

Canonical store: `~/.local/share/hearth/board.json` (gitignored live file).
Hermes chat memory is not the source of truth.

```
GET  /v1/board
POST /v1/board/apply   {"ops":[...], "source":"fridge", "ack":"..."}
GET  /v1/poster        flat keys the firmware can parse
POST /v1/utterance      STT + fast command, or agent filing in background
```

Apply ops: `add` / `complete` / `delete` on `buy|notes`, plus `set_menu`,
`set_alarm`, `clear_alarm`. Old `do`/`pack` list names still file into Notes.

Weather is a hub Open-Meteo one-liner plus a `wx` token (`sun` / `cloud` /
`rain` / `storm` / `snow` / `fog`) for the icon. Coords live in gitignored
`.env` (`HEARTH_LAT` / `HEARTH_LON`).

## Hermes

The NOTE4 talks only to the hub. Hermes stays behind SSH on `hermes-incus`.
The hub handles audio, weather, storage, and obvious commands; the dedicated
Hermes profile handles language that actually needs interpretation. This is
faster than sending every phrase to an agent and keeps the ESP32 thin.

Profile alias `hearth` on `hermes-incus`. Install SOUL + skill:

```bash
./tools/install_hearth_profile.sh
```

The remaining phrases use a fresh oneshot (`hearth --yolo --skills
hearth-board -z '…'`). The installer pins only this profile to
`openai-codex / gpt-5.6-luna` with reasoning disabled; default and sibling
Hermes profiles are untouched. `POST /v1/utterance` returns simple-command
results immediately. Agent-routed phrases return after STT with `pending=1`;
the device keeps the UI live and polls `/v1/poster` every 2 s for the ack.

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

Visual direction: [the Hearth e-paper concept](design/hearth-eink-concept.png).

![Today](img/01-today.png)
![Buy](img/02-buy.png)
![Menu](img/03-menu.png)
![Notes](img/04-notes.png)
![Pulse](img/05-pulse.png)
![Listen](img/06-listen.png)
![Filing](img/07-filing.png)
![Removed](img/08-removed.png)

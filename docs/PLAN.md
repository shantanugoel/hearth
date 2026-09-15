# Hearth — NOTE4 household working memory

A ZECTRIX NOTE4 on the fridge, serving as a persistent family todo / home manager. People press the front button and speak. A dedicated Hermes Agent profile files what they said into a household board. The e-paper holds what the family needs to see without unlocking a phone.

This document is the product and architecture plan. Build only after it is agreed.

## Hardware and machine

- Device: ZECTRIX NOTE4 (monochrome), ESP32-S3 N16R8, 4.2-inch 400 × 300 E Ink (SSD2683), 16 MB flash, 8 MB PSRAM.
- Peripherals: front BOOT / Confirm (GPIO0), side Up (GPIO39), side Down / Power (GPIO18), ES8311 mic + speaker, NFC, PCF8563 RTC, 2000+ mAh battery, magnetic back, USB-C native USB (`/dev/ttyACM0` on this machine).
- Connected as: Espressif USB JTAG/serial, serial `28:84:85:32:07:E0`.
- Host: an ESP-IDF v6.1 machine with reachable STT/TTS services and a local or SSH Hermes profile.

Do not flash NOTE4C images. Back up the full 16 MiB factory flash before the first custom image.

## Decisions (locked)

| Question | Choice |
|---|---|
| Product | Full household OS as a small set of strong posters, defaulting to Today's agenda. Beautiful e-ink, not a chat UI. |
| Firmware | ESP-IDF C. Official NOTE4 demo is the board bring-up base. Hub stays off-device. |
| Identity | Named in speech. No NFC tags in v1. Unnamed items are household-owned. |
| Second surface | Fridge + the same Hermes profile on Telegram / WhatsApp / CLI. |
| Orientation | Landscape 400 × 300 on the fridge. |
| Today weather | v1 Today always includes a weather one-liner, not optional. |
| Language routing | Every spoken command is interpreted by Hermes using native Hearth board tools. The hub accepts recordings and stores verified results. |
| Agent profile | Only `hearth` uses `openai-codex / gpt-5.6-luna` with reasoning off. Other Hermes profiles keep their settings. |
| Visual mode | Production posters use crisp 1-bit ink. The 16-gray driver remains available for imagery experiments. |

## What it is

**Hearth** — household working memory on a kitchen poster.

People do not pick a list. They say “we’re out of oat milk and pack Maya’s swim kit for Thursday.” Hermes splits that into Buy vs Notes, tags Maya, and the next glance shows it.

The fridge is one mouth. Chat is another. Both write the same board.

## What it is not

- A chat transcript on the panel.
- Twelve categories or nested on-device settings.
- Speaker identification or NFC identity tags in v1.
- A meal-plan generator that invents a week of dinners.
- An on-device LLM.
- A replacement for phones for long-form planning.

## Screens

400 × 300 holds about 8–12 lines. “Full household OS” means a small set of posters, not menus. Default after idle: **Today**.

| Screen | Job | Layout |
|---|---|---|
| **Today** | Kitchen glance | Date and weather, then the selected notes list; two Buy and two upcoming Menu items share a split footer. |
| **Buy** | Shopping | Selectable list with checkboxes. Named owners as a suffix. |
| **Menu** | Week of meals | Full weekday headings with Breakfast, Lunch, Dinner entries in order. |
| **Notes** | Household working memory | Selectable chores, bags, thoughts; checkboxes. |
| **Pulse** | Device health | Battery, Wi-Fi, last heard phrase, alarm, hub status. Last page, not daily. |

`today[]` is **derived**, not a fifth dump of todos: weather + prominently displayed Notes + a small Buy/Menu footer.

### Navigation

Three physical buttons, no nested settings on-device.

- **Front, press-and-hold:** speak until release. Always. Every screen.
- **Short front (no hold):** next page, wrapping after Pulse.
- **Up / Down:** move through items on the current page. After ~20 s idle, snap back to Today.
- **Long Up:** check or uncheck the selected note or Buy item.
- **Long Down:** delete the selected board item by exact ID/key.

Wi-Fi provisioning once via a setup AP. The hub owns config after that.

## Visual

E-ink is the feature. Design it like a kitchen print, not an app.

- **Landscape 400 × 300** on the fridge. The hub sends a flat poster payload;
  firmware renders it immediately with the shared bitmap type and icon system.
- **Pure black and white** for the final poster system. This keeps the bitmap
  type sharp and lets listening/filing feedback use partial refresh. The
  calibrated 16-gray panel path remains in the driver for future artwork.
- Type scale: huge date, one hero line, then a short list. Wide margins. Hairline rules. No icon chrome.
- Partial refresh for ticking an item. Full refresh when the screen changes.
- After a voice turn: one quiet confirmation line at the bottom
  (`Removed oat milk`), then settle back to the poster. Sound is reserved for
  alarms so the kitchen display does not speak after every interaction.

The firmware renders a compact flat poster payload from the hub. Keeping the
layout on-device makes navigation and filing feedback immediate and avoids
shipping full frames over Wi-Fi. The hub remains the source of content and
language behavior.

## Data model

Hermes chat memory is not the source of truth. A fridge that forgets milk after a context compact is a bad appliance.

Canonical store: a **board file** (YAML or SQLite) owned by the hub, mutated by a Hermes skill.

```text
board
  buy[]     text, optional owner, optional due/when, status, source
  menu[]    weekday, slot (breakfast/lunch/dinner), meal, optional notes
  notes[]   chores, bags, leftover thoughts; optional owner/when/kind
  alarms[]  one-shot due_at, hhmm, optional text
  today[]   derived, not stored as a fifth list
  weather   current condition + high/low, refreshed by the hub
  meta      last_utterance, last_ack, updated_at
```

Each item:

- `text` — what to show
- `owner` — person if named in speech, else unset (household)
- `when` — date/time if mentioned
- `status` — open | done
- `source` — fridge | telegram | whatsapp | cli | …

Completions are first-class (“we got eggs”, “pasta is done”).

## Hermes profile

Dedicated profile, working name `hearth`. Tight `SOUL.md`:

- You are the household chief of staff, not a chatbot.
- File utterances into the board. Split mixed sentences.
- Extract people when named; otherwise leave unassigned.
- Completions and deletions are first-class.
- Reply in one short sentence. The fridge cannot show a paragraph.
- Do not invent chores, meals, shops, or alarms.

Fridge and chat share one session key, e.g. `X-Hermes-Session-Key: family:kitchen`, so they share one thread of memory while the board remains canonical.

Telegram / WhatsApp / CLI hit the same profile and the same board. Someone at the shop can say “got milk”; the fridge redraws.

Local STT/TTS already exist. Use them. Do not send audio to a cloud ASR unless local fails.

## Architecture

```text
Note 4  --wifi-->  hub on this machine  -->  Hermes profile `hearth`
  mic/buttons         STT (local qwen)         skill writes board
  e-paper <---------  poster JSON              oneshot, not sticky
  speaker <---------  kitchen alarm chime
```

Voice turn:

1. Hold front button, speak, release.
2. Device uploads PCM to the hub.
3. Hub transcribes locally and returns immediately (`pending=1`).
4. The hub applies an obvious single command locally, or SSHes a `hearth`
   oneshot in the background for mixed/ambiguous language.
5. The selected path updates the board and returns a short ack.
6. Simple commands return in the STT response. For agent work, the device polls
   `/v1/poster` until `pending=0` and paints Today.
7. Kitchen alarms use the ES8311 speaker; ordinary acknowledgements stay quiet
   and visible in the bottom status line.

The device never talks to an LLM or to the Hermes HTTP API. Deep sleep between uses. E-paper keeps the last poster.

The device is a thin client: buttons, audio capture, display, sleep, Wi-Fi. All layout, lists, and language live on the hub.

## Repo layout (intended)

```text
firmware/          ESP-IDF C, NOTE4 board
hub/               STT, Hermes client, board store, renderer, device HTTP
hermes-profile/    SOUL.md, skill, sample board
docs/              this plan, later hardware notes
```

## Out of scope for v1

- NFC identity tags and speaker ID
- On-device settings UI
- Invented weekly meal plans
- A separate web editor (chat is the second surface)
- Running Hermes on the ESP32
- Portrait as the default orientation

## Open taste calls

None that block starting firmware bring-up. Weather source (hub fetch vs Hermes skill) can be decided during step 3.

## Build order

0. Dump the full 16 MiB factory flash before overwriting anything. Keep a tested restore path.
1. ESP-IDF bring-up from the official NOTE4 demo: display, buttons, power-hold, Wi-Fi, sleep.
2. Voice clip → hub → transcript shown on screen.
3. `hearth` profile + board + Today / Buy render. First family-useful day.
4. Menu, Do, Pack, owners-from-speech. Telegram on the same session is deferred
   (voice is the mouth for now).
5. Final poster polish: high-contrast layout, idle snap-back, quiet visual acks,
   Hermes board tools, queued voice calls, and weather + agenda refresh.

Steps 0–5 are implemented. Telegram remains a separate second-surface project;
it does not block the fridge appliance.

## References

- [NOTE4 hardware spec](https://wiki.zectrix.com/en/hardware/note/spec)
- [NOTE4 Developer Kit development guide](https://wiki.zectrix.com/en/software/note4-development-guide)
- [Official ESP-IDF reference firmware](https://github.com/itopinion/zectrix-note4-epd-demo)
- [Hermes profiles](https://hermes-agent.nousresearch.com/docs/user-guide/profiles)
- [Hermes API server](https://hermes-agent.nousresearch.com/docs/user-guide/features/api-server)

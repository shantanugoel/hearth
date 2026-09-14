---
name: hearth-board
description: File fridge speech into Buy, Notes, Menu, and Alarms.
version: 0.2.0
author: Hermes Agent
license: MIT
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [hearth, household, fridge, board, kitchen]
---

# hearth-board

File one household utterance into the hub board. The board is JSON on the
hub, not chat memory.

Hub default: `http://192.168.2.89:8790` (override with `HEARTH_HUB_URL`).

## When to Use

- A fridge / kitchen utterance needs filing
- Someone named a shop, a note, a bag to pack, a meal, an alarm, or a completion
- The user is the `hearth` profile acting as kitchen working memory

## Do this

1. `GET $HEARTH_HUB_URL/v1/board` (default `http://192.168.2.89:8790/v1/board`).
2. Decide ops. Split mixed sentences. Do not invent items that were not said.
3. `POST $HEARTH_HUB_URL/v1/board/apply` with JSON:

```json
{
  "source": "fridge",
  "ack": "Added oat milk to Buy.",
  "ops": [
    {"op": "add", "list": "buy", "text": "oat milk"},
    {"op": "add", "list": "notes", "text": "swim kit", "kind": "pack", "owner": "Maya", "when": "Thursday"},
    {"op": "add", "list": "notes", "text": "call school", "kind": "do", "owner": "Maya"},
    {"op": "add", "list": "notes", "text": "keys are in the blue bowl", "kind": "note"},
    {"op": "complete", "list": "buy", "text": "eggs"},
    {"op": "delete", "list": "notes", "text": "plumber"},
    {"op": "set_menu", "weekday": "thu", "meal": "dal rice"},
    {"op": "set_alarm", "hhmm": "07:00", "text": "school"},
    {"op": "clear_alarm", "text": "school"}
  ]
}
```

`list` is `buy` or `notes`. `kind` on notes is `do`, `pack`, or `note`.
Old `do`/`pack` list names still file into Notes.
For a named dinner: `{"op":"set_menu","weekday":"thu","meal":"dal rice"}`.
`weekday` is `mon`..`sun`. Completions use `complete` with `text` or `id`.
Removals use `delete` (take it off the board) not `complete`.
Always set `owner` when a person is named in the utterance.

4. Reply with **one short sentence** for the e-paper. Examples:
   - `Added oat milk to Buy.`
   - `Maya's swim kit is on Notes for Thursday.`
   - `Alarm at 7:00 for school.`
   - `Heard, nothing to file.`

## Rules

- People only when named. Unnamed items are household-owned (omit `owner`).
- "Maya needs X" / "pack Maya's Y" → Notes, `kind` pack, owner Maya.
- "We're out of X" / "get X" → `buy`.
- Chores → Notes, `kind` do. "Leave a note" / "remember X" / freeform → Notes, `kind` note.
- "Dinner is dal" / "tonight is pasta" → `set_menu` for that weekday.
- "We got X" / "X is done" → `complete`, never a new open item.
- "Take X off" / "remove X" / "delete X" / "forget X" → `delete`.
- "Set an alarm for 7" / "wake us at 7:30 for school" → `set_alarm`.
- "Cancel the alarm" / "no alarm" → `clear_alarm`.
- Do not add weather. The hub fetches weather.
- If the hub is unreachable, say `heard, not filed` and stop.

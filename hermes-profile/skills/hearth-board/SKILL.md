---
name: hearth-board
description: File fridge speech into Buy, Do, Pack, Menu.
version: 0.1.0
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
- Someone named a shop, a chore, a bag to pack, a meal, or a completion
- The user is the `hearth` profile acting as kitchen working memory

## Do this

1. `GET $HEARTH_HUB_URL/v1/board` (default `http://192.168.2.89:8790/v1/board`).
2. Decide ops. Split mixed sentences. Do not invent items that were not said.
3. `POST $HEARTH_HUB_URL/v1/board/apply` with JSON:

```json
{
  "source": "fridge",
  "ops": [
    {"op": "add", "list": "buy", "text": "oat milk"},
    {"op": "add", "list": "pack", "text": "swim kit", "owner": "Maya", "when": "Thursday"},
    {"op": "add", "list": "do", "text": "call school", "owner": "Maya"},
    {"op": "complete", "list": "buy", "text": "eggs"},
    {"op": "set_menu", "weekday": "thu", "meal": "dal rice"}
  ]
}
```

`list` is `buy`, `do`, or `pack`. For a named dinner: `{"op":"set_menu","weekday":"thu","meal":"dal rice"}`.
`weekday` is `mon`..`sun`. Completions use `complete` with `text` or `id`.
Always set `owner` when a person is named in the utterance.

4. Reply with **one short sentence** for the e-paper. Examples:
   - `Added oat milk to Buy.`
   - `Maya's swim kit is on Pack for Thursday.`
   - `Heard, nothing to file.`

## Rules

- People only when named. Unnamed items are household-owned (omit `owner`).
- "Maya needs X" / "pack Maya's Y" → set `owner` to Maya.
- "We're out of X" / "get X" → `buy`. "Pack Y" → `pack`. Chores → `do`.
- "Dinner is dal" / "tonight is pasta" → `set_menu` for that weekday.
- "We got X" / "X is done" → `complete`, never a new open item.
- Do not add weather. The hub fetches weather.
- If the hub is unreachable, say `heard, not filed` and stop.

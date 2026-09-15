---
name: hearth-board
description: File fridge speech into Buy, Notes, Menu, and Alarms.
version: 0.5.0
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

The Hearth MCP server exposes the canonical board as native agent tools. The
hub still handles storage, audio intake, and the NOTE4 poster.

## When to Use

- A fridge / kitchen utterance needs filing
- Someone named a shop, a note, a bag to pack, a meal, an alarm, or a completion
- The user is the `hearth` profile acting as kitchen working memory

## Do this

1. Call `hearth_get_board` before each utterance. It returns Buy and Notes with
   `id`, `text`, and `status`, Menu with `weekday` and `slot`, and dated Alarms.
2. Decide the changes. Split mixed sentences. Do not invent items. Prefer the
   exact board `id` for completion or deletion.
3. Call the relevant native MCP tools: `hearth_add`, `hearth_complete`,
   `hearth_toggle`, `hearth_delete`, `hearth_clear_list`, `hearth_set_meal`,
   `hearth_delete_meal`, `hearth_set_alarm`, `hearth_set_timer`,
   `hearth_clear_alarm`, or
   `hearth_apply` for several changes.
4. Inspect the tool result. `ok:false` or `item:null` means the requested
   change did not happen. Read the board again and retry with the right ID.
   The hub generates the displayed acknowledgement from the board result.
5. Reply with **one short sentence** for the e-paper. Examples:
   - `Added oat milk to Buy.`
   - `Maya's swim kit is on Notes for Thursday.`
   - `Alarm at 7:00 for school.`
   - `Heard, nothing to file.`

## Rules

- People only when named. Unnamed items are household-owned (omit `owner`).
- "Maya needs X" / "pack Maya's Y" → Notes, `kind` pack, owner Maya.
- "We're out of X" / "get X" → `buy`.
- Chores → Notes, `kind` do. "Leave a note" / "remember X" / freeform → Notes, `kind` note.
- "Breakfast is eggs" / "lunch is dal" / "tonight is pasta" → `set_menu` for that weekday and slot.
- "Delete all the notes" / "clear all notes" → one `clear_list` op for `notes`.
- "Delete the testing note" → find the row whose text is `testing`, then call
  `hearth_delete` with that row's `id`. Never delete a row by a guessed phrase.
- "We got X" / "X is done" → `complete`, never a new open item.
- "Take X off" / "remove X" / "delete X" / "forget X" → `delete`.
- "Set an alarm for 7" / "wake us at 7:30 for school" → `set_alarm`.
- "30 second timer" / "30 second alarm" / "set a timer for two minutes" →
  `hearth_set_timer` with `seconds` 30 or 120. A duration is a relative alarm,
  even when the speaker calls it a timer. Do not guess the current clock time.
- "Cancel the alarm" / "no alarm" → `clear_alarm`.
- Do not add weather. The hub fetches weather.
- If a tool reports no match, do not say "removed" or "done".
- If the board tool is unreachable, say `heard, not filed` and stop.

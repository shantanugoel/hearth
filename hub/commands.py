"""Fast, deterministic filing for simple kitchen commands.

Hermes still handles mixed or ambiguous language.  These patterns cover the
high-frequency commands where latency and destructive intent matter most.
"""

from __future__ import annotations

import re
import time

from hub.board import Board


def _target(raw: str) -> str:
    text = raw.strip(" \t\r\n.,!?;:")
    text = re.sub(r"\s+please$", "", text, flags=re.IGNORECASE)
    return re.sub(r"\s+", " ", text).strip()


def _label(text: str) -> str:
    return text[:1].upper() + text[1:] if text else "That"


def _matching_list(board: Board, text: str, preferred: str = "") -> str:
    needle = text.casefold()
    order = [preferred] if preferred in ("buy", "notes") else []
    order.extend(name for name in ("buy", "notes") if name not in order)
    for exact in (True, False):
        for name in order:
            for item in board.open_items(name):
                candidate = (item.get("text") or "").casefold()
                if (exact and candidate == needle) or (
                    not exact and (needle in candidate or candidate in needle)
                ):
                    return name
    return ""


def _list_hint(text: str) -> str:
    low = text.casefold()
    if "note" in low or "reminder" in low:
        return "notes"
    if "buy" in low or "shopping" in low:
        return "buy"
    return ""


def try_fast_command(board: Board, utterance: str, source: str = "fridge") -> str | None:
    """Apply one unambiguous command, or return ``None`` for Hermes.

    Multi-part utterances deliberately fall through so Hermes can split them.
    """
    text = re.sub(r"\s+", " ", (utterance or "").strip())
    if not text:
        return None
    if re.search(r"\b(?:and|then|also)\b", text, re.IGNORECASE):
        return None

    clear_notes = re.fullmatch(
        r"(?:please\s+)?(?:delete|remove|clear|erase)\s+all\s+(?:of\s+)?(?:the\s+)?notes?(?:\s+list)?(?:\s+please)?[.!?]?",
        text, re.IGNORECASE,
    )
    if clear_notes:
        count = board.clear_list("notes")
        return f"Deleted {count} notes." if count else "There are no notes to delete."

    remove_patterns = (
        r"^(?:please\s+)?(?:remove|delete|forget)\s+(.+?)(?:\s+from\s+(?:the\s+)?(?:buy|shopping|notes?|reminders?)(?:\s+list)?)?$",
        r"^(?:please\s+)?take\s+(.+?)\s+off(?:\s+(?:the\s+)?(?:buy|shopping|notes?|reminders?)?\s*(?:list|board)?)?$",
    )
    for pattern in remove_patterns:
        match = re.match(pattern, text, re.IGNORECASE)
        if match:
            target = _target(match.group(1))
            if re.match(r"^(?:the\s+)?alarm\b", target, re.IGNORECASE):
                break
            list_name = _matching_list(board, target, _list_hint(text))
            if not list_name:
                return f"{_label(target)} isn't on the board."
            removed = board.delete(list_name, text=target)
            shown = (removed or {}).get("text") or target
            return f"Removed {shown}."

    complete = re.match(
        r"^(?:please\s+)?(?:we\s+)?(?:got|bought|picked\s+up|finished)\s+(.+)$",
        text,
        re.IGNORECASE,
    )
    if complete:
        target = _target(complete.group(1))
        list_name = _matching_list(board, target, _list_hint(text))
        if not list_name:
            return f"{_label(target)} isn't on the board."
        item = board.complete(list_name, text=target)
        shown = (item or {}).get("text") or target
        return f"Done: {shown}."

    clear_alarm = re.match(
        r"^(?:please\s+)?(?:cancel|clear|delete|remove|turn\s+off)\s+(?:the\s+)?alarm(?:\s+(?:for|at)\s+(.+))?$",
        text,
        re.IGNORECASE,
    )
    if clear_alarm:
        detail = _target(clear_alarm.group(1) or "")
        alarm = None
        if detail:
            try:
                alarm = board.clear_alarm(hhmm=detail)
            except ValueError:
                alarm = board.clear_alarm(text=detail)
        else:
            alarm = board.clear_alarm()
        return "Alarm cleared." if alarm else "No matching alarm."

    set_alarm = re.match(
        r"^(?:please\s+)?(?:set\s+(?:an\s+)?alarm|wake\s+(?:us|me))\s+(?:for|at)\s+"
        r"(\d{1,2}(?::?\d{2})?\s*(?:am|pm)?)(?:\s+for\s+(.+))?$",
        text,
        re.IGNORECASE,
    )
    if set_alarm:
        detail = _target(set_alarm.group(2) or "")
        alarm = board.set_alarm(set_alarm.group(1), detail)
        suffix = f" for {detail}" if detail else ""
        return f"Alarm at {alarm['hhmm']}{suffix}."

    menu = re.match(
        r"^(?:please\s+)?(?:(mon(?:day)?|tue(?:sday)?|wed(?:nesday)?|thu(?:rsday)?|fri(?:day)?|sat(?:urday)?|sun(?:day)?)'?s?\s+)?"
        r"(breakfast|lunch|dinner|tonight(?:'s\s+dinner)?)\s+(?:is|will\s+be)\s+(.+)$",
        text,
        re.IGNORECASE,
    )
    if menu:
        day = (menu.group(1) or time.strftime("%a")).casefold()[:3]
        slot = menu.group(2).casefold()
        if slot.startswith("tonight"):
            slot = "dinner"
        meal = _target(menu.group(3))
        board.set_menu(day, meal, slot=slot)
        return f"Tonight: {meal}." if slot == "dinner" and not menu.group(1) else f"{day.title()} {slot}: {meal}."

    add_buy_patterns = (
        r"^(?:please\s+)?add\s+(.+?)\s+to\s+(?:the\s+)?(?:buy|shopping)(?:\s+list)?$",
        r"^(?:please\s+)?(?:buy|get)\s+(.+)$",
        r"^(?:please\s+)?we(?:'re|\s+are)\s+out\s+of\s+(.+)$",
        r"^(?:please\s+)?we\s+need\s+(.+)$",
    )
    for pattern in add_buy_patterns:
        match = re.match(pattern, text, re.IGNORECASE)
        if match:
            target = _target(match.group(1))
            existing = _matching_list(board, target, "buy") == "buy"
            item = board.add("buy", target, source=source)
            shown = item.get("text") or target
            if existing:
                return f"{_label(shown)} is already on Buy."
            return f"Added {shown} to Buy."

    return None

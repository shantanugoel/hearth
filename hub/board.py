"""Canonical household board. JSON on disk, not Hermes chat memory."""

from __future__ import annotations

import re
import threading
import time
from datetime import datetime, timedelta
from copy import deepcopy
from pathlib import Path
from typing import Any

LISTS = ("buy", "notes", "menu")
ITEM_LISTS = ("buy", "notes")
STATUSES = ("open", "done")
NOTE_KINDS = ("do", "pack", "note")
WEEKDAYS = ("mon", "tue", "wed", "thu", "fri", "sat", "sun")
WEEKDAY_LABELS = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")
MEAL_SLOTS = ("breakfast", "lunch", "dinner")
MEAL_HOURS = {"breakfast": 8, "lunch": 13, "dinner": 19}


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def empty_board() -> dict:
    return {
        "buy": [],
        "notes": [],
        "menu": [],
        "alarms": [],
        "weather": {"line": "", "fetched_at": "", "kind": "sun"},
        "meta": {
            "last_utterance": "",
            "last_ack": "",
            "ack_at": 0.0,
            "agent_ack": "",
            "agent_ack_run_id": "",
            "updated_at": "",
            "next_id": 1,
        },
    }


def _item(
    text: str,
    *,
    item_id: str,
    owner: str = "",
    when: str = "",
    status: str = "open",
    source: str = "fridge",
    kind: str = "",
) -> dict:
    row = {
        "id": item_id,
        "text": text.strip(),
        "owner": (owner or "").strip(),
        "when": (when or "").strip(),
        "status": status if status in STATUSES else "open",
        "source": source or "fridge",
        "added_at": _now(),
    }
    if kind:
        row["kind"] = kind if kind in NOTE_KINDS else "note"
    return row


def _list_key(name: str) -> str:
    name = (name or "").strip().casefold()
    if name in ("do", "pack"):
        return "notes"
    return name


def _item_reference(text: str, list_name: str) -> str:
    """Normalize spoken list nouns without changing the stored item text."""
    value = (text or "").strip().casefold().strip(" .,!?")
    value = re.sub(r"^(?:the|a|an)\s+", "", value)
    if list_name == "notes":
        value = re.sub(r"\s+(?:note|notes|reminder|reminders)$", "", value)
    else:
        value = re.sub(r"\s+(?:buy|shopping)\s+item$", "", value)
    return value.strip()


def parse_hhmm(raw: str) -> str:
    text = (raw or "").strip().casefold().replace(".", ":")
    match = re.search(r"(\d{1,2})(?::?(\d{2}))?\s*(am|pm)?", text)
    if not match:
        raise ValueError("need a time like 7:00 or 7am")
    hour = int(match.group(1))
    minute = int(match.group(2) or 0)
    ap = match.group(3)
    if ap == "pm" and hour < 12:
        hour += 12
    if ap == "am" and hour == 12:
        hour = 0
    if hour > 23 or minute > 59:
        raise ValueError("bad time")
    return f"{hour:02d}:{minute:02d}"


class Board:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = empty_board()
        self._io = threading.RLock()
        self.load()

    def load(self) -> None:
        with self._io:
            self._load_unlocked()

    def _load_unlocked(self) -> None:
        if not self.path.exists():
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self._save_unlocked()
            return
        raw = json_load(self.path)
        board = empty_board()
        migrated = False
        if isinstance(raw, dict):
            notes: list = []
            seen: set[str] = set()
            if isinstance(raw.get("notes"), list):
                for item in raw["notes"]:
                    if isinstance(item, dict):
                        notes.append(item)
                        if item.get("id"):
                            seen.add(str(item["id"]))
            for old, kind in (("do", "do"), ("pack", "pack")):
                rows = raw.get(old) or []
                if not rows:
                    continue
                migrated = True
                for item in rows:
                    if not isinstance(item, dict):
                        continue
                    item_id = str(item.get("id") or "")
                    if item_id and item_id in seen:
                        continue
                    row = dict(item)
                    row.setdefault("kind", kind)
                    notes.append(row)
                    if item_id:
                        seen.add(item_id)
            board["notes"] = notes
            if isinstance(raw.get("buy"), list):
                board["buy"] = raw["buy"]
            if isinstance(raw.get("menu"), list):
                board["menu"] = raw["menu"]
            if isinstance(raw.get("alarms"), list):
                board["alarms"] = raw["alarms"]
            if isinstance(raw.get("weather"), dict):
                line = raw["weather"].get("line") or ""
                board["weather"]["line"] = line
                board["weather"]["fetched_at"] = raw["weather"].get("fetched_at") or ""
                board["weather"]["kind"] = raw["weather"].get("kind") or weather_kind(line)
            if isinstance(raw.get("meta"), dict):
                board["meta"].update(raw["meta"])
        self.data = board
        if migrated:
            self._save_unlocked()

    def save(self) -> None:
        with self._io:
            self._save_unlocked()

    def _save_unlocked(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.data["meta"]["updated_at"] = _now()
        tmp = self.path.with_name(self.path.name + ".tmp")
        tmp.write_text(json_dumps(self.data) + "\n")
        tmp.replace(self.path)

    def snapshot(self) -> dict:
        return deepcopy(self.data)

    def _next_id(self, prefix: str) -> str:
        n = int(self.data["meta"].get("next_id") or 1)
        self.data["meta"]["next_id"] = n + 1
        return f"{prefix}{n}"

    def open_items(self, list_name: str) -> list[dict]:
        key = _list_key(list_name)
        return [i for i in self.data.get(key, []) if i.get("status") == "open"]

    def add(
        self,
        list_name: str,
        text: str,
        *,
        owner: str = "",
        when: str = "",
        source: str = "fridge",
        kind: str = "",
    ) -> dict:
        original = (list_name or "").strip().casefold()
        key = _list_key(list_name)
        if key not in ITEM_LISTS:
            raise ValueError(f"cannot add to {list_name}")
        if key == "notes" and not kind:
            kind = original if original in NOTE_KINDS else "note"
        text = text.strip()
        if not text:
            raise ValueError("empty item")
        for existing in self.open_items(key):
            if existing["text"].casefold() == text.casefold():
                owner = owner.strip()
                when = when.strip()
                changed = False
                if owner and not (existing.get("owner") or ""):
                    existing["owner"] = owner
                    changed = True
                if when and not (existing.get("when") or ""):
                    existing["when"] = when
                    changed = True
                if kind and not (existing.get("kind") or ""):
                    existing["kind"] = kind
                    changed = True
                if changed:
                    self.save()
                return existing
        prefix = "n" if key == "notes" else key[0]
        item = _item(
            text,
            item_id=self._next_id(prefix),
            owner=owner,
            when=when,
            source=source,
            kind=kind if key == "notes" else "",
        )
        self.data[key].append(item)
        self.save()
        return item

    def complete(self, list_name: str, *, item_id: str = "", text: str = "") -> dict | None:
        key = _list_key(list_name)
        if key not in ITEM_LISTS:
            raise ValueError(f"cannot complete {list_name}")
        needle = (text or "").strip().casefold()
        for item in self.data[key]:
            if item.get("status") != "open":
                continue
            if item_id and item.get("id") == item_id:
                item["status"] = "done"
                self.save()
                return item
            if needle and needle in (item.get("text") or "").casefold():
                item["status"] = "done"
                self.save()
                return item
        return None

    def toggle(self, list_name: str, *, item_id: str) -> dict | None:
        key = _list_key(list_name)
        if key not in ITEM_LISTS or not item_id:
            raise ValueError("toggle needs a list and item id")
        for item in self.data[key]:
            if item.get("id") == item_id:
                item["status"] = "open" if item.get("status") == "done" else "done"
                self.save()
                return item
        return None

    def clear_list(self, list_name: str) -> int:
        key = _list_key(list_name)
        if key not in ITEM_LISTS:
            raise ValueError("cannot clear that list")
        count = len(self.data[key])
        if count:
            self.data[key] = []
            self.save()
        return count

    def delete(self, list_name: str, *, item_id: str = "", text: str = "") -> dict | None:
        key = _list_key(list_name)
        if key not in ITEM_LISTS:
            raise ValueError(f"cannot delete {list_name}")
        rows = self.data[key]
        removed = next((row for row in rows if row.get("id") == item_id), None) if item_id else None
        if removed is None and not item_id and text:
            needle = _item_reference(text, key)
            exact = [row for row in rows if (row.get("text") or "").casefold() == needle]
            matches = exact or [row for row in rows if needle and needle in (row.get("text") or "").casefold()]
            if len(matches) == 1:
                removed = matches[0]
        if removed is not None:
            self.data[key] = [row for row in rows if row is not removed]
            self.save()
        return removed

    def delete_menu(self, *, key: str) -> dict | None:
        day, separator, slot = (key or "").partition("/")
        if not separator or day not in WEEKDAYS or slot not in MEAL_SLOTS:
            raise ValueError("menu delete needs weekday/slot")
        for row in self.data["menu"]:
            if row.get("weekday") == day and row.get("slot", "dinner") == slot:
                self.data["menu"] = [item for item in self.data["menu"] if item is not row]
                self.save()
                return row
        return None

    def set_menu(self, weekday: str, meal: str, notes: str = "", slot: str = "dinner") -> dict:
        day = weekday.strip().casefold()[:3]
        if day not in WEEKDAYS:
            raise ValueError("weekday must be mon..sun")
        slot = slot.strip().casefold()
        if slot not in MEAL_SLOTS:
            raise ValueError("meal slot must be breakfast, lunch, or dinner")
        entry = None
        for row in self.data["menu"]:
            if (row.get("weekday") or "").casefold()[:3] == day and row.get("slot", "dinner") == slot:
                entry = row
                break
        if entry is None:
            entry = {"weekday": day, "slot": slot, "meal": "", "notes": ""}
            self.data["menu"].append(entry)
        entry["slot"] = slot
        entry["meal"] = meal.strip()
        if notes:
            entry["notes"] = notes.strip()
        self.save()
        return entry

    def set_alarm(self, hhmm: str, text: str = "") -> dict:
        stamp = parse_hhmm(hhmm)
        label = (text or "").strip()
        now = datetime.now().astimezone()
        hour, minute = map(int, stamp.split(":"))
        due = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
        if due <= now:
            due += timedelta(days=1)
        for row in self.data["alarms"]:
            if (row.get("hhmm") == stamp and not label) or (
                label and (row.get("text") or "").casefold() == label.casefold()
            ):
                row["hhmm"] = stamp
                if label:
                    row["text"] = label
                row["enabled"] = True
                row["due_at"] = due.isoformat()
                self.save()
                return row
        item = {
            "id": self._next_id("a"),
            "hhmm": stamp,
            "text": label,
            "enabled": True,
            "due_at": due.isoformat(),
        }
        self.data["alarms"].append(item)
        self.save()
        return item

    def set_timer(self, seconds: int, text: str = "") -> dict:
        """A relative one-shot alarm, down to the second."""
        if isinstance(seconds, bool) or not isinstance(seconds, int) or not 1 <= seconds <= 86400:
            raise ValueError("timer seconds must be an integer from 1 to 86400")
        due = datetime.now().astimezone() + timedelta(seconds=seconds)
        if due.microsecond:
            due = due.replace(microsecond=0) + timedelta(seconds=1)
        item = {
            "id": self._next_id("a"),
            "hhmm": due.strftime("%H:%M"),
            "text": (text or "").strip() or "Timer",
            "enabled": True,
            "due_at": due.isoformat(),
            "duration_seconds": seconds,
        }
        self.data["alarms"].append(item)
        self.save()
        return item

    def clear_alarm(self, *, item_id: str = "", text: str = "", hhmm: str = "") -> dict | None:
        needle = (text or "").strip().casefold()
        stamp = parse_hhmm(hhmm) if hhmm else ""
        kept: list[dict] = []
        removed = None
        for row in self.data["alarms"]:
            if removed is None and (
                (item_id and row.get("id") == item_id)
                or (stamp and row.get("hhmm") == stamp)
                or (needle and needle in (row.get("text") or "").casefold())
            ):
                removed = row
                continue
            kept.append(row)
        if removed is None and not item_id and not needle and not stamp:
            if self.data["alarms"]:
                removed = self.data["alarms"][0]
                kept = self.data["alarms"][1:]
        if removed is not None:
            self.data["alarms"] = kept
            self.save()
        return removed

    def next_alarm(self, now_hm: str | None = None) -> dict | None:
        now = datetime.now().astimezone()
        if now_hm:
            hour, minute = map(int, now_hm.split(":"))
            now = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
        candidates = []
        expired = []
        for row in self.data.get("alarms") or []:
            if not row.get("enabled", True):
                continue
            raw = row.get("due_at")
            if raw:
                try:
                    due = datetime.fromisoformat(raw)
                except ValueError:
                    expired.append(row)
                    continue
                if due.tzinfo is None:
                    due = due.astimezone()
            else:
                # Old time-only alarms were never dated. Keep only future ones today.
                hour, minute = map(int, parse_hhmm(row.get("hhmm") or "").split(":"))
                due = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
                row["due_at"] = due.isoformat()
            if due + timedelta(seconds=70) < now:
                expired.append(row)
            else:
                candidates.append((due, row))
        if expired:
            self.data["alarms"] = [row for row in self.data["alarms"] if row not in expired]
            self.save()
        return min(candidates, key=lambda pair: pair[0])[1] if candidates else None

    def set_weather(self, line: str, kind: str = "") -> None:
        self.data["weather"]["line"] = line.strip()
        self.data["weather"]["kind"] = kind or weather_kind(line)
        self.data["weather"]["fetched_at"] = _now()
        self.save()

    def set_meta(self, utterance: str | None = None, ack: str | None = None) -> None:
        if utterance is not None:
            self.data["meta"]["last_utterance"] = utterance.strip()
        if ack is not None:
            self.data["meta"]["last_ack"] = ack.strip()
            self.data["meta"]["ack_at"] = time.time() if ack.strip() else 0.0
        self.save()

    def apply(self, ops: list[dict], *, source: str = "fridge") -> list[dict]:
        results: list[dict] = []
        for raw in ops:
            if not isinstance(raw, dict):
                continue
            op = (raw.get("op") or "").strip().casefold()
            if op == "add":
                item = self.add(
                    raw.get("list") or "buy",
                    raw.get("text") or "",
                    owner=raw.get("owner") or "",
                    when=raw.get("when") or "",
                    source=raw.get("source") or source,
                    kind=raw.get("kind") or "",
                )
                results.append({"op": "add", "list": _list_key(raw.get("list") or "buy"), "item": item})
            elif op == "complete":
                item = self.complete(
                    raw.get("list") or "buy",
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                )
                results.append({"op": "complete", "list": _list_key(raw.get("list") or "buy"), "item": item})
            elif op == "toggle":
                item = self.toggle(raw.get("list") or "notes", item_id=raw.get("id") or "")
                results.append({"op": "toggle", "list": _list_key(raw.get("list") or "notes"), "item": item})
            elif op == "clear_list":
                count = self.clear_list(raw.get("list") or "notes")
                results.append({"op": "clear_list", "list": _list_key(raw.get("list") or "notes"), "count": count})
            elif op == "delete":
                item = self.delete(
                    raw.get("list") or "notes",
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                )
                results.append({"op": "delete", "list": _list_key(raw.get("list") or "notes"), "item": item})
            elif op == "delete_menu":
                item = self.delete_menu(key=raw.get("key") or "")
                results.append({"op": "delete_menu", "item": item})
            elif op == "set_menu":
                entry = self.set_menu(raw.get("weekday") or "", raw.get("meal") or "", slot=raw.get("slot") or "dinner")
                results.append({"op": "set_menu", "item": entry})
            elif op == "set_alarm":
                item = self.set_alarm(raw.get("hhmm") or raw.get("text") or "", raw.get("text") or "")
                results.append({"op": "set_alarm", "item": item})
            elif op == "set_timer":
                item = self.set_timer(raw.get("seconds"), raw.get("text") or "")
                results.append({"op": "set_timer", "item": item})
            elif op == "clear_alarm":
                item = self.clear_alarm(
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                    hhmm=raw.get("hhmm") or "",
                )
                results.append({"op": "clear_alarm", "item": item})
            else:
                raise ValueError(f"unknown op {op!r}")
        return results


    def tonight_meal(self, weekday: str | None = None) -> str:
        day = (weekday or time.strftime("%a")).casefold()[:3]
        for row in self.data["menu"]:
            if (row.get("weekday") or "").casefold()[:3] == day and row.get("slot", "dinner") == "dinner":
                return (row.get("meal") or "").strip()
        return ""

    def today_lines(self, *, limit: int = 6) -> list[str]:
        lines: list[str] = []
        for item in self.open_items("notes")[:4]:
            if len(lines) >= limit:
                break
            lines.append(item_label(item))
        for item in self.open_items("buy")[:2]:
            if len(lines) >= limit:
                break
            lines.append(item_label(item))
        meal = self.tonight_meal()
        if meal and len(lines) < limit:
            lines.append(meal)
        return lines[:limit]

    def counts(self) -> dict[str, int]:
        return {
            "buy": len(self.open_items("buy")),
            "notes": len(self.open_items("notes")),
        }


def ack_for_results(results: list[dict]) -> tuple[bool, str]:
    """Only acknowledge changes confirmed by the canonical board."""
    if not results:
        return True, "Heard, nothing to file."
    failures = [result for result in results if result.get("op") in
                ("complete", "toggle", "delete", "delete_menu", "clear_alarm")
                and result.get("item") is None]
    successes = len(results) - len(failures)
    if failures:
        if successes:
            return False, f"Filed {successes} changes; {len(failures)} could not be matched."
        return False, "No matching board item found."
    if len(results) > 1:
        return True, f"Filed {len(results)} changes."
    result = results[0]
    op = result["op"]
    item = result.get("item") or {}
    text = item.get("text") or ""
    if op == "add":
        return True, f"Added {text} to {'Buy' if result.get('list') == 'buy' else 'Notes'}."
    if op == "complete":
        return True, f"Done: {text}."
    if op == "toggle":
        return True, f"{'Checked' if item.get('status') == 'done' else 'Unchecked'} {text}."
    if op == "delete":
        return True, f"Removed {text}."
    if op == "delete_menu":
        return True, f"Removed {item.get('weekday', '').title()} {item.get('slot', 'dinner')}."
    if op == "clear_list":
        count = result.get("count") or 0
        return True, f"Deleted {count} {result.get('list') or 'notes'} items."
    if op == "set_menu":
        return True, f"{item.get('weekday', '').title()} {item.get('slot', 'dinner')}: {item.get('meal', '')}."
    if op == "set_alarm":
        return True, f"Alarm at {item.get('hhmm', '')}."
    if op == "set_timer":
        seconds = item.get("duration_seconds") or 0
        amount = f"{seconds // 60} minute{'s' if seconds != 60 else ''}" if seconds % 60 == 0 else f"{seconds} second{'s' if seconds != 1 else ''}"
        return True, f"Alarm in {amount}."
    if op == "clear_alarm":
        return True, "Alarm cleared."
    return True, "Filed."


def json_load(path: Path) -> Any:
    import json

    return json.loads(path.read_text(encoding="utf-8"))


def json_dumps(data: dict) -> str:
    import json

    return json.dumps(data, indent=2, ensure_ascii=False)


_WMO = {
    0: "clear",
    1: "fair",
    2: "partly cloudy",
    3: "overcast",
    45: "fog",
    48: "fog",
    51: "drizzle",
    53: "drizzle",
    55: "drizzle",
    61: "rain",
    63: "rain",
    65: "rain",
    71: "snow",
    80: "showers",
    81: "showers",
    82: "showers",
    95: "storm",
    96: "storm",
    99: "storm",
}

_KIND_FROM_WORD = (
    ("storm", "storm"),
    ("thunder", "storm"),
    ("snow", "snow"),
    ("fog", "fog"),
    ("drizzle", "rain"),
    ("shower", "rain"),
    ("rain", "rain"),
    ("partly", "partly"),
    ("overcast", "cloud"),
    ("cloud", "cloud"),
    ("fair", "sun"),
    ("clear", "sun"),
)


def weather_kind(line: str = "", code: int | None = None) -> str:
    if code is not None:
        word = _WMO.get(int(code), "")
        return weather_kind(word)
    low = (line or "").casefold()
    for word, kind in _KIND_FROM_WORD:
        if word in low:
            return kind
    return "sun"


def weather_line(
    code: int,
    temp_c: float,
    high: float | None = None,
    low: float | None = None,
) -> str:
    word = _WMO.get(int(code), "outside")
    if high is not None and low is not None:
        return f"{int(round(temp_c))}C  {word}  {int(round(low))}-{int(round(high))}"
    return f"{int(round(temp_c))}C  {word}"


def item_label(item: dict) -> str:
    text = item.get("text") or ""
    owner = item.get("owner") or ""
    when = item.get("when") or ""
    suffix = "  ".join(p for p in (owner, when) if p)
    return f"{text}  {suffix}".strip() if suffix else text


def poster_from_board(
    board: Board, *, pending: bool = False, buy_offset: int = 0, notes_offset: int = 0,
    now: datetime | None = None,
) -> dict[str, Any]:
    """Flat JSON the firmware can pick apart with HearthJsonString."""
    date = time.strftime("%a ") + str(int(time.strftime("%d"))) + time.strftime(" %b")
    weather = (board.data.get("weather") or {}).get("line") or ""
    kind = (board.data.get("weather") or {}).get("kind") or weather_kind(weather)
    meal = board.tonight_meal()
    counts = board.counts()
    meta = board.data.get("meta") or {}
    now = now or datetime.now().astimezone()
    ack = meta.get("last_ack") or ""
    ack_at = float(meta.get("ack_at") or 0.0)
    if ack and (ack_at <= 0.0 or time.time() - ack_at > 20.0):
        ack = ""
    buy_offset = max(0, min(int(buy_offset), max(0, len(board.data["buy"]) - 1)))
    notes_offset = max(0, min(int(notes_offset), max(0, len(board.data["notes"]) - 1)))
    buy = board.data["buy"][buy_offset:buy_offset + 12]
    notes = board.data["notes"][notes_offset:notes_offset + 12]
    meals = {
        ((row.get("weekday") or "").casefold()[:3], row.get("slot", "dinner")): (row.get("meal") or "").strip()
        for row in board.data.get("menu") or []
    }
    alarm = board.next_alarm()
    payload: dict[str, Any] = {
        "date": date,
        "clock": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "weather": weather,
        "wx": kind,
        "meal": meal,
        "ack": ack,
        "pending": "1" if pending else "0",
        "heard": meta.get("last_utterance") or "",
        "n_buy": str(counts["buy"]),
        "n_notes": str(counts["notes"]),
        "r_buy": str(len(board.data["buy"])),
        "r_notes": str(len(board.data["notes"])),
        "buy_offset": str(buy_offset),
        "notes_offset": str(notes_offset),
    }
    if alarm:
        hhmm = alarm.get("hhmm") or ""
        due_at = alarm.get("due_at") or ""
        second = due_at[17:19] if len(due_at) >= 19 else "00"
        display_time = f"{hhmm}:{second}" if alarm.get("duration_seconds") else hhmm
        label = "  ".join(p for p in (display_time, alarm.get("text") or "") if p)
        payload["alarm"] = label
        payload["aid"] = alarm.get("id") or ""
        payload["adate"] = (alarm.get("due_at") or "")[:10]
        if ":" in hhmm:
            hour, minute = hhmm.split(":", 1)
            payload["ahh"] = str(int(hour))
            payload["amm"] = str(int(minute))
            payload["asec"] = str(int(second))
    else:
        payload["alarm"] = ""
        payload["aid"] = ""
        payload["adate"] = ""
    for i in range(12):
        for prefix, rows in (("b", buy), ("n", notes)):
            row = rows[i] if i < len(rows) else None
            payload[f"{prefix}{i}"] = item_label(row) if row else ""
            payload[f"{prefix}id{i}"] = row.get("id", "") if row else ""
            payload[f"{prefix}s{i}"] = row.get("status", "open") if row else ""
    open_buy = board.open_items("buy")[:2]
    for i in range(2):
        payload[f"pb{i}"] = item_label(open_buy[i]) if i < len(open_buy) else ""
    menu_lines: list[tuple[str, str, str]] = []
    for key in WEEKDAYS:
        for slot in MEAL_SLOTS:
            dish = meals.get((key, slot), "")
            if dish:
                menu_lines.append((f"{key}/{slot}", f"{slot.title()}: {dish}", dish))
    for i in range(21):
        payload[f"m{i}"] = menu_lines[i][1] if i < len(menu_lines) else ""
        payload[f"mid{i}"] = menu_lines[i][0] if i < len(menu_lines) else ""
    def next_occurrence(row: tuple[str, str, str]) -> datetime:
        day, slot = row[0].split("/")
        delta = (WEEKDAYS.index(day) - now.weekday()) % 7
        due = (now + timedelta(days=delta)).replace(
            hour=MEAL_HOURS[slot], minute=0, second=0, microsecond=0
        )
        return due if due > now else due + timedelta(days=7)

    upcoming = sorted(menu_lines, key=next_occurrence)
    for i in range(2):
        payload[f"pm{i}"] = upcoming[i][1] if i < len(upcoming) else ""
    return payload

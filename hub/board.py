"""Canonical household board. JSON on disk, not Hermes chat memory."""

from __future__ import annotations

import re
import threading
import time
from copy import deepcopy
from pathlib import Path
from typing import Any

LISTS = ("buy", "notes", "menu")
ITEM_LISTS = ("buy", "notes")
STATUSES = ("open", "done")
NOTE_KINDS = ("do", "pack", "note")
WEEKDAYS = ("mon", "tue", "wed", "thu", "fri", "sat", "sun")
WEEKDAY_LABELS = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")


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

    def delete(self, list_name: str, *, item_id: str = "", text: str = "") -> dict | None:
        key = _list_key(list_name)
        if key not in ITEM_LISTS:
            raise ValueError(f"cannot delete {list_name}")
        needle = (text or "").strip().casefold()
        kept: list[dict] = []
        removed = None
        for item in self.data[key]:
            if removed is None and (
                (item_id and item.get("id") == item_id)
                or (needle and needle in (item.get("text") or "").casefold())
            ):
                removed = item
                continue
            kept.append(item)
        if removed is not None:
            self.data[key] = kept
            self.save()
        return removed

    def set_menu(self, weekday: str, meal: str, notes: str = "") -> dict:
        day = weekday.strip().casefold()[:3]
        if day not in WEEKDAYS:
            raise ValueError("weekday must be mon..sun")
        entry = None
        for row in self.data["menu"]:
            if (row.get("weekday") or "").casefold()[:3] == day:
                entry = row
                break
        if entry is None:
            entry = {"weekday": day, "meal": "", "notes": ""}
            self.data["menu"].append(entry)
        entry["meal"] = meal.strip()
        if notes:
            entry["notes"] = notes.strip()
        self.save()
        return entry

    def set_alarm(self, hhmm: str, text: str = "") -> dict:
        stamp = parse_hhmm(hhmm)
        label = (text or "").strip()
        for row in self.data["alarms"]:
            if (row.get("hhmm") == stamp and not label) or (
                label and (row.get("text") or "").casefold() == label.casefold()
            ):
                row["hhmm"] = stamp
                if label:
                    row["text"] = label
                row["enabled"] = True
                self.save()
                return row
        item = {
            "id": self._next_id("a"),
            "hhmm": stamp,
            "text": label,
            "enabled": True,
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
        rows = [a for a in self.data.get("alarms") or [] if a.get("enabled", True)]
        if not rows:
            return None
        rows = sorted(rows, key=lambda a: a.get("hhmm") or "")
        now_hm = now_hm or time.strftime("%H:%M")
        for row in rows:
            if (row.get("hhmm") or "") >= now_hm:
                return row
        return rows[0]

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
                results.append({"op": "add", "item": item})
            elif op == "complete":
                item = self.complete(
                    raw.get("list") or "buy",
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                )
                results.append({"op": "complete", "item": item})
            elif op == "delete":
                item = self.delete(
                    raw.get("list") or "notes",
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                )
                results.append({"op": "delete", "item": item})
            elif op == "set_menu":
                entry = self.set_menu(raw.get("weekday") or "", raw.get("meal") or "")
                results.append({"op": "set_menu", "item": entry})
            elif op == "set_alarm":
                item = self.set_alarm(raw.get("hhmm") or raw.get("text") or "", raw.get("text") or "")
                results.append({"op": "set_alarm", "item": item})
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
            if (row.get("weekday") or "").casefold()[:3] == day:
                return (row.get("meal") or "").strip()
        return ""

    def today_lines(self, *, limit: int = 6) -> list[str]:
        lines: list[str] = []
        meal = self.tonight_meal()
        if meal:
            lines.append(meal)
        for item in self.open_items("notes")[:2]:
            if len(lines) >= limit:
                break
            lines.append(item_label(item))
        for item in self.open_items("buy")[:2]:
            if len(lines) >= limit:
                break
            lines.append(item_label(item))
        return lines[:limit]

    def counts(self) -> dict[str, int]:
        return {
            "buy": len(self.open_items("buy")),
            "notes": len(self.open_items("notes")),
        }


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


def poster_from_board(board: Board, *, pending: bool = False) -> dict[str, Any]:
    """Flat JSON the firmware can pick apart with HearthJsonString."""
    date = time.strftime("%a ") + str(int(time.strftime("%d"))) + time.strftime(" %b")
    weather = (board.data.get("weather") or {}).get("line") or ""
    kind = (board.data.get("weather") or {}).get("kind") or weather_kind(weather)
    meal = board.tonight_meal()
    counts = board.counts()
    ack = (board.data.get("meta") or {}).get("last_ack") or ""
    buy = [item_label(item) for item in board.open_items("buy")[:8]]
    notes = [item_label(item) for item in board.open_items("notes")[:8]]
    meals = {
        (row.get("weekday") or "").casefold()[:3]: (row.get("meal") or "").strip()
        for row in board.data.get("menu") or []
    }
    today_key = time.strftime("%a").casefold()[:3]
    alarm = board.next_alarm()
    payload: dict[str, Any] = {
        "date": date,
        "weather": weather,
        "wx": kind,
        "meal": meal,
        "ack": ack,
        "pending": "1" if pending else "0",
        "n_buy": str(counts["buy"]),
        "n_notes": str(counts["notes"]),
    }
    if alarm:
        hhmm = alarm.get("hhmm") or ""
        label = "  ".join(p for p in (hhmm, alarm.get("text") or "") if p)
        payload["alarm"] = label
        if ":" in hhmm:
            hour, minute = hhmm.split(":", 1)
            payload["ahh"] = str(int(hour))
            payload["amm"] = str(int(minute))
    else:
        payload["alarm"] = ""
    for i in range(8):
        payload[f"b{i}"] = buy[i] if i < len(buy) else ""
    for i in range(8):
        payload[f"n{i}"] = notes[i] if i < len(notes) else ""
    for i, (key, label) in enumerate(zip(WEEKDAYS, WEEKDAY_LABELS)):
        mark = "*" if key == today_key else " "
        dish = meals.get(key, "")
        payload[f"m{i}"] = f"{mark}{label}  {dish}".rstrip()
    return payload

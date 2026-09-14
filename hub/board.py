"""Canonical household board. JSON on disk, not Hermes chat memory."""

from __future__ import annotations

import json
import time
from copy import deepcopy
from pathlib import Path
from typing import Any

LISTS = ("buy", "do", "pack", "menu")
STATUSES = ("open", "done")
WEEKDAYS = ("mon", "tue", "wed", "thu", "fri", "sat", "sun")
WEEKDAY_LABELS = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def empty_board() -> dict:
    return {
        "buy": [],
        "do": [],
        "pack": [],
        "menu": [],
        "weather": {"line": "", "fetched_at": ""},
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
) -> dict:
    return {
        "id": item_id,
        "text": text.strip(),
        "owner": (owner or "").strip(),
        "when": (when or "").strip(),
        "status": status if status in STATUSES else "open",
        "source": source or "fridge",
        "added_at": _now(),
    }


class Board:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = empty_board()
        self.load()

    def load(self) -> None:
        if not self.path.exists():
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.save()
            return
        raw = json.loads(self.path.read_text(encoding="utf-8"))
        board = empty_board()
        if isinstance(raw, dict):
            for key in LISTS:
                if isinstance(raw.get(key), list):
                    board[key] = raw[key]
            if isinstance(raw.get("weather"), dict):
                board["weather"].update(
                    {k: raw["weather"].get(k, "") for k in ("line", "fetched_at")}
                )
            if isinstance(raw.get("meta"), dict):
                board["meta"].update(raw["meta"])
        self.data = board

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.data["meta"]["updated_at"] = _now()
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data, indent=2, ensure_ascii=False) + "\n")
        tmp.replace(self.path)

    def snapshot(self) -> dict:
        return deepcopy(self.data)

    def _next_id(self, prefix: str) -> str:
        n = int(self.data["meta"].get("next_id") or 1)
        self.data["meta"]["next_id"] = n + 1
        return f"{prefix}{n}"

    def open_items(self, list_name: str) -> list[dict]:
        return [i for i in self.data.get(list_name, []) if i.get("status") == "open"]

    def add(
        self,
        list_name: str,
        text: str,
        *,
        owner: str = "",
        when: str = "",
        source: str = "fridge",
    ) -> dict:
        if list_name not in ("buy", "do", "pack"):
            raise ValueError(f"cannot add to {list_name}")
        text = text.strip()
        if not text:
            raise ValueError("empty item")
        for existing in self.open_items(list_name):
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
                if changed:
                    self.save()
                return existing
        item = _item(
            text,
            item_id=self._next_id(list_name[0]),
            owner=owner,
            when=when,
            source=source,
        )
        self.data[list_name].append(item)
        self.save()
        return item

    def complete(self, list_name: str, *, item_id: str = "", text: str = "") -> dict | None:
        if list_name not in ("buy", "do", "pack"):
            raise ValueError(f"cannot complete {list_name}")
        needle = (text or "").strip().casefold()
        for item in self.data[list_name]:
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

    def set_weather(self, line: str) -> None:
        self.data["weather"]["line"] = line.strip()
        self.data["weather"]["fetched_at"] = _now()
        self.save()

    def set_meta(self, utterance: str = "", ack: str = "") -> None:
        if utterance:
            self.data["meta"]["last_utterance"] = utterance.strip()
        if ack:
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
                )
                results.append({"op": "add", "item": item})
            elif op == "complete":
                item = self.complete(
                    raw.get("list") or "buy",
                    item_id=raw.get("id") or "",
                    text=raw.get("text") or "",
                )
                results.append({"op": "complete", "item": item})
            elif op == "set_menu":
                entry = self.set_menu(raw.get("weekday") or "", raw.get("meal") or "")
                results.append({"op": "set_menu", "item": entry})
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

        def label(item: dict) -> str:
            return item_label(item)

        for item in self.open_items("pack") + self.open_items("do"):
            if len(lines) >= limit:
                break
            lines.append(label(item))
        for item in self.open_items("buy"):
            if len(lines) >= limit:
                break
            lines.append(label(item))
        return lines[:limit]

    def counts(self) -> dict[str, int]:
        return {
            "buy": len(self.open_items("buy")),
            "do": len(self.open_items("do")),
            "pack": len(self.open_items("pack")),
        }


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


def poster_from_board(board: Board) -> dict[str, Any]:
    """Flat JSON the firmware can pick apart with HearthJsonString."""
    date = time.strftime("%a ") + str(int(time.strftime("%d"))) + time.strftime(" %b")
    weather = (board.data.get("weather") or {}).get("line") or ""
    meal = board.tonight_meal()
    counts = board.counts()
    ack = (board.data.get("meta") or {}).get("last_ack") or ""
    today = board.today_lines()
    buy = [item_label(item) for item in board.open_items("buy")[:8]]
    do = [item_label(item) for item in board.open_items("do")[:6]]
    pack = [item_label(item) for item in board.open_items("pack")[:6]]
    meals = {
        (row.get("weekday") or "").casefold()[:3]: (row.get("meal") or "").strip()
        for row in board.data.get("menu") or []
    }
    today_key = time.strftime("%a").casefold()[:3]
    payload: dict[str, Any] = {
        "date": date,
        "weather": weather,
        "meal": meal,
        "ack": ack,
        "n_buy": str(counts["buy"]),
        "n_do": str(counts["do"]),
        "n_pack": str(counts["pack"]),
    }
    for i in range(6):
        payload[f"t{i}"] = today[i] if i < len(today) else ""
    for i in range(8):
        payload[f"b{i}"] = buy[i] if i < len(buy) else ""
    for i in range(6):
        payload[f"d{i}"] = do[i] if i < len(do) else ""
    for i in range(6):
        payload[f"p{i}"] = pack[i] if i < len(pack) else ""
    for i, (key, label) in enumerate(zip(WEEKDAYS, WEEKDAY_LABELS)):
        mark = "*" if key == today_key else " "
        dish = meals.get(key, "")
        payload[f"m{i}"] = f"{mark}{label}  {dish}".rstrip()
    return payload


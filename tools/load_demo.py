#!/usr/bin/env python3
"""Replace the live hub board with a varied Hearth showcase after backing it up."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
WEEK_MENU = {
    "mon": ("Yogurt and berries", "Dal rice", "Pasta and salad"),
    "tue": ("Toast and eggs", "Veg wraps", "Paneer curry"),
    "wed": ("Oats and banana", "Chickpea bowls", "Noodle soup"),
    "thu": ("Idli and chutney", "Lemon rice", "Roast vegetables"),
    "fri": ("Fruit and granola", "Sandwiches", "Pizza night"),
    "sat": ("Pancakes", "Leftover bowls", "Tacos"),
    "sun": ("Poha", "Family lunch", "Khichdi"),
}


def demo_ops(existing: dict) -> list[dict]:
    ops: list[dict] = [
        {"op": "clear_list", "list": "buy"},
        {"op": "clear_list", "list": "notes"},
    ]
    for row in existing.get("menu") or []:
        ops.append({"op": "delete_menu", "key": f"{row['weekday']}/{row.get('slot', 'dinner')}"})
    for row in existing.get("alarms") or []:
        ops.append({"op": "clear_alarm", "id": row["id"]})

    for text in (
        "Oat milk", "Eggs", "Bananas", "Rice", "Tomatoes", "Coffee beans",
        "Dish soap", "Fresh flowers", "Bread", "Olive oil",
    ):
        ops.append({"op": "add", "list": "buy", "text": text})
    for text in ("Eggs", "Bread"):
        ops.append({"op": "complete", "list": "buy", "text": text})

    for kind, text, owner, when in (
        ("pack", "Swim kit", "Maya", "Thursday"),
        ("do", "Call school", "Arun", "Today"),
        ("note", "Leave bags by the door", "", ""),
        ("do", "Water the plants", "", "Saturday"),
        ("pack", "Library books", "Maya", "Friday"),
        ("note", "Grandma visits on Sunday", "", "Sunday"),
        ("do", "Book dentist appointment", "Arun", "Next week"),
        ("pack", "Rain jackets", "", "Tomorrow"),
        ("note", "Spare key is in the drawer", "", ""),
        ("do", "Put recycling out", "", "Wednesday"),
    ):
        ops.append({"op": "add", "list": "notes", "kind": kind,
                    "text": text, "owner": owner, "when": when})
    for text in ("Leave bags by the door", "Put recycling out"):
        ops.append({"op": "complete", "list": "notes", "text": text})

    for weekday, dishes in WEEK_MENU.items():
        for slot, dish in zip(("breakfast", "lunch", "dinner"), dishes):
            ops.append({"op": "set_menu", "weekday": weekday, "slot": slot, "meal": dish})
    ops.append({"op": "set_alarm", "hhmm": "07:00", "text": "School morning"})
    return ops


def request(hub: str, path: str, payload: dict | None = None) -> dict:
    body = json.dumps(payload).encode() if payload is not None else None
    req = Request(hub.rstrip("/") + path, data=body,
                  headers={"Content-Type": "application/json"} if body else {})
    with urlopen(req, timeout=20) as response:
        return json.load(response)


def load_demo(hub: str, backup_dir: Path) -> Path:
    current = request(hub, "/v1/board")
    if not isinstance(current, dict) or not all(key in current for key in ("buy", "notes", "menu", "alarms")):
        raise ValueError("hub did not return a Hearth board")
    backup_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = backup_dir / f"board-before-demo-{stamp}.json"
    if backup.exists():
        raise FileExistsError(backup)
    backup.write_text(json.dumps(current, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result = request(hub, "/v1/board/apply", {"ops": demo_ops(current), "source": "demo"})
    if not result.get("ok"):
        raise RuntimeError(f"demo board load failed; previous board is saved at {backup}: {result.get('ack')}")
    return backup


def main() -> None:
    parser = argparse.ArgumentParser(description="Load a showcase board into a running Hearth hub")
    parser.add_argument("--hub", default="http://127.0.0.1:8790")
    parser.add_argument("--backup-dir", type=Path, default=ROOT / "backups")
    args = parser.parse_args()
    backup = load_demo(args.hub, args.backup_dir)
    print(f"Demo board loaded. Previous board saved at {backup}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Small stdio MCP bridge from the Hearth Hermes profile to the board API.

The device-facing hub keeps storage and the poster. Hermes owns interpretation
and calls these tools for every board change. Tool results contain the hub's
verified acknowledgement, never an unverified model claim.
"""

from __future__ import annotations

import json
import os
import sys
from urllib.request import Request, urlopen

HUB = os.environ.get("HEARTH_HUB_URL", "").rstrip("/")
RUN_ID = os.environ.get("HEARTH_RUN_ID", "")


def prop(description: str, kind: str = "string") -> dict:
    return {"type": kind, "description": description}


def tool(name: str, description: str, properties: dict, required: list[str]) -> dict:
    return {
        "name": name,
        "description": description,
        "inputSchema": {
            "type": "object",
            "properties": properties,
            "required": required,
            "additionalProperties": False,
        },
    }


TOOLS = [
    tool("hearth_get_board", "Read the current canonical Buy, Notes, Menu, and Alarms with stable item IDs and statuses. Call before editing.", {}, []),
    tool("hearth_add", "Add a Buy or Notes item; Notes can carry a kind, owner, and when. Read the returned ack.", {
        "list": prop("buy or notes"), "text": prop("Exact item text"),
        "kind": prop("For notes: do, pack, or note"),
        "owner": prop("Named person only"), "when": prop("Optional due wording"),
    }, ["list", "text"]),
    tool("hearth_complete", "Check off an existing Buy or Notes item. Prefer its stable ID from hearth_get_board.", {
        "list": prop("buy or notes"), "id": prop("Exact item ID"),
        "text": prop("Text only if ID is unavailable"),
    }, ["list"]),
    tool("hearth_toggle", "Check or uncheck an existing item by exact ID.", {
        "list": prop("buy or notes"), "id": prop("Exact item ID"),
    }, ["list", "id"]),
    tool("hearth_delete", "Permanently delete one existing Buy or Notes item. Use the exact ID; a null result means nothing was removed.", {
        "list": prop("buy or notes"), "id": prop("Exact item ID"),
        "text": prop("Text only if ID is unavailable"),
    }, ["list"]),
    tool("hearth_clear_list", "Delete every item in one list only when the utterance explicitly says all.", {
        "list": prop("buy or notes"),
    }, ["list"]),
    tool("hearth_set_meal", "Set a weekday breakfast, lunch, or dinner.", {
        "weekday": prop("mon, tue, wed, thu, fri, sat, or sun"),
        "slot": prop("breakfast, lunch, or dinner"), "meal": prop("Dish text"),
    }, ["weekday", "slot", "meal"]),
    tool("hearth_delete_meal", "Delete one menu entry by its weekday/slot key from hearth_get_board.", {
        "key": prop("weekday/slot, for example tue/lunch"),
    }, ["key"]),
    tool("hearth_set_alarm", "Set a one-shot alarm at the next occurrence of a local time.", {
        "hhmm": prop("Time such as 07:30 or 7am"),
        "text": prop("Optional alarm purpose"),
    }, ["hhmm"]),
    tool("hearth_set_timer", "Set a one-shot alarm after a duration. Use for both '30 second timer' and '30 second alarm'; never turn a duration into a guessed clock time.", {
        "seconds": prop("Duration in whole seconds, for example 30, 120, or 3600", "integer"),
        "text": prop("Optional purpose; leave blank for Timer"),
    }, ["seconds"]),
    tool("hearth_clear_alarm", "Cancel one alarm by its exact ID, time, or purpose.", {
        "id": prop("Exact alarm ID"), "hhmm": prop("Alarm time"),
        "text": prop("Alarm purpose"),
    }, []),
    tool("hearth_apply", "Apply several board operations from a mixed utterance, then inspect every result. An unmatched result is not a success.", {
        "ops": {"type": "array", "items": {"type": "object"}, "description": "Board operations"},
    }, ["ops"]),
]

OPS = {
    "hearth_add": "add", "hearth_complete": "complete",
    "hearth_toggle": "toggle", "hearth_delete": "delete",
    "hearth_clear_list": "clear_list", "hearth_set_meal": "set_menu",
    "hearth_delete_meal": "delete_menu", "hearth_set_alarm": "set_alarm",
    "hearth_set_timer": "set_timer",
    "hearth_clear_alarm": "clear_alarm",
}


def request(path: str, payload: dict | None = None) -> dict:
    if not HUB:
        raise ValueError("HEARTH_HUB_URL is required for the Hearth MCP bridge")
    body = json.dumps(payload).encode() if payload is not None else None
    req = Request(
        HUB + path, data=body,
        headers={"Content-Type": "application/json"} if body else {},
    )
    with urlopen(req, timeout=12) as response:
        return json.load(response)


def call(name: str, arguments: dict) -> dict:
    if name == "hearth_get_board":
        return request("/v1/board")
    if name == "hearth_apply":
        ops = arguments.get("ops")
        if not isinstance(ops, list):
            raise ValueError("ops must be a list")
    elif name in OPS:
        ops = [{"op": OPS[name], **arguments}]
    else:
        raise ValueError(f"unknown tool {name}")
    result = request("/v1/board/apply", {"ops": ops, "source": "fridge", "run_id": RUN_ID})
    return {key: result.get(key) for key in ("ok", "ack", "results")}


def respond(message_id, result: dict | None = None, error: str | None = None) -> None:
    frame = {"jsonrpc": "2.0", "id": message_id}
    if error is None:
        frame["result"] = result or {}
    else:
        frame["error"] = {"code": -32603, "message": error}
    sys.stdout.write(json.dumps(frame, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def handle(frame: dict) -> None:
    method = frame.get("method")
    message_id = frame.get("id")
    if method == "initialize":
        version = (frame.get("params") or {}).get("protocolVersion") or "2024-11-05"
        respond(message_id, {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": "hearth-board", "version": "0.5.0"},
        })
    elif method == "ping":
        respond(message_id, {})
    elif method == "tools/list":
        respond(message_id, {"tools": TOOLS})
    elif method == "tools/call":
        params = frame.get("params") or {}
        try:
            payload = call(params.get("name") or "", params.get("arguments") or {})
            result = {
                "content": [{"type": "text", "text": json.dumps(payload, ensure_ascii=False)}],
                "isError": payload.get("ok") is False,
            }
        except Exception as exc:  # noqa: BLE001
            result = {
                "content": [{"type": "text", "text": str(exc)}],
                "isError": True,
            }
        respond(message_id, result)
    elif message_id is not None and method not in ("notifications/initialized",):
        respond(message_id, error=f"unknown method {method}")


def main() -> None:
    for line in sys.stdin:
        try:
            frame = json.loads(line)
            if isinstance(frame, dict):
                handle(frame)
        except Exception as exc:  # noqa: BLE001
            sys.stderr.write(f"hearth mcp input error: {exc}\n")


if __name__ == "__main__":
    main()

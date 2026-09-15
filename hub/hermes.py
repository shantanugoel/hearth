"""Talk to the hearth Hermes profile on hermes-incus."""

from __future__ import annotations

import os
import shlex
import subprocess

DEFAULT_SSH = os.environ.get("HEARTH_HERMES_SSH", "hermes-incus")
DEFAULT_PROFILE = os.environ.get("HEARTH_HERMES_PROFILE", "hearth")
DEFAULT_SESSION = os.environ.get("HEARTH_HERMES_SESSION", "family-kitchen")


class HermesError(RuntimeError):
    pass


def _prompt(text: str, source: str) -> str:
    return (
        "The kitchen fridge heard this.\n"
        f"source: {source}\n"
        f"utterance: {text}\n"
        "\nUse the hearth-board skill and native Hearth MCP tools to file this. "
        "Get the full fresh board with hearth_get_board before changing it. "
        "Prefer an exact item ID for a deletion or completion. Split mixed sentences. "
        "For a duration such as '30 second timer' or '30 second alarm', call "
        "hearth_set_timer with the duration in seconds. "
        "Read tool results; if ok is false or item is null, retry with the correct ID. "
        "Never claim an item changed unless a tool confirms it. "
        "Do not invent chores, meals, shops, or alarms. "
        "Reply with one short sentence for the e-paper."
    )


def file_utterance(
    text: str,
    source: str = "fridge",
    board_snapshot: dict | None = None,
    *,
    ssh: str = DEFAULT_SSH,
    timeout_s: float = 90.0,
) -> str:
    if not text.strip():
        return ""
    run_id = str((board_snapshot or {}).get("meta", {}).get("file_run_id") or "")
    remote_timeout_s = max(1, int(timeout_s) - 5)
    remote = (
        'export PATH="$HOME/.local/bin:$PATH"; '
        f"export HEARTH_RUN_ID={shlex.quote(run_id)}; "
        f"timeout --signal=TERM --kill-after=5s {remote_timeout_s}s "
        f"{DEFAULT_PROFILE} --yolo --skills hearth-board "
        f"-z {shlex.quote(_prompt(text, source))}"
    )
    try:
        completed = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", ssh, remote],
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
    except subprocess.TimeoutExpired as exc:
        raise HermesError(f"hermes profile timed out after {timeout_s:g}s") from exc
    except OSError as exc:
        raise HermesError(f"hermes ssh unavailable: {type(exc).__name__}") from exc
    if completed.returncode in (124, 137):
        raise HermesError(f"hermes profile timed out after {remote_timeout_s}s")
    if completed.returncode != 0:
        err = (completed.stderr or completed.stdout or "exit %d" % completed.returncode)
        raise HermesError(err.strip()[:400])
    ack = (completed.stdout or "").strip()
    lines = [line.strip() for line in ack.splitlines() if line.strip()]
    if not lines:
        return "filed"
    return lines[-1][:120]

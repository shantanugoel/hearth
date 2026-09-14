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
        f"utterance: {text}\n\n"
        "File it into the household board using the hearth-board skill. "
        "Split mixed sentences. Tag a person only when named. "
        "Completions and deletions are first-class. Notes holds chores, "
        "bags, and leftover thoughts. Alarms use set_alarm/clear_alarm. "
        "Do not invent chores, meals, shops, or alarms. "
        "Reply with one short sentence for the e-paper, nothing else."
    )


def file_utterance(
    text: str,
    source: str = "fridge",
    *,
    ssh: str = DEFAULT_SSH,
    timeout_s: float = 90.0,
) -> str:
    if not text.strip():
        return ""
    remote = (
        'export PATH="$HOME/.local/bin:$PATH"; '
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
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HermesError(f"hermes ssh failed: {exc}") from exc
    if completed.returncode != 0:
        err = (completed.stderr or completed.stdout or "exit %d" % completed.returncode)
        raise HermesError(err.strip()[:400])
    ack = (completed.stdout or "").strip()
    lines = [line.strip() for line in ack.splitlines() if line.strip()]
    if not lines:
        return "filed"
    return lines[-1][:120]

"""Call the dedicated Hearth Hermes profile locally or over SSH."""

from __future__ import annotations

import os
import shlex
import subprocess

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
    method: str | None = None,
    ssh: str | None = None,
    command: str | None = None,
    timeout_s: float | None = None,
) -> str:
    if not text.strip():
        return ""
    host = ssh or os.environ.get("HEARTH_HERMES_SSH_HOST") or os.environ.get("HEARTH_HERMES_SSH") or ""
    method = method or os.environ.get("HEARTH_HERMES_METHOD") or ("ssh" if host else "local")
    if method not in ("local", "ssh"):
        raise HermesError("HEARTH_HERMES_METHOD must be local or ssh")
    if method == "ssh" and not host:
        raise HermesError("HEARTH_HERMES_SSH_HOST is required for SSH")
    command = command or os.environ.get("HEARTH_HERMES_COMMAND") or os.environ.get("HEARTH_HERMES_PROFILE") or "hearth"
    try:
        cli = shlex.split(command)
    except ValueError as exc:
        raise HermesError("HEARTH_HERMES_COMMAND has invalid quoting") from exc
    if not cli:
        raise HermesError("HEARTH_HERMES_COMMAND cannot be empty")
    if timeout_s is None:
        try:
            timeout_s = float(os.environ.get("HEARTH_HERMES_TIMEOUT_S") or "90")
        except ValueError as exc:
            raise HermesError("HEARTH_HERMES_TIMEOUT_S must be a number") from exc
    if timeout_s <= 5:
        raise HermesError("HEARTH_HERMES_TIMEOUT_S must be above 5 seconds")
    run_id = str((board_snapshot or {}).get("meta", {}).get("file_run_id") or "")
    remote_timeout_s = max(1, int(timeout_s) - 5)
    cli.extend(("--yolo", "--skills", "hearth-board", "-z", _prompt(text, source)))
    timed = ["timeout", "--signal=TERM", "--kill-after=5s", f"{remote_timeout_s}s", *cli]
    if method == "ssh":
        remote = (
            'export PATH="$HOME/.local/bin:$PATH"; '
            f"export HEARTH_RUN_ID={shlex.quote(run_id)}; "
            + shlex.join(timed)
        )
        argv = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host, remote]
        env = None
    else:
        argv = timed
        env = os.environ.copy()
        env["HEARTH_RUN_ID"] = run_id
        env["PATH"] = str(os.path.expanduser("~/.local/bin")) + os.pathsep + env.get("PATH", "")
    try:
        completed = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_s,
            env=env,
        )
    except subprocess.TimeoutExpired as exc:
        raise HermesError(f"hermes profile timed out after {timeout_s:g}s") from exc
    except OSError as exc:
        raise HermesError(f"hermes command unavailable: {type(exc).__name__}") from exc
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

#!/bin/sh
# Install SOUL, hearth-board skill, and MCP board tools onto the hearth profile.
# Does not make hearth the sticky default profile.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
ENV_FILE="${HEARTH_ENV_FILE:-$ROOT/.env}"
if [ -f "$ENV_FILE" ]; then
  set -a
  . "$ENV_FILE"
  set +a
fi
HOST="${HEARTH_HERMES_SSH_HOST:-${HEARTH_HERMES_SSH:-}}"
METHOD="${HEARTH_HERMES_METHOD:-}"
if [ -z "$METHOD" ]; then
  if [ -n "$HOST" ]; then METHOD=ssh; else METHOD=local; fi
fi
case "$METHOD" in
  local) ;;
  ssh) [ -n "$HOST" ] || { echo "Set HEARTH_HERMES_SSH_HOST for SSH installation" >&2; exit 2; } ;;
  *) echo "HEARTH_HERMES_METHOD must be local or ssh" >&2; exit 2 ;;
esac
PROFILE="${HEARTH_HERMES_PROFILE:-hearth}"
case "$PROFILE" in
  ''|*[!A-Za-z0-9_-]*) echo "HEARTH_HERMES_PROFILE must be a simple name" >&2; exit 2 ;;
esac
HUB_URL="${HEARTH_HUB_URL:-}"
[ -n "$HUB_URL" ] || { echo "Set HEARTH_HUB_URL in .env before installing" >&2; exit 2; }
CLI_RAW="${HEARTH_HERMES_COMMAND:-$PROFILE}"
CLI_PREFIX="$(python3 - "$CLI_RAW" <<'PY'
import shlex
import sys
words = shlex.split(sys.argv[1])
if not words:
    raise SystemExit("HEARTH_HERMES_COMMAND cannot be empty")
print(shlex.join(words))
PY
)"
DEST="${HEARTH_HERMES_PROFILE_DIR:-${HEARTH_HERMES_HOME:-}}"
if [ -n "$DEST" ]; then
  case "$DEST" in
    /*) ;;
    *) echo "HEARTH_HERMES_PROFILE_DIR must be an absolute path" >&2; exit 2 ;;
  esac
fi

if [ "${1:-}" = "--dry-run" ]; then
  [ -n "$DEST" ] || DEST="<target-home>/.hermes/profiles/$PROFILE"
  if [ "$METHOD" = ssh ]; then echo "method: ssh ($HOST)"; else echo "method: local"; fi
  echo "profile directory: $DEST"
  echo "Hermes command: $CLI_PREFIX"
  echo "hub URL: $HUB_URL"
  exit 0
fi
[ "$#" -eq 0 ] || { echo "Usage: $0 [--dry-run]" >&2; exit 2; }

target_run() {
  if [ "$METHOD" = ssh ]; then
    ssh -o BatchMode=yes "$HOST" "$1"
  else
    sh -c "$1"
  fi
}

target_copy() {
  if [ "$METHOD" = ssh ]; then
    scp -q "$1" "$HOST:$2"
  else
    cp "$1" "$2"
  fi
}

shell_quote() {
  python3 -c 'import shlex, sys; print(shlex.quote(sys.argv[1]))' "$1"
}

if [ -z "$DEST" ]; then
  TARGET_HOME="$(target_run 'printf "%s" "$HOME"')"
  DEST="$TARGET_HOME/.hermes/profiles/$PROFILE"
fi
case "$DEST" in
  /*) ;;
  *) echo "HEARTH_HERMES_PROFILE_DIR must be an absolute path" >&2; exit 2 ;;
esac
DEST_Q="$(shell_quote "$DEST")"
HUB_Q="$(shell_quote "$HUB_URL")"
MCP_Q="$(shell_quote "$DEST/tools/hearth_mcp.py")"

target_run "mkdir -p $(shell_quote "$DEST/skills/productivity/hearth-board") $(shell_quote "$DEST/tools")"
target_copy "$ROOT/hermes-profile/SOUL.md" "$DEST/SOUL.md"
target_copy "$ROOT/hermes-profile/skills/hearth-board/SKILL.md" \
  "$DEST/skills/productivity/hearth-board/SKILL.md"
target_copy "$ROOT/hermes-profile/hearth_mcp.py" "$DEST/tools/hearth_mcp.py"
target_run "chmod 755 $MCP_Q; export PATH=\"\$HOME/.local/bin:\$PATH\"; if ! $CLI_PREFIX mcp list | grep -q 'hearth-board'; then printf 'y\\n' | $CLI_PREFIX mcp add hearth-board --command $MCP_Q; fi"

# Disable bundled coding skills (leave the files) and publish the hub URL.
target_run "python3 - $DEST_Q $HUB_Q" <<'PY'
import sys
import json
from pathlib import Path

dest = Path(sys.argv[1])
hub = sys.argv[2]

env = dest / ".env"
text = env.read_text(encoding="utf-8") if env.exists() else ""
text = "\n".join(line for line in text.splitlines() if not line.startswith("HEARTH_HUB_URL="))
if text:
    text += "\n"
env.write_text(text + f"HEARTH_HUB_URL={hub}\n", encoding="utf-8")

cfg = dest / "config.yaml"
raw = cfg.read_text(encoding="utf-8") if cfg.exists() else "skills:\n  disabled: []\n"

def upsert(section, key, value):
    global raw
    lines = raw.splitlines(keepends=True)
    start = next(
        (i for i, line in enumerate(lines) if line.rstrip("\r\n") == f"{section}:"),
        None,
    )
    if start is None:
        if raw and not raw.endswith("\n"):
            raw += "\n"
        raw += f"{section}:\n  {key}: {value}\n"
        return
    end = len(lines)
    for i in range(start + 1, len(lines)):
        stripped = lines[i].strip()
        if stripped and not lines[i].startswith((" ", "\t", "#")):
            end = i
            break
    prefix = f"  {key}:"
    for i in range(start + 1, end):
        if lines[i].startswith(prefix):
            newline = "\r\n" if lines[i].endswith("\r\n") else "\n"
            lines[i] = f"  {key}: {value}{newline}"
            raw = "".join(lines)
            return
    lines.insert(start + 1, f"  {key}: {value}\n")
    raw = "".join(lines)

# Keep the kitchen agent fast and isolated. These values land only in the
# hearth profile; the default profile and every sibling profile are untouched.
upsert("model", "provider", "openai-codex")
upsert("model", "default", "gpt-5.6-luna")
upsert("agent", "reasoning_effort", "none")

# Hermes filters the environment for MCP children. Explicitly pass the
# per-utterance ID so the hub can report the result of this turn only.
lines = raw.splitlines(keepends=True)
i = len(lines) - 1
while i >= 0:
    if (lines[i].strip() == "HEARTH_RUN_ID: ${HEARTH_RUN_ID}"
            or lines[i].strip().startswith("HEARTH_HUB_URL:")):
        del lines[i]
        if (0 <= i - 1 < len(lines) and lines[i - 1].strip() == "env:"
                and (i >= len(lines) or not lines[i].startswith("      "))):
            del lines[i - 1]
            i -= 1
    i -= 1
server = next((i for i, line in enumerate(lines) if line.rstrip("\r\n") == "  hearth-board:"), None)
if server is not None:
    end = next((i for i in range(server + 1, len(lines)) if not lines[i].startswith("    ")), len(lines))
    env_line = next((i for i in range(server + 1, end) if lines[i].strip() == "env:"), None)
    settings = f"      HEARTH_RUN_ID: ${{HEARTH_RUN_ID}}\n      HEARTH_HUB_URL: {json.dumps(hub)}\n"
    if env_line is None:
        lines.insert(end, "    env:\n" + settings)
    else:
        lines.insert(env_line + 1, settings)
    raw = "".join(lines)

needed = [
    "github",
    "codebase-inspection",
    "simplify-code",
    "systematic-debugging",
    "test-driven-development",
    "requesting-code-review",
    "spike",
    "dogfood",
    "hermes-agent-skill-authoring",
    "node-inspect-debugger",
    "python-debugpy",
    "inspecting-hermes-desktop-dom",
    "hermes-agent",
    "claude-code",
    "codex",
    "opencode",
    "computer-use",
]
missing = [name for name in needed if f"- {name}" not in raw]
if missing:
    lines = raw.splitlines(keepends=True)
    out = []
    added = False
    for line in lines:
        disabled_line = line.lstrip().startswith("disabled:")
        if disabled_line and "[]" in line:
            out.append(line.replace("[]", ""))
        else:
            out.append(line)
        if not added and disabled_line:
            indent = "    " if line.startswith(" ") else "  "
            for name in missing:
                out.append(f"{indent}- {name}\n")
            added = True
    if not added:
        out.append("skills:\n  disabled:\n")
        for name in missing:
            out.append(f"    - {name}\n")
    raw = "".join(out)
cfg.write_text(raw, encoding="utf-8")
print("profile files ready")
PY

echo "installed Hearth profile files via $METHOD"

#!/bin/sh
# Install SOUL, hearth-board skill, and MCP board tools onto the hearth profile.
# Does not make hearth the sticky default profile.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
if [ -f "$ROOT/.env" ]; then
  set -a
  . "$ROOT/.env"
  set +a
fi
HOST="${HEARTH_HERMES_SSH:-hermes-incus}"
DEST="${HEARTH_HERMES_HOME:-/home/hermes/.hermes/profiles/hearth}"
HUB_URL="${HEARTH_HUB_URL:-http://192.168.2.89:8790}"

ssh -o BatchMode=yes "$HOST" "mkdir -p '$DEST/skills/productivity/hearth-board' '$DEST/tools'"
scp -q "$ROOT/hermes-profile/SOUL.md" "$HOST:$DEST/SOUL.md"
scp -q "$ROOT/hermes-profile/skills/hearth-board/SKILL.md" \
  "$HOST:$DEST/skills/productivity/hearth-board/SKILL.md"
scp -q "$ROOT/hermes-profile/hearth_mcp.py" "$HOST:$DEST/tools/hearth_mcp.py"
ssh -o BatchMode=yes "$HOST" "chmod 755 '$DEST/tools/hearth_mcp.py'; export PATH=\"\$HOME/.local/bin:\$PATH\"; if ! hearth mcp list | grep -q 'hearth-board'; then printf 'y\\n' | hearth mcp add hearth-board --command '$DEST/tools/hearth_mcp.py'; fi"

# Disable bundled coding skills (leave the files) and publish the hub URL.
ssh -o BatchMode=yes "$HOST" "python3 - '$DEST' '$HUB_URL'" <<'PY'
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

echo "installed hearth profile files on $HOST:$DEST"
echo "hub url $HUB_URL (not the sticky default profile)"

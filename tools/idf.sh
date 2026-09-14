#!/bin/bash
# ESP-IDF on this machine was installed by the ESP-IDF Installation Manager,
# which has two quirks worth writing down:
#
#  * The checkout's own export.sh looks for a venv under ~/.espressif/python_env
#    and fails - the real venv is at ~/.espressif/tools/python/v6.1/venv.
#  * activate_idf_v6.1.sh is the correct entry point, but it refuses to be
#    sourced from a script (it decides "sourced" by testing whether $0 is a
#    shell name). Its supported non-interactive interface is `-e`, which prints
#    the environment as KEY=VALUE lines - that is what is used here.
#
# It also exposes idf.py only as a shell function, so the script is invoked
# directly through the venv's python.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACTIVATE="${IDF_ACTIVATE:-$HOME/.espressif/tools/activate_idf_v6.1.sh}"
if [ ! -x "$ACTIVATE" ] && [ ! -f "$ACTIVATE" ]; then
    echo "ESP-IDF activate script not found: $ACTIVATE" >&2
    echo "Set IDF_ACTIVATE, or use a standard idf.py install." >&2
    exit 1
fi
while IFS= read -r line; do
    case "$line" in
        SYSTEM_PATH=*) ;;                       # informational only
        PATH=*) export PATH="${line#PATH=}:$PATH" ;;
        *=*) export "${line?}" ;;
    esac
done < <(bash "$ACTIVATE" -e)

cd "$ROOT/firmware" || exit 1
"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@" 2>&1

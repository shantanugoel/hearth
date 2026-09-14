"""Hearth hub: STT, household board, Hermes filing, poster JSON."""

from __future__ import annotations

import os
from pathlib import Path


def _load_dotenv(path: Path | None = None) -> None:
    env_path = path or Path(".env")
    if not env_path.exists():
        return
    for raw in env_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if not key or key in os.environ:
            continue
        os.environ[key] = value.strip().strip('"').strip("'")


_load_dotenv()

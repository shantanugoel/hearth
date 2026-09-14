"""Forward a WAV to the local OpenAI-compatible transcription endpoint."""

from __future__ import annotations

import json
import os
import uuid
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from hub.wavutil import clean_asr_text

DEFAULT_STT_URL = os.environ.get(
    "HEARTH_STT_URL", "http://127.0.0.1:8080/v1/audio/transcriptions"
)
DEFAULT_STT_MODEL = os.environ.get("HEARTH_STT_MODEL", "qwen3-asr-0.6b-cpu")


class SttError(RuntimeError):
    pass


def transcribe(
    wav: bytes,
    *,
    url: str = DEFAULT_STT_URL,
    model: str = DEFAULT_STT_MODEL,
    timeout_s: float = 90.0,
) -> dict:
    boundary = f"----hearth{uuid.uuid4().hex}"
    filename = "utterance.wav"
    parts = []

    def add_field(name: str, value: bytes, filename: str | None, ctype: str | None) -> None:
        parts.append(f"--{boundary}\r\n".encode("ascii"))
        disposition = f'Content-Disposition: form-data; name="{name}"'
        if filename:
            disposition += f'; filename="{filename}"'
        parts.append((disposition + "\r\n").encode("ascii"))
        if ctype:
            parts.append(f"Content-Type: {ctype}\r\n".encode("ascii"))
        parts.append(b"\r\n")
        parts.append(value)
        parts.append(b"\r\n")

    add_field("model", model.encode("utf-8"), None, None)
    add_field("file", wav, filename, "audio/wav")
    parts.append(f"--{boundary}--\r\n".encode("ascii"))
    body = b"".join(parts)

    request = Request(
        url,
        data=body,
        method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )
    try:
        with urlopen(request, timeout=timeout_s) as response:
            raw = response.read()
            status = response.status
    except HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:500]
        raise SttError(f"STT HTTP {exc.code}: {detail}") from exc
    except URLError as exc:
        raise SttError(f"STT unreachable: {exc.reason}") from exc

    try:
        payload = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise SttError(f"STT returned non-JSON (HTTP {status})") from exc

    if not isinstance(payload, dict):
        raise SttError("STT JSON was not an object")
    raw_text = payload.get("text")
    if not isinstance(raw_text, str):
        raise SttError("STT JSON missing text")
    text = clean_asr_text(raw_text)
    return {"text": text, "raw": raw_text, "model": model}

"""Hearth hub HTTP server.

POST /v1/utterance  audio/wav or raw PCM16  ->  {text, raw, ms}
GET  /health
GET  /
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs, urlparse

from hub.stt import DEFAULT_STT_MODEL, DEFAULT_STT_URL, SttError, transcribe
from hub.wavutil import PCM_RATE, parse_wav, wrap_pcm16

TranscribeFn = Callable[[bytes], dict]


class HubState:
    def __init__(
        self,
        transcribe_fn: TranscribeFn,
        dump_dir: Path | None = None,
        stt_url: str = DEFAULT_STT_URL,
        stt_model: str = DEFAULT_STT_MODEL,
    ) -> None:
        self.transcribe_fn = transcribe_fn
        self.dump_dir = dump_dir
        self.stt_url = stt_url
        self.stt_model = stt_model
        self.last_text = ""
        self.last_ms = 0
        self.last_error = ""
        self.utterances = 0


def _multipart_file(body: bytes, content_type: str) -> bytes | None:
    if "multipart/form-data" not in content_type:
        return None
    boundary = ""
    for part in content_type.split(";"):
        part = part.strip()
        if part.lower().startswith("boundary="):
            boundary = part.split("=", 1)[1].strip().strip('"')
    if not boundary:
        return None
    marker = b"--" + boundary.encode("ascii", errors="replace")
    for section in body.split(marker):
        if b"filename=" not in section and b"name=\"file\"" not in section:
            continue
        split = section.split(b"\r\n\r\n", 1)
        if len(split) != 2:
            continue
        payload = split[1]
        if payload.endswith(b"\r\n"):
            payload = payload[:-2]
        if payload.endswith(b"--"):
            payload = payload[:-2]
        return payload
    return None


class HubHandler(BaseHTTPRequestHandler):
    server_version = "HearthHub/0.2"

    @property
    def state(self) -> HubState:
        return self.server.state  # type: ignore[attr-defined]

    def log_message(self, format: str, *args) -> None:  # noqa: A003
        sys.stderr.write("%s - %s\n" % (self.address_string(), format % args))

    def _send_json(self, code: int, payload: dict) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_text(self, code: int, text: str) -> None:
        data = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/health":
            self._send_json(
                200,
                {
                    "ok": True,
                    "stt": self.state.stt_url,
                    "model": self.state.stt_model,
                    "utterances": self.state.utterances,
                    "last_text": self.state.last_text,
                    "last_error": self.state.last_error,
                },
            )
            return
        if path in ("/", "/index.html"):
            self._send_text(
                200,
                "hearth hub\n"
                f"stt {self.state.stt_model}\n"
                f"utterances {self.state.utterances}\n"
                f"last {self.state.last_text or '(none)'}\n"
                "POST /v1/utterance with audio/wav\n",
            )
            return
        self._send_json(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path != "/v1/utterance":
            self._send_json(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 4 * 1024 * 1024:
            self._send_json(400, {"error": "need a WAV body under 4 MiB"})
            return
        body = self.rfile.read(length)
        ctype = self.headers.get("Content-Type", "")
        query = parse_qs(urlparse(self.path).query)
        try:
            wav = self._to_wav(body, ctype, query)
        except ValueError as exc:
            self._send_json(400, {"error": str(exc)})
            return

        if self.state.dump_dir is not None:
            self.state.dump_dir.mkdir(parents=True, exist_ok=True)
            stamp = time.strftime("%Y%m%d-%H%M%S")
            (self.state.dump_dir / f"{stamp}.wav").write_bytes(wav)

        started = time.monotonic()
        try:
            result = self.state.transcribe_fn(wav)
        except SttError as exc:
            self.state.last_error = str(exc)
            self._send_json(502, {"error": str(exc)})
            return
        except Exception as exc:  # noqa: BLE001
            self.state.last_error = str(exc)
            self._send_json(500, {"error": str(exc)})
            return
        ms = int((time.monotonic() - started) * 1000)
        text = result.get("text") or ""
        self.state.last_text = text
        self.state.last_ms = ms
        self.state.last_error = ""
        self.state.utterances += 1
        self._send_json(
            200,
            {
                "text": text,
                "raw": result.get("raw", text),
                "ms": ms,
                "model": result.get("model", self.state.stt_model),
            },
        )

    def _to_wav(self, body: bytes, ctype: str, query: dict) -> bytes:
        multi = _multipart_file(body, ctype)
        if multi is not None:
            body = multi
            ctype = "audio/wav"
        if "wav" in ctype.lower() or body[:4] == b"RIFF":
            pcm, rate = parse_wav(body)
            if rate != PCM_RATE:
                # Device and STT both expect 16 kHz; refuse to resample here.
                raise ValueError(f"need 16 kHz WAV, got {rate}")
            return wrap_pcm16(pcm, rate)
        rate = PCM_RATE
        if "rate" in query:
            rate = int(query["rate"][0])
        elif self.headers.get("X-Sample-Rate"):
            rate = int(self.headers["X-Sample-Rate"])
        if rate != PCM_RATE:
            raise ValueError(f"need 16 kHz PCM, got {rate}")
        if len(body) < 320:
            raise ValueError("clip too short")
        return wrap_pcm16(body, rate)


def make_server(
    host: str,
    port: int,
    state: HubState,
) -> ThreadingHTTPServer:
    httpd = ThreadingHTTPServer((host, port), HubHandler)
    httpd.state = state  # type: ignore[attr-defined]
    return httpd


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Hearth hub")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=int(os.environ.get("HEARTH_PORT", "8790")))
    parser.add_argument("--stt-url", default=DEFAULT_STT_URL)
    parser.add_argument("--stt-model", default=DEFAULT_STT_MODEL)
    parser.add_argument("--dump-dir", type=Path, default=None)
    parser.add_argument(
        "--mock-text",
        default=None,
        help="Skip STT and always return this transcript (for layout tests)",
    )
    args = parser.parse_args(argv)

    if args.mock_text is not None:
        def transcribe_fn(wav: bytes) -> dict:
            return {"text": args.mock_text, "raw": args.mock_text, "model": "mock"}
    else:
        def transcribe_fn(wav: bytes) -> dict:
            return transcribe(wav, url=args.stt_url, model=args.stt_model)

    state = HubState(
        transcribe_fn,
        dump_dir=args.dump_dir,
        stt_url=args.stt_url,
        stt_model=args.stt_model,
    )
    httpd = make_server(args.host, args.port, state)
    print(f"hearth hub on http://{args.host}:{args.port}  stt={args.stt_model}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("stopping", flush=True)
    finally:
        httpd.server_close()
    return 0

"""Hearth hub HTTP server.

POST /v1/utterance  audio/wav or raw PCM16  ->  {text, ack, poster fields, ms}
GET  /v1/poster
GET  /v1/board
POST /v1/board/apply
GET  /health
GET  /
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs, urlparse

from hub.board import Board, poster_from_board, weather_kind
from hub.commands import try_fast_command
from hub.hermes import HermesError, file_utterance
from hub.stt import DEFAULT_STT_MODEL, DEFAULT_STT_URL, SttError, transcribe
from hub.wavutil import PCM_RATE, parse_wav, wrap_pcm16
from hub.weather import fetch_weather

TranscribeFn = Callable[[bytes], dict]
FileFn = Callable[[str, str], str]


def default_board_path() -> Path:
    override = os.environ.get("HEARTH_BOARD_PATH")
    if override:
        return Path(override).expanduser()
    return Path.home() / ".local/share/hearth/board.json"


class HubState:
    def __init__(
        self,
        transcribe_fn: TranscribeFn,
        dump_dir: Path | None = None,
        stt_url: str = DEFAULT_STT_URL,
        stt_model: str = DEFAULT_STT_MODEL,
        board: Board | None = None,
        file_fn: FileFn | None = None,
        lat: float | None = None,
        lon: float | None = None,
    ) -> None:
        self.transcribe_fn = transcribe_fn
        self.dump_dir = dump_dir
        self.stt_url = stt_url
        self.stt_model = stt_model
        self.board = board
        self.file_fn = file_fn
        self.lat = lat
        self.lon = lon
        self.last_text = ""
        self.last_ack = ""
        self.last_ms = 0
        self.last_error = ""
        self.utterances = 0
        self.last_weather_mono = 0.0
        self.pending = False
        self.lock = threading.Lock()

    def refresh_weather(self, force: bool = False) -> None:
        if self.board is None:
            return
        now = time.monotonic()
        if not force and self.board.data["weather"]["line"] and (
            now - self.last_weather_mono < 900
        ):
            return
        if self.lat is None or self.lon is None:
            if not self.board.data["weather"]["line"]:
                self.board.set_weather("weather unknown")
            return
        try:
            line = fetch_weather(self.lat, self.lon)
        except Exception as exc:  # noqa: BLE001
            if not self.board.data["weather"]["line"]:
                self.board.set_weather("weather unknown")
            sys.stderr.write("weather fetch failed: %s\n" % exc)
            return
        self.board.set_weather(line, weather_kind(line))
        self.last_weather_mono = now

    def poster(self) -> dict:
        if self.board is None:
            return {}
        self.refresh_weather()
        self.board.load()
        with self.lock:
            pending = self.pending
        return poster_from_board(self.board, pending=pending)

    def file_transcript(self, text: str, source: str = "fridge") -> str:
        if not text.strip():
            return ""
        if self.file_fn is None:
            return ""
        ack = None
        if self.board is not None:
            self.board.load()
            ack = try_fast_command(self.board, text, source)
        if ack is None:
            ack = self.file_fn(text, source)
        if self.board is not None:
            self.board.load()
            self.board.set_meta(utterance=text, ack=ack)
        return ack

    def file_fast(self, text: str, source: str = "fridge") -> str | None:
        """File a simple command before returning the STT response."""
        if self.file_fn is None or self.board is None or not text.strip():
            return None
        self.board.load()
        ack = try_fast_command(self.board, text, source)
        if ack is None:
            return None
        self.board.set_meta(utterance=text, ack=ack)
        with self.lock:
            self.last_ack = ack
            self.last_error = ""
            self.pending = False
        return ack

    def file_in_background(self, text: str, source: str = "fridge") -> None:
        if self.file_fn is None or not text.strip():
            with self.lock:
                self.pending = False
            return

        def run() -> None:
            ack = "heard, not filed"
            err = ""
            try:
                ack = self.file_transcript(text, source)
            except HermesError as exc:
                ack = "heard, not filed"
                err = str(exc)
            except Exception as exc:  # noqa: BLE001
                ack = "heard, not filed"
                err = str(exc)
            try:
                if self.board is not None:
                    self.board.load()
                    self.board.set_meta(ack=ack)
            except Exception as exc:  # noqa: BLE001
                if not err:
                    err = str(exc)
            with self.lock:
                self.last_ack = ack
                self.last_error = err
                self.pending = False

        with self.lock:
            self.pending = True
            self.last_ack = ""
        if self.board is not None:
            self.board.set_meta(utterance=text, ack="")
        threading.Thread(target=run, daemon=True, name="hearth-file").start()


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
        if b"filename=" not in section and b'name="file"' not in section:
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


def _read_json(body: bytes) -> dict:
    if not body:
        return {}
    payload = json.loads(body.decode("utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("JSON object required")
    return payload


class HubHandler(BaseHTTPRequestHandler):
    server_version = "HearthHub/0.5"

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
                    "last_ack": self.state.last_ack,
                    "last_error": self.state.last_error,
                    "pending": self.state.pending,
                },
            )
            return
        if path in ("/v1/poster", "/v1/snapshot"):
            self._send_json(200, self.state.poster())
            return
        if path == "/v1/board":
            if self.state.board is None:
                self._send_json(503, {"error": "no board"})
                return
            self.state.board.load()
            self._send_json(200, self.state.board.snapshot())
            return
        if path in ("/", "/index.html"):
            self._send_text(
                200,
                "hearth hub\n"
                f"stt {self.state.stt_model}\n"
                f"utterances {self.state.utterances}\n"
                f"last {self.state.last_text or '(none)'}\n"
                f"ack {self.state.last_ack or '(none)'}\n"
                "POST /v1/utterance with audio/wav\n"
                "GET /v1/poster  GET /v1/board  POST /v1/board/apply\n",
            )
            return
        self._send_json(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/v1/board/apply":
            self._apply()
            return
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
        self.state.utterances += 1
        if self.state.file_fn is not None and text.strip():
            ack = self.state.file_fast(text, "fridge")
            if ack is None:
                self.state.file_in_background(text, "fridge")
        else:
            with self.state.lock:
                self.state.pending = False
        payload = {
            "text": text,
            "raw": result.get("raw", text),
            "ack": self.state.last_ack,
            "ms": ms,
            "model": result.get("model", self.state.stt_model),
        }
        payload.update(self.state.poster())
        payload["text"] = text
        self._send_json(200, payload)

    def _apply(self) -> None:
        if self.state.board is None:
            self._send_json(503, {"error": "no board"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 64 * 1024:
            self._send_json(400, {"error": "need a JSON body"})
            return
        try:
            payload = _read_json(self.rfile.read(length))
            ops = payload.get("ops")
            if not isinstance(ops, list):
                raise ValueError("ops must be a list")
            results = self.state.board.apply(
                ops, source=str(payload.get("source") or "fridge")
            )
        except (ValueError, json.JSONDecodeError) as exc:
            self._send_json(400, {"error": str(exc)})
            return
        ack = str(payload.get("ack") or "")
        if ack:
            self.state.board.set_meta(ack=ack)
            self.state.last_ack = ack
        self._send_json(
            200,
            {"ok": True, "results": results, "poster": self.state.poster()},
        )

    def _to_wav(self, body: bytes, ctype: str, query: dict) -> bytes:
        multi = _multipart_file(body, ctype)
        if multi is not None:
            body = multi
            ctype = "audio/wav"
        if "wav" in ctype.lower() or body[:4] == b"RIFF":
            pcm, rate = parse_wav(body)
            if rate != PCM_RATE:
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


def _env_float(name: str) -> float | None:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return None
    try:
        return float(raw)
    except ValueError:
        return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Hearth hub")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=int(os.environ.get("HEARTH_PORT", "8790")))
    parser.add_argument("--stt-url", default=DEFAULT_STT_URL)
    parser.add_argument("--stt-model", default=DEFAULT_STT_MODEL)
    parser.add_argument("--dump-dir", type=Path, default=None)
    parser.add_argument("--board", type=Path, default=default_board_path())
    parser.add_argument(
        "--mock-text",
        default=None,
        help="Skip STT and always return this transcript (for layout tests)",
    )
    parser.add_argument(
        "--no-hermes",
        action="store_true",
        help="Transcribe only; do not file through the hearth profile",
    )
    parser.add_argument(
        "--mock-hermes",
        action="store_true",
        help="File with a tiny in-process heuristic instead of SSH",
    )
    args = parser.parse_args(argv)

    if args.mock_text is not None:
        def transcribe_fn(wav: bytes) -> dict:
            return {"text": args.mock_text, "raw": args.mock_text, "model": "mock"}
    else:
        def transcribe_fn(wav: bytes) -> dict:
            return transcribe(wav, url=args.stt_url, model=args.stt_model)

    board = Board(Path(args.board).expanduser())

    def mock_file(text: str, source: str) -> str:
        ack = try_fast_command(board, text, source)
        return ack or "Heard, nothing to file."

    file_fn: FileFn | None
    if args.no_hermes:
        file_fn = None
    elif args.mock_hermes:
        file_fn = mock_file
    else:
        file_fn = file_utterance

    state = HubState(
        transcribe_fn,
        dump_dir=args.dump_dir,
        stt_url=args.stt_url,
        stt_model=args.stt_model,
        board=board,
        file_fn=file_fn,
        lat=_env_float("HEARTH_LAT"),
        lon=_env_float("HEARTH_LON"),
    )
    httpd = make_server(args.host, args.port, state)
    print(
        f"hearth hub on http://{args.host}:{args.port}  "
        f"stt={args.stt_model}  board={board.path}",
        flush=True,
    )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("stopping", flush=True)
    finally:
        httpd.server_close()
    return 0

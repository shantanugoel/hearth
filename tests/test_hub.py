#!/usr/bin/env python3
"""Hub tests. Live STT is optional; default path is in-process mock."""

from __future__ import annotations

import json
import struct
import threading
import time
import unittest
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
import sys

sys.path.insert(0, str(ROOT))

from hub.server import HubState, make_server  # noqa: E402
from hub.wavutil import clean_asr_text, parse_wav, wrap_pcm16  # noqa: E402


def silence_wav(ms: int = 400) -> bytes:
    n = 16000 * ms // 1000
    return wrap_pcm16(b"\x00\x00" * n, 16000)


class WavTests(unittest.TestCase):
    def test_roundtrip(self) -> None:
        pcm = struct.pack("<" + "h" * 16, *range(16))
        wav = wrap_pcm16(pcm, 16000)
        out, rate = parse_wav(wav)
        self.assertEqual(rate, 16000)
        self.assertEqual(out, pcm)

    def test_rejects_stereo(self) -> None:
        # Manually smash channels=2 into a valid header.
        wav = bytearray(wrap_pcm16(b"\x00\x00" * 8, 16000))
        wav[22:24] = (2).to_bytes(2, "little")
        with self.assertRaises(ValueError):
            parse_wav(bytes(wav))


class CleanTests(unittest.TestCase):
    def test_qwen_wrap(self) -> None:
        self.assertEqual(
            clean_asr_text("language English<asr_text>we are out of oat milk"),
            "we are out of oat milk",
        )
        self.assertEqual(clean_asr_text("language None<asr_text>"), "")
        self.assertEqual(clean_asr_text("hello"), "hello")


class HubTests(unittest.TestCase):
    def setUp(self) -> None:
        self.seen = []

        def fake(wav: bytes) -> dict:
            self.seen.append(wav[:12])
            return {"text": "we are out of oat milk", "raw": "language En<asr_text>we are out of oat milk", "model": "mock"}

        self.state = HubState(fake, stt_url="mock", stt_model="mock")
        self.httpd = make_server("127.0.0.1", 0, self.state)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()

    def test_health_and_utterance(self) -> None:
        health = json.loads(urlopen(f"http://127.0.0.1:{self.port}/health", timeout=2).read())
        self.assertTrue(health["ok"])
        wav = silence_wav()
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=wav,
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertTrue(payload["accepted"])
        self.assertTrue(payload["request_id"])
        self.assertEqual(self.state.utterances, 1)
        deadline = time.time() + 2
        while not self.seen and time.time() < deadline:
            time.sleep(0.01)
        self.assertTrue(self.seen)

    def test_rejects_wrong_rate(self) -> None:
        wav = wrap_pcm16(b"\x00\x00" * 800, 8000)
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=wav,
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        try:
            urlopen(req, timeout=2)
            self.fail("expected HTTPError")
        except Exception as exc:
            self.assertEqual(getattr(exc, "code", None), 400)

    def test_upload_retry_id_is_queued_once(self) -> None:
        started = threading.Event()
        release = threading.Event()
        calls: list[int] = []
        def slow(wav: bytes) -> dict:
            calls.append(len(wav))
            started.set()
            self.assertTrue(release.wait(3))
            return {"text": "test", "model": "mock"}
        self.state.transcribe_fn = slow
        def post() -> dict:
            req = Request(f"http://127.0.0.1:{self.port}/v1/utterance",
                          data=silence_wav(),
                          headers={"Content-Type": "audio/wav",
                                   "X-Hearth-Request-Id": "fixed-upload-id"})
            return json.loads(urlopen(req, timeout=1).read())
        first = post()
        self.assertTrue(started.wait(1))
        duplicate = post()
        self.assertEqual(first["request_id"], duplicate["request_id"])
        self.assertEqual(self.state.utterances, 1)
        release.set()
        self.state.jobs.join()
        self.assertEqual(len(calls), 1)

    def test_full_voice_queue_rejects_before_accepting_more_work(self) -> None:
        release = threading.Event()
        def slow(wav: bytes) -> dict:
            self.assertTrue(release.wait(3))
            return {"text": "test", "model": "mock"}
        self.state.transcribe_fn = slow
        for i in range(8):
            req = Request(f"http://127.0.0.1:{self.port}/v1/utterance",
                          data=silence_wav(),
                          headers={"Content-Type": "audio/wav",
                                   "X-Hearth-Request-Id": f"queue-{i}"})
            self.assertTrue(json.loads(urlopen(req, timeout=1).read())["accepted"])
        self.assertEqual(self.state.queue_depth, 8)
        req = Request(f"http://127.0.0.1:{self.port}/v1/utterance",
                      data=silence_wav(), headers={"Content-Type": "audio/wav"})
        with self.assertRaises(Exception) as rejected:
            urlopen(req, timeout=1)
        self.assertEqual(getattr(rejected.exception, "code", None), 429)
        release.set()
        self.state.jobs.join()
        self.assertEqual(self.state.utterances, 8)


if __name__ == "__main__":
    unittest.main()

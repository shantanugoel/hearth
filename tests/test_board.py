#!/usr/bin/env python3
"""Board store and hub filing tests. No SSH, no live STT."""

from __future__ import annotations

import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from hub.board import Board, poster_from_board, weather_line  # noqa: E402
from hub.hermes import HermesError  # noqa: E402
from hub.server import HubState, make_server  # noqa: E402
from hub.wavutil import wrap_pcm16  # noqa: E402


def silence_wav(ms: int = 400) -> bytes:
    n = 16000 * ms // 1000
    return wrap_pcm16(b"\x00\x00" * n, 16000)


class BoardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.board = Board(Path(self.tmp.name) / "board.json")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_add_and_complete(self) -> None:
        self.board.add("buy", "oat milk")
        self.board.add("pack", "swim kit", owner="Maya", when="Thursday")
        self.assertEqual(self.board.counts()["buy"], 1)
        self.board.complete("buy", text="milk")
        self.assertEqual(self.board.counts()["buy"], 0)
        self.assertEqual(self.board.counts()["pack"], 1)
        lines = self.board.today_lines()
        self.assertTrue(any("swim kit" in line for line in lines))

    def test_dedup_open_item(self) -> None:
        a = self.board.add("buy", "Eggs")
        b = self.board.add("buy", "eggs")
        self.assertEqual(a["id"], b["id"])
        self.assertEqual(self.board.counts()["buy"], 1)

    def test_owner_attaches_on_repeat(self) -> None:
        self.board.add("do", "call school")
        again = self.board.add("do", "call school", owner="Maya")
        self.assertEqual(again["owner"], "Maya")
        self.assertEqual(self.board.counts()["do"], 1)

    def test_menu_and_poster(self) -> None:
        self.board.set_menu("mon", "dal rice")
        self.board.add("do", "call school", owner="Maya")
        self.board.add("pack", "swim kit", owner="Maya", when="Thursday")
        self.board.set_weather("29C  fair  24-32")
        poster = poster_from_board(self.board)
        self.assertEqual(poster["weather"], "29C  fair  24-32")
        self.assertIn("n_buy", poster)
        self.assertIn("d0", poster)
        self.assertIn("p0", poster)
        self.assertIn("m0", poster)
        self.assertIn("Maya", poster["d0"])
        self.assertIn("Maya", poster["p0"])

    def test_weather_line(self) -> None:
        self.assertEqual(weather_line(0, 29.4, 32, 24), "29C  clear  24-32")


class HubBoardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.board = Board(Path(self.tmp.name) / "board.json")

        def fake_stt(wav: bytes) -> dict:
            return {"text": "we are out of oat milk", "raw": "oat milk", "model": "mock"}

        def fake_file(text: str, source: str) -> str:
            self.board.apply(
                [{"op": "add", "list": "buy", "text": "oat milk", "source": source}]
            )
            return "Added oat milk to Buy."

        self.state = HubState(
            fake_stt,
            stt_url="mock",
            stt_model="mock",
            board=self.board,
            file_fn=fake_file,
        )
        self.httpd = make_server("127.0.0.1", 0, self.state)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
        self.tmp.cleanup()

    def test_utterance_files_and_poster(self) -> None:
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=silence_wav(),
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertEqual(payload["text"], "we are out of oat milk")
        self.assertEqual(payload["ack"], "Added oat milk to Buy.")
        self.assertEqual(payload["b0"], "oat milk")
        self.assertEqual(payload["n_buy"], "1")
        poster = json.loads(
            urlopen(f"http://127.0.0.1:{self.port}/v1/poster", timeout=2).read()
        )
        self.assertEqual(poster["b0"], "oat milk")

    def test_apply_complete(self) -> None:
        self.board.add("buy", "eggs")
        body = json.dumps(
            {"ops": [{"op": "complete", "list": "buy", "text": "eggs"}]}
        ).encode()
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/board/apply",
            data=body,
            method="POST",
            headers={"Content-Type": "application/json"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["poster"]["n_buy"], "0")

    def test_hermes_failure_keeps_transcript(self) -> None:
        def boom(text: str, source: str) -> str:
            raise HermesError("ssh down")

        self.state.file_fn = boom
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=silence_wav(),
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertEqual(payload["text"], "we are out of oat milk")
        self.assertEqual(payload["ack"], "heard, not filed")
        health = json.loads(
            urlopen(f"http://127.0.0.1:{self.port}/health", timeout=2).read()
        )
        self.assertIn("ssh down", health["last_error"])


if __name__ == "__main__":
    unittest.main()

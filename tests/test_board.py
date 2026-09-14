#!/usr/bin/env python3
"""Board store and hub filing tests. No SSH, no live STT."""

from __future__ import annotations

import json
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from hub.board import Board, parse_hhmm, poster_from_board, weather_kind, weather_line  # noqa: E402
from hub.hermes import HermesError  # noqa: E402
from hub.server import HubState, make_server  # noqa: E402
from hub.wavutil import wrap_pcm16  # noqa: E402


def silence_wav(ms: int = 400) -> bytes:
    n = 16000 * ms // 1000
    return wrap_pcm16(b"\x00\x00" * n, 16000)


def wait_poster(port: int, timeout: float = 2.0) -> dict:
    deadline = time.time() + timeout
    last: dict = {}
    while time.time() < deadline:
        last = json.loads(urlopen(f"http://127.0.0.1:{port}/v1/poster", timeout=2).read())
        if last.get("pending") != "1":
            return last
        time.sleep(0.05)
    return last


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
        self.assertEqual(self.board.counts()["notes"], 1)
        lines = self.board.today_lines()
        self.assertTrue(any("swim kit" in line for line in lines))

    def test_migrate_do_pack(self) -> None:
        path = Path(self.tmp.name) / "legacy.json"
        path.write_text(
            json.dumps(
                {
                    "buy": [],
                    "do": [{"id": "d1", "text": "call school", "status": "open"}],
                    "pack": [{"id": "p1", "text": "swim kit", "status": "open", "owner": "Maya"}],
                    "menu": [],
                    "weather": {"line": "21C  drizzle  21-30"},
                    "meta": {"next_id": 3},
                }
            )
        )
        board = Board(path)
        self.assertEqual(board.counts()["notes"], 2)
        board.load()
        self.assertEqual(board.counts()["notes"], 2)
        self.assertNotIn("do", board.snapshot())
        self.assertNotIn("pack", board.snapshot())
        self.assertEqual(weather_kind(board.data["weather"]["line"]), "rain")

    def test_delete(self) -> None:
        self.board.add("notes", "leave bags by the door")
        gone = self.board.delete("notes", text="bags")
        self.assertIsNotNone(gone)
        self.assertEqual(self.board.counts()["notes"], 0)

    def test_alarm(self) -> None:
        self.assertEqual(parse_hhmm("7am"), "07:00")
        self.board.set_alarm("7:30", "school")
        nxt = self.board.next_alarm("06:00")
        self.assertEqual(nxt["hhmm"], "07:30")
        poster = poster_from_board(self.board)
        self.assertIn("07:30", poster["alarm"])
        self.assertEqual(poster["ahh"], "7")
        self.board.clear_alarm(text="school")
        self.assertIsNone(self.board.next_alarm())

    def test_dedup_open_item(self) -> None:
        a = self.board.add("buy", "Eggs")
        b = self.board.add("buy", "eggs")
        self.assertEqual(a["id"], b["id"])
        self.assertEqual(self.board.counts()["buy"], 1)

    def test_owner_attaches_on_repeat(self) -> None:
        self.board.add("do", "call school")
        again = self.board.add("do", "call school", owner="Maya")
        self.assertEqual(again["owner"], "Maya")
        self.assertEqual(self.board.counts()["notes"], 1)

    def test_menu_and_poster(self) -> None:
        self.board.set_menu("mon", "dal rice")
        self.board.add("do", "call school", owner="Maya")
        self.board.add("pack", "swim kit", owner="Maya", when="Thursday")
        self.board.set_weather("29C  fair  24-32")
        poster = poster_from_board(self.board)
        self.assertEqual(poster["weather"], "29C  fair  24-32")
        self.assertEqual(poster["wx"], "sun")
        self.assertIn("n_buy", poster)
        self.assertIn("n0", poster)
        self.assertIn("Maya", poster["n0"])
        self.assertEqual(poster["pending"], "0")

    def test_weather_line(self) -> None:
        self.assertEqual(weather_line(0, 29.4, 32, 24), "29C  clear  24-32")
        self.assertEqual(weather_kind(code=61), "rain")


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
        poster = wait_poster(self.port)
        self.assertEqual(poster["b0"], "oat milk")
        self.assertEqual(poster["n_buy"], "1")
        self.assertEqual(poster["pending"], "0")
        self.assertEqual(poster["ack"], "Added oat milk to Buy.")

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

    def test_apply_delete(self) -> None:
        self.board.add("notes", "plumber Thursday")
        body = json.dumps(
            {"ops": [{"op": "delete", "list": "notes", "text": "plumber"}]}
        ).encode()
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/board/apply",
            data=body,
            method="POST",
            headers={"Content-Type": "application/json"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["poster"]["n_notes"], "0")

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
        poster = wait_poster(self.port)
        self.assertEqual(poster["ack"], "heard, not filed")
        health = json.loads(
            urlopen(f"http://127.0.0.1:{self.port}/health", timeout=2).read()
        )
        self.assertIn("ssh down", health["last_error"])


if __name__ == "__main__":
    unittest.main()

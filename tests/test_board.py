#!/usr/bin/env python3
"""Board store and hub filing tests. No SSH, no live STT."""

from __future__ import annotations

import json
import importlib.util
import sys
import tempfile
import threading
import time
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from hub.board import Board, ack_for_results, parse_hhmm, poster_from_board, weather_kind, weather_line  # noqa: E402
from hub.commands import try_fast_command  # noqa: E402
from hub.hermes import HermesError  # noqa: E402
from hub.server import HubState, make_server  # noqa: E402
from hub.wavutil import wrap_pcm16  # noqa: E402
from tools.load_demo import demo_ops  # noqa: E402


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

    def test_spoken_note_reference_deletes_exact_row(self) -> None:
        note = self.board.add("notes", "testing")
        self.assertEqual(self.board.delete("notes", text="the testing note")["id"], note["id"])
        self.assertEqual(self.board.data["notes"], [])

    def test_unmatched_delete_cannot_generate_removed_ack(self) -> None:
        self.board.add("notes", "testing")
        results = self.board.apply([{"op": "delete", "list": "notes", "text": "unrelated"}])
        self.assertEqual(ack_for_results(results), (False, "No matching board item found."))
        self.assertEqual(self.board.counts()["notes"], 1)

    def test_fast_remove_never_adds_item(self) -> None:
        self.board.add("buy", "oat milk")
        ack = try_fast_command(self.board, "remove oat milk")
        self.assertEqual(ack, "Removed oat milk.")
        self.assertEqual(self.board.counts()["buy"], 0)
        ack = try_fast_command(self.board, "delete oat milk")
        self.assertEqual(ack, "Oat milk isn't on the board.")
        self.assertEqual(self.board.counts()["buy"], 0)

    def test_delete_all_notes_is_a_bulk_command(self) -> None:
        self.board.add("notes", "call school")
        self.board.add("notes", "pack bag")
        self.assertEqual(try_fast_command(self.board, "delete all the notes"), "Deleted 2 notes.")
        self.assertEqual(self.board.data["notes"], [])
        self.assertEqual(try_fast_command(self.board, "delete all the notes"), "There are no notes to delete.")

    def test_toggle_keeps_stable_id_and_unchecks(self) -> None:
        note = self.board.add("notes", "call school")
        self.board.toggle("notes", item_id=note["id"])
        self.assertEqual(self.board.data["notes"][0]["status"], "done")
        poster = poster_from_board(self.board)
        self.assertEqual(poster["nid0"], note["id"])
        self.assertEqual(poster["ns0"], "done")
        self.board.toggle("notes", item_id=note["id"])
        self.assertEqual(self.board.data["notes"][0]["status"], "open")

    def test_poster_window_reaches_later_notes(self) -> None:
        for i in range(14):
            self.board.add("notes", f"note {i}")
        poster = poster_from_board(self.board, notes_offset=6)
        self.assertEqual(poster["r_notes"], "14")
        self.assertEqual(poster["notes_offset"], "6")
        self.assertEqual(poster["n0"], "note 6")
        self.assertEqual(poster["n7"], "note 13")
        self.assertEqual(poster["n8"], "")

    def test_fast_take_off_and_complete(self) -> None:
        self.board.add("buy", "free-range eggs")
        self.assertEqual(
            try_fast_command(self.board, "take eggs off the shopping list"),
            "Removed free-range eggs.",
        )
        self.board.add("buy", "oat milk")
        self.assertEqual(try_fast_command(self.board, "we got milk"), "Done: oat milk.")
        self.assertEqual(self.board.counts()["buy"], 0)

    def test_fast_add_menu_and_alarm(self) -> None:
        self.assertEqual(
            try_fast_command(self.board, "we are out of oat milk"),
            "Added oat milk to Buy.",
        )
        self.assertEqual(
            try_fast_command(self.board, "we are out of oat milk"),
            "Oat milk is already on Buy.",
        )
        self.assertEqual(try_fast_command(self.board, "dinner is dal rice"), "Tonight: dal rice.")
        self.assertIn("dal rice", self.board.tonight_meal())
        self.assertEqual(
            try_fast_command(self.board, "set an alarm for 7am for school"),
            "Alarm at 07:00 for school.",
        )
        self.assertEqual(try_fast_command(self.board, "cancel the alarm"), "Alarm cleared.")
        self.board.set_alarm("8pm", "oven")
        self.assertEqual(try_fast_command(self.board, "remove the alarm"), "Alarm cleared.")

    def test_mixed_command_falls_through(self) -> None:
        self.assertIsNone(
            try_fast_command(self.board, "remove oat milk and pack Maya's bag")
        )

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

    def test_past_alarm_expires_and_disappears(self) -> None:
        alarm = self.board.set_alarm("7am", "school")
        self.assertIn("due_at", alarm)
        alarm["due_at"] = (datetime.now().astimezone() - timedelta(minutes=2)).isoformat()
        self.board.save()
        self.assertIsNone(self.board.next_alarm())
        self.assertEqual(poster_from_board(self.board)["alarm"], "")
        self.board.load()
        self.assertEqual(self.board.data["alarms"], [])

    def test_relative_timer_is_second_precise_one_shot_alarm(self) -> None:
        before = datetime.now().astimezone()
        result = self.board.apply([{"op": "set_timer", "seconds": 30}])
        after = datetime.now().astimezone()
        self.assertEqual(ack_for_results(result), (True, "Alarm in 30 seconds."))
        alarm = result[0]["item"]
        due = datetime.fromisoformat(alarm["due_at"])
        self.assertGreaterEqual(due, before + timedelta(seconds=30))
        self.assertLessEqual(due, after + timedelta(seconds=31))
        poster = poster_from_board(self.board)
        self.assertEqual(poster["asec"], str(due.second))
        self.assertIn(due.strftime("%H:%M:%S"), poster["alarm"])
        self.assertEqual(poster["aid"], alarm["id"])
        with self.assertRaises(ValueError):
            self.board.set_timer(0)
        with self.assertRaises(ValueError):
            self.board.set_timer("30")

    def test_demo_board_covers_each_item_and_meal_type(self) -> None:
        self.board.add("buy", "real shopping")
        self.board.add("notes", "real note")
        self.board.set_menu("mon", "real dinner")
        self.board.set_alarm("08:00", "real alarm")
        existing = self.board.snapshot()
        results = self.board.apply(demo_ops(existing), source="demo")
        self.assertTrue(ack_for_results(results)[0])
        self.assertEqual(len(self.board.data["buy"]), 10)
        self.assertEqual(len(self.board.data["notes"]), 10)
        self.assertEqual({row["kind"] for row in self.board.data["notes"]}, {"do", "pack", "note"})
        self.assertEqual(len(self.board.data["menu"]), 21)
        self.assertEqual(len(self.board.data["alarms"]), 1)
        self.assertFalse(any(row["text"].startswith("real") for row in self.board.data["buy"] + self.board.data["notes"]))
        self.assertEqual(sum(row["status"] == "done" for row in self.board.data["buy"]), 2)
        self.assertEqual(sum(row["status"] == "done" for row in self.board.data["notes"]), 2)

    def test_all_meal_slots(self) -> None:
        day = time.strftime("%a").casefold()[:3]
        self.assertIn("breakfast", try_fast_command(self.board, "breakfast is eggs"))
        self.assertIn("lunch", try_fast_command(self.board, "lunch is dal"))
        self.assertEqual(try_fast_command(self.board, "dinner is soup"), "Tonight: soup.")
        self.assertEqual(self.board.tonight_meal(), "soup")
        today = [r for r in self.board.data["menu"] if r["weekday"] == day]
        self.assertEqual({r["slot"] for r in today}, {"breakfast", "lunch", "dinner"})
        poster = poster_from_board(self.board)
        self.assertTrue(any(poster[f"m{i}"] == "Breakfast: eggs" for i in range(21)))

    def test_menu_preview_uses_next_meals_after_current_time(self) -> None:
        self.board.set_menu("mon", "eggs", slot="breakfast")
        self.board.set_menu("mon", "dal", slot="lunch")
        self.board.set_menu("mon", "pasta", slot="dinner")
        self.board.set_menu("tue", "rice", slot="lunch")
        monday_afternoon = datetime(2026, 9, 14, 14, 0,
                                    tzinfo=datetime.now().astimezone().tzinfo)
        poster = poster_from_board(self.board, now=monday_afternoon)
        self.assertEqual([poster[f"m{i}"] for i in range(4)],
                         ["Breakfast: eggs", "Lunch: dal", "Dinner: pasta", "Lunch: rice"])
        self.assertEqual((poster["pm0"], poster["pm1"]),
                         ("Dinner: pasta", "Lunch: rice"))

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

    def test_ack_expires_from_poster(self) -> None:
        self.board.set_meta(ack="Removed oat milk.")
        self.assertEqual(poster_from_board(self.board)["ack"], "Removed oat milk.")
        self.board.data["meta"]["ack_at"] = time.time() - 21
        self.assertEqual(poster_from_board(self.board)["ack"], "")


class HubBoardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.board = Board(Path(self.tmp.name) / "board.json")

        def fake_stt(wav: bytes) -> dict:
            return {"text": "we are out of oat milk", "raw": "oat milk", "model": "mock"}

        def fake_file(text: str, source: str, snapshot: dict) -> str:
            self.assertIn("buy", snapshot)
            body = json.dumps({"run_id": snapshot["meta"]["file_run_id"],
                               "ops": [{"op": "add", "list": "buy", "text": "oat milk"}]}).encode()
            req = Request(f"http://127.0.0.1:{self.port}/v1/board/apply",
                          data=body, headers={"Content-Type": "application/json"})
            with urlopen(req, timeout=2) as response:
                self.assertEqual(json.load(response)["ack"], "Added oat milk to Buy.")
            return "A made-up agent reply."

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
        self.assertTrue(payload["accepted"])
        self.assertTrue(payload["request_id"])
        poster = wait_poster(self.port)
        self.assertEqual(poster["b0"], "oat milk")
        self.assertEqual(poster["n_buy"], "1")
        self.assertEqual(poster["pending"], "0")
        self.assertEqual(poster["ack"], "Added oat milk to Buy.")
        self.assertEqual(poster["heard"], "we are out of oat milk")

    def test_voice_requests_reply_immediately_and_file_in_order(self) -> None:
        first_started = threading.Event()
        release_first = threading.Event()
        calls: list[str] = []
        first_size = len(silence_wav(400))

        def stt(wav: bytes) -> dict:
            if len(wav) == first_size:
                first_started.set()
                self.assertTrue(release_first.wait(3))
                return {"text": "add queue trial", "model": "mock"}
            return {"text": "delete queue trial", "model": "mock"}

        def file(text: str, source: str, snapshot: dict) -> str:
            calls.append(text)
            op = ({"op": "add", "list": "notes", "text": "queue trial"}
                  if text.startswith("add") else
                  {"op": "delete", "list": "notes", "id": snapshot["notes"][0]["id"]})
            body = json.dumps({"run_id": snapshot["meta"]["file_run_id"],
                               "ops": [op]}).encode()
            req = Request(f"http://127.0.0.1:{self.port}/v1/board/apply",
                          data=body, headers={"Content-Type": "application/json"})
            urlopen(req, timeout=2).close()
            return "filed"

        self.state.transcribe_fn = stt
        self.state.file_fn = file
        def post(ms: int, client_id: str) -> dict:
            req = Request(f"http://127.0.0.1:{self.port}/v1/utterance",
                          data=silence_wav(ms),
                          headers={"Content-Type": "audio/wav",
                                   "X-Hearth-Request-Id": client_id})
            return json.loads(urlopen(req, timeout=1).read())

        first = post(400, "queue-one")
        self.assertTrue(first_started.wait(1))
        second = post(500, "queue-two")
        self.assertNotEqual(first["request_id"], second["request_id"])
        self.assertEqual(second["queue"], 2)
        self.assertEqual(calls, [])
        release_first.set()
        poster = wait_poster(self.port, timeout=4)
        self.assertEqual(calls, ["add queue trial", "delete queue trial"])
        self.assertEqual(poster["ack"], "Removed queue trial.")
        self.assertEqual(self.board.data["notes"], [])

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
        self.assertEqual(json.loads(urlopen(f"http://127.0.0.1:{self.port}/v1/poster").read())["n_buy"], "0")

    def test_native_hermes_duration_tool_sets_alarm(self) -> None:
        spec = importlib.util.spec_from_file_location("hearth_mcp", ROOT / "hermes-profile/hearth_mcp.py")
        self.assertIsNotNone(spec)
        bridge = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bridge)
        bridge.HUB = f"http://127.0.0.1:{self.port}"
        result = bridge.call("hearth_set_timer", {"seconds": 30})
        self.assertTrue(result["ok"])
        self.assertEqual(result["ack"], "Alarm in 30 seconds.")
        self.assertEqual(self.board.data["alarms"][0]["duration_seconds"], 30)
        self.assertEqual(json.loads(urlopen(f"http://127.0.0.1:{self.port}/v1/poster").read())["aid"],
                         self.board.data["alarms"][0]["id"])

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
        self.assertEqual(json.loads(urlopen(f"http://127.0.0.1:{self.port}/v1/poster").read())["n_notes"], "0")

    def test_every_voice_command_reaches_agent_with_board_ids(self) -> None:
        self.board.add("buy", "oat milk")
        hermes_calls: list[tuple[str, str]] = []
        self.state.transcribe_fn = lambda wav: {
            "text": "remove oat milk",
            "raw": "remove oat milk",
            "model": "mock",
        }
        def remove(text: str, source: str, snapshot: dict) -> str:
            item_id = snapshot["buy"][0]["id"]
            hermes_calls.append((text, item_id))
            body = json.dumps({"run_id": snapshot["meta"]["file_run_id"],
                               "ops": [{"op": "delete", "list": "buy", "id": item_id}]}).encode()
            req = Request(f"http://127.0.0.1:{self.port}/v1/board/apply",
                          data=body, headers={"Content-Type": "application/json"})
            urlopen(req, timeout=2).close()
            return "Agent said something else."
        self.state.file_fn = remove
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=silence_wav(),
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertEqual(payload["pending"], "1")
        poster = wait_poster(self.port)
        self.assertEqual(poster["ack"], "Removed oat milk.")
        self.assertEqual(poster["n_buy"], "0")
        self.assertEqual(hermes_calls[0][0], "remove oat milk")

    def test_false_agent_removed_claim_is_not_displayed(self) -> None:
        self.board.add("notes", "testing")
        def false_remove(text: str, source: str, snapshot: dict) -> str:
            body = json.dumps({"run_id": snapshot["meta"]["file_run_id"],
                               "ops": [{"op": "delete", "list": "notes", "text": "wrong note"}]}).encode()
            req = Request(f"http://127.0.0.1:{self.port}/v1/board/apply",
                          data=body, headers={"Content-Type": "application/json"})
            with urlopen(req, timeout=2) as response:
                self.assertFalse(json.load(response)["ok"])
            return "Removed the testing note."
        self.state.file_fn = false_remove
        self.state.transcribe_fn = lambda wav: {
            "text": "delete the testing note", "raw": "delete the testing note", "model": "mock"
        }
        req = Request(f"http://127.0.0.1:{self.port}/v1/utterance",
                      data=silence_wav(), headers={"Content-Type": "audio/wav"})
        urlopen(req, timeout=2).close()
        poster = wait_poster(self.port)
        self.assertEqual(poster["ack"], "No matching board item found.")
        self.assertEqual(self.board.counts()["notes"], 1)

    def test_hermes_failure_keeps_transcript(self) -> None:
        def boom(text: str, source: str, snapshot: dict) -> str:
            raise HermesError("ssh down")

        self.state.file_fn = boom
        self.state.transcribe_fn = lambda wav: {
            "text": "pack Maya's swim kit for Thursday",
            "raw": "pack Maya's swim kit for Thursday",
            "model": "mock",
        }
        req = Request(
            f"http://127.0.0.1:{self.port}/v1/utterance",
            data=silence_wav(),
            method="POST",
            headers={"Content-Type": "audio/wav"},
        )
        payload = json.loads(urlopen(req, timeout=2).read())
        self.assertTrue(payload["accepted"])
        poster = wait_poster(self.port)
        self.assertEqual(poster["ack"], "Heard, not filed.")
        self.assertEqual(poster["heard"], "pack Maya's swim kit for Thursday")
        health = json.loads(
            urlopen(f"http://127.0.0.1:{self.port}/health", timeout=2).read()
        )
        self.assertIn("ssh down", health["last_error"])


if __name__ == "__main__":
    unittest.main()

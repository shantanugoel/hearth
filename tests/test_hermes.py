"""Hermes invocation should keep board data out of process arguments and errors."""

from __future__ import annotations

import subprocess
import unittest
from unittest.mock import patch

from hub.hermes import HermesError, file_utterance


class HermesInvocationTests(unittest.TestCase):
    def test_remote_prompt_reads_board_with_tool(self) -> None:
        snapshot = {"notes": [{"text": "private household note"}], "meta": {"file_run_id": "trial"}}
        with patch("hub.hermes.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "Filed.\n", "")) as run:
            file_utterance("30 second timer", board_snapshot=snapshot)
        remote = run.call_args.args[0][-1]
        self.assertIn("hearth_get_board", remote)
        self.assertIn("timeout --signal=TERM", remote)
        self.assertNotIn("private household note", remote)

    def test_timeout_error_does_not_echo_board_or_command(self) -> None:
        snapshot = {"notes": [{"text": "private household note"}], "meta": {"file_run_id": "trial"}}
        expired = subprocess.TimeoutExpired(["ssh", "private household note"], 90)
        with patch("hub.hermes.subprocess.run", side_effect=expired):
            with self.assertRaises(HermesError) as raised:
                file_utterance("30 second timer", board_snapshot=snapshot)
        self.assertEqual(str(raised.exception), "hermes profile timed out after 90s")
        self.assertNotIn("private household note", str(raised.exception))


if __name__ == "__main__":
    unittest.main()

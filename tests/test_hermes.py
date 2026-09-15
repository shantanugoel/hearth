"""Hermes invocation should keep board data out of process arguments and errors."""

from __future__ import annotations

import os
import subprocess
import unittest
from unittest.mock import patch

from hub.hermes import HermesError, file_utterance


class HermesInvocationTests(unittest.TestCase):
    def test_remote_prompt_reads_board_with_tool(self) -> None:
        snapshot = {"notes": [{"text": "private household note"}], "meta": {"file_run_id": "trial"}}
        with patch("hub.hermes.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "Filed.\n", "")) as run:
            file_utterance("30 second timer", board_snapshot=snapshot,
                           method="ssh", ssh="hermes.example", command="hearth")
        remote = run.call_args.args[0][-1]
        self.assertEqual(run.call_args.args[0][-2], "hermes.example")
        self.assertIn("hearth_get_board", remote)
        self.assertIn("timeout --signal=TERM", remote)
        self.assertNotIn("private household note", remote)

    def test_timeout_error_does_not_echo_board_or_command(self) -> None:
        snapshot = {"notes": [{"text": "private household note"}], "meta": {"file_run_id": "trial"}}
        expired = subprocess.TimeoutExpired(["ssh", "private household note"], 90)
        with patch("hub.hermes.subprocess.run", side_effect=expired):
            with self.assertRaises(HermesError) as raised:
                file_utterance("30 second timer", board_snapshot=snapshot,
                               method="ssh", ssh="hermes.example", command="hearth")
        self.assertEqual(str(raised.exception), "hermes profile timed out after 90s")
        self.assertNotIn("private household note", str(raised.exception))

    def test_local_method_uses_configured_profile_command_and_run_id(self) -> None:
        with patch("hub.hermes.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "Filed.\n", "")) as run:
            file_utterance("30 second alarm", board_snapshot={"meta": {"file_run_id": "local-trial"}},
                           method="local", command="hermes -p hearth")
        argv = run.call_args.args[0]
        self.assertEqual(argv[:4], ["timeout", "--signal=TERM", "--kill-after=5s", "85s"])
        self.assertEqual(argv[4:7], ["hermes", "-p", "hearth"])
        self.assertEqual(run.call_args.kwargs["env"]["HEARTH_RUN_ID"], "local-trial")
        self.assertIn("hearth_set_timer", argv[-1])

    def test_ssh_method_requires_a_host(self) -> None:
        with patch.dict(os.environ, {"HEARTH_HERMES_SSH_HOST": "", "HEARTH_HERMES_SSH": ""}):
            with self.assertRaisesRegex(HermesError, "HEARTH_HERMES_SSH_HOST"):
                file_utterance("30 second timer", method="ssh", ssh="", command="hearth")

    def test_invalid_configured_command_reports_clear_error(self) -> None:
        with self.assertRaisesRegex(HermesError, "invalid quoting"):
            file_utterance("30 second timer", method="local", command="'hearth")

    def test_env_selects_method_command_and_timeout(self) -> None:
        config = {"HEARTH_HERMES_METHOD": "local",
                  "HEARTH_HERMES_COMMAND": "hermes -p kitchen",
                  "HEARTH_HERMES_TIMEOUT_S": "45"}
        with patch.dict(os.environ, config):
            with patch("hub.hermes.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "Filed.\n", "")) as run:
                file_utterance("30 second timer")
        argv = run.call_args.args[0]
        self.assertEqual(argv[:7], ["timeout", "--signal=TERM", "--kill-after=5s", "40s",
                                    "hermes", "-p", "kitchen"])
        self.assertEqual(run.call_args.kwargs["timeout"], 45.0)


if __name__ == "__main__":
    unittest.main()

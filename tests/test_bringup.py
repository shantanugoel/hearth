#!/usr/bin/env python3
"""Host checks for bring-up screens. No serial hardware required."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "sim"


class ModelTests(unittest.TestCase):
    def test_screen_wrap(self) -> None:
        binary = ROOT / "tests" / "test_hearth_model"
        subprocess.check_call(
            [
                "g++",
                "-std=c++17",
                "-B/usr/bin",
                "-I",
                str(ROOT / "firmware/main"),
                "-I",
                str(ROOT / "firmware/components/zectrix_board/include"),
                str(ROOT / "tests/test_hearth_model.cc"),
                "-o",
                str(binary),
            ]
        )
        subprocess.check_call([str(binary)])


class SimulatorTests(unittest.TestCase):
    def test_sim_writes_poster_pngs(self) -> None:
        subprocess.check_call(["make", "-C", str(SIM), "run"])
        for name in (
            "01-today.png",
            "02-buy.png",
            "03-menu.png",
            "04-notes.png",
            "05-pulse.png",
            "06-listen.png",
            "07-filing.png",
            "08-removed.png",
        ):
            path = SIM / "out" / name
            self.assertTrue(path.exists(), path)
            self.assertGreater(path.stat().st_size, 800)
            with path.open("rb") as handle:
                self.assertEqual(handle.read(8), b"\x89PNG\r\n\x1a\n")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Offline tests for tools/note4_flash.py. No serial hardware required."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools" / "note4_flash.py"


def load_module():
    spec = importlib.util.spec_from_file_location("note4_flash", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


nf = load_module()


class ChecksumTests(unittest.TestCase):
    def test_sha256_and_sidecar_roundtrip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "factory-flash-16MiB.bin"
            payload = b"hearth-factory-test\n" * 32
            image.write_bytes(payload)
            digest = nf.sha256_file(image)
            self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
            sidecar = nf.write_sidecar(image, digest)
            self.assertEqual(sidecar.name, "factory-flash-16MiB.bin.sha256")
            self.assertEqual(nf.read_sidecar(image), digest)
            self.assertEqual(nf.require_checksum(image, digest), digest)

    def test_sidecar_mismatch_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "dump.bin"
            image.write_bytes(b"abc")
            nf.write_sidecar(image, "0" * 64)
            with self.assertRaises(nf.FlashError):
                nf.require_checksum(image, None)


class ImageSizeTests(unittest.TestCase):
    def test_require_image_rejects_wrong_size(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "tiny.bin"
            image.write_bytes(b"not 16MiB")
            with self.assertRaises(nf.FlashError) as ctx:
                nf.require_image(image)
            self.assertIn("exactly", str(ctx.exception))

    def test_require_image_accepts_16mib(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "full.bin"
            # Sparse-ish: write the last byte so st_size is 16 MiB without
            # filling the whole file on filesystems that support it. Fall back
            # to truncate.
            with image.open("wb") as handle:
                handle.truncate(nf.FLASH_SIZE)
            nf.require_image(image)


class ParserTests(unittest.TestCase):
    def test_parse_mac(self) -> None:
        sample = (
            "Connected to ESP32-S3 on /dev/ttyACM0:\n"
            "MAC:                28:84:85:32:07:e0\n"
            "Detected flash size: 16MB\n"
        )
        self.assertEqual(nf.parse_mac(sample), "28:84:85:32:07:e0")
        self.assertTrue(nf.parse_flash_size_ok(sample))
        self.assertFalse(nf.parse_flash_size_ok("Detected flash size: 8MB"))

    def test_restore_refuses_without_latch(self) -> None:
        parser = nf.build_parser()
        args = parser.parse_args(["restore"])
        self.assertFalse(args.i_know_this_overwrites_the_device)

    def test_restore_dry_run_flag(self) -> None:
        parser = nf.build_parser()
        args = parser.parse_args(["restore", "--dry-run"])
        self.assertTrue(args.dry_run)

    def test_subcommands_exist(self) -> None:
        parser = nf.build_parser()
        for cmd in ("info", "dump", "verify", "restore"):
            args = parser.parse_args([cmd] if cmd != "restore" else [cmd, "--dry-run"])
            self.assertEqual(args.cmd, cmd)


class SafetyTests(unittest.TestCase):
    def test_restore_without_latch_does_not_write(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "full.bin"
            with image.open("wb") as handle:
                handle.truncate(nf.FLASH_SIZE)
            digest = nf.sha256_file(image)
            nf.write_sidecar(image, digest)

            identity = (
                "MAC:                28:84:85:32:07:e0\n"
                "Detected flash size: 16MB\n"
            )
            with mock.patch.object(nf, "find_esptool", return_value=Path("/bin/true")), mock.patch.object(
                nf, "run_esptool", return_value=identity
            ) as run:
                argv = [
                    "--image",
                    str(image),
                    "--expect-mac",
                    "28:84:85:32:07:e0",
                    "restore",
                ]
                code = nf.main(argv)
                self.assertEqual(code, 1)
                self.assertTrue(all("write-flash" not in call.args[3] for call in run.call_args_list))

    def test_restore_dry_run_skips_write_flash(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "full.bin"
            with image.open("wb") as handle:
                handle.truncate(nf.FLASH_SIZE)
            digest = nf.sha256_file(image)
            nf.write_sidecar(image, digest)
            identity = (
                "MAC:                28:84:85:32:07:e0\n"
                "Detected flash size: 16MB\n"
            )
            with mock.patch.object(nf, "find_esptool", return_value=Path("/bin/true")), mock.patch.object(
                nf, "run_esptool", return_value=identity
            ) as run:
                argv = [
                    "--image",
                    str(image),
                    "--expect-mac",
                    "28:84:85:32:07:e0",
                    "restore",
                    "--dry-run",
                ]
                buf = io.StringIO()
                with mock.patch.object(sys, "stdout", buf):
                    code = nf.main(argv)
                self.assertEqual(code, 0)
                self.assertTrue(all("write-flash" not in call.args[3] for call in run.call_args_list))
                self.assertIn("dry-run", buf.getvalue())

    def test_dump_refuses_wrong_mac(self) -> None:
        identity = (
            "MAC:                aa:bb:cc:dd:ee:ff\n"
            "Detected flash size: 16MB\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "out.bin"
            with mock.patch.object(nf, "find_esptool", return_value=Path("/bin/true")), mock.patch.object(
                nf, "run_esptool", return_value=identity
            ):
                code = nf.main(
                    [
                        "--image",
                        str(image),
                        "--expect-mac",
                        "28:84:85:32:07:e0",
                        "dump",
                    ]
                )
                self.assertEqual(code, 1)
                self.assertFalse(image.exists())


if __name__ == "__main__":
    unittest.main()

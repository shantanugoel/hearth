#!/usr/bin/env python3
"""Dump, verify, and restore the full 16 MiB flash of a ZECTRIX NOTE4.

This is the factory-restore path. It never writes flash unless the caller
passes --i-know-this-overwrites-the-device on the restore subcommand.

The 16 MiB image is a local artifact. Do not commit it. See
docs/FACTORY_FLASH.md.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

CHIP = "esp32s3"
FLASH_SIZE = 16 * 1024 * 1024
FLASH_SIZE_LABEL = "16MB"
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 921600
EXPECTED_MAC = "28:84:85:32:07:e0"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DUMP = REPO_ROOT / "backups" / "factory-flash-16MiB.bin"
MIRROR_DIR = Path.home() / ".local" / "share" / "hearth"
ESPTOOL_CANDIDATES = (
    Path.home() / ".espressif/tools/python/v6.1/venv/bin/esptool",
    Path.home() / ".espressif/python_env/idf5.5_py3.14_env/bin/esptool",
)


class FlashError(RuntimeError):
    pass


def find_esptool() -> Path:
    env = os.environ.get("ESPTOOL")
    if env:
        path = Path(env)
        if path.exists():
            return path
        raise FlashError(f"ESPTOOL={env} does not exist")
    env_dir = os.environ.get("IDF_PYTHON_ENV_PATH")
    if env_dir:
        candidate = Path(env_dir) / "bin" / "esptool"
        if candidate.exists():
            return candidate
    for candidate in ESPTOOL_CANDIDATES:
        if candidate.exists():
            return candidate
    which = shutil.which("esptool")
    if which:
        return Path(which)
    raise FlashError(
        "esptool not found. Install ESP-IDF via EIM, or set ESPTOOL="
        "to the esptool binary."
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sidecar_path(image: Path) -> Path:
    return image.with_suffix(image.suffix + ".sha256")


def write_sidecar(image: Path, digest: str) -> Path:
    path = sidecar_path(image)
    path.write_text(f"{digest}  {image.name}\n", encoding="utf-8")
    return path


def read_sidecar(image: Path) -> str | None:
    path = sidecar_path(image)
    if not path.exists():
        return None
    line = path.read_text(encoding="utf-8").strip().split()
    if not line:
        return None
    return line[0]


def wait_for_port(port: str, timeout_s: float = 20.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if Path(port).exists():
            return
        time.sleep(0.2)
    raise FlashError(f"serial port {port} did not appear within {timeout_s:.0f}s")


def run_esptool(esptool: Path, port: str, baud: int, args: list[str]) -> str:
    wait_for_port(port)
    cmd = [
        str(esptool),
        "--chip",
        CHIP,
        "--port",
        port,
        "--baud",
        str(baud),
        "--after",
        "hard-reset",
        *args,
    ]
    try:
        completed = subprocess.run(
            cmd,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except subprocess.CalledProcessError as exc:
        output = exc.stdout or ""
        raise FlashError(
            f"esptool failed ({exc.returncode}): {' '.join(cmd)}\n{output}"
        ) from exc
    return completed.stdout


def require_image(image: Path) -> None:
    if not image.exists():
        raise FlashError(f"image not found: {image}")
    size = image.stat().st_size
    if size != FLASH_SIZE:
        raise FlashError(
            f"{image} is {size} bytes; NOTE4 factory dump must be "
            f"exactly {FLASH_SIZE} (16 MiB)"
        )


def require_checksum(image: Path, expected: str | None) -> str:
    digest = sha256_file(image)
    sidecar = read_sidecar(image)
    if sidecar and sidecar != digest:
        raise FlashError(
            f"SHA-256 mismatch versus sidecar {sidecar_path(image)}:\n"
            f"  file     {digest}\n"
            f"  sidecar  {sidecar}"
        )
    if expected and expected != digest:
        raise FlashError(
            f"SHA-256 mismatch versus --expect-sha256:\n"
            f"  file     {digest}\n"
            f"  expected {expected}"
        )
    return digest


def parse_mac(output: str) -> str | None:
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.lower().startswith("mac:"):
            return stripped.split(":", 1)[1].strip().lower()
    return None


def parse_flash_size_ok(output: str) -> bool:
    return "Detected flash size: 16MB" in output


def cmd_info(args: argparse.Namespace) -> int:
    esptool = find_esptool()
    print(f"esptool: {esptool}")
    print(f"port:    {args.port}")
    output = run_esptool(esptool, args.port, args.baud, ["flash-id"])
    print(output)
    mac = parse_mac(output)
    if mac:
        print(f"parsed MAC: {mac}")
        if args.expect_mac and mac != args.expect_mac.lower():
            raise FlashError(
                f"MAC {mac} does not match expected {args.expect_mac.lower()}"
            )
    if not parse_flash_size_ok(output):
        raise FlashError("flash-id did not report 16MB; refusing to treat this as NOTE4")
    return 0


def cmd_dump(args: argparse.Namespace) -> int:
    esptool = find_esptool()
    image: Path = args.image
    image.parent.mkdir(parents=True, exist_ok=True)
    if image.exists() and not args.force:
        raise FlashError(f"{image} already exists; pass --force to overwrite a local copy")

    print(f"Identifying {args.port} ...")
    identity = run_esptool(esptool, args.port, args.baud, ["flash-id"])
    print(identity)
    mac = parse_mac(identity)
    if args.expect_mac:
        if mac is None:
            raise FlashError("could not parse MAC from flash-id")
        if mac != args.expect_mac.lower():
            raise FlashError(
                f"MAC {mac} does not match expected {args.expect_mac.lower()}"
            )
    if not parse_flash_size_ok(identity):
        raise FlashError("flash-id did not report 16MB; refusing to dump")

    print(f"Dumping 16 MiB to {image} ...")
    dump_out = run_esptool(
        esptool,
        args.port,
        args.baud,
        ["read-flash", "0x0", str(FLASH_SIZE), str(image)],
    )
    print(dump_out)
    require_image(image)
    digest = sha256_file(image)
    write_sidecar(image, digest)
    print(f"SHA-256  {digest}")
    print(f"sidecar  {sidecar_path(image)}")

    if args.mirror:
        MIRROR_DIR.mkdir(parents=True, exist_ok=True)
        mirror = MIRROR_DIR / image.name
        shutil.copy2(image, mirror)
        write_sidecar(mirror, digest)
        print(f"mirror   {mirror}")

    print("Dump complete. Run `tools/note4_flash.py verify` next.")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    esptool = find_esptool()
    image: Path = args.image
    require_image(image)
    digest = require_checksum(image, args.expect_sha256)
    print(f"image    {image}")
    print(f"SHA-256  {digest}")
    print(f"Verifying device flash against {image} ...")
    output = run_esptool(
        esptool,
        args.port,
        args.baud,
        ["verify-flash", "--diff", "0x0", str(image)],
    )
    print(output)
    print("verify-flash: OK (dump matches the chip)")
    return 0


def cmd_restore(args: argparse.Namespace) -> int:
    esptool = find_esptool()
    image: Path = args.image
    require_image(image)
    digest = require_checksum(image, args.expect_sha256)
    print(f"image    {image}")
    print(f"SHA-256  {digest}")

    identity = run_esptool(esptool, args.port, args.baud, ["flash-id"])
    print(identity)
    mac = parse_mac(identity)
    if args.expect_mac:
        if mac is None:
            raise FlashError("could not parse MAC from flash-id")
        if mac != args.expect_mac.lower():
            raise FlashError(
                f"MAC {mac} does not match expected {args.expect_mac.lower()}"
            )
    if not parse_flash_size_ok(identity):
        raise FlashError("flash-id did not report 16MB; refusing to restore")

    if args.dry_run:
        print("dry-run: image, checksum, chip, and MAC checks passed. No write.")
        return 0

    if not args.i_know_this_overwrites_the_device:
        raise FlashError(
            "refusing to write flash. Re-run with "
            "--i-know-this-overwrites-the-device after reading "
            "docs/FACTORY_FLASH.md"
        )

    print(f"Writing 16 MiB from {image} to {args.port} ...")
    output = run_esptool(
        esptool,
        args.port,
        args.baud,
        [
            "write-flash",
            "--flash-mode",
            "keep",
            "--flash-freq",
            "keep",
            "--flash-size",
            FLASH_SIZE_LABEL,
            "0x0",
            str(image),
        ],
    )
    print(output)
    print("Restore write complete. Verifying ...")
    verify_out = run_esptool(
        esptool,
        args.port,
        args.baud,
        ["verify-flash", "0x0", str(image)],
    )
    print(verify_out)
    print("restore: OK")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("NOTE4_PORT", DEFAULT_PORT))
    parser.add_argument("--baud", type=int, default=int(os.environ.get("NOTE4_BAUD", DEFAULT_BAUD)))
    parser.add_argument(
        "--image",
        type=Path,
        default=DEFAULT_DUMP,
        help="16 MiB dump path (default: backups/factory-flash-16MiB.bin)",
    )
    parser.add_argument(
        "--expect-mac",
        default=os.environ.get("NOTE4_EXPECT_MAC", EXPECTED_MAC),
        help="Refuse to operate on a chip with a different MAC. Empty string disables.",
    )
    parser.add_argument("--expect-sha256", default=None)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info", help="Print chip / flash identity")
    dump = sub.add_parser("dump", help="Read the full 16 MiB flash")
    dump.add_argument("--force", action="store_true")
    dump.add_argument(
        "--mirror",
        action="store_true",
        default=True,
        help=f"Also copy the dump to {MIRROR_DIR} (default: on)",
    )
    dump.add_argument("--no-mirror", action="store_false", dest="mirror")
    sub.add_parser("verify", help="Compare the dump to the chip without writing")
    restore = sub.add_parser("restore", help="Write the dump back to the chip")
    restore.add_argument(
        "--i-know-this-overwrites-the-device",
        action="store_true",
        help="Required safety latch. Restore is destructive.",
    )
    restore.add_argument(
        "--dry-run",
        action="store_true",
        help="Check image, checksum, chip, and MAC, then stop before write-flash",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if hasattr(args, "expect_mac") and args.expect_mac == "":
        args.expect_mac = None
    try:
        if args.cmd == "info":
            return cmd_info(args)
        if args.cmd == "dump":
            return cmd_dump(args)
        if args.cmd == "verify":
            return cmd_verify(args)
        if args.cmd == "restore":
            return cmd_restore(args)
        parser.error(f"unknown command {args.cmd}")
    except FlashError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

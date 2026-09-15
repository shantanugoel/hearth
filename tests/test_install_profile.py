"""Profile installer configures a local target and previews SSH without connecting."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSTALLER = ROOT / "tools/install_hearth_profile.sh"


class ProfileInstallerTests(unittest.TestCase):
    def test_local_install_copies_profile_and_sets_hub_url(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            profile = root / "profile"
            cli = root / "hermes-test"
            cli.write_text(
                "#!/bin/sh\n"
                "if [ \"$1 $2\" = 'mcp list' ]; then exit 0; fi\n"
                "if [ \"$1 $2\" = 'mcp add' ]; then\n"
                "  profile_dir=$(dirname \"$(dirname \"$5\")\")\n"
                "  printf 'mcp_servers:\\n  hearth-board:\\n    command: %s\\n' \"$5\" > \"$profile_dir/config.yaml\"\n"
                "  exit 0\n"
                "fi\n"
                "exit 1\n"
            )
            cli.chmod(0o755)
            env_file = root / ".env"
            env_file.write_text(
                f"HEARTH_HUB_URL=http://hub.example:8790\n"
                f"HEARTH_HERMES_METHOD=local\n"
                f"HEARTH_HERMES_PROFILE_DIR={profile}\n"
                f"HEARTH_HERMES_COMMAND={cli}\n"
            )
            env = os.environ.copy()
            env["HEARTH_ENV_FILE"] = str(env_file)
            result = subprocess.run([str(INSTALLER)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("hub.example", result.stdout)
            self.assertNotIn(str(profile), result.stdout)
            self.assertTrue((profile / "SOUL.md").exists())
            self.assertTrue((profile / "skills/productivity/hearth-board/SKILL.md").exists())
            self.assertTrue((profile / "tools/hearth_mcp.py").exists())
            self.assertIn("HEARTH_HUB_URL=http://hub.example:8790", (profile / ".env").read_text())
            self.assertIn("HEARTH_RUN_ID", (profile / "config.yaml").read_text())

    def test_ssh_dry_run_needs_no_connection(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            env_file = Path(temp) / ".env"
            env_file.write_text(
                "HEARTH_HUB_URL=http://hub.example:8790\n"
                "HEARTH_HERMES_METHOD=ssh\n"
                "HEARTH_HERMES_SSH_HOST=hermes.example\n"
                "HEARTH_HERMES_COMMAND='hermes -p hearth'\n"
            )
            env = os.environ.copy()
            env["HEARTH_ENV_FILE"] = str(env_file)
            result = subprocess.run([str(INSTALLER), "--dry-run"], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("method: ssh (hermes.example)", result.stdout)
            self.assertIn("Hermes command: hermes -p hearth", result.stdout)
            self.assertIn("<target-home>/.hermes/profiles/hearth", result.stdout)


if __name__ == "__main__":
    unittest.main()

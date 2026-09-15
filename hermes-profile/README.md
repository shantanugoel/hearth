# Hearth Hermes profile

Install onto the hub machine or an SSH host after creating a `hearth` profile.
Set `HEARTH_HERMES_METHOD`, `HEARTH_HERMES_SSH_HOST` (for SSH),
`HEARTH_HERMES_PROFILE_DIR`, `HEARTH_HERMES_COMMAND`, and `HEARTH_HUB_URL`
in the gitignored `.env`.

```bash
./tools/install_hearth_profile.sh --dry-run
./tools/install_hearth_profile.sh
```

By default, that writes under the target user's home directory:

- `~/.hermes/profiles/hearth/SOUL.md`
- `~/.hermes/profiles/hearth/skills/productivity/hearth-board/SKILL.md`

`HEARTH_HERMES_COMMAND` must select the dedicated profile. For example, an
alias named `hearth` produces this one-shot call:

```bash
hearth --yolo --skills hearth-board -z '…'
```

Do not make `hearth` the sticky default profile. The kitchen board lives on
the hub (`~/.local/share/hearth/board.json` on the host), not in Hermes memory.
The NOTE4 never calls the Hermes HTTP API; the hub runs a local or SSH one-shot
after STT.
The installer disables bundled coding skills in `config.yaml` without
deleting them, and writes `HEARTH_HUB_URL` into the profile `.env`.

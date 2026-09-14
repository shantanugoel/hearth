# Hearth Hermes profile

Copy onto the Incus VM after `hermes profile create hearth`.

```bash
./tools/install_hearth_profile.sh
```

That writes:

- `~/.hermes/profiles/hearth/SOUL.md`
- `~/.hermes/profiles/hearth/skills/hearth-board/SKILL.md`

The profile alias is `hearth`. The hub files utterances with:

```bash
hearth --yolo --skills hearth-board -z '…'
```

Do not make `hearth` the sticky default profile. The kitchen board lives on
the hub (`~/.local/share/hearth/board.json` on the host), not in Hermes memory.
The installer disables bundled coding skills in `config.yaml` without
deleting them, and writes `HEARTH_HUB_URL` into the profile `.env`.

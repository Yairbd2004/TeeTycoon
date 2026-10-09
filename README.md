# TeeTycoon

TeeTycoon is a DDNet-based tycoon and social game mode. It adds player accounts, progression, houses, pets, economy tiles, shop commands, effects, and event/minigame behavior.

## Source branches

- `main` keeps the original DDNet 15.9.1-based source as the legacy baseline.
- `dev` is the upgrade branch, based on upstream DDNet 20.1.1, with the TeeTycoon features ported onto it.

The original development computer's ignored `out` build and account data are not included in a fresh clone. Build outputs and runtime databases are local files under `TeeTycoon/out/`; recover databases and private configuration only from a verified backup before starting a server with existing player accounts.

For the upstream upgrade and build workflow, see [UPGRADING.md](UPGRADING.md).

For new-computer setup, build requirements, runtime data, and Codex handoff instructions, see [README-INSTALL-AND-STANDARD.md](README-INSTALL-AND-STANDARD.md).

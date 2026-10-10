# TeeTycoon: install, handoff, and working standards

This is the handoff guide for continuing TeeTycoon on a new computer or in a fresh Codex session. Read it before changing code. The repository and current checked-out source are authoritative if a detail here has become stale; when that happens, update this guide in the same change.

## What this project is

Teeworlds is a small, pixel-art online multiplayer game that began with simple shooter modes. The community created many game modes around its movement and map editor. DDRace is a cooperative race mode where players cross custom maps together; freeze tiles and teamwork are central mechanics. Block uses related movement and freeze mechanics for player-versus-player play. DDNet (DDraceNetwork) is the actively maintained Teeworlds modification and game network built around DDRace.

TeeTycoon is this repository's custom DDNet server/game-mode project. Its intended loop is a lightweight tycoon simulator inside the Teeworlds/DDNet world: players register accounts and progress, earn experience and money from map entities/tiles, own and improve houses, buy and upgrade pets, and participate in votes/events and other activities. Some functionality is implemented across the server and DDNet core; do not assume every idea is complete or runtime-verified just because it is described here. Confirm behavior in source and with the user before redesigning it.

Project references:

- [DDNet website and game description](https://ddnet.org/)
- [DDNet source repository](https://github.com/ddnet/ddnet)
- [Teeworlds official website](https://www.teeworlds.com/)
- [DDNet source README](TeeTycoon/README.md)

## Repository state and branch policy

- Git repository: `https://github.com/Yairbd2004/TeeTycoon.git` (`origin`).
- Work on `dev`; it is the upgraded TeeTycoon branch. `main` is retained as the legacy DDNet 15.9.1-based baseline. Keep only these two local branches unless the user asks otherwise.
- The new-PC checkout inspected for this handoff currently has `dev`, `main`, and the `origin` branch refs at `f43b9c1` (`Initial commit`). This does not contain the historical commit chain described below; inspect the actual branch/log before relying on old hashes or assuming the branches differ.
- The current `dev` source is based on DDNet 20.1.1, upstream commit `647a2db7e3581c1deb45ff5dd877a41278a22798`, imported under `TeeTycoon/` as a Git subtree. The current handoff baseline at the time this file was written is root commit `fc1a54598` (`Fix stale client changes after DDNet port`). Always inspect `git log` for newer work instead of treating this hash as permanently current.
- The upstream remote is named `upstream` and should point to `https://github.com/ddnet/ddnet.git`. A fresh clone may only configure `origin`; add `upstream` if missing.
- `TeeTycoon/ddnet-libs` is the pinned upstream dependency submodule. Root `.gitmodules` maps it there; its currently pinned commit is `c0e6703fbcdbe03df2f26875427ec3951ce4ec21`. Initialize it after cloning.
- Keep upstream changes under the `TeeTycoon/` subtree. Read [UPGRADING.md](UPGRADING.md) before pulling a new DDNet version. The subtree workflow makes future updates manageable but cannot eliminate conflicts: TeeTycoon still modifies some DDNet core files.
- Preserve user edits, databases, maps, configs, and untracked files. Before any branch switch, merge, or cleanup, inspect `git status`; never use destructive reset/clean commands on user data.

## New-computer setup (Windows)

Windows is the platform on which the current port was compiled. A compatible newer/older toolchain may work, but the verified environment was Visual Studio 2026 with MSVC 14.51 and a Windows SDK.

1. Install Git for Windows.
2. Install Visual Studio (the IDE or Build Tools) with **Desktop development with C++**, MSVC x64 tools, a Windows SDK, and CMake tools. Ensure Ninja is available; the tested Visual Studio installation bundled CMake and Ninja.
3. Install Python 3 and make its interpreter available to CMake. DDNet 20.1.1's configure step requires Python 3 for code generation; Python 3.12.14 was used for the current server/client builds. In Developer PowerShell, set `$pythonExe = (Get-Command python).Source`; if Python is not on `PATH`, set `$pythonExe` to its full path and pass it to each configure command below.
4. Install Rust with `rustup`, then install/select the project-tested toolchain:

   ```powershell
   rustup toolchain install 1.85.0
   ```

   Configure builds with `RUSTUP_TOOLCHAIN=1.85.0` as shown below. If a future source update requires another Rust version, verify it from the project/Cargo files and build, then record the change here.
5. Clone the repository and initialize the dependency submodule:

   ```powershell
   git clone https://github.com/Yairbd2004/TeeTycoon.git
   cd TeeTycoon
   git switch dev
   git submodule update --init --recursive
   git remote add upstream https://github.com/ddnet/ddnet.git
   ```

   If `upstream` already exists, verify its URL instead of adding a duplicate. Confirm that `git status` is clean. A normal gitlink checkout should show the pinned `ddnet-libs` checkout in `git submodule status`; the current `f43b9c1` snapshot instead contains a flattened `TeeTycoon/ddnet-libs` tree, so submodule status is empty.
6. Open **Developer PowerShell for Visual Studio** (or a Developer Command Prompt) so MSVC's compiler and Windows SDK environment are configured. From the repository root, configure/build the server:

   ```powershell
   $env:RUSTUP_TOOLCHAIN = "1.85.0"
   $pythonExe = (Get-Command python).Source
   cmake -S TeeTycoon -B 'TeeTycoon/out/build/TeeTycoon Server' -G Ninja `
     -DCMAKE_BUILD_TYPE=Debug -DCLIENT=OFF -DSERVER=ON -DPREFER_BUNDLED_LIBS=ON `
     -DPython3_EXECUTABLE="$pythonExe" -DDOWNLOAD_GTEST=OFF `
     -DRUST_RUSTC="$env:USERPROFILE\.rustup\toolchains\1.85.0-x86_64-pc-windows-msvc\bin\rustc.exe" `
     -DRUST_CARGO="$env:USERPROFILE\.rustup\toolchains\1.85.0-x86_64-pc-windows-msvc\bin\cargo.exe"
   cmake --build 'TeeTycoon/out/build/TeeTycoon Server' --target game-server
   ```

   The server binary is `TeeTycoon/out/build/TeeTycoon Server/DDNet-Server.exe`. CMake stages `storage.cfg` beside it and the tracked `data/` files, including `autoexec_server.cfg` and `myServerconfig.cfg`, into the build's `data` directory on configure. The two TeeTycoon server configs are configure dependencies, so changing them causes CMake to refresh the staged copies on the next build.
7. To compile the client/editor too, use a separate build directory:

   ```powershell
   cmake -S TeeTycoon -B 'TeeTycoon/out/build/TeeTycoon Client' -G Ninja `
     -DCMAKE_BUILD_TYPE=Debug -DCLIENT=ON -DSERVER=OFF -DPREFER_BUNDLED_LIBS=ON `
     -DPython3_EXECUTABLE="$pythonExe" -DDOWNLOAD_GTEST=OFF
   cmake --build 'TeeTycoon/out/build/TeeTycoon Client' --target game-client
   ```

   The client binary is `TeeTycoon/out/build/TeeTycoon Client/DDNet.exe`.

Keep only `TeeTycoon Server` and `TeeTycoon Client` under `TeeTycoon/out/build/`, and rebuild in those same directories after source changes. If a DDNet or toolchain update requires a clean configure, preserve the server's runtime database and local data first, then recreate the same two directories. This checkout is for development; if its server is running when updating the build, close that process and rebuild in place. `out/` is ignored by Git and keeps generated files separate from source. CMake 3.22.1, Ninja 1.10.2, and Python 3.12.14 were used for the current builds. CMake may download build-time dependencies on the first configure, so allow network access. `DOWNLOAD_GTEST=OFF` avoids downloading the optional test dependency when only building the server/client.

Pets spawn as virtual bots outside the human connection slots. An admin can use
`tt_blocker_slot` to spawn a blocker that takes a connection slot or
`tt_blocker_virtual` to spawn one without a connection slot; `tt_blocker_list`
shows their IDs and `tt_blocker_remove <id>` removes one. Players can use
`/invite <player>`, `/accept`, and `/decline` for house visits. A guest can use
`/leave_house` or the Travel vote to return to their previous position and
DDRace team. House travel also moves each player's pet into that team. Players
cannot use the regular DDRace team management chat commands; admins retain
`set_team_ddr` through the server console.

Pet owners can use `/pet_relation help <in-game name>`,
`/pet_relation block <in-game name>`, or
`/pet_relation neutral <in-game name>` to save the player names their pet helps
or blocks. Names are matched exactly against the name shown in-game, including
players who are not logged in; quote names containing spaces. The player can
be offline when added. A renamed player needs a new list entry.
Entries saved by the earlier account-name version should be replaced if that
account name differs from the player's in-game name.
`/pet_relations` lists the entries. The pet always helps its owner; other
targets must be nearby and in the same DDRace team. `/pet_freeze_timeout <seconds>`
sets how long a continuously frozen pet waits before respawning (default 10,
valid 1-120, or 0 to disable). The My Pet vote page shows the current setting
and offers common values. A frozen bot cannot use a server-driven follow or
house teleport to escape freeze. After respawning, a pet returns to an unfrozen
owner; for a frozen owner it searches a map path, including reachable teleporter
entrances. Map pathing and bot combat still need live gameplay checks.

When a safe route is unavailable, level-9+ Race and Defense pets evaluate
short freeze crossings with the DDNet movement core and only attempt one if
the predicted momentum carries them out to safe ground. A pet already frozen
cannot move or hook; its configured timeout respawns it at an unfrozen owner.
For combat, pets keep pursuing a frozen target that is off a freeze tile, but
do not fire an unfreezing weapon at it. They select nearby freeze tiles,
prefer supported tiles that can hold the target in freeze, and release a
player hook if the projected pull moves the target away from that tile.
These movement and combat decisions still need gameplay checks on the map.

Gun and laser hits with no damage do not make a protected player an attack
target. Shotgun knockback and grenade force do. The optional server setting
`sv_bot_damage_mode 1` also treats positive weapon damage as aggression; if
the damaged player owns a pet, the attacker's current in-game name is saved
to that pet's block list. This setting changes pet retaliation and does not
turn on a separate damage game mode.

Admin blockers use maximum bot skill levels. In RCON, use
`tt_blocker_whitelist <id> add <in-game name>`, `remove <in-game name>`, or
`list` to manage names protected by a specific blocker. Other nearby players, including
admins, are block targets. `tt_blocker_freeze_timeout <id> <seconds>` changes
that blocker's freeze recovery (0 disables it). Blocker whitelists and timeouts
are runtime settings and reset when the blocker is removed or the server restarts.

Pet skills start at level 1 and can each reach level 10. Use the My Pet vote
page or `/pet_upgrade <race|blocker|defense|helper|aim>`; the vote page shows
each next price and the overall rating. Upgrades are saved in `Accounts.sqlite`
and older BOTS rows gain default level-1 skill columns when the server starts.
Spawned pets apply purchased skill levels on their next bot tick. Race and
Helper use a nearby tile route to get around walls and reach safe rescue
positions; Defense rejects dangerous movement and hook pulls. Pet weapons can
be bought permanently with `/pet_weapon gun|shotgun|grenade|laser|ninja`; the
saved weapon mask is applied to the active pet immediately and restored after
each spawn. Existing pets keep weapons from their former weapon tier during the
database migration. If laser is owned, distant rescues prefer it; close rescues
use hammer. Ninja stays in inventory and is activated only for short travel
bursts when the owner is far away, then waits through a cooldown.
The portable decision rules are in `TeeTycoon/src/game/server/bot_ai/`;
`bot.cpp` and `botengine.cpp` remain the DDNet adapter and pathfinder.

The separate [TeeTycoon bots repository](https://github.com/Yairbd2004/TeeTycoon-bots) carries a copy of those rules and
the TeeTycoon adapter, plus a short installation script and DDNet porting
notes. The adapter needs TeeTycoon's modified bot IDs, teams, player state,
and game context; it is not a standalone drop-in for stock DDNet. After bot
changes, sync the separate repository before publishing either copy.
The latest rescue pass checks the full tee footprint for freeze, uses DDNet's
hammer query geometry, and routes a frozen player toward a safe hook pull
position. Blockers seek a longer, better-aligned pull before launching a
throw. A high Defense pet simulates evasive movement and nearby wall hooks
when an upward throw leads toward freeze. The local route now looks for a
reachable climb hook staging point when the target is far above. These are
bounded predictions; the pictured cases still need live gameplay checks.
The server tracks how long each human tee remains continuously frozen while
touching freeze tiles. After one second, pets and admin blockers drop that tee
as a block target regardless of whether the freeze has solid floor support;
this includes airborne freeze and speed tiles. Leaving the freeze tiles resets
the timer and makes the player eligible again, even while their ordinary
out-of-tile freeze timer runs. A respawn resets the timer. Helper targets are
not affected. The local tile route now rejects uphill air steps beyond jump
height unless a hookable surface is reachable from a jump position, reducing
routes that ask the bot to climb empty air.

The checkout at `f43b9c1` stores `TeeTycoon/ddnet-libs` as ordinary tracked files rather than an initialized Git submodule, and some required Windows DLLs are ignored by Git. If configure reports a missing `libcurl.dll`, `zlib1.dll`, or other dependency DLL, restore the Windows x64 DLL files from the pinned `ddnet-libs` commit `c0e6703fbcdbe03df2f26875427ec3951ce4ec21` into the matching `TeeTycoon/ddnet-libs/<library>/windows/lib64/` directories. This checkout also needs `TeeTycoon/cmake/checksummed_extra.txt`, whose contents are in DDNet commit `647a2db7e3581c1deb45ff5dd877a41278a22798`; both items were restored locally for this build and remain ignored.

### Starting the server and preserving runtime data

The project source includes [storage.cfg](TeeTycoon/storage.cfg), `TeeTycoon/data/autoexec_server.cfg`, and the tracked TeeTycoon overrides in `TeeTycoon/data/myServerconfig.cfg`. CMake stages `storage.cfg` beside the executable and the TeeTycoon config in the build's `data` directory, so normal configure/build steps produce a server that reads both files. If CMake is changed to handle data differently, preserve this behavior. The override intentionally contains no RCON secrets; with no configured RCON password, the server generates a random one at startup. Set secure admin passwords in a local ignored config if needed, and never commit them.

Start the standard local build by running `DDNet-Server.exe` from its build directory without `-f`; it automatically loads the staged `autoexec_server.cfg`, which executes `myServerconfig.cfg`. Passing `-f data/autoexec_server.cfg` from the build directory executes the autoexec twice and logs duplicate vote-option errors.

For local-network discovery, the DDNet client sends LAN broadcasts to UDP ports 8303–8310. Keep the server on one of those ports; `sv_register 0` disables public master-server registration but does not disable LAN discovery. If the server is running but not in the LAN list, try connecting directly to its LAN IPv4 address and port, then check Windows Defender Firewall for an inbound allow rule for the **current** `DDNet-Server.exe` path. Allow it on the Private profile for a trusted home/work LAN; do not disable the firewall. A newly built executable in a different folder may need a new rule. On the original development computer, the Wi-Fi profile is now Private and a Private-only inbound rule allows this build's executable on UDP 8303. Firewall rules and network profiles are per-computer and must be set again on a new machine.

Be aware that DDNet searches configured storage paths in order. The default `$DATADIR` config is normally read before the build directory's `$CURRENTDIR`; a separate `autoexec_server.cfg` beside the executable may therefore not be the active config. The default data config executes `myServerconfig.cfg` as a customization hook, which is the intended place to override settings such as `sv_name`, `sv_map`, and `sv_port`. Confirm the active name/map in the startup log. `blmapV3ROYAL-TT` is the default map. The edited `blmapV3ROYAL-TT` and `Copy Love Box-TT` maps, plus their 0.7 conversions, are in `TeeTycoon/data/maps` and `TeeTycoon/data/maps7` and are staged into build data by CMake. Both retain the source block and freeze layouts; the TeeTycoon map additions are documented in `map_design/README.md`.

LAN check on the original development computer: the 20.1.1 server build completed, started with the staged TeeTycoon config/name, and bound UDP 8303. A Private-only Windows Defender Firewall rule for this exact executable is enabled on the trusted Private Wi-Fi profile. LAN listing was not verified from a second device; if it still does not appear, test direct IPv4:8303 connectivity and check client/server network profiles and firewall rules.

The normal browser game type is `TT`. The tracked server override sets `sv_test_cmds 0`, so the running server is not in DDNet test mode and does not enable test/cheat commands. If test mode is deliberately enabled for development, the browser label is `TestTT`.

Runtime databases and server configs are not source files and must be backed up separately when moving machines. The development server keeps `Accounts.sqlite` beside `TeeTycoon Server/DDNet-Server.exe`; it stores accounts, pets, pet relations, and the event-vote cooldown. Active event registration is transient server state and does not need a file. A fresh clone does **not** contain ignored `out/` data. Before using a new build with existing player accounts, copy the database and local server config from a verified backup and keep an untouched backup. Never commit live databases, passwords, or private server configuration.

## Linux server build for Debian VPS hosting

For the Pterodactyl/Pelican Debian yolk (`ghcr.io/parkervcp/yolks:debian`), build a native Linux x86-64 server; the Windows `.exe` cannot run in that container. The package produced for the current VPS setup is `TeeTycoon-prod-linux/teeworlds_srv`, with `data/`, `storage.cfg`, and a `start-server.sh` launcher. It was built in Debian stable (Debian 13) with GCC 14, CMake 3.31, Ninja 1.12, Python 3.13, and Rust 1.85.

On a Debian stable x86-64 build machine, install `build-essential cmake ninja-build python3 python3-dev rustc cargo pkg-config libcurl4-openssl-dev libsqlite3-dev libpng-dev libssl-dev zlib1g-dev`, then configure and build outside `out/build` so the Windows build layout remains unchanged:

```sh
cmake -S TeeTycoon -B /tmp/teetycoon-linux-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCLIENT=OFF -DSERVER=ON \
  -DPREFER_BUNDLED_LIBS=ON -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DDOWNLOAD_GTEST=OFF -DSERVER_EXECUTABLE=teeworlds_srv
cmake --build /tmp/teetycoon-linux-build --target game-server --parallel 4
```

Deploy the resulting `teeworlds_srv`, `storage.cfg`, and staged `data/` together. Start it from that directory with `./teeworlds_srv` (or `./start-server.sh`) and keep `Accounts.sqlite` there for persistent account data. The executable is dynamically linked; use `ldd teeworlds_srv` to confirm runtime libraries are available in the selected container. The current VPS package targets Linux x86-64 and sets `sv_register ipv4` so a host without outbound IPv6 does not keep retrying failed IPv6 registration.

## Production release archives

When asked for a production build, create a Linux x86-64 `.tar.gz` archive in `prod-releases/` named `TeeTycoon-vX.Y.Z-linux-amd64.tar.gz`. Keep the archive's contents at its root so extraction directly into `/home/container` places `teeworlds_srv`, `storage.cfg`, and `data/` beside one another. Store the executable and launcher with mode `755`; ordinary files use `644`. Choose the next TeeTycoon release version based on the scope of changes, independently of the upstream DDNet version. Do not include development databases. The first packaged release was `TeeTycoon-v0.1.0-linux-amd64.tar.gz`; the current map release is `TeeTycoon-v0.2.4-linux-amd64.tar.gz`.

## Source map for catching up

Start with the current `dev` diff/log, then these locations. Search for command registration and table/entity setup rather than relying only on file names; upstream updates can move code.

- `TeeTycoon/src/game/server/teetycoon.cpp` — main TeeTycoon mode logic: economy/progression, custom tiles/entities, house/pet and related server behavior.
- `TeeTycoon/src/game/server/ddracecommands.cpp`, `teetycoon.cpp`, and `gamecontext.cpp` — global event votes, registration, and the event runner. Active event state stays in memory; the 10-minute event-vote cooldown is persisted in `TT_EVENT_STATE` in `Accounts.sqlite`. Event registration no longer depends on `StartingEvent.txt` or `EventType.txt`.
- `TeeTycoon/src/game/server/entities/character.cpp` — TeeTycoon character cosmetics. Bloody effects must stay rate-limited and must never emit death events using virtual bot IDs.
- `TeeTycoon/data/autoexec_server.cfg` — global event start votes only. The server builds a separate vote-option page for each player: event votes and category labels on the main page, then private Shop, Pet, Cosmetics, Travel, and Account pages with a Back label. Status rows do nothing when selected; personal actions execute immediately, while event starts remain global ballots.
- `TeeTycoon/src/game/server/gamecontext.cpp` and `teetycoon.cpp` — the server sends and validates each player's current vote page. `gamecontext.cpp` initializes the shared account database and pet tables; `teetycoon.cpp` builds private status labels and owns the personal action allowlist. `player.h` stores each player's current page and labels. The client renders the server list directly so the same categories work in custom and standard DDNet clients. Refresh the page after changing displayed status; keep private data out of global `add_vote` options.
- Pet purchases save the pet and charge money in one SQLite transaction; event start commands are only submitted after their global vote passes. When adding personal menu actions, update the menu builder and allowlist together.
- `PET-BOT-ROADMAP.md` — next phase for slot-free pets, both blocker-bot modes, and later pet progression.
- Player-facing TeeTycoon command replies in `teetycoon.cpp` use the `chatresp` console/log channel; DDNet's per-client chat logger forwards that channel to the player who ran the command. Using another channel only writes the response to the server log.
- `TeeTycoon/src/game/server/gamecontext.{h,cpp}` — server context, initialization, hooks, commands and shared mode state.
- `TeeTycoon/src/game/server/player.{h,cpp}` and `entities/character.{h,cpp}` — per-player data and gameplay integration.
- `TeeTycoon/src/game/server/ddracecommands.cpp`, `ddracechat.cpp`, and `scoreworker.cpp` — commands/chat and score/database-adjacent integration; inspect the current implementation before making assumptions about persistence.
- `TeeTycoon/src/game/server/bot.cpp`, `botengine.cpp`, `ai/` — bot and AI behavior.
- `TeeTycoon/src/game/server/server.cpp`, `src/engine/server.*`, `src/engine/shared/config_variables.h` — server lifecycle and custom server/config integration.
- `TeeTycoon/src/game/collision.{h,cpp}`, `mapitems.{h,cpp}`, and `src/game/editor/` — custom map tile/entity definitions and editor integration.
- `TeeTycoon/src/game/client/` — client-side changes. The last client compile found and removed stale fragments in menus, skin loading, and snapshot processing; the corresponding fixed files are in the current `dev` history.
- Root `README.md` — short project/branch summary. [UPGRADING.md](UPGRADING.md) — upstream subtree update and server-build workflow.

Useful first-pass commands:

```powershell
git status --short --branch
git log --oneline --decorate -12
git diff main...dev --stat
rg -n "Register\(|CREATE TABLE|sqlite|Pet|House|EXP|MONEY|CallVote" TeeTycoon/src/game/server
```

The last `rg` command is only a starting point; inspect call paths, persistence, tile IDs, client/server protocol assumptions, and map/editor registration before changing gameplay.

## Working standards for Codex and contributors

1. Begin by reading this file, [README.md](README.md), and [UPGRADING.md](UPGRADING.md), then inspect the actual branch, recent commits, submodule state, and worktree status. Treat user edits and runtime data as valuable.
2. Do development on `dev`. Keep upstream source updates reproducible through the subtree process in `UPGRADING.md`; do not replace the whole tree with a fresh upstream checkout.
3. Understand an existing feature's server, client, map, database, and configuration paths before changing it. Ask the user when a gameplay/economy decision is unclear; preserve existing data formats unless a migration is intentionally planned.
4. Build the relevant target(s) when changing source and report the exact result. The server and client have both compiled on the verified Windows toolchain. Do not claim runtime behavior is verified unless the server/client was actually launched and exercised.
5. **Handoff maintenance is required:** whenever a dependency, compiler/toolchain, SDK, external program, setup command, build option, or other installation requirement is added, removed, or changed, update this file in the same task. Record the exact dependency/version, why it is needed, how to install/select it, and any changed build steps. Also update it when the upstream baseline, branch policy, build commands, runtime-data location, or important feature/source map changes. Do this before finishing the task, and keep the Codex prompt below enforcing the same rule.
6. Keep this guide concise enough to use as a handoff, but specific enough to reproduce setup. Separate verified facts from plans or unverified behavior.

Recent runtime/build note: bot-engine grid and segment setup happens at map initialization, while triangle navigation-graph/path setup is deferred until a bot is first added. The all-pairs shortest-path computation uses BFS rather than cubic Floyd–Warshall. This keeps normal server startup prompt and avoids allocating snapshot IDs for graph/triangle data that is not snapped. Adding a bot can still incur the one-time map-navigation graph generation cost; profile that path before increasing bot use.

## Prompt for a Codex session on another computer

Copy/paste this prompt after opening the cloned repository:

> Continue work on the TeeTycoon DDNet mod. First read `README-INSTALL-AND-STANDARD.md`, `README.md`, and `UPGRADING.md`. Inspect `git status`, the current branch, recent commits, remotes, and submodule state before editing. The intended work branch is `dev`; `main` is the legacy baseline. Understand the requested feature and inspect its current server/client/map/database/config paths before changing code. Preserve all user edits and runtime data, and never use destructive reset/clean operations. Build the relevant target when possible and report what was and was not verified. **Whenever you add, remove, or change any dependency, toolchain, SDK, installation requirement, setup command, build option, upstream baseline, or important source/build fact, update `README-INSTALL-AND-STANDARD.md` in the same task so another computer can be prepared without rediscovering it.** Ask me when a gameplay or economy decision is ambiguous; otherwise proceed with the smallest complete change and explain it clearly.

After the initial inspection, summarize the actual checked-out project state and any missing setup/data before starting feature work. Do not assume ignored build output or account databases transferred with the Git clone.

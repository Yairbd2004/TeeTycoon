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
   cmake -S TeeTycoon -B TeeTycoon/out/build/20.1.1-msvc -G Ninja `
     -DCMAKE_BUILD_TYPE=Debug -DCLIENT=OFF -DSERVER=ON -DPREFER_BUNDLED_LIBS=ON `
     -DPython3_EXECUTABLE="$pythonExe" -DDOWNLOAD_GTEST=OFF `
     -DRUST_RUSTC="$env:USERPROFILE\.rustup\toolchains\1.85.0-x86_64-pc-windows-msvc\bin\rustc.exe" `
     -DRUST_CARGO="$env:USERPROFILE\.rustup\toolchains\1.85.0-x86_64-pc-windows-msvc\bin\cargo.exe"
   cmake --build TeeTycoon/out/build/20.1.1-msvc --target game-server
   ```

   The server binary is `TeeTycoon/out/build/20.1.1-msvc/DDNet-Server.exe`. CMake stages `storage.cfg` beside it and the tracked `data/` files, including `autoexec_server.cfg` and `myServerconfig.cfg`, into the build's `data` directory on configure. The two TeeTycoon server configs are configure dependencies, so changing them causes CMake to refresh the staged copies on the next build.
7. To compile the client/editor too, use a separate build directory:

   ```powershell
   cmake -S TeeTycoon -B TeeTycoon/out/build/20.1.1-client-msvc -G Ninja `
     -DCMAKE_BUILD_TYPE=Debug -DCLIENT=ON -DSERVER=OFF -DPREFER_BUNDLED_LIBS=ON `
     -DPython3_EXECUTABLE="$pythonExe" -DDOWNLOAD_GTEST=OFF
   cmake --build TeeTycoon/out/build/20.1.1-client-msvc --target game-client
   ```

   The client binary is `TeeTycoon/out/build/20.1.1-client-msvc/DDNet.exe`.

Use a new versioned build directory when changing DDNet versions or toolchains. `out/` is ignored by Git and keeps generated files separate from source. CMake 3.22.1, Ninja 1.10.2, and Python 3.12.14 were used for the current builds. CMake may download build-time dependencies on the first configure, so allow network access. `DOWNLOAD_GTEST=OFF` avoids downloading the optional test dependency when only building the server/client.

The checkout at `f43b9c1` stores `TeeTycoon/ddnet-libs` as ordinary tracked files rather than an initialized Git submodule, and some required Windows DLLs are ignored by Git. If configure reports a missing `libcurl.dll`, `zlib1.dll`, or other dependency DLL, restore the Windows x64 DLL files from the pinned `ddnet-libs` commit `c0e6703fbcdbe03df2f26875427ec3951ce4ec21` into the matching `TeeTycoon/ddnet-libs/<library>/windows/lib64/` directories. This checkout also needs `TeeTycoon/cmake/checksummed_extra.txt`, whose contents are in DDNet commit `647a2db7e3581c1deb45ff5dd877a41278a22798`; both items were restored locally for this build and remain ignored.

### Starting the server and preserving runtime data

The project source includes [storage.cfg](TeeTycoon/storage.cfg), `TeeTycoon/data/autoexec_server.cfg`, and the tracked TeeTycoon overrides in `TeeTycoon/data/myServerconfig.cfg`. CMake stages `storage.cfg` beside the executable and the TeeTycoon config in the build's `data` directory, so normal configure/build steps produce a server that reads both files. If CMake is changed to handle data differently, preserve this behavior. The override intentionally contains no RCON secrets; with no configured RCON password, the server generates a random one at startup. Set secure admin passwords in a local ignored config if needed, and never commit them.

Start the standard local build by running `DDNet-Server.exe` from its build directory without `-f`; it automatically loads the staged `autoexec_server.cfg`, which executes `myServerconfig.cfg`. Passing `-f data/autoexec_server.cfg` from the build directory executes the autoexec twice and logs duplicate vote-option errors.

For local-network discovery, the DDNet client sends LAN broadcasts to UDP ports 8303–8310. Keep the server on one of those ports; `sv_register 0` disables public master-server registration but does not disable LAN discovery. If the server is running but not in the LAN list, try connecting directly to its LAN IPv4 address and port, then check Windows Defender Firewall for an inbound allow rule for the **current** `DDNet-Server.exe` path. Allow it on the Private profile for a trusted home/work LAN; do not disable the firewall. A newly built executable in a different folder may need a new rule. On the original development computer, the Wi-Fi profile is now Private and a Private-only inbound rule allows this build's executable on UDP 8303. Firewall rules and network profiles are per-computer and must be set again on a new machine.

Be aware that DDNet searches configured storage paths in order. The default `$DATADIR` config is normally read before the build directory's `$CURRENTDIR`; a separate `autoexec_server.cfg` beside the executable may therefore not be the active config. The default data config executes `myServerconfig.cfg` as a customization hook, which is the intended place to override settings such as `sv_name`, `sv_map`, and `sv_port`. Confirm the active name/map in the startup log. Both `TeeTycoon/data/maps/TeeTycoon.map` and `TeeTycoon/data/maps7/TeeTycoon.map` are tracked and staged into build data by CMake. The 0.7 variant was generated with DDNet's `map_convert_07`. The server was startup-verified using these map files on the new-PC build; no 0.7 client visual/gameplay session has been tested yet.

LAN check on the original development computer: the 20.1.1 server build completed, started with the staged TeeTycoon config/name, and bound UDP 8303. A Private-only Windows Defender Firewall rule for this exact executable is enabled on the trusted Private Wi-Fi profile. LAN listing was not verified from a second device; if it still does not appear, test direct IPv4:8303 connectivity and check client/server network profiles and firewall rules.

The normal browser game type is `TT`. The tracked server override sets `sv_test_cmds 0`, so the running server is not in DDNet test mode and does not enable test/cheat commands. If test mode is deliberately enabled for development, the browser label is `TestTT`.

Runtime databases, server configs, and event files are not source files and must be backed up separately when moving machines. On the original development computer, the active build data was under `TeeTycoon/out/build/20.1.1-msvc/` and included `Accounts.sqlite`, `ddnet-server.sqlite`, `autoexec_server.cfg`, `EventType.txt`, and `StartingEvent.txt`. A fresh clone does **not** contain ignored `out/` data. Before using a new build with existing player accounts, copy the needed database/config/event files from a verified backup and keep an untouched backup. Never commit live databases, passwords, or private server configuration.

## Source map for catching up

Start with the current `dev` diff/log, then these locations. Search for command registration and table/entity setup rather than relying only on file names; upstream updates can move code.

- `TeeTycoon/src/game/server/teetycoon.cpp` — main TeeTycoon mode logic: economy/progression, custom tiles/entities, house/pet and related server behavior.
- `TeeTycoon/data/autoexec_server.cfg` — server vote options. The menu contains TeeTycoon sections only, with event actions first; default DDNet map and gravity options were removed. Selecting menu entries executes immediately without starting a ballot. Event start actions require an authenticated server admin; event join and personal game actions run for the selecting player.
- The TeeTycoon account/shop information row sends money, level, XP, current upgrade levels, and next prices only to the selecting player. Pet details are also private when selected. Generic action labels are used so personal account values are not disclosed in server-wide vote messages.
- `TeeTycoon/src/game/server/gamecontext.cpp` — intercepts TeeTycoon menu rows before DDNet vote rate limits and ballot creation; `TeeTycoon/src/game/server/teetycoon.cpp` contains the action allowlist and private information responses. Event start handlers in `ddracecommands.cpp` are registered as admin-only `tt_start_event_*` server commands. When adding menu actions, update both the allowlist and the vote options, preserve the immediate/no-ballot behavior, and keep account/pet data private.
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

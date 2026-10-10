# Pet and blocker bot follow-up

The vote menu is complete. Pets now use virtual internal client IDs outside
the human connection range. Both admin blocker variants are implemented.

## Project attribution

Planned goal: audit project-maintained documentation, server branding, and
release materials so they identify the mod as TeeTycoon and credit Yair as its
creator. In one or two suitable project-level places, describe TeeTycoon as a
mod created by Yair and based on DDNet 20.1.1. Keep DDNet's upstream copyright,
license, and third-party notices intact; give DDNet a respectful upstream
acknowledgment without repeating it throughout gameplay text. Check that future
release notes and packaged project information preserve the same attribution.

## Bot slots

1. Trace every use of bot client IDs through `AddBot`, `CBot`, `CPlayer`,
   `CGameTeams`, player mapping, snapshots, damage, teleports, events, and
   disconnect handling. Separate a bot's stable internal identity from a real
   connection slot and from the client ID shown to each viewer.
2. Store slot-free pets as server-owned actors. Give viewers a temporary visible
   client ID only where the network protocol needs one. Keep human connection
   capacity available even when every player has a pet. Check the protocol's
   visible client-ID limit separately: at a full 64-player server, showing an
   extra tee may need a custom-client extension or a reduced-visibility fallback.
3. Add two admin blocker commands: one creates a slot-backed blocker (for an
   occupied-looking server); the other creates a slot-free blocker that works
   when all human slots are filled. Blockers have no owner and can fight
   everyone. Include explicit despawn and cleanup paths for both kinds.
4. Runtime gameplay checks remain: full-server joins, pet and blocker visibility
   for older/newer clients, owner disconnects, death/respawn, house travel,
   events, map changes, and mixed slot-backed/slot-free blockers. Pets are
   already spawned through the virtual-ID path. Virtual IDs range from the
   configured human maximum through ID 127, so simultaneous pets plus virtual
   blockers are capped by that range. Legacy clients hide bots when their
   visible ID space is full.

The engine send path now skips bot IDs as packet recipients, including virtual
IDs above the real connection limit. Human clients can still receive bot tees
through snapshots. Bot spawn no longer sends tuning/team packets or starts a
player demo for a virtual ID. This addresses the
`Invalid pChunk->m_ClientId: 64` spawn crash.

## Virtual ID safety invariant

Every pet command must treat a pet ID as an internal actor ID, not as a human
connection ID. Do not pass pet IDs to APIs that send client messages, query
human connection state, or assume a network-visible ID. Read and update pet
names, emotes, appearance, and other persistent state through `CPlayer` and the
database; use an explicit visible-ID mapping only when producing a snapshot.
Review this boundary whenever adding pet commands or server callbacks.

## House and team integration

5. Keep a pet in its owner's DDRace team and location when it spawns, respawns,
   or the owner travels between public space and a house. Cover a pet spawned
   after the owner enters a house and a guest pet entering or leaving a visit.
6. Reserve DDRace team changes for server house logic and admin commands.
   Remove player-facing team join, lock, and invite commands.
7. Make house invitations usable: accepting while the host is in their house
   saves the guest's position and team, brings the guest and pet to the host,
   and makes them visible in the host's team. A leave action restores the
   saved position and team. Clean up visits if the host disconnects or leaves,
   and guard missing characters, expired invites, and reconnects.

The house and team code is implemented. Runtime gameplay checks remain for
guest death, host logout/disconnect, repeated invites, and pet spawn during a
house visit.

After the current gameplay pass, consider persisted weapon choices, cosmetics,
friend marking for rescue targets, and further skill tuning from live sessions.

## Target lists and freeze recovery

Persist per-pet in-game-name relations: help, block, or neutral. The owner is
always helped; other listed players are pursued only inside a bounded radius.
Each admin blocker has its own runtime in-game-name whitelist; all other
players are block targets, including admins. Blockers use the same level-10
decision rules as pets. Add RCON commands to manage these lists.

Never let bot follow/house teleports remove an active freeze. After a saved
per-pet timeout (default 10 seconds, 0 disables), respawn the frozen bot
without killing its owner/team. On respawn, return to an unfrozen owner; if
the owner is frozen, navigate through reachable map teleporter entrances where
direct graph navigation cannot reach them. Keep the freeze policy configurable
for admin blockers as well.

Implemented: pet target relations and timeout persist in `Accounts.sqlite`;
target names are matched exactly against current in-game names, including
unlogged players. A renamed player needs a new entry;
blocker target lists and timeout are RCON-managed per active blocker. Pet
target selection remains team-aware and range-limited. Blocker bots select the
closest eligible opponent anywhere on the map. Freeze respawn preserves house
teams and uses the map navigation graph and teleporter entrances to find a
reachable target when direct navigation fails. Live gameplay checks remain for
multiple map teleporter exits, crowded blocker fights, freeze tiles during
house travel, and performance with many active bots.

## Upgradeable pet intelligence

Implemented five independent skills, each starting at level 1 and capped at 10:
Race (route movement), Blocker (hooks and freeze pressure), Defense (predictive
freeze/death avoidance), Helper (owner rescue), and Aim (moving-target lead and
weapon accuracy). Store levels in the existing BOTS row, migrate existing pet
rows to level 1, and buy levels with account money in one database transaction.
Show current levels, next costs, and an overall rating in the pet vote page and
profile. Skill effects change bot input decisions. Runtime balance and gameplay
checks remain, especially rescue reliability and full-server CPU cost. Pet help
targets now use in-game-name relations. Leave weapon purchases and cosmetics
for later work.

The current intelligence pass adds a bounded tile route around nearby walls,
safe rescue firing positions, deterministic wall-hook scoring, earlier stuck
recovery, and immediate route invalidation after a skill purchase. Laser is
preferred for distant rescue only when the pet owns one; hammer is used nearby
and by pets without laser. Blockers avoid firing an unfreezing laser at their
targets. Hook movement and difficult map geometry still require live gameplay
checks and tuning.

The next AI pass adds DDNet-core wall-hook prediction, a simulated fallback
for short freeze crossings that end on safe ground, alternative rescue
positions after stalled movement, and blocker hook decisions based on the
target's projected movement with and without the hook. Blockers keep targeting
tees frozen outside freeze tiles, avoid unfreezing shots, and seek supported
freeze tiles for longer holds. Shotgun and grenade knockback trigger temporary
defense; `sv_bot_damage_mode 1` additionally saves actual damage attackers to
the pet's in-game-name block list. These are bounded local predictions, not a
guarantee of solving every map geometry or opponent input. Live gameplay and
full-server performance checks are still needed.

The current follow-up addresses reported close-range rescue stalls and short
blocker throws. Hammer decisions use the normal DDNet query radius and team
collision, rescue checks freeze under the tee's feet, and hook pulls launch
from a safe, longer stance. The wall-hook planner gives climbing height credit
even when it temporarily moves sideways around a ledge. A high Defense pet
simulates a short response to upward throws near freeze. Compile succeeds;
live checks on the three reported screenshots, varied maps, and full-server
CPU load remain open. If those scenarios still fail, capture player/pet map
positions and current hook state in a trace so the route and physics scores
can be corrected against an exact reproduction.

For `TeeTycoon`, the complete map graph remains the long-distance planner and
nearby obstacle refinement uses tile A*. For `Copy Love Box-TT`, tile A* scans
the whole 387x250 game layer. If there is no opponent to pursue, a blocker
targets teleport 249 out of spawn when present; otherwise it uses an available
reachable teleporter and then patrols reachable graph points. Combat and rescue
hooks stay held while the hook is in flight; jump-based blocking momentum no
longer disables climb hooks. Route history for both maps is stored in
`Accounts.sqlite`: the last visited tiles before a bot death gain a persistent
risk cost, and the fastest observed crossing times slightly favor traversed
tiles. This improves route choices over time but does not prove every physics
route is reachable; reproduce and tune live on each map after deployment.

Block targets continuously frozen on freeze tiles for one server second are
now skipped, including targets held in airborne freeze by speed tiles. This
timer is shared by pets and admin blockers and resets when a tee leaves the
freeze tiles or respawns. The nearby tile route also requires an uphill step
beyond jump height to have a reachable hookable surface. Both changes still
need a live gameplay pass against moving targets and the reported ledges.

For Copy Love Box-TT, the blocker planner now gives supported side and lower
freeze tiles a stronger preference. If a direct hook or shotgun line cannot
move a target usefully, it searches grenade trajectories for a terrain impact
that blasts the target toward the chosen freeze tile while keeping the pet and
its owner outside the blast radius. Nearby blocker bots temporarily whitelist
each other when they share an eligible real opponent within 1000 units, and
resume duels when that local fight ends. Compile and production packaging are
included in this pass; live play and crowded-server CPU checks remain open.

Bulk load handling: blocker commands queue up to 32 bots and create one per game
tick; `tt_admin_teleport_all` and `_xy` queue destinations and relocate up to
four characters per tick. AI thinking is staggered by bot ID across adjacent
ticks. The Copy Love Box-TT grenade trajectory search is restricted to fights
within 450 units, samples 19 directions, caps simulation at 0.9 seconds, and
replans every 25 ticks. Bot/pet characters keep their normal network snapshots;
the TeeTycoon client uses negative snapshot latency as a bot marker to hide
them from scoreboard rows without hiding them in the world. CPU impact still
needs confirmation on a live server with 32 bots and a crowded teleport.

Keep the skill rules and decision policy in `TeeTycoon/src/game/server/bot_ai/`
without DDNet headers, so other mods can copy them. `bot.cpp` translates
DDNet characters and collision results into those decisions. The older bot
path graph in `botengine.cpp` still needs a separate extraction before the
entire bot implementation is portable.

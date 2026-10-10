# Pet and blocker bot follow-up

The vote menu is complete. Pets now use virtual internal client IDs outside
the human connection range. Both admin blocker variants are implemented.

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
blocker target lists and timeout are RCON-managed per active blocker. Bot
target selection respects DDRace teams and a bounded pursuit radius. Freeze
respawn preserves house teams and uses the bot's navigation graph to find a
reachable teleporter entrance when the owner remains frozen. Live gameplay
checks remain for multiple map teleporter exits, crowded blocker fights,
freeze tiles during house travel, and performance with many active bots.

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

Block targets continuously frozen on freeze tiles for one server second are
now skipped, including targets held in airborne freeze by speed tiles. This
timer is shared by pets and admin blockers and resets when a tee leaves the
freeze tiles or respawns. The nearby tile route also requires an uphill step
beyond jump height to have a reachable hookable surface. Both changes still
need a live gameplay pass against moving targets and the reported ledges.

Keep the skill rules and decision policy in `TeeTycoon/src/game/server/bot_ai/`
without DDNet headers, so other mods can copy them. `bot.cpp` translates
DDNet characters and collision results into those decisions. The older bot
path graph in `botengine.cpp` still needs a separate extraction before the
entire bot implementation is portable.

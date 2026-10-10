# TeeTycoon commands

Player commands go in game chat with `/`. Admin commands go in the server console or an authenticated RCON console **without** `/`. In the examples below, `<value>` is required and `[value]` is optional. Names with spaces should be quoted.

The in-game vote menu also has Shop, Pet, Cosmetics, Travel, and Account pages. Its event-start options are global votes; its personal actions run immediately for the player who selects them. `tt_menu_action` and `tt_menu_info` are menu plumbing, not commands you need to type.

## Player commands

### Account and progress

| Command | What it does |
| --- | --- |
| `/register <username> <password>` | Create an account and enter your house. Username: 3–15 letters or digits; password: at least 4 characters. |
| `/login <username> <password>` | Log in to an existing account. |
| `/logout` | Log out. |
| `/profile [player name]` | Show your account progress, or the profile of an online player named by their **in-game name**. |
| `/commands` | Show the short in-game TeeTycoon command list. |
| `/rules`, `/credits` | Show server rules or credits. |

### Shop and travel

| Command | What it does |
| --- | --- |
| `/shop [item]` | Show prices, or an item's description. Items: `farm`, `house`, `vip`, `rebirth`, `pet`, `rainbow`, `bloody`. |
| `/buy <item> [amount]` | Buy an item. `amount` is supported for farm, house, and VIP upgrades; buy other items one at a time. Requires login and enough money. |
| `/home [house number]` | Go to your house. Without a number, use your highest owned house level; available numbers are `0` through your house level. |
| `/spawn` | Return to the public spawn area. |
| `/invite <player name>` | Invite an online player by **in-game name** while you are in your own house. |
| `/accept`, `/decline` | Accept or decline a house invitation. An accepted visit moves you and your pet into the host's house team. |
| `/leave_house` | Return from a house visit to your previous position and team. |

Shop quick reference: farm/money tile has 100 levels; house has levels 0–2; VIP has levels 0–5. Rebirth requires house level 2 and resets money, farm, house, and VIP. A pet costs $1,000,000 and ownership is saved. Rainbow costs $10,000 and bloody costs $50,000; both effects last until death or until turned off.

### Pet

| Command | What it does |
| --- | --- |
| `/pet_spawn` | Spawn your owned pet. It uses a virtual bot ID instead of a human connection slot. |
| `/pet_profile [player name]` | Show your pet's level, weapon, skills, and other stats; an optional online **in-game name** selects another player's pet. |
| `/stay enable` | Tell a spawned pet to stay here. |
| `/stay disable` | Tell a spawned pet to follow you. |
| `/pet_upgrade <race\|blocker\|defense\|helper\|aim>` | Buy one level of a pet skill. Skills start at level 1 and cap at 10; next-level cost is `$100,000 × current level²`. A spawned pet uses the upgrade immediately. |
| `/pet_relation <help\|block\|neutral> <in-game name>` | Make your pet help, block, or stop specially targeting an **in-game name**. Quote names with spaces. Your pet always helps you. |
| `/pet_relations` | List saved help and block names. |
| `/pet_freeze_timeout <seconds>` | Respawn a pet after 1–120 seconds of continuous freeze; `0` disables automatic freeze respawn. Default: 10 seconds. |

For example: `/pet_relation help "Player Name"`, `/pet_relation block Rival`, then `/pet_relations`. Pet relations use in-game names, so they can include players who never logged in. They are saved with your account.

### Events and cosmetics

| Command | What it does |
| --- | --- |
| `/join` or `/event_join` | Join the current event while registration is open. Requires login. |
| `/unrainbow` | Turn off your active rainbow effect. |
| `/unbloody` | Turn off your active bloody effect. |

## Admin / RCON commands

### Events

| Command | What it does |
| --- | --- |
| `tt_start_event_survival` | Start survival event registration. |
| `tt_start_event_race` | Start race event registration. |
| `tt_start_event_dm` | Start deathmatch event registration. |
| `tt_start_event_freezerace` | Start freeze-race event registration. |
| `tt_start_event_fng` | Start FNG event registration. |

Players can then join with `/join` or the main vote menu. An event already in progress cannot be started again by these commands.

### Global blocker bots

| Command | What it does |
| --- | --- |
| `tt_blocker_slot` | Spawn a blocker using a normal server client slot and visible player ID. Requires a free connection slot. |
| `tt_blocker_virtual` | Spawn a blocker without using a human connection slot. Its tee can be hidden from standard clients when no visible ID is available. |
| `tt_blocker_list` | List active blocker IDs, slot modes, protected-name counts, and freeze timeouts. |
| `tt_blocker_remove <id>` | Remove the blocker with that internal ID. |
| `tt_blocker_whitelist <id> list` | List that blocker's protected **in-game names**. |
| `tt_blocker_whitelist <id> add <in-game name>` | Protect a player from that blocker and allow the bot to help them. |
| `tt_blocker_whitelist <id> remove <in-game name>` | Remove protection; the blocker may target that player again. |
| `tt_blocker_freeze_timeout <id> <seconds>` | Set frozen respawn time to 1–120 seconds, or `0` to disable it. Default: 10 seconds. |

Use the ID printed by `tt_blocker_slot` or `tt_blocker_virtual`, or find it with `tt_blocker_list`. For example, `tt_blocker_whitelist 64 add "Player Name"`. Blockers use maximum pet skills. Their whitelist is kept in memory for that blocker; add names again after removing and recreating it or restarting the server. Everyone not on its whitelist, including admins, is a potential block target.

### Relevant server settings

Set these in the server config or RCON, for example `sv_bot_damage_mode 1`.

| Setting | Default | What it controls |
| --- | ---: | --- |
| `sv_bot_slots` | `20` | Maximum bot count used by the older player-replacement bot feature; it does not reserve human slots for virtual pets. |
| `sv_bot_skin` | `default` | Bot skin. |
| `sv_bot_allow_hook` | `1` | Let bots hook. |
| `sv_bot_allow_move` | `1` | Let bots move. |
| `sv_bot_allow_fire` | `1` | Let bots fire. |
| `sv_bot_damage_mode` | `0` | Treat weapon damage to a pet owner as aggression and save the attacker to that pet's block list. |
| `sv_bot_draw_target` | `0` | Show bot targets for debugging. |
| `sv_botengine_draw_graph` | `0` | Draw the bot navigation graph for debugging. |

`0` disables and `1` enables the switches above. Other DDNet administration commands still exist; this page focuses on TeeTycoon features. Useful related RCON commands include `kill_pl <id> [reason]` and `set_team_ddr <id> <team>`. Normal players cannot set DDNet teams themselves.

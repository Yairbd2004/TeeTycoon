# Reusable bot decisions

`skills.h` defines five 1–10 pet skills and their upgrade costs. `brain.h`
contains gameplay decisions and has no DDNet headers or global state. Both are
header-only C++20 and can be copied into another mod without changing its
build system.

The DDNet adapter is `../bot.cpp`: it turns game character, map, and weapon
state into the simple values accepted by `bot_ai::ChooseRole`,
`ChooseMovement`, `ChooseWeapon`, `AimLeadTicks`, and `ShouldHookEnemy`.
The movement predictor callback supplies simulated candidate outcomes. The
adapter also owns collision sampling, laser bounce geometry, pathfinding,
networked bot identities, and the account database. A different mod can keep
the decision rules and replace those adapter parts with its own APIs.

The extracted rules make skill effects reusable; the older pathfinding code in
`../botengine.cpp` remains tied to DDNet's collision and graph types. Moving
that graph into a general-purpose library would be a separate migration.

Skill effects: Race refreshes routes more often and explores farther around
nearby walls; Defense predicts movement and hook risk farther ahead while
avoiding freeze/death tiles; Blocker favors hooks and knockback without
unfreezing opponents; Helper chooses safe rescue positions and uses laser when
owned, with hammer or a direct hook to pull a nearby player out of freeze;
Aim reduces angular error and improves bounce searches. The tile route and
hook physics remain in the DDNet adapter, while the skill-dependent hook and
movement policy is reusable here.
The first upgrade costs 100,000 money; each later cost is
`100000 * current_level * current_level`. Levels 1 and 10 are the bounds.

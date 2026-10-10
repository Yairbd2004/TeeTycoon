#ifndef GAME_SERVER_PET_SKILL_STORAGE_H
#define GAME_SERVER_PET_SKILL_STORAGE_H

#include "bot_ai/skills.h"

// TeeTycoon's SQLite adapter. Portable bot decisions do not depend on this.
constexpr const char *PET_SKILL_COLUMNS[NUM_PET_SKILLS] = {"SKILL_RACE", "SKILL_BLOCKER", "SKILL_DEFENSE", "SKILL_HELPER", "SKILL_AIM"};

#endif

#ifndef GAME_SERVER_BOT_AI_SKILLS_H
#define GAME_SERVER_BOT_AI_SKILLS_H

#include <algorithm>
#include <string_view>

enum EPetSkill
{
	PET_SKILL_RACE,
	PET_SKILL_BLOCKER,
	PET_SKILL_DEFENSE,
	PET_SKILL_HELPER,
	PET_SKILL_AIM,
	NUM_PET_SKILLS,
};

constexpr int PET_SKILL_MAX_LEVEL = 10;
constexpr const char *PET_SKILL_NAMES[NUM_PET_SKILLS] = {"Race", "Blocker", "Defense", "Helper", "Aim"};
constexpr const char *PET_SKILL_KEYS[NUM_PET_SKILLS] = {"race", "blocker", "defense", "helper", "aim"};

inline int PetSkillFromKey(std::string_view Key)
{
	for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
		if(Key == PET_SKILL_KEYS[Skill])
			return Skill;
	return -1;
}

inline int PetSkillLevel(int Level)
{
	return std::clamp(Level, 1, PET_SKILL_MAX_LEVEL);
}

inline int PetSkillUpgradeCost(int CurrentLevel)
{
	return 100000 * CurrentLevel * CurrentLevel;
}

#endif

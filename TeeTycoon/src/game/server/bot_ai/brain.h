#ifndef GAME_SERVER_BOT_AI_BRAIN_H
#define GAME_SERVER_BOT_AI_BRAIN_H

// Pure decision rules. This header has no DDNet types, global configuration,
// networking, or map APIs. A game-specific adapter supplies observations and
// applies the chosen movement, hook, aim, and weapon inputs.

#include "skills.h"

#include <algorithm>
#include <cmath>

namespace bot_ai
{
enum class ERole
{
	FOLLOW,
	RESCUE,
	FIGHT,
};

enum class EWeapon
{
	HAMMER,
	GUN,
	SHOTGUN,
	GRENADE,
	LASER,
};

struct SMovement
{
	int m_Direction;
	int m_Jump;
};

struct SPrediction
{
	float m_DistanceToGoal;
	float m_HazardPenalty;
};

inline ERole ChooseRole(bool OwnerFrozen, bool ThreatActive)
{
	return OwnerFrozen ? ERole::RESCUE : ThreatActive ? ERole::FIGHT : ERole::FOLLOW;
}

inline int RouteRefreshDistance(int RaceLevel)
{
	return RaceLevel >= 7 ? 48 : 112;
}

inline int MovementInterval(int RaceLevel, int DefenseLevel)
{
	return std::max(2, 7 - RaceLevel / 3 - DefenseLevel / 3);
}

inline int PredictionHorizon(int RaceLevel, int DefenseLevel)
{
	// Include enough of the jump arc to catch freeze platforms reached after
	// the initial impulse, while keeping the per-tick simulation bounded.
	return 6 + std::max(RaceLevel, DefenseLevel) * 2;
}

// Request the next jump only after the current upward impulse is almost spent.
// m_JumpedTotal counts air jumps; a grounded jump leaves it at zero, so this
// naturally delays the second jump until the tee reaches the top of its arc.
inline bool JumpPressAllowed(bool Requested, int Jumped, int JumpedTotal, float VerticalVelocity)
{
	if(!Requested || (Jumped & 1))
		return false;
	if(JumpedTotal == 0 && !(Jumped & 2) && VerticalVelocity < -1.5f)
		return false;
	return true;
}

template<typename TPredict>
SMovement ChooseMovement(SMovement Base, int RaceLevel, int DefenseLevel, TPredict Predict)
{
	float BestScore = 1e30f;
	SMovement Best = Base;
	for(int Direction = -1; Direction <= 1; Direction++)
	{
		for(int Jump = 0; Jump <= 1; Jump++)
		{
			const SPrediction Result = Predict(Direction, Jump, PredictionHorizon(RaceLevel, DefenseLevel), DefenseLevel >= 6);
			const float Score = Result.m_HazardPenalty +
				(RaceLevel >= 4 ? Result.m_DistanceToGoal : Result.m_DistanceToGoal * 0.12f) +
				(Direction != Base.m_Direction ? 3.0f : 0.0f) +
				(Jump != Base.m_Jump ? 5.0f : 0.0f);
			if(Score < BestScore)
			{
				BestScore = Score;
				Best = {Direction, Jump};
			}
		}
	}
	return Best;
}

inline EWeapon ChooseWeapon(ERole Role, float Distance, int BlockerLevel, int HelperLevel, int AimLevel)
{
	if(Role == ERole::RESCUE)
		return HelperLevel >= 5 && Distance > 55.0f ? EWeapon::LASER : EWeapon::HAMMER;
	if(Distance <= 65.0f)
		return EWeapon::HAMMER;
	// Laser unfreezes opponents. Shotgun gives the blocker useful knockback.
	return BlockerLevel >= 5 && AimLevel >= 3 ? EWeapon::SHOTGUN : EWeapon::GUN;
}

inline float AimLeadTicks(EWeapon Weapon, float Distance, int AimLevel)
{
	// DDNet's laser resolves immediately; leading it makes precise shots miss.
	if(Weapon == EWeapon::LASER || Weapon == EWeapon::HAMMER || Weapon == EWeapon::GRENADE)
		return 0.0f;
	return std::min(9.0f, Distance / 80.0f) * (AimLevel - 1) / 9.0f;
}

inline float AimErrorRadians(int AimLevel, float RandomMinusOneToOne)
{
	return RandomMinusOneToOne * (PET_SKILL_MAX_LEVEL - AimLevel) * 0.012f;
}

inline bool ShouldHookEnemy(int BlockerLevel, bool DirectSight, float Distance, float HookLength, bool FreezeBetween)
{
	return BlockerLevel >= 4 && DirectSight && Distance < HookLength * 0.9f &&
		(FreezeBetween || BlockerLevel >= 8);
}

inline float FreezeGoalScore(float Distance, bool Supported, float TravelTicks, float FreezeTicks)
{
	return Distance + (Supported ? -320.0f : 0.0f) +
		(TravelTicks > FreezeTicks ? 240.0f : 0.0f);
}

inline bool HoldEnemyHook(float GoalGain, float FreezeGain, int BlockerLevel)
{
	return GoalGain + FreezeGain > (BlockerLevel >= 8 ? 3.0f : 12.0f);
}

inline bool FireAtTarget(bool Fighting, bool TargetFrozen, bool ShotTowardFreeze)
{
	return ShotTowardFreeze && (!Fighting || !TargetFrozen);
}

inline bool FreezeTileLocked(int NowTick, int EnterTick, int TicksPerSecond)
{
	return EnterTick >= 0 && TicksPerSecond > 0 && NowTick - EnterTick >= TicksPerSecond;
}

// Matches CCharacter::FireWeapon's hammer query: the query center is three
// quarters of the attacker's radius ahead of the tee and FindEntities adds
// the target's radius to half the attacker's radius.
inline bool HammerHits(float DeltaX, float DeltaY, float AttackerRadius, float TargetRadius)
{
	const float Distance = std::hypot(DeltaX, DeltaY);
	if(Distance < 0.001f)
		return true;
	const float QueryX = DeltaX - DeltaX / Distance * AttackerRadius * 0.75f;
	const float QueryY = DeltaY - DeltaY / Distance * AttackerRadius * 0.75f;
	// Match the server's hammer query tolerance (the normal entity query adds
	// two units to its radius). Keep the prediction and hit check in sync.
	return std::hypot(QueryX, QueryY) < AttackerRadius * 0.5f + TargetRadius + 2.0f;
}

inline bool GoodPullAngle(float BotX, float BotY, float TargetX, float TargetY,
	float GoalX, float GoalY, float MinimumCosine)
{
	const float PullX = BotX - TargetX;
	const float PullY = BotY - TargetY;
	const float GoalDX = GoalX - TargetX;
	const float GoalDY = GoalY - TargetY;
	const float Product = std::hypot(PullX, PullY) * std::hypot(GoalDX, GoalDY);
	return Product > 0.001f && (PullX * GoalDX + PullY * GoalDY) / Product >= MinimumCosine;
}

inline bool ShouldWallHook(int RaceLevel, int DefenseLevel, bool Obstructed, bool Climbing,
	bool FallingTowardHazard, bool Stuck, int Tick, int BotId)
{
	if(!Obstructed && !Climbing && !FallingTowardHazard && !Stuck)
		return false;
	const int Interval = std::max(1, 8 - RaceLevel / 2 - DefenseLevel / 4);
	return (Tick + BotId) % Interval == 0;
}

inline float WallHookScore(float Progress, float UpwardPull, float HazardRisk,
	bool FallingTowardHazard, int RaceLevel, int DefenseLevel)
{
	return Progress * (1.0f + RaceLevel * 0.12f) +
		(FallingTowardHazard ? UpwardPull * (2.0f + DefenseLevel * 0.2f) : UpwardPull * 0.25f) -
		HazardRisk * (2.0f + DefenseLevel * 0.7f);
}
} // namespace bot_ai

#endif

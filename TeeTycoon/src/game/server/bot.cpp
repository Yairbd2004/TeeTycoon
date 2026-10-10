#include <generated/protocol.h>

#include <game/gamecore.h>

#include <engine/serverbrowser.h>

#include <engine/shared/config.h>

#include <game/layers.h>
#include <game/mapitems.h>

#include "gamecontext.h"



#include "botengine.h"
#include "bot_ai/brain.h"



#include "bot.h"

#include "player.h"

#include "entities/character.h"

#include "entities/pickup.h"



#include "ai/defence.h"

#include <thread>

#include <string>

#include <algorithm>

#include <queue>

#include <limits>

#include <vector>

#include <cstdlib>



CBot::CBot(CBotEngine *pBotEngine, CPlayer *pPlayer, int ownerid) : m_Genetics(CTarget::NUM_TARGETS,10)

{

	ownerAttacked = false;

	m_destruct = false;

	m_pBotEngine = pBotEngine;

	m_pPlayer = pPlayer;

	enemyID = -1;

	isClose = false;

	owner = ownerid;

	stuck = false;

	stay = false;

	m_pGameServer = pBotEngine->GameServer();

	m_Flags = 0;

	mem_zero(&m_InputData, sizeof(m_InputData));

	m_LastData = m_InputData;



	m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);

	m_ComputeTarget = {};
	m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
	m_ComputeTarget.m_PlayerCID = -1;
	m_ComputeTarget.m_NeedUpdate = true;

	m_pPath = &(pBotEngine->m_aPaths[pPlayer->GetCid()]);

	UpdateTargetOrder();



	BotEngine()->RegisterBot(m_pPlayer->GetCid(), this);

	stuckTick = 0;

	m_pStrategyPosition = NULL;

}



CBot::~CBot()

{

	delete m_pStrategyPosition;

	BotEngine()->UnRegisterBot(m_pPlayer->GetCid());

	m_pPath->m_Size = 0;

	GameServer()->Server()->SnapFreeId(m_SnapID);

}



void CBot::OnReset()

{

	m_Flags = 0;

	m_pPath->m_Size = 0;

	m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
	m_HasPathTarget = false;
	m_LastFreezeCrossPlanTick = -1;
	m_FreezeCrossUntilTick = -1;

	//m_Genetics.SetFitness(m_GenomeTick);

	//m_Genetics.NextGenome();

	//m_GenomeTick = 0;

	//UpdateTargetOrder();

	//dbg_msg("bot", "new target order %d %d %d %d %d %d %d %d", m_aTargetOrder[0], m_aTargetOrder[1], m_aTargetOrder[2], m_aTargetOrder[3], m_aTargetOrder[4], m_aTargetOrder[5], m_aTargetOrder[6], m_aTargetOrder[7]);

}



void CBot::UpdateTargetOrder()

{

	//int *pGenome = m_Genetics.GetGenome();

	const int *pGenome = &g_aBotPriority[m_pPlayer->GetCid()][0];

	for(int i = 0 ; i < CTarget::NUM_TARGETS ; i++)

	{

		int j = i;

		while(j > 0 && pGenome[i] > pGenome[m_aTargetOrder[j-1]])

		{

			m_aTargetOrder[j] = m_aTargetOrder[j-1];

			j--;

		}

		m_aTargetOrder[j] = i;

	}

}



vec2 CBot::ClosestCharacter()

{

	/*float d = -1;

	for(int c = 0; c < MAX_CLIENTS; c++)

		if(c != m_pPlayer->GetCid() && GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter() && (d < -1 || d > distance(m_pPlayer->GetCharacter()->GetPos(),GameServer()->m_apPlayers[c]->GetCharacter()->GetPos())))

		{

			d = distance(m_pPlayer->GetCharacter()->GetPos(),GameServer()->m_apPlayers[c]->GetCharacter()->GetPos());

			Pos = GameServer()->m_apPlayers[c]->GetCharacter()->GetPos();

		}*/

	vec2 Pos = vec2(0, 0);

	if(owner >= 0 && owner < MAX_CLIENTS && GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

		Pos = GameServer()->m_apPlayers[owner]->GetCharacter()->GetPos();

	return Pos;

}



bool CBot::IsHelpTarget(const CPlayer *pTarget, int TargetId) const
{
	if(TargetId == owner && owner >= 0)
		return true;
	return pTarget && m_HelpNames.contains(m_pGameServer->Server()->ClientName(TargetId));
}

bool CBot::IsBlockTarget(const CPlayer *pTarget, int TargetId) const
{
	if(!pTarget || IsHelpTarget(pTarget, TargetId))
		return false;
	return owner < 0 || m_BlockNames.contains(m_pGameServer->Server()->ClientName(TargetId));
}

void CBot::NotifyProtectedPlayerHurt(int VictimId, int EnemyId, bool DealtDamage)
{
	if(VictimId < 0 || VictimId >= MAX_CLIENTS || EnemyId < 0 || EnemyId >= MAX_CLIENTS ||
		!GameServer()->m_apPlayers[VictimId] || !GameServer()->m_apPlayers[EnemyId] ||
		!GameServer()->m_apPlayers[EnemyId]->GetCharacter() ||
		!IsHelpTarget(GameServer()->m_apPlayers[VictimId], VictimId) ||
		(owner < 0 && IsHelpTarget(GameServer()->m_apPlayers[EnemyId], EnemyId)))
		return;
	if(g_Config.m_SvBotDamageMode && DealtDamage && VictimId == owner)
		GameServer()->AutoBlockPetAggressor(owner, EnemyId);
	enemyID = EnemyId;
	enemyTime = GameServer()->m_apPlayers[EnemyId]->GetCharacter()->m_SpawnTick;
	ownerAttacked = true;
	m_ThreatUntilTick = GameServer()->Server()->Tick() + 10 * GameServer()->Server()->TickSpeed();
}

void CBot::UpdateTarget()
{
	m_Rescuing = false;
	m_Fighting = false;
	m_UsingLocalRoute = false;
	m_NoSafeRoute = false;
	m_pPlayer->to_fire = false;
	CPlayer *pOwner = owner >= 0 && owner < MAX_CLIENTS ? GameServer()->m_apPlayers[owner] : nullptr;
	if(owner >= 0 && (!pOwner || !pOwner->GetCharacter()))
	{
		m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
		m_ComputeTarget.m_PlayerCID = -1;
		return;
	}
	const vec2 MyPos = m_pPlayer->GetCharacter()->GetPos();
	const vec2 OwnerPos = pOwner ? pOwner->GetCharacter()->GetPos() : MyPos;
	const float Radius = owner < 0 ? 900.0f : 750.0f;
	int RescueId = -1;
	int BlockId = -1;
	int FollowId = -1;
	float RescueDist = 1e30f;
	float BlockDist = 1e30f;
	float FollowDist = 1e30f;
	if(pOwner && GameServer()->IsBotFrozen(pOwner->GetCharacter()))
	{
		RescueId = owner;
		RescueDist = 0.0f;
	}
	const bool ThreatActive = ownerAttacked && GameServer()->Server()->Tick() < m_ThreatUntilTick &&
		enemyID >= 0 && enemyID < MAX_CLIENTS && GameServer()->m_apPlayers[enemyID] &&
		GameServer()->m_apPlayers[enemyID]->GetCharacter() &&
		GameServer()->m_apPlayers[enemyID]->GetCharacter()->m_SpawnTick == enemyTime;
	for(int ClientId = 0; ClientId < GameServer()->Server()->MaxClients(); ClientId++)
	{
		CPlayer *pTarget = GameServer()->m_apPlayers[ClientId];
		if(!pTarget || pTarget->m_IsBot || !pTarget->GetCharacter() || ClientId == owner)
			continue;
		CCharacter *pChr = pTarget->GetCharacter();
		if(pChr->Team() != m_pPlayer->GetCharacter()->Team())
			continue;
		const float Dist = distance_squared(MyPos, pChr->GetPos());
		if(Dist > Radius * Radius || (pOwner && distance_squared(OwnerPos, pChr->GetPos()) > Radius * Radius))
			continue;
		const bool ThreatForPet = owner >= 0 && ThreatActive && ClientId == enemyID;
		if(IsHelpTarget(pTarget, ClientId) && !ThreatForPet)
		{
			if(GameServer()->IsBotFrozen(pChr) && RescueId != owner && Dist < RescueDist)
			{
				RescueId = ClientId;
				RescueDist = Dist;
			}
			if(Dist < FollowDist)
			{
				FollowId = ClientId;
				FollowDist = Dist;
			}
		}
		else if((IsBlockTarget(pTarget, ClientId) || (ThreatActive && ClientId == enemyID)) &&
			!(GameServer()->IsBotFrozen(pChr) && IsInFreezeFootprint(pChr->GetPos()) &&
				Collision()->CheckPoint(pChr->GetPos() + vec2(0, 32))) &&
			(Dist < BlockDist || (ThreatActive && ClientId == enemyID)))
		{
			BlockId = ClientId;
			BlockDist = Dist;
		}
	}
	if(!ThreatActive)
		ownerAttacked = false;
	const int TargetId = RescueId >= 0 ? RescueId : BlockId >= 0 ? BlockId : owner >= 0 ? owner : FollowId;
	if(TargetId < 0)
	{
		m_HasRescueRoute = false;
		m_HasLocalPath = false;
		m_LocalTargetId = -1;
		m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;
		m_ComputeTarget.m_PlayerCID = -1;
		m_pPath->m_Size = 0;
		m_Target = vec2(0, 0);
		return;
	}
	const bot_ai::ERole Role = bot_ai::ChooseRole(RescueId >= 0, RescueId < 0 && BlockId >= 0);
	m_Rescuing = Role == bot_ai::ERole::RESCUE;
	m_Fighting = Role == bot_ai::ERole::FIGHT;
	if(!m_Fighting || m_BlockFreezeTargetId != TargetId)
	{
		m_LastBlockPlanTick = -1;
		m_LastBlockStancePlanTick = -1;
		m_HasBlockFreezeGoal = false;
		m_HasBlockStance = false;
	}
	m_BlockFreezeTargetId = m_Fighting ? TargetId : -1;
	if(!m_Rescuing || m_RescueTargetId != TargetId)
	{
		m_HasRescueRoute = false;
		m_LastRescuePlanTick = -1;
		m_RescueCandidateOffset = 0;
		m_RescueProgressTick = -1;
	}
	m_RescueTargetId = m_Rescuing ? TargetId : -1;
	const vec2 TargetPos = GameServer()->m_apPlayers[TargetId]->GetCharacter()->GetPos();
	const float RefreshDist = bot_ai::RouteRefreshDistance(Skill(PET_SKILL_RACE));
	const bool WasTeleTarget = m_ComputeTarget.m_Type == CTarget::TARGET_AIR;
	m_ComputeTarget.m_NeedUpdate = m_ComputeTarget.m_Type != CTarget::TARGET_PLAYER ||
		m_ComputeTarget.m_PlayerCID != TargetId ||
		!m_HasPathTarget || distance_squared(m_LastPathTargetPos, TargetPos) > RefreshDist * RefreshDist ||
		(m_LastRouteRefreshTick >= 0 && GameServer()->Server()->Tick() - m_LastRouteRefreshTick >=
			std::max(20, 160 - Skill(PET_SKILL_RACE) * 14));
	m_ComputeTarget.m_Pos = TargetPos;
	m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;
	m_ComputeTarget.m_PlayerCID = TargetId;
	// After a freeze respawn, reach a frozen owner through the map. A teleporter
	// is a navigation waypoint, never a code-driven escape from freeze.
	if(m_RaceToFrozenOwner && TargetId == owner && pOwner && GameServer()->IsBotFrozen(pOwner->GetCharacter()) &&
		!BotEngine()->HasRoute(MyPos, TargetPos))
	{
		const int Now = GameServer()->Server()->Tick();
		if(m_LastTeleSearchTick < 0 || Now - m_LastTeleSearchTick >= 2 * GameServer()->Server()->TickSpeed())
		{
			m_LastTeleSearchTick = Now;
			m_HasTeleTarget = false;
			float BestDistance = 1e30f;
			for(const auto &Entrance : BotEngine()->m_vTeleEntrances)
			{
				if(!BotEngine()->HasRoute(MyPos, Entrance.m_Pos))
					continue;
				for(const vec2 Exit : GameServer()->Collision()->TeleOuts(Entrance.m_Number - 1))
				{
					if(!BotEngine()->HasRoute(Exit, TargetPos))
						continue;
					const float Distance = distance(MyPos, Entrance.m_Pos) + distance(Exit, TargetPos);
					if(Distance < BestDistance)
					{
						BestDistance = Distance;
						m_TeleTarget = Entrance.m_Pos;
						m_HasTeleTarget = true;
					}
				}
			}
		}
		if(m_HasTeleTarget)
		{
			m_ComputeTarget.m_NeedUpdate = !WasTeleTarget ||
				!m_HasPathTarget || distance_squared(m_LastPathTargetPos, m_TeleTarget) > 1.0f;
			m_ComputeTarget.m_Pos = m_TeleTarget;
			m_ComputeTarget.m_Type = CTarget::TARGET_AIR;
			m_ComputeTarget.m_PlayerCID = -1;
		}
	}
	else
		m_HasTeleTarget = false;
	if(m_Rescuing && !m_HasTeleTarget && GameServer()->IsBotFrozen(GameServer()->m_apPlayers[TargetId]->GetCharacter()))
	{
		CCharacter *pRescueTarget = GameServer()->m_apPlayers[TargetId]->GetCharacter();
		const bool InFreezeTile = IsInFreezeFootprint(TargetPos);
		const bool NeedHammer = Skill(PET_SKILL_HELPER) < 5 || !CanUseWeapon(WEAPON_LASER) ||
			IsDangerous(TargetPos + vec2(0, 24));
		const bool CanShootHere = !InFreezeTile && !IsDangerous(MyPos) &&
			(NeedHammer ? CanHammerHit(pRescueTarget) :
				distance(MyPos, TargetPos) < Tuning()->m_LaserReach - 24.0f &&
				!Collision()->FastIntersectLine(MyPos, TargetPos, nullptr, nullptr));
		if(!CanShootHere)
		{
			const int Now = GameServer()->Server()->Tick();
			const int Interval = 12 + (PET_SKILL_MAX_LEVEL - Skill(PET_SKILL_RACE)) * 3;
			if(m_RescueProgressTick < 0 || distance_squared(MyPos, m_RescueProgressPos) > 72.0f * 72.0f)
			{
				m_RescueProgressPos = MyPos;
				m_RescueProgressTick = Now;
			}
			const bool RescueStalled = Now - m_RescueProgressTick > 2 * GameServer()->Server()->TickSpeed();
			if((stuck || RescueStalled) && m_LastRescuePlanTick >= 0 && Now - m_LastRescuePlanTick >= 20)
			{
				// A geometrically reachable firing position can still be hard to
				// reach with tee physics. Try another approach instead of jumping
				// at the same wall indefinitely.
				m_RescueCandidateOffset += 8;
				m_HasRescueRoute = false;
				m_LastRescuePlanTick = -1;
				m_RescueProgressPos = MyPos;
				m_RescueProgressTick = Now;
			}
			if(m_LastRescuePlanTick < 0 || Now - m_LastRescuePlanTick >= Interval ||
				(m_HasRescueRoute && distance(MyPos, m_RescueWaypoint) < 30.0f) ||
				distance_squared(m_LastRescueTargetPos, TargetPos) > 36.0f * 36.0f)
			{
				m_LastRescuePlanTick = Now;
				m_LastRescueTargetPos = TargetPos;
				m_HasRescueRoute = FindRescueRoute(TargetId, TargetPos, &m_RescueWaypoint);
			}
			if(m_HasRescueRoute)
			{
				m_UsingLocalRoute = true;
				m_HasPathTarget = false;
				m_pPath->m_Size = 0;
				m_ComputeTarget.m_Type = CTarget::TARGET_AIR;
				m_ComputeTarget.m_Pos = m_RescueWaypoint;
				m_ComputeTarget.m_PlayerCID = TargetId;
				m_ComputeTarget.m_NeedUpdate = false;
			}
			else
				m_NoSafeRoute = true;
		}
		else
			m_HasRescueRoute = false;
	}
	// The large-map triangulation is deliberately sparse. Nearby obstacles
	// need a finer route for following and blocking as well as for rescues.
	if(!m_Rescuing && !m_HasTeleTarget && Skill(PET_SKILL_RACE) >= 4 &&
		distance(MyPos, TargetPos) < 1200.0f &&
		Collision()->FastIntersectLine(MyPos, TargetPos, nullptr, nullptr))
	{
		const int Now = GameServer()->Server()->Tick();
		const int Interval = 18 + (PET_SKILL_MAX_LEVEL - Skill(PET_SKILL_RACE)) * 4;
		if(m_LocalTargetId != TargetId)
		{
			m_LocalTargetId = TargetId;
			m_LastLocalPlanTick = -1;
			m_HasLocalPath = false;
		}
		if(m_LastLocalPlanTick < 0 || Now - m_LastLocalPlanTick >= Interval ||
			(m_HasLocalPath && distance(MyPos, m_LocalWaypoint) < 30.0f) ||
			distance_squared(m_LastLocalTargetPos, TargetPos) > 48.0f * 48.0f)
		{
			m_LastLocalPlanTick = Now;
			m_LastLocalTargetPos = TargetPos;
			m_HasLocalPath = FindLocalRoute(MyPos, TargetPos, &m_LocalWaypoint);
		}
		if(m_HasLocalPath)
		{
			m_UsingLocalRoute = true;
			m_HasPathTarget = false;
			m_pPath->m_Size = 0;
			m_ComputeTarget.m_Type = CTarget::TARGET_AIR;
			m_ComputeTarget.m_Pos = m_LocalWaypoint;
			m_ComputeTarget.m_PlayerCID = TargetId;
			m_ComputeTarget.m_NeedUpdate = false;
		}
		else
			m_NoSafeRoute = true;
	}
	else
		m_HasLocalPath = false;
	if(m_Fighting && Skill(PET_SKILL_BLOCKER) >= 4 && !m_HasTeleTarget)
	{
		const int Now = GameServer()->Server()->Tick();
		if(m_LastBlockPlanTick < 0 || Now - m_LastBlockPlanTick >=
			std::max(15, 60 - Skill(PET_SKILL_BLOCKER) * 4) ||
			distance_squared(m_LastBlockTargetPos, TargetPos) > 48.0f * 48.0f)
		{
			m_LastBlockPlanTick = Now;
			m_LastBlockTargetPos = TargetPos;
			bool Supported = false;
			const int FreezeTicks = GameServer()->m_apPlayers[TargetId]->GetCharacter()->m_FreezeTime > 0 ?
				GameServer()->m_apPlayers[TargetId]->GetCharacter()->m_FreezeTime :
				g_Config.m_SvFreezeDelay * GameServer()->Server()->TickSpeed();
			m_HasBlockFreezeGoal = FindBlockFreezeGoal(TargetPos, FreezeTicks, &m_BlockFreezeGoal, &Supported);
			m_HasBlockStance = false;
			m_LastBlockStancePlanTick = -1;
		}
		if(m_HasBlockFreezeGoal)
		{
			const vec2 ToFreeze = m_BlockFreezeGoal - TargetPos;
			const vec2 ToBot = MyPos - TargetPos;
			const bool DirectHook = !Collision()->FastIntersectLine(MyPos, TargetPos, nullptr, nullptr) &&
				length(ToBot) < static_cast<float>(Tuning()->m_HookLength) * 0.85f;
			const float LaunchDistance = std::min(static_cast<float>(Tuning()->m_HookLength) * 0.68f,
				std::max(135.0f, length(ToFreeze) * 0.8f));
			const bool Aligned = length(ToBot) >= LaunchDistance &&
				bot_ai::GoodPullAngle(MyPos.x, MyPos.y, TargetPos.x, TargetPos.y,
					m_BlockFreezeGoal.x, m_BlockFreezeGoal.y, 0.72f);
			if(!DirectHook || !Aligned)
			{
				if(m_LastBlockStancePlanTick < 0 || Now - m_LastBlockStancePlanTick >= 20 ||
					(m_HasBlockStance && distance(MyPos, m_BlockWaypoint) < 36.0f))
				{
					m_LastBlockStancePlanTick = Now;
					m_HasBlockStance = FindBlockStance(MyPos, TargetPos, m_BlockFreezeGoal,
						&m_BlockStance, &m_BlockWaypoint);
				}
				if(m_HasBlockStance)
				{
					m_UsingLocalRoute = true;
					m_pPath->m_Size = 0;
					m_ComputeTarget.m_Type = CTarget::TARGET_AIR;
					m_ComputeTarget.m_Pos = m_BlockWaypoint;
					m_ComputeTarget.m_PlayerCID = TargetId;
					m_ComputeTarget.m_NeedUpdate = false;
				}
			}
		}
	}
}

bool CBot::NeedPickup(int Type)

{

	switch(Type)

	{

	//case CTarget::TARGET_HEALTH:

		//return m_pPlayer->GetCharacter()->getHealth < 5; //need to add getHealth function.

	case CTarget::TARGET_ARMOR:

		return m_pPlayer->GetCharacter()->GetArmor() < 5;

	//need to add those cases.

	/*case CTarget::TARGET_WEAPON_SHOTGUN:

		return m_pPlayer->GetCharacter()->GetAmmoCount(WEAPON_SHOTGUN) < 5;

	case CTarget::TARGET_WEAPON_GRENADE:

		return m_pPlayer->GetCharacter()->GetAmmoCount(WEAPON_GRENADE) < 5;

	case CTarget::TARGET_WEAPON_LASER:

		return m_pPlayer->GetCharacter()->GetAmmoCount(WEAPON_RIFLE) < 5;*/

	}

	return false;

}



bool CBot::FindPickup(int Type, vec2 *pPos, float Radius)

{

	int SubType = 0;

	switch(Type)

	{

		case CTarget::TARGET_ARMOR:

			Type = POWERUP_ARMOR;

			break;

		case CTarget::TARGET_HEALTH:

			Type = POWERUP_HEALTH;

			break;

		case CTarget::TARGET_WEAPON_SHOTGUN:

			Type = POWERUP_WEAPON;

			SubType = WEAPON_SHOTGUN;

			break;

		case CTarget::TARGET_WEAPON_GRENADE:

			Type = POWERUP_WEAPON;

			SubType = WEAPON_GRENADE;

			break;

		case CTarget::TARGET_WEAPON_LASER:

			Type = POWERUP_WEAPON;

			SubType = WEAPON_LASER;

			break;

	}

	CEntity *pEnt = GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_PICKUP);

	bool Found = false;

	for(;	pEnt; pEnt = pEnt->TypeNext())

	{

		CPickup *pPickup = (CPickup *) pEnt;

		if(/*pPickup->GetType() == Type && pPickup->GetSubType() == SubType && pPickup->IsSpawned() && */Radius > distance(pPickup->GetPos(),m_pPlayer->GetCharacter()->GetPos()) )

		{

			*pPos = pPickup->GetPos();

			Radius = distance(pPickup->GetPos(),m_pPlayer->GetCharacter()->GetPos());

			Found = true;

		}

	}

	return Found;

}



bool CBot::IsGrounded()

{

	return m_pPlayer->GetCharacter()->IsGrounded();

}



void CBot::checkStuck(bool inSight)

{
	if(inSight || m_LastProgressTick == GameServer()->Server()->Tick())
	{
		stuckTick = GameServer()->Server()->Tick();
		stuck = false;
		return;
	}

	if(stuckTick == 0 && !inSight)

	{

		stuckTick = GameServer()->Server()->Tick();

	}

	else if(stuckTick + std::max(45, 210 - Skill(PET_SKILL_RACE) * 15) <= GameServer()->Server()->Tick() && !inSight)

	{

		this->stuck = true;

		stuckTick = GameServer()->Server()->Tick();

	}

}



void CBot::emote()

{

	if(emoteTick == 0)

	{

		emoteTick = GameServer()->Server()->Tick();

	}

	else if(emoteTick + 100 <= GameServer()->Server()->Tick())

	{

		CNetMsg_Sv_Emoticon Msg;

		Msg.m_ClientId = m_pPlayer->GetCid();

		Msg.m_Emoticon = 2;

		GameServer()->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

		emoteTick = GameServer()->Server()->Tick();

	}

}



int CBot::Skill(EPetSkill SkillId) const
{
	return owner < 0 ? PET_SKILL_MAX_LEVEL : PetSkillLevel(m_pPlayer->m_aPetSkills[SkillId]);
}

bool CBot::CanUseWeapon(int Weapon) const
{
	return Weapon == WEAPON_HAMMER || owner < 0 || (Weapon >= WEAPON_GUN && Weapon <= m_pPlayer->weaponBot);
}

bool CBot::CanHammerHit(CCharacter *pTarget)
{
	CCharacter *pMe = m_pPlayer->GetCharacter();
	if(!pMe || !pTarget || pMe->Core()->m_HammerHitDisabled ||
		!pMe->CanCollide(pTarget->GetPlayer()->GetCid()))
		return false;
	const vec2 Delta = pTarget->GetPos() - pMe->GetPos();
	return bot_ai::HammerHits(Delta.x, Delta.y, pMe->GetProximityRadius(), pTarget->GetProximityRadius());
}

bool CBot::IsDangerous(vec2 Pos)
{
	// GetMapIndex deliberately returns -1 on ordinary empty air tiles.
	// PureMapIndex is required for collision sampling in open space.
	const int Index = Collision()->GetPureMapIndex(Pos);
	const auto DangerousTile = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE || Tile == TILE_DEATH;
	};
	return DangerousTile(Collision()->GetTileIndex(Index)) || DangerousTile(Collision()->GetFrontTileIndex(Index)) ||
		DangerousTile(Collision()->GetSwitchType(Index));
}

bool CBot::IsFreezeAt(vec2 Pos)
{
	const int Index = Collision()->GetPureMapIndex(Pos);
	const auto FreezeTile = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};
	return FreezeTile(Collision()->GetTileIndex(Index)) ||
		FreezeTile(Collision()->GetFrontTileIndex(Index)) || FreezeTile(Collision()->GetSwitchType(Index));
}

bool CBot::IsInFreezeFootprint(vec2 Pos)
{
	return IsFreezeAt(Pos) || IsFreezeAt(Pos + vec2(0, 14)) ||
		IsFreezeAt(Pos + vec2(0, 24)) || IsFreezeAt(Pos + vec2(14, 0)) ||
		IsFreezeAt(Pos - vec2(14, 0));
}

bool CBot::IsDeathAt(vec2 Pos)
{
	const int Index = Collision()->GetPureMapIndex(Pos);
	return Collision()->GetTileIndex(Index) == TILE_DEATH ||
		Collision()->GetFrontTileIndex(Index) == TILE_DEATH ||
		Collision()->GetSwitchType(Index) == TILE_DEATH;
}

bool CBot::FindSafeFreezeCrossing(vec2 Goal, int *pDirection, int *pJump, int *pHook, vec2 *pAim)
{
	if(!pDirection || !pJump || !pHook || !pAim || !m_pPlayer->GetCharacter())
		return false;
	const CCharacterCore *pCore = m_pPlayer->GetCharacter()->Core();
	const float StartDistance = distance(pCore->m_Pos, Goal);
	if(StartDistance < 100.0f || StartDistance > 650.0f)
		return false;
	float BestScore = 0.0f;
	bool Found = false;
	for(int Direction = -1; Direction <= 1; Direction++)
	{
		for(int Jump = 0; Jump <= 1; Jump++)
		{
			for(int HookIndex = -1; HookIndex < 12; HookIndex++)
			{
				const bool Hook = HookIndex >= 0 && g_Config.m_SvBotAllowHook;
				if(HookIndex >= 0 && !Hook)
					continue;
				const vec2 Aim = Hook ? direction(2.0f * pi * HookIndex / 12.0f) *
					static_cast<float>(Tuning()->m_HookLength) : Goal - pCore->m_Pos;
				if(Hook)
				{
					vec2 HitPos;
					const int HitTile = Collision()->FastIntersectLine(pCore->m_Pos, pCore->m_Pos + Aim,
						&HitPos, nullptr);
					if(!HitTile || HitTile == TILE_NOHOOK)
						continue;
				}
				CWorldCore World;
				CCharacterCore Predicted = *pCore;
				Predicted.Init(&World, Collision());
				Predicted.m_Tuning = pCore->m_Tuning;
				Predicted.m_Input.m_TargetX = Aim.x;
				Predicted.m_Input.m_TargetY = Aim.y;
				bool TouchedFreeze = false;
				int SafeGroundTicks = 0;
				for(int Tick = 0; Tick < 80; Tick++)
				{
					Predicted.m_Input.m_Direction = TouchedFreeze ? 0 : Direction;
					Predicted.m_Input.m_Jump = TouchedFreeze ? 0 : Jump;
					Predicted.m_Input.m_Hook = TouchedFreeze ? 0 : Hook;
					Predicted.Tick(true);
					Predicted.Move();
					Predicted.Quantize();
					const vec2 Pos = Predicted.m_Pos;
					if(IsDeathAt(Pos) || IsDeathAt(Pos + vec2(0, 14)) || IsDeathAt(Pos - vec2(0, 14)) ||
						IsDeathAt(Pos + vec2(14, 0)) || IsDeathAt(Pos - vec2(14, 0)))
						break;
					TouchedFreeze |= IsFreezeAt(Pos) || IsFreezeAt(Pos + vec2(0, 14)) ||
						IsFreezeAt(Pos - vec2(0, 14)) || IsFreezeAt(Pos + vec2(14, 0)) ||
						IsFreezeAt(Pos - vec2(14, 0));
					if(!TouchedFreeze)
						continue;
					if(!IsFreezeAt(Pos) && !IsFreezeAt(Pos + vec2(0, 14)) &&
						!IsFreezeAt(Pos + vec2(14, 0)) && !IsFreezeAt(Pos - vec2(14, 0)) &&
						Collision()->IsOnGround(Pos, CCharacterCore::PhysicalSize()) &&
						length(Predicted.m_Vel) < 2.5f)
						SafeGroundTicks++;
					else
						SafeGroundTicks = 0;
					if(SafeGroundTicks < 6)
						continue;
					const float Gain = StartDistance - distance(Pos, Goal);
					const float Score = Gain - Tick * 0.7f - (Hook ? 5.0f : 0.0f);
					if(Gain > 80.0f && Score > BestScore)
					{
						BestScore = Score;
						*pDirection = Direction;
						*pJump = Jump;
						*pHook = Hook;
						*pAim = Aim;
						Found = true;
					}
					break;
				}
			}
		}
	}
	return Found;
}

bool CBot::FindBlockFreezeGoal(vec2 TargetPos, int RemainingFreezeTicks, vec2 *pGoal, bool *pSupported)
{
	if(!pGoal || !pSupported)
		return false;
	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	const int CenterX = std::clamp(static_cast<int>(TargetPos.x / 32), 0, Width - 1);
	const int CenterY = std::clamp(static_cast<int>(TargetPos.y / 32), 0, Height - 1);
	const int Reach = Skill(PET_SKILL_BLOCKER) >= 8 ? 13 : 7;
	float BestScore = 1e30f;
	bool Found = false;
	for(int Y = std::max(0, CenterY - Reach); Y <= std::min(Height - 1, CenterY + Reach); Y++)
	{
		for(int X = std::max(0, CenterX - Reach); X <= std::min(Width - 1, CenterX + Reach); X++)
		{
			const vec2 Pos(X * 32.0f + 16.0f, Y * 32.0f + 16.0f);
			if(!IsFreezeAt(Pos) || Collision()->CheckPoint(Pos) ||
				Collision()->FastIntersectLine(TargetPos, Pos, nullptr, nullptr))
				continue;
			const bool Supported = Collision()->CheckPoint(Pos + vec2(0, 32));
			const float Dist = distance(TargetPos, Pos);
			// A supported freeze keeps the target frozen. Prefer it when it can
			// be reached before the current freeze wears off; otherwise throw
			// the target at a nearby freeze first.
			const float TimeBudget = static_cast<float>(std::max(1, RemainingFreezeTicks));
			const float TravelTicks = Dist / std::max(1.0f, static_cast<float>(Tuning()->m_HookDragSpeed)) + 12.0f;
			const float Score = bot_ai::FreezeGoalScore(Dist, Supported, TravelTicks, TimeBudget);
			if(Score < BestScore)
			{
				BestScore = Score;
				*pGoal = Pos;
				*pSupported = Supported;
				Found = true;
			}
		}
	}
	return Found;
}

bool CBot::FindBlockStance(vec2 MyPos, vec2 TargetPos, vec2 FreezeGoal, vec2 *pStance, vec2 *pWaypoint)
{
	if(!pStance || !pWaypoint || distance_squared(TargetPos, FreezeGoal) < 24.0f * 24.0f)
		return false;
	const vec2 TowardFreeze = normalize(FreezeGoal - TargetPos);
	const vec2 Across(-TowardFreeze.y, TowardFreeze.x);
	float BestScore = 1e30f;
	bool Found = false;
	const float DesiredPull = std::min(static_cast<float>(Tuning()->m_HookLength) * 0.73f,
		std::max(160.0f, distance(TargetPos, FreezeGoal) * 0.9f));
	for(float PullDistance : {160.0f, 205.0f, 250.0f, 295.0f, 340.0f})
	{
		if(PullDistance < DesiredPull * 0.88f ||
			PullDistance > static_cast<float>(Tuning()->m_HookLength) * 0.82f)
			continue;
		for(float Side : {0.0f, -32.0f, 32.0f, -64.0f, 64.0f})
		{
			const vec2 Pos = TargetPos + TowardFreeze * PullDistance + Across * Side;
			if(Collision()->CheckPoint(Pos) || Collision()->CheckPoint(Pos + vec2(0, 14)) ||
				Collision()->CheckPoint(Pos - vec2(0, 14)) || IsDangerous(Pos) ||
				IsDangerous(Pos + vec2(0, 14)) || IsDangerous(Pos + vec2(14, 0)) ||
				IsDangerous(Pos - vec2(14, 0)) ||
				Collision()->FastIntersectLine(Pos, TargetPos, nullptr, nullptr))
				continue;
			vec2 Waypoint;
			if(!FindLocalRoute(MyPos, Pos, &Waypoint))
				continue;
			const float Score = distance(MyPos, Pos) * 0.65f + std::abs(Side) * 0.6f +
				std::abs(PullDistance - DesiredPull) * 1.2f;
			if(Score < BestScore)
			{
				BestScore = Score;
				*pStance = Pos;
				*pWaypoint = Waypoint;
				Found = true;
			}
		}
	}
	return Found;
}

bool CBot::ShouldHoldEnemyHook(const CCharacter *pTarget, vec2 FreezeGoal)
{
	if(!pTarget || !m_pPlayer->GetCharacter())
		return false;
	const vec2 TargetPos = pTarget->GetPos();
	if(IsInFreezeFootprint(TargetPos) && Collision()->CheckPoint(TargetPos + vec2(0, 32)))
		return false;
	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->Core();
	vec2 BotPos = pMe->m_Pos;
	vec2 BotVel = pMe->m_Vel;
	vec2 PulledPos = TargetPos;
	vec2 PulledVel = pTarget->Core()->m_Vel;
	vec2 FreePos = TargetPos;
	vec2 FreeVel = PulledVel;
	const float Gravity = static_cast<float>(Tuning()->m_Gravity);
	const float Drag = static_cast<float>(Tuning()->m_HookDragSpeed);
	const float Accel = static_cast<float>(Tuning()->m_HookDragAccel);
	const float GroundFriction = static_cast<float>(Tuning()->m_GroundFriction);
	const float AirFriction = static_cast<float>(Tuning()->m_AirFriction);
	float FinalGain = -1e30f;
	float FreezeGain = 0.0f;
	for(int Tick = 0; Tick < 16; Tick++)
	{
		BotVel.x *= Collision()->IsOnGround(BotPos, CCharacterCore::PhysicalSize()) ? GroundFriction : AirFriction;
		BotVel.y += Gravity;
		Collision()->MoveBox(&BotPos, &BotVel, CCharacterCore::PhysicalSizeVec2(), vec2(0, 0));
		FreeVel.x *= Collision()->IsOnGround(FreePos, CCharacterCore::PhysicalSize()) ? GroundFriction : AirFriction;
		FreeVel.y += Gravity;
		Collision()->MoveBox(&FreePos, &FreeVel, CCharacterCore::PhysicalSizeVec2(), vec2(0, 0));
		const vec2 Delta = BotPos - PulledPos;
		const float Dist = length(Delta);
		PulledVel.x *= Collision()->IsOnGround(PulledPos, CCharacterCore::PhysicalSize()) ? GroundFriction : AirFriction;
		if(Dist > CCharacterCore::PhysicalSize() * 1.5f)
		{
			const float HookAccel = Accel * Dist / std::max(1.0f, static_cast<float>(Tuning()->m_HookLength));
			const vec2 Force = Delta / Dist * HookAccel * 1.5f;
			PulledVel.x = std::clamp(PulledVel.x + Force.x, -Drag, Drag);
			PulledVel.y = std::clamp(PulledVel.y + Force.y, -Drag, Drag);
			BotVel.x = std::clamp(BotVel.x - Force.x / 6.0f, -Drag, Drag);
			BotVel.y = std::clamp(BotVel.y - Force.y / 6.0f, -Drag, Drag);
		}
		PulledVel.y += Gravity;
		Collision()->MoveBox(&PulledPos, &PulledVel, CCharacterCore::PhysicalSizeVec2(), vec2(0, 0));
		if(IsInFreezeFootprint(PulledPos) && Collision()->CheckPoint(PulledPos + vec2(0, 32)))
			return false; // release on a freeze tile with a floor
		if(IsInFreezeFootprint(PulledPos) && !IsInFreezeFootprint(FreePos))
			FreezeGain = 45.0f;
		if(IsInFreezeFootprint(FreePos) && !IsInFreezeFootprint(PulledPos))
			return false;
		FinalGain = distance(FreePos, FreezeGoal) - distance(PulledPos, FreezeGoal);
	}
	return bot_ai::HoldEnemyHook(FinalGain, FreezeGain, Skill(PET_SKILL_BLOCKER));
}

bool CBot::SafeTravelSegment(vec2 Start, vec2 End)
{
	if(Collision()->FastIntersectLine(Start, End, nullptr, nullptr) ||
		Collision()->FastIntersectLine(Start + vec2(0, 14), End + vec2(0, 14), nullptr, nullptr) ||
		Collision()->FastIntersectLine(Start - vec2(0, 14), End - vec2(0, 14), nullptr, nullptr) ||
		Collision()->FastIntersectLine(Start + vec2(14, 0), End + vec2(14, 0), nullptr, nullptr) ||
		Collision()->FastIntersectLine(Start - vec2(14, 0), End - vec2(14, 0), nullptr, nullptr))
		return false;
	if(Skill(PET_SKILL_DEFENSE) < 6)
		return true;
	const int Steps = std::max(1, static_cast<int>(distance(Start, End) / 16.0f));
	for(int i = 1; i <= Steps; i++)
	{
		const vec2 Pos = Start + (End - Start) * (static_cast<float>(i) / Steps);
		if(IsDangerous(Pos) || IsDangerous(Pos + vec2(0, 14)) || IsDangerous(Pos - vec2(0, 14)) ||
			IsDangerous(Pos + vec2(14, 0)) || IsDangerous(Pos - vec2(14, 0)))
			return false;
	}
	return true;
}

bool CBot::HasFreezeBelow(vec2 Pos, float MaxDistance)
{
	for(float Offset = 20.0f; Offset <= MaxDistance; Offset += 12.0f)
	{
		const vec2 Sample = Pos + vec2(0, Offset);
		if(IsDangerous(Sample) || IsDangerous(Sample + vec2(10, 0)) || IsDangerous(Sample - vec2(10, 0)))
			return true;
		// A solid floor above the hazard makes the drop safe at this x.
		if(Collision()->CheckPoint(Sample))
			return false;
	}
	return false;
}

void CBot::OnSkillUpgrade()
{
	// The next bot tick must use the new skill level and choose a fresh route.
	m_LastRescuePlanTick = -1;
	m_HasRescueRoute = false;
	m_LastLocalPlanTick = -1;
	m_HasLocalPath = false;
	m_LastBlockPlanTick = -1;
	m_LastBlockStancePlanTick = -1;
	m_HasBlockFreezeGoal = false;
	m_HasBlockStance = false;
	m_HasPathTarget = false;
	m_pPath->m_Size = 0;
	m_ComputeTarget.m_NeedUpdate = true;
	m_LastProgressTick = -1;
	m_LastGoalProgressTick = -1;
	m_LastWallHookPlanTick = -1;
	m_LastFreezeCrossPlanTick = -1;
	m_FreezeCrossUntilTick = -1;
}

bool CBot::FindLocalRoute(vec2 Start, vec2 Goal, vec2 *pWaypoint)
{
	if(!pWaypoint)
		return false;
	// A tile path can walk through open air. When a goal is far above the bot,
	// first walk to a place where a jump can actually reach a wall hook.
	if(Goal.y < Start.y - 160.0f && g_Config.m_SvBotAllowHook && Skill(PET_SKILL_RACE) >= 5)
	{
		const auto HasClimbHook = [this, Start](vec2 Position) {
			const vec2 Launch = Position + vec2(0, -72.0f);
			if(Collision()->TestBox(Launch, CCharacterCore::PhysicalSizeVec2()) ||
				Collision()->FastIntersectLine(Position, Launch, nullptr, nullptr) || IsDangerous(Launch))
				return false;
			for(int Ray = 0; Ray < 24; Ray++)
			{
				const vec2 Aim = direction(2.0f * pi * Ray / 24.0f);
				if(Aim.y > -0.35f)
					continue;
				vec2 Hit;
				const int Tile = Collision()->FastIntersectLine(Launch,
					Launch + Aim * static_cast<float>(Tuning()->m_HookLength), &Hit, nullptr);
				if(Tile && Tile != TILE_NOHOOK && Hit.y < Start.y - 125.0f &&
					distance(Launch, Hit) > 55.0f)
					return true;
			}
			return false;
		};
		if(!HasClimbHook(Start))
		{
			vec2 Best = Start;
			float BestScore = 1e30f;
			for(int Side : {-1, 1})
			{
				for(int Step = 1; Step <= 10; Step++)
				{
					const vec2 Staging = Start + vec2(Side * Step * 32.0f, 0);
					if(Collision()->TestBox(Staging, CCharacterCore::PhysicalSizeVec2()) ||
						IsDangerous(Staging) ||
						(Collision()->IsOnGround(Start, CCharacterCore::PhysicalSize()) &&
							!Collision()->IsOnGround(Staging, CCharacterCore::PhysicalSize())) ||
						!SafeTravelSegment(Start, Staging) ||
						!HasClimbHook(Staging))
						continue;
					const float Score = distance(Start, Staging) + distance(Staging, Goal) * 0.35f;
					if(Score < BestScore)
					{
						BestScore = Score;
						Best = Staging;
					}
				}
			}
			if(BestScore < 1e29f)
			{
				*pWaypoint = Best;
				return true;
			}
		}
	}
	if(distance(Start, Goal) < 300.0f && SafeTravelSegment(Start, Goal))
	{
		*pWaypoint = Goal;
		return true;
	}
	const int MapWidth = Collision()->GetWidth();
	const int MapHeight = Collision()->GetHeight();
	const int StartX = std::clamp(static_cast<int>(Start.x / 32), 0, MapWidth - 1);
	const int StartY = std::clamp(static_cast<int>(Start.y / 32), 0, MapHeight - 1);
	const int GoalX = std::clamp(static_cast<int>(Goal.x / 32), 0, MapWidth - 1);
	const int GoalY = std::clamp(static_cast<int>(Goal.y / 32), 0, MapHeight - 1);
	const int MinX = std::max(0, std::min(StartX, GoalX) - 20);
	const int MinY = std::max(0, std::min(StartY, GoalY) - 20);
	const int MaxX = std::min(MapWidth - 1, std::max(StartX, GoalX) + 20);
	const int MaxY = std::min(MapHeight - 1, std::max(StartY, GoalY) + 20);
	const int Width = MaxX - MinX + 1;
	const int Height = MaxY - MinY + 1;
	if(Width * Height > 16000)
		return false;
	const int StartIndex = (StartY - MinY) * Width + StartX - MinX;
	const int GoalIndex = (GoalY - MinY) * Width + GoalX - MinX;
	const auto Position = [MinX, MinY, Width](int Index) {
		return vec2((MinX + Index % Width) * 32 + 16.0f, (MinY + Index / Width) * 32 + 16.0f);
	};
	std::vector<int8_t> aPassable(Width * Height, -1);
	const int Defense = Skill(PET_SKILL_DEFENSE);
	const auto Passable = [this, &aPassable, &Position, Defense, StartIndex](int Index) {
		if(aPassable[Index] != -1)
			return aPassable[Index] == 1;
		const vec2 Pos = Position(Index);
		const bool Clear = !Collision()->CheckPoint(Pos) &&
			!Collision()->CheckPoint(Pos + vec2(14, 0)) && !Collision()->CheckPoint(Pos - vec2(14, 0)) &&
			!Collision()->CheckPoint(Pos + vec2(0, 14)) && !Collision()->CheckPoint(Pos - vec2(0, 14));
		const bool Safe = Index == StartIndex || Defense < 7 ||
			(!IsDangerous(Pos) && !IsDangerous(Pos + vec2(0, 14)) && !IsDangerous(Pos - vec2(0, 14)) &&
				!IsDangerous(Pos + vec2(14, 0)) && !IsDangerous(Pos - vec2(14, 0)));
		aPassable[Index] = Clear && Safe ? 1 : 0;
		return aPassable[Index] == 1;
	};
	if(!Passable(GoalIndex))
		return false;
	std::vector<float> aCost(Width * Height, std::numeric_limits<float>::infinity());
	std::vector<int> aParent(Width * Height, -1);
	std::vector<uint8_t> aClosed(Width * Height, 0);
	using CQueueEntry = std::pair<float, int>;
	std::priority_queue<CQueueEntry, std::vector<CQueueEntry>, std::greater<CQueueEntry>> Queue;
	aCost[StartIndex] = 0;
	Queue.emplace(0.0f, StartIndex);
	static constexpr int s_aDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
	static constexpr int s_aDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
	int Expanded = 0;
	const int Limit = 1200 + Skill(PET_SKILL_RACE) * 320;
	while(!Queue.empty() && Expanded++ < Limit)
	{
		const int Current = Queue.top().second;
		Queue.pop();
		if(aClosed[Current])
			continue;
		aClosed[Current] = 1;
		if(Current == GoalIndex)
			break;
		const int X = Current % Width;
		const int Y = Current / Width;
		for(int Direction = 0; Direction < 8; Direction++)
		{
			const int NX = X + s_aDx[Direction];
			const int NY = Y + s_aDy[Direction];
			if(NX < 0 || NX >= Width || NY < 0 || NY >= Height)
				continue;
			const int Next = NY * Width + NX;
			if(aClosed[Next] || !Passable(Next))
				continue;
			if(s_aDx[Direction] && s_aDy[Direction] &&
				(!Passable(Y * Width + NX) || !Passable(NY * Width + X)))
				continue;
			const float Hazard = Defense < 7 && IsDangerous(Position(Next)) ? 100.0f : 0.0f;
			const float Upward = s_aDy[Direction] < 0 ? 3.0f : 0.0f;
			const float NextCost = aCost[Current] + (s_aDx[Direction] && s_aDy[Direction] ? 14.0f : 10.0f) + Hazard + Upward;
			if(NextCost >= aCost[Next])
				continue;
			aCost[Next] = NextCost;
			aParent[Next] = Current;
			const float Estimate = 10.0f * distance(Position(Next), Position(GoalIndex)) / 32.0f;
			Queue.emplace(NextCost + Estimate, Next);
		}
	}
	if(!aClosed[GoalIndex])
		return false;
	std::vector<int> vPath;
	for(int Index = GoalIndex; Index >= 0; Index = aParent[Index])
	{
		vPath.push_back(Index);
		if(Index == StartIndex)
			break;
	}
	if(vPath.empty() || vPath.back() != StartIndex)
		return false;
	std::reverse(vPath.begin(), vPath.end());
	int Best = 0;
	const int LookAhead = std::min(static_cast<int>(vPath.size()) - 1, 5 + Skill(PET_SKILL_RACE));
	for(int i = 1; i <= LookAhead; i++)
	{
		const vec2 Candidate = i == static_cast<int>(vPath.size()) - 1 ? Goal : Position(vPath[i]);
		if(SafeTravelSegment(Start, Candidate))
			Best = i;
	}
	*pWaypoint = Best == static_cast<int>(vPath.size()) - 1 ? Goal : Position(vPath[std::max(1, Best)]);
	return true;
}

bool CBot::FindRescueRoute(int TargetId, vec2 TargetPos, vec2 *pWaypoint)
{
	if(!pWaypoint || TargetId < 0 || TargetId >= MAX_CLIENTS || !GameServer()->m_apPlayers[TargetId])
		return false;
	const vec2 MyPos = m_pPlayer->GetCharacter()->GetPos();
	if(m_HasRescueRoute && IsInFreezeFootprint(TargetPos) &&
		distance_squared(TargetPos, m_LastRescueTargetPos) < 30.0f * 30.0f &&
		!IsDangerous(m_RescueVantage) &&
		!Collision()->FastIntersectLine(m_RescueVantage, TargetPos, nullptr, nullptr) &&
		FindLocalRoute(MyPos, m_RescueVantage, pWaypoint))
		return true;
	if(IsInFreezeFootprint(TargetPos) &&
		Skill(PET_SKILL_HELPER) >= 5 && g_Config.m_SvBotAllowHook)
	{
		struct CHookStance { float m_Score; vec2 m_Pos; };
		std::vector<CHookStance> vStances;
		const float MaxPull = static_cast<float>(Tuning()->m_HookLength) * 0.78f;
		for(float Radius : {120.0f, 170.0f, 225.0f, 280.0f})
		{
			if(Radius >= MaxPull)
				continue;
			for(int i = 0; i < 16; i++)
			{
				const vec2 Pull = direction(2.0f * pi * i / 16.0f);
				const vec2 Pos = TargetPos + Pull * Radius;
				const vec2 Destination = TargetPos + Pull * std::min(100.0f, Radius * 0.65f);
				if(Collision()->TestBox(Pos, CCharacterCore::PhysicalSizeVec2()) ||
					IsDangerous(Pos) || IsDangerous(Pos + vec2(0, 14)) ||
					IsDangerous(Destination) || IsDangerous(Destination + vec2(0, 24)) ||
					Collision()->FastIntersectLine(Pos, TargetPos, nullptr, nullptr))
					continue;
				const bool Grounded = Collision()->IsOnGround(Pos, CCharacterCore::PhysicalSize());
				vStances.push_back({distance(MyPos, Pos) + (Grounded ? 0.0f : 65.0f) +
					std::abs(Radius - 170.0f) * 0.25f, Pos});
			}
		}
		std::sort(vStances.begin(), vStances.end(), [](const CHookStance &A, const CHookStance &B) {
			return A.m_Score < B.m_Score;
		});
		for(int i = 0; i < std::min(static_cast<int>(vStances.size()), 8 + Skill(PET_SKILL_RACE)); i++)
		{
			const int Index = (i + m_RescueCandidateOffset) % vStances.size();
			if(FindLocalRoute(MyPos, vStances[Index].m_Pos, pWaypoint))
			{
				m_RescueVantage = vStances[Index].m_Pos;
				return true;
			}
		}
	}
	const bool NeedHammer = Skill(PET_SKILL_HELPER) < 5 || !CanUseWeapon(WEAPON_LASER) ||
		IsDangerous(TargetPos + vec2(0, 24));
	struct CCandidate { float m_Score; vec2 m_Pos; };
	const auto TryCandidates = [&](bool HammerOnly) {
		std::vector<CCandidate> vCandidates;
		static constexpr float s_aRadii[] = {45.0f, 58.0f, 85.0f, 140.0f, 220.0f, 340.0f, 470.0f};
		for(float Radius : s_aRadii)
		{
			if(HammerOnly && Radius > 58.0f)
				break;
			if(!HammerOnly && Radius < 85.0f)
				continue;
			if(!HammerOnly && Radius > Tuning()->m_LaserReach - 40.0f)
				break;
			for(int i = 0; i < 16; i++)
			{
				const vec2 Pos = TargetPos + direction(2.0f * pi * i / 16.0f) * Radius;
				if(Collision()->CheckPoint(Pos) || Collision()->CheckPoint(Pos + vec2(14, 0)) ||
					Collision()->CheckPoint(Pos - vec2(14, 0)) ||
					IsDangerous(Pos) || IsDangerous(Pos + vec2(0, 14)) ||
					Collision()->FastIntersectLine(Pos, TargetPos, nullptr, nullptr))
					continue;
				const float GroundPenalty = Collision()->CheckPoint(Pos + vec2(0, 22)) ? 0.0f : 45.0f;
				vCandidates.push_back({distance(MyPos, Pos) + Radius * 0.3f + GroundPenalty, Pos});
			}
		}
		std::sort(vCandidates.begin(), vCandidates.end(), [](const CCandidate &A, const CCandidate &B) { return A.m_Score < B.m_Score; });
		const int Attempts = std::min(static_cast<int>(vCandidates.size()), 5 + Skill(PET_SKILL_RACE));
		for(int i = 0; i < Attempts; i++)
		{
			const int Index = (i + m_RescueCandidateOffset) % vCandidates.size();
			if(FindLocalRoute(MyPos, vCandidates[Index].m_Pos, pWaypoint))
			{
				m_RescueVantage = vCandidates[Index].m_Pos;
				return true;
			}
		}
		return false;
	};
	return TryCandidates(NeedHammer) || (NeedHammer && CanUseWeapon(WEAPON_LASER) && TryCandidates(false));
}

void CBot::ApplyMovementSkills()
{
	if(!m_pPlayer->GetCharacter() || stay || m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY)
		return;
	if(!m_Fighting && !m_Rescuing && owner >= 0 && owner < MAX_CLIENTS && GameServer()->m_apPlayers[owner] &&
		GameServer()->m_apPlayers[owner]->GetCharacter() &&
		distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[owner]->GetCharacter()->GetPos()) < 40.0f)
		return;
	const int Race = Skill(PET_SKILL_RACE);
	const int Defense = Skill(PET_SKILL_DEFENSE);
	const CCharacterCore *pCore = m_pPlayer->GetCharacter()->Core();
	const vec2 Goal = pCore->m_Pos + m_Target;
	vec2 CrossingGoal = Goal;
	if(m_NoSafeRoute && m_ComputeTarget.m_PlayerCID >= 0 &&
		m_ComputeTarget.m_PlayerCID < MAX_CLIENTS &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter())
		CrossingGoal = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->GetPos();
	const int Now = GameServer()->Server()->Tick();
	if(m_NoSafeRoute && Race >= 9 && Defense >= 9 && pCore->m_HookState == HOOK_IDLE &&
		!m_InputData.m_Hook)
	{
		if(m_LastFreezeCrossPlanTick < 0 || Now - m_LastFreezeCrossPlanTick >= 20 ||
			distance_squared(CrossingGoal, m_FreezeCrossGoal) > 80.0f * 80.0f)
		{
			m_LastFreezeCrossPlanTick = Now;
			m_FreezeCrossGoal = CrossingGoal;
			m_FreezeCrossUntilTick = FindSafeFreezeCrossing(CrossingGoal, &m_FreezeCrossDirection,
				&m_FreezeCrossJump, &m_FreezeCrossHook, &m_FreezeCrossAim) ? Now + 20 : -1;
		}
		if(Now <= m_FreezeCrossUntilTick)
		{
			m_InputData.m_Direction = m_FreezeCrossDirection;
			m_InputData.m_Jump = m_FreezeCrossJump;
			m_InputData.m_Hook = m_FreezeCrossHook;
			if(m_FreezeCrossHook)
				m_Target = m_FreezeCrossAim;
			return;
		}
	}
	const int Interval = bot_ai::MovementInterval(Race, Defense);
	if(GameServer()->Server()->Tick() % Interval != m_pPlayer->GetCid() % Interval)
		return;
	const bot_ai::SMovement Choice = bot_ai::ChooseMovement(
		{m_InputData.m_Direction, m_InputData.m_Jump}, Race, Defense,
		[this, pCore, Goal](int Direction, int Jump, int Horizon, bool CheckFootprint) {
			CCharacterCore Predicted = *pCore;
			CWorldCore World;
			Predicted.m_Tuning = pCore->m_Tuning;
			Predicted.Init(&World, Collision());
			Predicted.m_Input = m_InputData;
			Predicted.m_Input.m_Direction = Direction;
			Predicted.m_Input.m_Jump = Jump;
			Predicted.m_Input.m_Hook = m_InputData.m_Hook;
			float Risk = 0.0f;
			for(int Step = 0; Step < Horizon; Step++)
			{
				Predicted.Tick(true);
				Predicted.Move();
				Predicted.Quantize();
				const vec2 Position = Predicted.m_Pos;
				if(IsDangerous(Position) || (CheckFootprint &&
					(IsDangerous(Position + vec2(10, 0)) || IsDangerous(Position - vec2(10, 0)) ||
						IsDangerous(Position + vec2(0, 10)) || IsDangerous(Position - vec2(0, 10)))))
				{
					Risk += 10000.0f / (Step + 1);
					break;
				}
				if(CheckFootprint && (Step == 0 || (Step + 1) % 4 == 0) && Predicted.m_Vel.y > 0.5f &&
					HasFreezeBelow(Position, std::min(240.0f, 48.0f + Predicted.m_Vel.y * 18.0f)))
					Risk += 180.0f / (Step + 1);
			}
			return bot_ai::SPrediction{distance(Predicted.m_Pos, Goal), Risk};
		});
	m_InputData.m_Direction = Choice.m_Direction;
	m_InputData.m_Jump = Choice.m_Jump;
}

bool CBot::DefendAgainstUpwardThrow()
{
	if(Skill(PET_SKILL_DEFENSE) < 7 || !g_Config.m_SvBotAllowMove ||
		!m_pPlayer->GetCharacter())
		return false;
	const CCharacterCore *pCore = m_pPlayer->GetCharacter()->Core();
	if(pCore->m_Vel.y > -2.5f || IsDangerous(pCore->m_Pos))
		return false;
	bool FreezeAbove = false;
	for(float Height = 32.0f; Height <= 190.0f; Height += 16.0f)
	{
		if(IsDangerous(pCore->m_Pos + vec2(pCore->m_Vel.x * Height / 12.0f, -Height)) ||
			IsDangerous(pCore->m_Pos + vec2(0, -Height)))
		{
			FreezeAbove = true;
			break;
		}
		if(Collision()->CheckPoint(pCore->m_Pos + vec2(0, -Height)))
			break;
	}
	if(!FreezeAbove)
		return false;
	struct SDefenseChoice
	{
		float m_Score;
		int m_Direction;
		int m_Jump;
		bool m_Hook;
		vec2 m_Aim;
	};
	const auto Predict = [this, pCore](int Direction, int Jump, bool Hook, vec2 Aim) {
		CWorldCore World;
		CCharacterCore Predicted = *pCore;
		Predicted.Init(&World, Collision());
		Predicted.m_Tuning = pCore->m_Tuning;
		Predicted.m_Input.m_Direction = Direction;
		Predicted.m_Input.m_Jump = Jump;
		Predicted.m_Input.m_Hook = Hook ? 1 : 0;
		Predicted.m_Input.m_TargetX = Aim.x;
		Predicted.m_Input.m_TargetY = Aim.y;
		float Score = 0.0f;
		for(int Tick = 0; Tick < 22; Tick++)
		{
			Predicted.Tick(true);
			Predicted.Move();
			Predicted.Quantize();
			const vec2 Pos = Predicted.m_Pos;
			if(IsDangerous(Pos) || IsDangerous(Pos + vec2(0, 13)) ||
				IsDangerous(Pos - vec2(0, 13)) || IsDangerous(Pos + vec2(13, 0)) ||
				IsDangerous(Pos - vec2(13, 0)))
				return 100000.0f - Tick * 1000.0f;
			Score += std::max(0.0f, -Predicted.m_Vel.y) * 0.15f;
		}
		return Score + std::max(0.0f, -Predicted.m_Vel.y) * 8.0f;
	};
	SDefenseChoice Best{Predict(m_InputData.m_Direction, m_InputData.m_Jump, false, vec2(0, 1)),
		m_InputData.m_Direction, m_InputData.m_Jump, false, vec2(0, 1)};
	for(int Direction = -1; Direction <= 1; Direction++)
	{
		for(int Jump = 0; Jump <= 1; Jump++)
		{
			if(Jump && (pCore->m_Jumped & 1))
				continue;
			const float Score = Predict(Direction, Jump, false, vec2(0, 1));
			if(Score + 1.0f < Best.m_Score)
				Best = {Score, Direction, Jump, false, vec2(0, 1)};
		}
	}
	if(g_Config.m_SvBotAllowHook && pCore->m_HookState == HOOK_IDLE && !m_LastData.m_Hook)
	{
		for(int i = 0; i < 16; i++)
		{
			const vec2 Aim = direction(2.0f * pi * i / 16.0f) * static_cast<float>(Tuning()->m_HookLength);
			if(Aim.y < -0.4f * Tuning()->m_HookLength)
				continue; // hooking the ceiling accelerates an upward throw
			vec2 Hit;
			const int Tile = Collision()->FastIntersectLine(pCore->m_Pos, pCore->m_Pos + Aim, &Hit, nullptr);
			if(!Tile || Tile == TILE_NOHOOK || distance(pCore->m_Pos, Hit) < 50.0f)
				continue;
			for(int Direction = -1; Direction <= 1; Direction++)
			{
				const float Score = Predict(Direction, 0, true, Hit - pCore->m_Pos);
				if(Score + 4.0f < Best.m_Score)
					Best = {Score, Direction, 0, true, Hit - pCore->m_Pos};
			}
		}
	}
	if(Best.m_Score >= 100000.0f ||
		(Best.m_Direction == m_InputData.m_Direction && Best.m_Jump == m_InputData.m_Jump &&
			!Best.m_Hook && !m_InputData.m_Hook))
		return false;
	m_InputData.m_Direction = Best.m_Direction;
	m_InputData.m_Jump = Best.m_Jump;
	m_InputData.m_Hook = Best.m_Hook ? 1 : 0;
	if(Best.m_Hook)
	{
		m_InputData.m_Hook = 1;
		m_InputData.m_TargetX = Best.m_Aim.x;
		m_InputData.m_TargetY = Best.m_Aim.y;
		m_pPlayer->to_fire = false;
	}
	return true;
}

bool CBot::FindBounceAim(vec2 Target, vec2 *pAim)
{
	if(!pAim || Tuning()->m_LaserBounceNum < 1)
		return false;
	const vec2 Start = m_pPlayer->GetCharacter()->GetPos();
	const float Reach = Tuning()->m_LaserReach;
	float BestMiss = 27.0f * 27.0f;
	const int Rays = Skill(PET_SKILL_AIM) >= 10 ? 180 : Skill(PET_SKILL_AIM) >= 9 ? 120 : 72;
	for(int Ray = 0; Ray < Rays; Ray++)
	{
		const vec2 Direction = direction(2.0f * pi * Ray / Rays);
		vec2 Hit, Before;
		if(!Collision()->IntersectLine(Start, Start + Direction * Reach, &Hit, &Before))
			continue;
		vec2 BouncePos = Before;
		vec2 BounceVelocity = Direction * 4.0f;
		Collision()->MovePoint(&BouncePos, &BounceVelocity, 1.0f, nullptr);
		if(length(BounceVelocity) < 0.01f)
			continue;
		const float Remaining = Reach - distance(Start, BouncePos) - Tuning()->m_LaserBounceCost;
		if(Remaining <= 0)
			continue;
		const vec2 BounceEnd = BouncePos + normalize(BounceVelocity) * Remaining;
		vec2 Closest;
		closest_point_on_line(BouncePos, BounceEnd, Target, Closest);
		const float Miss = distance_squared(Closest, Target);
		if(Miss < BestMiss && !Collision()->FastIntersectLine(BouncePos, Closest, nullptr, nullptr))
		{
			BestMiss = Miss;
			*pAim = Direction * 100.0f;
		}
	}
	return BestMiss < 27.0f * 27.0f;
}

void CBot::Tick()

{

	if(!m_pPlayer->GetCharacter())

		return;
	if(GameServer()->IsBotFrozen(m_pPlayer->GetCharacter()))
	{
		if(m_FrozenSinceTick < 0)
			m_FrozenSinceTick = GameServer()->Server()->Tick();
		if(m_FreezeRespawnSeconds > 0 &&
			GameServer()->Server()->Tick() - m_FrozenSinceTick >= m_FreezeRespawnSeconds * GameServer()->Server()->TickSpeed())
		{
			m_RespawnFromFreeze = true;
			m_RaceToFrozenOwner = owner >= 0 && owner < MAX_CLIENTS && GameServer()->m_apPlayers[owner] &&
				GameServer()->IsBotFrozen(GameServer()->m_apPlayers[owner]->GetCharacter());
			m_pPlayer->KillCharacter(WEAPON_GAME, false);
			m_pPlayer->Respawn();
			m_FrozenSinceTick = -1;
			return;
		}
		mem_zero(&m_InputData, sizeof(m_InputData));
		m_LastData = m_InputData;
		m_pPlayer->to_fire = false;
		return;
	}
	else
		m_FrozenSinceTick = -1;



	if(m_pStrategyPosition == NULL)

	{

		m_pStrategyPosition = new CDefence(BotEngine());

		m_pStrategyPosition->SetTeam(0);

	}



	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->Core();
	const int Now = GameServer()->Server()->Tick();
	if(m_LastProgressTick < 0 || distance_squared(m_LastProgressPos, pMe->m_Pos) > 36.0f * 36.0f)
	{
		m_LastProgressPos = pMe->m_Pos;
		m_LastProgressTick = Now;
		stuck = false;
	}
	else if(Now - m_LastProgressTick > (110 - Skill(PET_SKILL_RACE) * 7) &&
		m_ComputeTarget.m_Type != CTarget::TARGET_EMPTY && length(m_Target) > 80.0f)
		stuck = true;

	UpdateTarget();

	m_pPlayer->GetCharacter()->SetWeaponAmmo(1, 9);

	//if(m_ComputeTarget.m_NeedUpdate)

		//dbg_msg("bot", "new target pos=(%f,%f) type=%d", m_ComputeTarget.m_Pos.x, m_ComputeTarget.m_Pos.y, m_ComputeTarget.m_Type);



	UpdateEdge();



	mem_zero(&m_InputData, sizeof(m_InputData));



	m_InputData.m_WantedWeapon = m_LastData.m_WantedWeapon;



	vec2 Pos = pMe->m_Pos;

	bool InSight = false;

	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER)

	{

		const CCharacterCore *pClosest = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->Core();

		InSight = !Collision()->FastIntersectLine(Pos, pClosest->m_Pos, 0, 0);

		m_Target = pClosest->m_Pos - Pos;

		m_RealTarget = pClosest->m_Pos;

	}
	else if(m_ComputeTarget.m_Type == CTarget::TARGET_AIR)
	{
		m_Target = m_ComputeTarget.m_Pos - Pos;
		m_RealTarget = m_ComputeTarget.m_Pos;
	}
	bool RescueHold = false;
	if(m_Rescuing && m_ComputeTarget.m_PlayerCID >= 0 && m_ComputeTarget.m_PlayerCID < MAX_CLIENTS &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter())
	{
		const vec2 TargetPos = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->GetPos();
		const bool NeedHammer = Skill(PET_SKILL_HELPER) < 5 || !CanUseWeapon(WEAPON_LASER) ||
			IsDangerous(TargetPos + vec2(0, 24));
		const bool InFreezeTile = IsInFreezeFootprint(TargetPos);
		RescueHold = !InFreezeTile && IsGrounded() && !IsDangerous(Pos) &&
			(NeedHammer ? CanHammerHit(GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()) :
				distance(Pos, TargetPos) < Tuning()->m_LaserReach - 24.0f &&
				!Collision()->FastIntersectLine(Pos, TargetPos, nullptr, nullptr));
	}

	if(InSight)

		stuckTick = GameServer()->Server()->Tick();

	checkStuck(InSight);

	if(owner >= 0 && owner < MAX_CLIENTS && GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);

		const bool PetFrozen = GameServer()->IsBotFrozen(m_pPlayer->GetCharacter());
		const bool OwnerFrozen = GameServer()->IsBotFrozen(GameServer()->m_apPlayers[owner]->GetCharacter());
		if(!PetFrozen && !OwnerFrozen && (m_RaceToFrozenOwner || (dist >= 2000 && !stay && !InSight &&
			!(std::find(GameServer()->playersJoined.begin(), GameServer()->playersJoined.end(), owner) != GameServer()->playersJoined.end()))))

		{

			stuck = false;
			m_RaceToFrozenOwner = false;

			GameServer()->Teleport(m_pPlayer->GetCharacter(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);

		}

		if (dist <= 70)

		{

			emote();

		}

	}

	MakeChoice(InSight);

	m_RealTarget = m_Target + Pos;
	const float GoalDistance = distance(Pos, m_RealTarget);
	if(m_LastGoalProgressTick < 0 ||
		distance_squared(m_RealTarget, m_LastGoalProgressTarget) > 80.0f * 80.0f ||
		GoalDistance < m_LastGoalProgressDistance - 22.0f)
	{
		m_LastGoalProgressTarget = m_RealTarget;
		m_LastGoalProgressDistance = GoalDistance;
		m_LastGoalProgressTick = Now;
	}
	else if(GoalDistance > 80.0f && Now - m_LastGoalProgressTick >
		2 * GameServer()->Server()->TickSpeed())
		stuck = true;
	if(stuck && GoalDistance > 64.0f)
		m_NoSafeRoute = true;

	//HandleWeapon(InSight);



	if(!RescueHold)
		HandleHook(false);

	if (owner >= 0 && owner < MAX_CLIENTS && GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);
		if((m_Rescuing || m_Fighting) && m_ComputeTarget.m_PlayerCID >= 0 && m_ComputeTarget.m_PlayerCID < MAX_CLIENTS &&
			GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] && GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter())
			dist = distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->m_Pos);

		if(dist >= (Skill(PET_SKILL_RACE) >= 7 ? 25 : 50) && !stay && !(std::find(GameServer()->playersJoined.begin(), GameServer()->playersJoined.end(), owner) != GameServer()->playersJoined.end()))

		{

			if(m_Flags & BFLAG_LEFT)

				m_InputData.m_Direction = -1;

			if(m_Flags & BFLAG_RIGHT)

				m_InputData.m_Direction = 1;

			if(m_Flags & BFLAG_JUMP)

				m_InputData.m_Jump = 1;

		}

	}
	else if(owner < 0 && m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER)
	{
		const float Dist = length(m_Target);
		if(Dist > 48.0f)
		{
			if(m_Flags & BFLAG_LEFT)
				m_InputData.m_Direction = -1;
			if(m_Flags & BFLAG_RIGHT)
				m_InputData.m_Direction = 1;
			if(m_Flags & BFLAG_JUMP)
				m_InputData.m_Jump = 1;
		}
	}

	if(m_InputData.m_Hook && stay)

		m_InputData.m_Hook = 0;

	//if(InSight && diffPos.y < - Close && diffVel.y < 0)

		//m_InputData.m_Jump = 1;



	m_InputData.m_TargetX = m_LastData.m_TargetX;

	m_InputData.m_TargetY = m_LastData.m_TargetY;

	if(m_InputData.m_Hook || m_InputData.m_Fire || m_pPlayer->to_fire) {

		m_InputData.m_TargetX = m_Target.x;

		m_InputData.m_TargetY = m_Target.y;

	}

	//m_pPlayer->to_fire = true; //to fire.



	if(!g_Config.m_SvBotAllowMove) {

		m_InputData.m_Direction = 0;

		m_InputData.m_Jump = 0;

		m_InputData.m_Hook = 0;

	}

	if(!g_Config.m_SvBotAllowHook)

		m_InputData.m_Hook = 0;

	ApplyMovementSkills();
	if((pMe->m_Jumped & 1) && m_InputData.m_Jump)
		m_InputData.m_Jump = 0; // release before a later air or ground jump
	if(RescueHold)
	{
		m_InputData.m_Direction = 0;
		m_InputData.m_Jump = 0;
		m_InputData.m_Hook = 0;
	}
	if(!g_Config.m_SvBotAllowMove)
	{
		m_InputData.m_Direction = 0;
		m_InputData.m_Jump = 0;
		m_InputData.m_Hook = 0;
	}

	const bool NavigationHook = m_InputData.m_Hook != 0;
	const vec2 NavigationHookAim = m_Target;
	bool CombatHookAim = false;
	if((m_Rescuing || m_Fighting) &&
		m_ComputeTarget.m_PlayerCID >= 0 && m_ComputeTarget.m_PlayerCID < MAX_CLIENTS &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] &&
		GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter())
	{
		CCharacter *pTarget = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter();
		const int TargetId = m_ComputeTarget.m_PlayerCID;
		const vec2 MyPos = m_pPlayer->GetCharacter()->GetPos();
		const float Dist = distance(MyPos, pTarget->GetPos());
		const bool Direct = !Collision()->FastIntersectLine(MyPos, pTarget->GetPos(), nullptr, nullptr);
		const bool TargetFrozen = GameServer()->IsBotFrozen(pTarget);
		const int Aim = Skill(PET_SKILL_AIM);
		bot_ai::EWeapon Choice = bot_ai::ChooseWeapon(m_Rescuing ? bot_ai::ERole::RESCUE : bot_ai::ERole::FIGHT,
			Dist, Skill(PET_SKILL_BLOCKER), Skill(PET_SKILL_HELPER), Aim);
		if(m_Rescuing)
		{
			const bool NeedsPush = IsDangerous(pTarget->GetPos() + vec2(0, 24));
			Choice = (Dist <= 55.0f && CanHammerHit(pTarget)) || !CanUseWeapon(WEAPON_LASER) ||
				Skill(PET_SKILL_HELPER) < 5 || (NeedsPush && CanHammerHit(pTarget)) ?
				bot_ai::EWeapon::HAMMER : bot_ai::EWeapon::LASER;
		}
		bool ShotTowardFreeze = true;
		if(m_Fighting && m_HasBlockFreezeGoal && Dist > 1.0f &&
			distance_squared(pTarget->GetPos(), m_BlockFreezeGoal) > 24.0f * 24.0f)
		{
			const vec2 ToFreeze = normalize(m_BlockFreezeGoal - pTarget->GetPos());
			const vec2 ToEnemy = normalize(pTarget->GetPos() - MyPos);
			const bool HammerHelps = CanHammerHit(pTarget) &&
				dot(normalize(ToEnemy + vec2(0, -1.1f)), ToFreeze) > 0.25f;
			const bool ShotgunHelps = CanUseWeapon(WEAPON_SHOTGUN) &&
				dot(-ToEnemy, ToFreeze) > 0.3f;
			ShotTowardFreeze = HammerHelps || ShotgunHelps;
			if(HammerHelps)
				Choice = bot_ai::EWeapon::HAMMER;
			else if(ShotgunHelps)
				Choice = bot_ai::EWeapon::SHOTGUN;
		}
		int Weapon = Choice == bot_ai::EWeapon::LASER ? WEAPON_LASER :
			Choice == bot_ai::EWeapon::SHOTGUN ? WEAPON_SHOTGUN :
			Choice == bot_ai::EWeapon::GUN ? WEAPON_GUN : WEAPON_HAMMER;
		while(Weapon > WEAPON_HAMMER && !CanUseWeapon(Weapon))
			--Weapon;
		vec2 AimVector = pTarget->GetPos() - MyPos;
		AimVector += pTarget->Core()->m_Vel * bot_ai::AimLeadTicks(Choice, Dist, Aim);
		bool CanHit = bot_ai::FireAtTarget(m_Fighting, TargetFrozen, ShotTowardFreeze) &&
			!(m_Rescuing && IsInFreezeFootprint(pTarget->GetPos())) &&
			(Weapon == WEAPON_HAMMER ? CanHammerHit(pTarget) :
			Direct && (Weapon == WEAPON_LASER ? Dist < Tuning()->m_LaserReach : Dist < 500.0f));
		if(!CanHit && Weapon == WEAPON_LASER && Aim >= 8 && Dist < Tuning()->m_LaserReach)
			CanHit = FindBounceAim(pTarget->GetPos(), &AimVector);
		if(CanHit && g_Config.m_SvBotAllowFire)
		{
			m_pPlayer->GetCharacter()->GiveWeapon(Weapon);
			m_pPlayer->GetCharacter()->SetActiveWeapon(Weapon);
			m_InputData.m_WantedWeapon = Weapon + 1;
			m_pPlayer->to_fire = true;
			const float Error = bot_ai::AimErrorRadians(Aim, ((std::rand() % 201) - 100) / 100.0f);
			AimVector = direction(angle(AimVector) + Error) * std::max(1.0f, length(AimVector));
			m_Target = AimVector;
		}
		else
			m_pPlayer->to_fire = false;
		if(m_Rescuing && TargetFrozen && g_Config.m_SvBotAllowHook &&
			Skill(PET_SKILL_HELPER) >= 5 &&
			IsInFreezeFootprint(pTarget->GetPos()))
		{
			const bool HookedTarget = pMe->m_HookState == HOOK_GRABBED && pMe->HookedPlayer() == TargetId;
			const vec2 TowardMe = Dist > 1.0f ? (MyPos - pTarget->GetPos()) / Dist : vec2(0, 0);
			const vec2 PullDestination = pTarget->GetPos() + TowardMe * std::min(100.0f, Dist * 0.65f);
			const bool UsefulPull = !IsDangerous(PullDestination) &&
				!IsDangerous(PullDestination + vec2(0, 24));
			const bool AtStance = !m_HasRescueRoute || distance(MyPos, m_RescueVantage) < 48.0f;
			if(HookedTarget)
			{
				m_InputData.m_Hook = UsefulPull && Dist > CCharacterCore::PhysicalSize() * 1.5f;
				CombatHookAim = m_InputData.m_Hook != 0;
			}
			else if(UsefulPull && AtStance && Direct && !IsDangerous(MyPos) &&
				Dist >= 90.0f && Dist < static_cast<float>(Tuning()->m_HookLength) * 0.82f)
			{
				// A wall hook cannot rescue the target. Release it before sending
				// a fresh player hook, then keep that hook held while it pulls.
				m_InputData.m_Hook = pMe->m_HookState == HOOK_IDLE && !m_LastData.m_Hook ? 1 : 0;
				CombatHookAim = m_InputData.m_Hook != 0;
			}
			if(CombatHookAim)
			{
				m_Target = pTarget->GetPos() - MyPos;
				m_pPlayer->to_fire = false;
			}
		}
		if(m_Fighting && g_Config.m_SvBotAllowHook)
		{
			const bool HookedTarget = pMe->m_HookState == HOOK_GRABBED && pMe->HookedPlayer() == TargetId;
			if(HookedTarget)
				m_InputData.m_Hook = m_HasBlockFreezeGoal &&
					ShouldHoldEnemyHook(pTarget, m_BlockFreezeGoal);
			else if(m_HasBlockFreezeGoal && pMe->m_HookState == HOOK_IDLE && !m_LastData.m_Hook && Direct &&
				Dist < static_cast<float>(Tuning()->m_HookLength) * 0.85f &&
				Dist >= std::min(static_cast<float>(Tuning()->m_HookLength) * 0.68f,
					std::max(135.0f, distance(pTarget->GetPos(), m_BlockFreezeGoal) * 0.8f)) &&
				bot_ai::GoodPullAngle(MyPos.x, MyPos.y, pTarget->GetPos().x, pTarget->GetPos().y,
					m_BlockFreezeGoal.x, m_BlockFreezeGoal.y, 0.72f) &&
				ShouldHoldEnemyHook(pTarget, m_BlockFreezeGoal))
			{
				const float LeadTicks = std::min(6.0f, Dist /
					std::max(1.0f, static_cast<float>(Tuning()->m_HookFireSpeed)));
				m_Target = pTarget->GetPos() - MyPos + pTarget->Core()->m_Vel * LeadTicks;
				m_InputData.m_Hook = 1;
				CombatHookAim = m_InputData.m_Hook != 0;
			}
		}
	}
	if(NavigationHook && m_InputData.m_Hook && !CombatHookAim)
	{
		m_Target = NavigationHookAim;
		m_pPlayer->to_fire = false;
	}
	if(m_InputData.m_Hook || m_InputData.m_Fire || m_pPlayer->to_fire)
	{
		m_InputData.m_TargetX = m_Target.x;
		m_InputData.m_TargetY = m_Target.y;
	}
	DefendAgainstUpwardThrow();



	m_LastData = m_InputData;

	return;

}



void CBot::HandleHook(bool SeeTarget)

{

	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->Core();



	if(!pMe)

		return;

	int CurTile = GetTile(pMe->m_Pos.x, pMe->m_Pos.y);

	if(pMe->m_HookState == HOOK_FLYING)

	{

		m_InputData.m_Hook = 1;

		return;

	}

	if(SeeTarget)

	{

		const CCharacterCore *pClosest = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()->Core();

		float dist = distance(pClosest->m_Pos,pMe->m_Pos);

		if(pMe->m_HookState == HOOK_GRABBED && pMe->HookedPlayer() == m_ComputeTarget.m_PlayerCID)

			m_InputData.m_Hook = 1;

		else if(!m_InputData.m_Fire)

		{

			if(dist < Tuning()->m_HookLength*0.9f)

				m_InputData.m_Hook = m_LastData.m_Hook^1;

			SeeTarget = dist < Tuning()->m_HookLength*0.9f;

		}

	}

	if(!SeeTarget)

	{

		if(pMe->m_HookState == HOOK_GRABBED && pMe->HookedPlayer() == -1)

		{

			vec2 HookVel = normalize(pMe->m_HookPos-pMe->m_Pos)*GameServer()->GlobalTuning()->m_HookDragAccel;



			// from gamecore;cpp

			if(HookVel.y > 0)

				HookVel.y *= 0.3f;

			if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) || (HookVel.x > 0 && pMe->m_Input.m_Direction > 0))

				HookVel.x *= 0.95f;

			else

				HookVel.x *= 0.75f;



			HookVel += vec2(0,1)*GameServer()->GlobalTuning()->m_Gravity;



			vec2 Target = m_Target;

			float ps = dot(Target, HookVel);
			const vec2 WithHook = pMe->m_Pos + (pMe->m_Vel + HookVel) * 10.0f;
			const vec2 WithoutHook = pMe->m_Pos + pMe->m_Vel * 10.0f;
			const bool HookUnsafe = Skill(PET_SKILL_DEFENSE) >= 6 && IsDangerous(WithHook) && !IsDangerous(WithoutHook);

			if(!HookUnsafe && (ps > 0 ||
				(IsDangerous(WithoutHook) && !IsDangerous(WithHook)) ||
				(CurTile & BTILE_HOLE && m_Target.y < 0 && pMe->m_Vel.y > 0.f && pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED/2)))

				m_InputData.m_Hook = 1;

			if(pMe->m_HookTick > 4*SERVER_TICK_SPEED || length(pMe->m_HookPos-pMe->m_Pos) < 20.0f)

				m_InputData.m_Hook = 0;

			// if(Flags & BFLAG_HOOK && ps < dot(Target,HookVel-Accel))

			// 	Flags ^= BFLAG_RIGHT | BFLAG_LEFT;

		}

		if(pMe->m_HookState == HOOK_FLYING)

			m_InputData.m_Hook = 1;

		// Aim for a hook anchor that advances the route and keeps the bot clear
		// of freeze. High skill levels retry wall hooks promptly and predict risk.
		const vec2 Travel = m_Target;
		const bool Obstructed = length(Travel) > 30.0f &&
			Collision()->FastIntersectLine(pMe->m_Pos,
				pMe->m_Pos + normalize(Travel) * std::min(length(Travel), 120.0f), nullptr, nullptr);
		const bool FallingTowardHazard = pMe->m_Vel.y > 0.5f &&
			HasFreezeBelow(pMe->m_Pos, std::min(static_cast<float>(Tuning()->m_HookLength), 320.0f));
		if(!m_InputData.m_Fire && m_LastData.m_Hook == 0 && pMe->m_HookState == HOOK_IDLE &&
			bot_ai::ShouldWallHook(Skill(PET_SKILL_RACE), Skill(PET_SKILL_DEFENSE), Obstructed,
				Travel.y < -18.0f, FallingTowardHazard, stuck, GameServer()->Server()->Tick(), m_pPlayer->GetCid()) &&
			(m_LastWallHookPlanTick < 0 || GameServer()->Server()->Tick() - m_LastWallHookPlanTick >= 5))

		{
			m_LastWallHookPlanTick = GameServer()->Server()->Tick();
			struct SHookPrediction { float m_BestDistance; float m_Rise; bool m_Hazard; };
			const vec2 Goal = pMe->m_Pos + Travel;
			const auto SimulateHook = [this, pMe, Goal](vec2 Aim, bool Hook) {
				CWorldCore World;
				CCharacterCore Predicted = *pMe;
				Predicted.Init(&World, Collision());
				Predicted.m_Tuning = pMe->m_Tuning;
				Predicted.m_Input.m_Direction = Hook && Goal.y < pMe->m_Pos.y - 90.0f &&
					std::abs(Goal.x - pMe->m_Pos.x) < 100.0f && std::abs(Aim.x) > 40.0f ?
					(Aim.x > 0 ? 1 : -1) :
					Goal.x > pMe->m_Pos.x + 28.0f ? 1 :
					Goal.x < pMe->m_Pos.x - 28.0f ? -1 : 0;
				Predicted.m_Input.m_Jump = (m_Flags & BFLAG_JUMP) && !(pMe->m_Jumped & 1) ? 1 : 0;
				Predicted.m_Input.m_Hook = Hook ? 1 : 0;
				Predicted.m_Input.m_TargetX = Aim.x;
				Predicted.m_Input.m_TargetY = Aim.y;
				SHookPrediction Result{distance(pMe->m_Pos, Goal), 0.0f, false};
				for(int Step = 0; Step < 40; Step++)
				{
					Predicted.Tick(true);
					Predicted.Move();
					Predicted.Quantize();
					if(IsDangerous(Predicted.m_Pos) || IsDangerous(Predicted.m_Pos + vec2(0, 14)) ||
						IsDangerous(Predicted.m_Pos - vec2(0, 14)))
					{
						Result.m_Hazard = true;
						break;
					}
					Result.m_BestDistance = std::min(Result.m_BestDistance, distance(Predicted.m_Pos, Goal));
					Result.m_Rise = std::max(Result.m_Rise, pMe->m_Pos.y - Predicted.m_Pos.y);
					if(Hook && Step > 4 && Predicted.m_HookState == HOOK_RETRACTED)
						break;
				}
				return Result;
			};
			const bool Simulate = Skill(PET_SKILL_RACE) >= 8 && Skill(PET_SKILL_DEFENSE) >= 6;
			const SHookPrediction Baseline = Simulate ? SimulateHook(Travel, false) : SHookPrediction{0.0f, 0.0f, false};

			const int NumDir = Simulate ? 24 : Skill(PET_SKILL_RACE) >= 7 ? 48 : BOT_HOOK_DIRS;

			vec2 HookDir(0.0f,0.0f);

			float BestScore = -1e30f;

			vec2 Target = m_Target;

			for(int i = 0 ; i < NumDir; i++)

			{

				float a = 2*i*pi / NumDir;

				vec2 dir = direction(a);

				vec2 Pos = pMe->m_Pos+dir*Tuning()->m_HookLength;



				const int HitTile = Collision()->FastIntersectLine(pMe->m_Pos, Pos, &Pos, 0);
				if(HitTile && HitTile != TILE_NOHOOK)

				{

					vec2 HookVel = dir*GameServer()->GlobalTuning()->m_HookDragAccel;



					// from gamecore.cpp

					if(HookVel.y > 0)

						HookVel.y *= 0.3f;

					if((HookVel.x < 0 && pMe->m_Input.m_Direction < 0) || (HookVel.x > 0 && pMe->m_Input.m_Direction > 0))

						HookVel.x *= 0.95f;

					else

						HookVel.x *= 0.75f;



					HookVel += vec2(0,1)*GameServer()->GlobalTuning()->m_Gravity;



					const float Progress = length(Target) > 1.0f ? dot(normalize(Target), HookVel) : 0.0f;
					float HazardRisk = 0.0f;
					float SafetyGain = 0.0f;
					for(int Step = 1; Step <= 3; Step++)
					{
						const vec2 Projected = pMe->m_Pos + (pMe->m_Vel + HookVel) * (Step * 5.0f);
						const vec2 WithoutHook = pMe->m_Pos + pMe->m_Vel * (Step * 5.0f);
						const bool WithRisk = IsDangerous(Projected) || IsDangerous(Projected + vec2(0, 10));
						const bool WithoutRisk = IsDangerous(WithoutHook) || IsDangerous(WithoutHook + vec2(0, 10));
						if(WithRisk && !WithoutRisk)
							HazardRisk += 100.0f / Step;
						else if(!WithRisk && WithoutRisk)
							SafetyGain += 20.0f / Step;
					}
					float Score = bot_ai::WallHookScore(Progress, std::max(0.0f, -HookVel.y),
						HazardRisk, FallingTowardHazard, Skill(PET_SKILL_RACE), Skill(PET_SKILL_DEFENSE)) + SafetyGain;
					if(Simulate)
					{
						const SHookPrediction Prediction = SimulateHook(Pos - pMe->m_Pos, true);
						Score = (Baseline.m_BestDistance - Prediction.m_BestDistance) * 1.5f +
							(Travel.y < -90.0f ?
								(Prediction.m_Rise - Baseline.m_Rise) * 1.6f : 0.0f) +
							(Baseline.m_Hazard && !Prediction.m_Hazard ? 180.0f : 0.0f) -
							(Prediction.m_Hazard ? 10000.0f : 0.0f);
					}

					if(Score > BestScore)

					{

						BestScore = Score;

						HookDir = Pos - pMe->m_Pos;

					}

				}

			}

			if(length(HookDir) > 32.f && (BestScore > 0.05f || FallingTowardHazard))

			{

				m_Target = HookDir;

				m_InputData.m_Hook = 1;

				// if(Collision()->CheckPoint(pMe->m_Pos+normalize(vec2(0,m_Target.y))*28) && absolute(Target.x) < 30)

				// 	Flags = (Flags & (~BFLAG_LEFT)) | BFLAG_RIGHT;

			}

		}

	}

}



void CBot::HandleWeapon(bool SeeTarget)

{

	CCharacter *pMe = m_pPlayer->GetCharacter();



	if(!pMe)

		return;



	vec2 Pos = pMe->GetCore().m_Pos;



	const CCharacterCore *apTarget[MAX_CLIENTS];

	int Count = 0;



	for(int c = 0 ; c < MAX_CLIENTS ; c++)

	{

		if(c == m_pPlayer->GetCid())

			continue;

		if(SeeTarget && c == m_ComputeTarget.m_PlayerCID)

		{

			apTarget[Count++] = apTarget[0];

			apTarget[0] = GameServer()->m_apPlayers[c]->GetCharacter()->Core();

		}

		else if(GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter())

			apTarget[Count++] = GameServer()->m_apPlayers[c]->GetCharacter()->Core();

	}

	int Weapon = -1;

	vec2 Target;

	for(int c = 0; c < Count; c++)

	{

		float ClosestRange = distance(Pos, apTarget[c]->m_Pos);

		float Close = 65.0f;

		Target = apTarget[c]->m_Pos - Pos;

		if(ClosestRange < Close)

		{

			Weapon = WEAPON_HAMMER;

			break;

		}

		else if(ClosestRange < GameServer()->GlobalTuning()->m_LaserReach && !Collision()->FastIntersectLine(Pos, apTarget[c]->m_Pos, 0, 0))

		{

			Weapon = WEAPON_LASER;

			break;

		}

	}

	if(Weapon < 0)

	{

		int GoodDir = -1;



		vec2 aProjectilePos[BOT_HOOK_DIRS];



		const int NbLoops = 10;



		vec2 aTargetPos[MAX_CLIENTS];

		vec2 aTargetVel[MAX_CLIENTS];



		const int Weapons[] = {WEAPON_GRENADE, WEAPON_SHOTGUN, WEAPON_GUN};

		for(int j = 0 ; j < 3 ; j++)

		{

			float Curvature = 0, Speed = 0, Time = 0;

			switch(Weapons[j])

			{

				case WEAPON_GRENADE:

					Curvature = GameServer()->GlobalTuning()->m_GrenadeCurvature;

					Speed = GameServer()->GlobalTuning()->m_GrenadeSpeed;

					Time = GameServer()->GlobalTuning()->m_GrenadeLifetime;

					break;



				case WEAPON_SHOTGUN:

					Curvature = GameServer()->GlobalTuning()->m_ShotgunCurvature;

					Speed = GameServer()->GlobalTuning()->m_ShotgunSpeed;

					Time = GameServer()->GlobalTuning()->m_ShotgunLifetime;

					break;



				case WEAPON_GUN:

					Curvature = GameServer()->GlobalTuning()->m_GunCurvature;

					Speed = GameServer()->GlobalTuning()->m_GunSpeed;

					Time = GameServer()->GlobalTuning()->m_GunLifetime;

					break;

			}

			//DTime /= NbLoops;

			int DTick = (int) (Time * GameServer()->Server()->TickSpeed() / NbLoops);

			// DTime *= Speed;

			// Curvature *= 0.00001f;



			for(int c = 0; c < Count; c++)

			{

				aTargetPos[c] = apTarget[c]->m_Pos;

				aTargetVel[c] = apTarget[c]->m_Vel*DTick;

			}



			for(int i = 0 ; i < BOT_HOOK_DIRS ; i++) {

				vec2 dir = direction(2*i*pi / BOT_HOOK_DIRS);

				aProjectilePos[i] = Pos + dir*28.*0.75;

			}



			int aIsDead[BOT_HOOK_DIRS] = {0};



			for(int k = 0; k < NbLoops && GoodDir == -1; k++) {

				for(int i = 0; i < BOT_HOOK_DIRS; i++) {

					if(aIsDead[i])

						continue;

					vec2 dir = direction(2*i*pi / BOT_HOOK_DIRS);

					vec2 NextPos = CalcPos(Pos + dir*28.*0.75, dir, Curvature, Speed, (k+1) * Time / NbLoops);

					// vec2 NextPos = aProjectilePos[i];

					// NextPos.x += dir.x*DTime;

					// NextPos.y += dir.y*DTime + Curvature*(DTime*DTime)*(2*k+1);

					aIsDead[i] = Collision()->FastIntersectLine(aProjectilePos[i], NextPos, &NextPos, 0);

					for(int c = 0; c < Count; c++)

					{

						vec2 InterPos;

						closest_point_on_line(aProjectilePos[i], NextPos, aTargetPos[c], InterPos);

						if(distance(aTargetPos[c], InterPos)< 28) {

							GoodDir = i;

							break;

						}

					}

					aProjectilePos[i] = NextPos;

				}

				for(int c = 0; c < Count; c++)

				{

					//Collision()->MoveBox(&aTargetPos[c], &aTargetVel[c], vec2(28.f,28.f), 0);

					Collision()->FastIntersectLine(aTargetPos[c], aTargetPos[c]+aTargetVel[c], 0, &aTargetPos[c]);

					aTargetVel[c].y += GameServer()->GlobalTuning()->m_Gravity*DTick*DTick;

				}

			}

			if(GoodDir != -1)

			{

				Target = direction(2*GoodDir*pi / BOT_HOOK_DIRS)*50;

				Weapon = Weapons[j];

				break;

			}

		}

	}

	if(Weapon > -1)

	{

		m_InputData.m_WantedWeapon = Weapon+1;

		m_InputData.m_Fire = m_LastData.m_Fire^1;

		if(m_InputData.m_Fire)

			m_Target = Target;

	}





	// Accuracy

	float Angle = angle(m_Target) + (std::rand()%64-32)*pi / 1024.0f;

	m_Target = direction(Angle)*length(m_Target);

}



void CBot::UpdateEdge()

{
	if(m_UsingLocalRoute)
	{
		m_pPath->m_Size = 0;
		return;
	}

	vec2 Pos = m_pPlayer->GetCharacter()->GetPos();

	if(m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY)

	{

		return;

	}

	if(m_ComputeTarget.m_NeedUpdate)

	{

		m_pPath->m_Size = 0;

		BotEngine()->GetPath(Pos, m_ComputeTarget.m_Pos, m_pPath);
		m_LastRouteRefreshTick = GameServer()->Server()->Tick();
		m_LastPathTargetPos = m_ComputeTarget.m_Pos;
		m_HasPathTarget = true;

		m_ComputeTarget.m_NeedUpdate = false;

		// dbg_msg("bot", "%d new path of size=%d for type=%d cid=%d", m_pPlayer->GetCid(), m_pPath->m_Size, m_ComputeTarget.m_Type, m_ComputeTarget.m_PlayerCID);

	}

}



void CBot::MakeChoice(bool UseTarget)

{

	if(!UseTarget)

	{

		vec2 Pos = m_pPlayer->GetCharacter()->GetPos();



		if(m_pPath->m_Size)

		{

			int dist = BotEngine()->FarestPointOnEdge(m_pPath, Pos, &m_Target);

			if(dist >= 0)

			{

				UseTarget = true;

				m_Target -= Pos;

			}

			else

				m_Target = BotEngine()->NextPoint(Pos,m_ComputeTarget.m_Pos) - Pos;

		}

	}



	int Flags = 0;

	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->Core();

	CCharacterCore TempChar = *pMe;

	TempChar.m_Input = m_InputData;

	vec2 CurPos = TempChar.m_Pos;



	int CurTile = GetTile(TempChar.m_Pos.x, TempChar.m_Pos.y);

	bool Grounded = IsGrounded();



	TempChar.m_Input.m_Direction = (m_Target.x > 28.f) ? 1 : (m_Target.x < -28.f) ? -1:0;

	CWorldCore TempWorld;

	TempChar.m_Tuning = *GameServer()->GlobalTuning();

	TempChar.Init(&TempWorld, Collision());

	TempChar.Tick(true);

	TempChar.Move();

	TempChar.Quantize();



	int NextTile = GetTile(TempChar.m_Pos.x, TempChar.m_Pos.y);

	vec2 NextPos = TempChar.m_Pos;



	if(TempChar.m_Input.m_Direction > 0)

		Flags |= BFLAG_RIGHT;



	if(TempChar.m_Input.m_Direction < 0)

		Flags |= BFLAG_LEFT;



	if(m_Target.y < 0)

	{

		if(CurTile & BTILE_SAFE && NextTile & BTILE_HOLE && (Grounded || TempChar.m_Vel.y > 0))

			Flags |= BFLAG_JUMP;

		if(CurTile & BTILE_SAFE && NextTile & BTILE_SAFE)

		{

			if(absolute(CurPos.x - NextPos.x) < 1.0f && TempChar.m_Input.m_Direction)

			{

				if(Grounded)

				{

					Flags |= BFLAG_JUMP;

					m_TriedWallJump = true;

				}

				else if(m_TriedWallJump && !(TempChar.m_Jumped) && TempChar.m_Vel.y > 0)

					Flags |= BFLAG_JUMP;

				else if(m_TriedWallJump && TempChar.m_Jumped & 2 && TempChar.m_Vel.y > 0)

					Flags ^= BFLAG_RIGHT | BFLAG_LEFT;

			}

			else

				m_TriedWallJump = false;

			// if(m_Target.y < 0 && TempChar.m_Vel.y > 1.f && !(TempChar.m_Jumped) && !Grounded)

			// 	Flags |= BFLAG_JUMP;

		}



		if(!(pMe->m_Jumped))

		{

			vec2 Vel(pMe->m_Vel.x, std::min(pMe->m_Vel.y, 0.0f));

			if(Collision()->FastIntersectLine(pMe->m_Pos,pMe->m_Pos+Vel*10.0f,0,0))

				Flags |= BFLAG_JUMP;

			if(absolute(m_Target.x) < 28.f && pMe->m_Vel.y > -1.f)

				Flags |= BFLAG_JUMP;

		}

	}

	// if(Flags & BFLAG_JUMP || pMe->m_Vel.y < 0)

	// 	m_InputData.m_WantedWeapon = WEAPON_GRENADE +1;

	// if(m_Target.y < -400 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(0,50)))

	// {

	// 	Flags &= ~BFLAG_HOOK;

	// 	Flags |= BFLAG_FIRE;

	// 	m_Target = vec2(0,28);

	// }

	// else if(m_Target.y < -300 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(32,48)))

	// {

	// 	Flags &= ~BFLAG_HOOK;

	// 	Flags |= BFLAG_FIRE;

	// 	m_Target = vec2(14,28);

	// }

	// else if(m_Target.y < -300 && pMe->m_Vel.y < 0 && absolute(m_Target.x) < 30 && Collision()->CheckPoint(pMe->m_Pos+vec2(-32,48)))

	// {

	// 	Flags &= ~BFLAG_HOOK;

	// 	Flags |= BFLAG_FIRE;

	// 	m_Target = vec2(-14,28);

	// }

	m_Flags = Flags;

}



void CBot::Snap(int SnappingClient)

{

	if(SnappingClient == -1)

		return;



	CCharacter *pMe = m_pPlayer->GetCharacter();

	if(!pMe)

		return;



	vec2 Pos = pMe->GetCore().m_Pos;

	{

		CNetObj_Laser Obj{};

		CNetObj_Laser *pObj = &Obj;



		pObj->m_X = (int)(m_RealTarget.x);

		pObj->m_Y = (int)(m_RealTarget.y);

		pObj->m_FromX = (int)Pos.x;

		pObj->m_FromY = (int)Pos.y;

		pObj->m_StartTick = GameServer()->Server()->Tick();

		GameServer()->Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), &Obj, sizeof(Obj));

	}

	for(int l = 0 ; l < m_pPath->m_Size-1 ; l++)

	{

		vec2 From = m_pPath->m_pVertices[l];

		vec2 To = m_pPath->m_pVertices[l+1];

		if(BotEngine()->NetworkClipped(SnappingClient, To) && BotEngine()->NetworkClipped(SnappingClient, From))

			continue;

		CNetObj_Laser Obj{};

		CNetObj_Laser *pObj = &Obj;

		pObj->m_X = (int) To.x;

		pObj->m_Y = (int) To.y;

		pObj->m_FromX = (int) From.x;

		pObj->m_FromY = (int) From.y;

		pObj->m_StartTick = GameServer()->Server()->Tick();

		GameServer()->Server()->SnapNewItem(NETOBJTYPE_LASER, m_pPath->m_pSnapID[l], &Obj, sizeof(Obj));

	}

}

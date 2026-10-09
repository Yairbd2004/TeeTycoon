#include <generated/protocol.h>

#include <game/gamecore.h>

#include <engine/serverbrowser.h>

#include <engine/shared/config.h>

#include <game/layers.h>

#include "gamecontext.h"



#include "botengine.h"



#include "bot.h"

#include "player.h"

#include "entities/character.h"

#include "entities/pickup.h"



#include "ai/defence.h"

#include <thread>

#include <string>

#include <algorithm>

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

	m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;

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

	if(GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

		Pos = GameServer()->m_apPlayers[owner]->GetCharacter()->GetPos();

	return Pos;

}



void CBot::UpdateTarget()

{

	//m_GenomeTick++;

	bool FindNewTarget = m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY;// || !m_pPath->m_Size;

	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER && !(GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID] && GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID]->GetCharacter()))

		FindNewTarget = true;

	FindNewTarget = true;

	// Timeout: 30s

	if(m_ComputeTarget.m_StartTick + 30 * GameServer()->Server()->TickSpeed() < GameServer()->Server()->Tick())

		FindNewTarget = true;

	if(m_ComputeTarget.m_Type == CTarget::TARGET_AIR)

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), m_ComputeTarget.m_Pos);

		if(dist < 60)

			FindNewTarget = true;

	}

	if(m_ComputeTarget.m_Type > CTarget::TARGET_PLAYER)

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), m_ComputeTarget.m_Pos);

		if(dist < 28)

			FindNewTarget = true;

	}

	if(FindNewTarget)

	{

		m_ComputeTarget.m_StartTick = GameServer()->Server()->Tick();

		m_ComputeTarget.m_NeedUpdate = true;

		m_ComputeTarget.m_Type = CTarget::TARGET_EMPTY;

		vec2 NewTarget;

		for(int i = 0 ; i < CTarget::NUM_TARGETS ; i++)

		{

			switch(m_aTargetOrder[i])

			{

			case CTarget::TARGET_FLAG:

				/*if(GameServer()->m_pController->IsFlagGame()) {

					int Team = m_pPlayer->GetTeam();

					CGameControllerCTF *pController = (CGameControllerCTF*)GameServer()->m_pController;

					CFlag **apFlags = pController->m_apFlags;

					if(apFlags[Team])

					{

						// Retrieve missing flag

						if(!apFlags[Team]->IsAtStand() && !apFlags[Team]->GetCarrier())

						{

							m_ComputeTarget.m_Pos = apFlags[Team]->GetPos();

							m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;

							return;

						}

						// Target flag carrier

						if(!apFlags[Team]->IsAtStand() && apFlags[Team]->GetCarrier())

						{

							m_ComputeTarget.m_Pos = apFlags[Team]->GetPos();

							m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;

							m_ComputeTarget.m_PlayerCID = apFlags[Team]->GetCarrier()->GetPlayer()->GetCid();

							return;

						}

					}

					if(apFlags[Team^1])

					{

						// Go to enemy flagstand

						if(apFlags[Team^1]->IsAtStand())

						{

							m_ComputeTarget.m_Pos = BotEngine()->GetFlagStandPos(Team^1);

							m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;

							return;

						}

						// Go to base carrying flag

						if(apFlags[Team^1]->GetCarrier() == m_pPlayer->GetCharacter() && (!apFlags[Team] || apFlags[Team]->IsAtStand()))

						{

							m_ComputeTarget.m_Pos = BotEngine()->GetFlagStandPos(Team);

							m_ComputeTarget.m_Type = CTarget::TARGET_FLAG;

							return;

						}

					}

				}*/

				break;

			case CTarget::TARGET_ARMOR:

			case CTarget::TARGET_HEALTH:

			case CTarget::TARGET_WEAPON_SHOTGUN:

			case CTarget::TARGET_WEAPON_GRENADE:

			case CTarget::TARGET_WEAPON_LASER:

				{

					float Radius = distance(m_pPlayer->GetCharacter()->GetPos(), ClosestCharacter());

					if(NeedPickup(m_aTargetOrder[i]) && FindPickup(m_aTargetOrder[i], &m_ComputeTarget.m_Pos, Radius))

					{

						m_ComputeTarget.m_Type = m_aTargetOrder[i];

						return;

					}

				}

				break;

			case CTarget::TARGET_PLAYER:

				{

					//if i want to target random close member then enable this.

					/*int Count = 0;

					for(int c = 0; c < MAX_CLIENTS; c++)

						if(c != m_pPlayer->GetCid() && GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter() && !GameServer()->m_apPlayers[c]->m_IsBot)

							Count++;

					if(Count)

					{

						Count = std::rand() % Count + 1;

						int c = 0;

						for(; Count; c++)

							if(c != m_pPlayer->GetCid() && GameServer()->m_apPlayers[c] && GameServer()->m_apPlayers[c]->GetCharacter() && !GameServer()->m_apPlayers[c]->m_IsBot)

								Count--;

						c--;

						m_ComputeTarget.m_Pos = GameServer()->m_apPlayers[c]->GetCharacter()->GetPos();

						m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;

						m_ComputeTarget.m_PlayerCID = c;

						return;

					}*/





					//first priority, if the owner under attack run to kill the enemy that attacks him.

					if (enemyID > 0)

					{

						if (GameServer()->m_apPlayers[enemyID] && GameServer()->m_apPlayers[enemyID]->GetCharacter())

						{

							if (!Collision()->FastIntersectLine(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[enemyID]->GetCharacter()->m_Pos, 0, 0))

							{

								isClose = true;

								if(ownerAttacked)

									m_pPlayer->to_fire = true;

							}

							else

							{

								isClose = false;

								m_pPlayer->to_fire = false;

							}

						}

					}

					if(ownerAttacked && isClose)

					{

						m_pPlayer->GetCharacter()->GiveWeapon(3, false);

						m_pPlayer->GetCharacter()->SetActiveWeapon(3);

						if (GameServer()->m_apPlayers[enemyID] && GameServer()->m_apPlayers[enemyID]->GetCharacter())

						{

							m_ComputeTarget.m_Pos = GameServer()->m_apPlayers[enemyID]->GetCharacter()->GetPos();

							m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;

							m_ComputeTarget.m_PlayerCID = enemyID;

							m_pPlayer->to_fire = true;

							if(GameServer()->m_apPlayers[enemyID]->GetCharacter()->m_SpawnTick > enemyTime) //target killed. (if the curr play time lower means he respawned).

							{

								ownerAttacked = false;

								m_pPlayer->to_fire = false;

							}

							return;

						}

					}

					//second priority (enemy close),do check if anyone of the monster is in range of sight, if it is then change the target to the monster and fight it (use fire).



					//last priority (follow owner).

					else if (GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

					{

						m_ComputeTarget.m_Pos = GameServer()->m_apPlayers[owner]->GetCharacter()->GetPos();

						m_ComputeTarget.m_Type = CTarget::TARGET_PLAYER;

						m_ComputeTarget.m_PlayerCID = owner;

						return;

					}

				}

				break;

			case CTarget::TARGET_AIR:

				{

					// Random destination

					int Count = 0;

					for(int v = 0 ; v < BotEngine()->GetGraph()->m_NumVertices ; v++)

						if(m_pStrategyPosition->IsInsideZone(BotEngine()->GetGraph()->m_pVertices[v].m_Pos))

							Count++;

					if(Count)

					{

						Count = std::rand()%Count+1;

						int v = 0;

						for(; Count ; v++)

							if(m_pStrategyPosition->IsInsideZone(BotEngine()->GetGraph()->m_pVertices[v].m_Pos))

								Count--;

						m_ComputeTarget.m_Pos = BotEngine()->GetGraph()->m_pVertices[--v].m_Pos;

						m_ComputeTarget.m_Type = CTarget::TARGET_AIR;

						return;

					}

				}

			}

		}

	}



	if(m_ComputeTarget.m_Type == CTarget::TARGET_PLAYER)

	{

		CPlayer *pPlayer = GameServer()->m_apPlayers[m_ComputeTarget.m_PlayerCID];

		if(Collision()->FastIntersectLine(m_ComputeTarget.m_Pos, pPlayer->GetCharacter()->GetPos(),0,0))

		{

			m_ComputeTarget.m_NeedUpdate = true;

			m_ComputeTarget.m_Pos = pPlayer->GetCharacter()->GetPos();

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

	if(stuckTick == 0 && !inSight)

	{

		stuckTick = GameServer()->Server()->Tick();

	}

	else if(stuckTick + 250 <= GameServer()->Server()->Tick() && !inSight)

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



void CBot::Tick()

{

	if(!m_pPlayer->GetCharacter())

		return;



	if(m_pStrategyPosition == NULL)

	{

		m_pStrategyPosition = new CDefence(BotEngine());

		m_pStrategyPosition->SetTeam(0);

	}



	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->Core();

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

	if(InSight)

		stuckTick = GameServer()->Server()->Tick();

	checkStuck(InSight);

	if(GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);

		if((dist >= 2000 || stuck) && !stay && !InSight && !(std::find(GameServer()->playersJoined.begin(), GameServer()->playersJoined.end(), owner) != GameServer()->playersJoined.end()))

		{

			stuck = false;

			GameServer()->Teleport(m_pPlayer->GetCharacter(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);

		}

		if (dist <= 70)

		{

			emote();

		}

	}

	MakeChoice(InSight);

	m_RealTarget = m_Target + Pos;

	//HandleWeapon(InSight);



	HandleHook(InSight);

	if (GameServer()->m_apPlayers[owner] && GameServer()->m_apPlayers[owner]->GetCharacter())

	{

		float dist = distance(m_pPlayer->GetCharacter()->GetPos(), GameServer()->m_apPlayers[owner]->GetCharacter()->m_Pos);

		if(dist >= 50 && !stay && !(std::find(GameServer()->playersJoined.begin(), GameServer()->playersJoined.end(), owner) != GameServer()->playersJoined.end()))

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

	if(m_InputData.m_Hook || m_InputData.m_Fire) {

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

			if(ps > 0 || (CurTile & BTILE_HOLE && m_Target.y < 0 && pMe->m_Vel.y > 0.f && pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED/2))

				m_InputData.m_Hook = 1;

			if(pMe->m_HookTick > 4*SERVER_TICK_SPEED || length(pMe->m_HookPos-pMe->m_Pos) < 20.0f)

				m_InputData.m_Hook = 0;

			// if(Flags & BFLAG_HOOK && ps < dot(Target,HookVel-Accel))

			// 	Flags ^= BFLAG_RIGHT | BFLAG_LEFT;

		}

		if(pMe->m_HookState == HOOK_FLYING)

			m_InputData.m_Hook = 1;

		// do random hook

		if(!m_InputData.m_Fire && m_LastData.m_Hook == 0 && pMe->m_HookState == HOOK_IDLE && (std::rand()%10 == 0 || (CurTile & BTILE_HOLE && std::rand()%4 == 0)))

		{

			int NumDir = BOT_HOOK_DIRS;

			vec2 HookDir(0.0f,0.0f);

			float MaxForce = (CurTile & BTILE_HOLE) ? -10000.0f : 0;

			vec2 Target = m_Target;

			for(int i = 0 ; i < NumDir; i++)

			{

				float a = 2*i*pi / NumDir;

				vec2 dir = direction(a);

				vec2 Pos = pMe->m_Pos+dir*Tuning()->m_HookLength;



				if(Collision()->FastIntersectLine(pMe->m_Pos, Pos, &Pos, 0))

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



					float ps = dot(Target, HookVel);

					if( ps > MaxForce)

					{

						MaxForce = ps;

						HookDir = Pos - pMe->m_Pos;

					}

				}

			}

			if(length(HookDir) > 32.f)

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

	vec2 Pos = m_pPlayer->GetCharacter()->GetPos();

	if(m_ComputeTarget.m_Type == CTarget::TARGET_EMPTY)

	{

		return;

	}

	if(m_ComputeTarget.m_NeedUpdate)

	{

		m_pPath->m_Size = 0;

		BotEngine()->GetPath(Pos, m_ComputeTarget.m_Pos, m_pPath);

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

			static bool tried = false;

			if(absolute(CurPos.x - NextPos.x) < 1.0f && TempChar.m_Input.m_Direction)

			{

				if(Grounded)

				{

					Flags |= BFLAG_JUMP;

					tried = true;

				}

				else if(tried && !(TempChar.m_Jumped) && TempChar.m_Vel.y > 0)

					Flags |= BFLAG_JUMP;

				else if(tried && TempChar.m_Jumped & 2 && TempChar.m_Vel.y > 0)

					Flags ^= BFLAG_RIGHT | BFLAG_LEFT;

			}

			else

				tried = false;

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

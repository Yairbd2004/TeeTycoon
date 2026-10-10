/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "gamecontext.h"

#include <base/io.h>
#include <base/log.h>
#include <base/time.h>

#include <engine/antibot.h>
#include <engine/shared/config.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/player.h>
#include <game/server/save.h>
#include <game/server/teams.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ctime>
#include <sqlite3.h>
#include <game/race_state.h>

void CGameContext::ConGoLeft(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, -1 * Tiles, 0);
}

void CGameContext::ConGoRight(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, Tiles, 0);
}

void CGameContext::ConGoDown(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, 0, Tiles);
}

void CGameContext::ConGoUp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Tiles = pResult->NumArguments() == 1 ? pResult->GetInteger(0) : 1;

	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, 0, -1 * Tiles);
}

void CGameContext::ConMove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, pResult->GetInteger(0),
		pResult->GetInteger(1));
}

void CGameContext::ConMoveRaw(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	pSelf->MoveCharacter(pResult->m_ClientId, pResult->GetInteger(0),
		pResult->GetInteger(1), true);
}

void CGameContext::MoveCharacter(int ClientId, int X, int Y, bool Raw)
{
	CCharacter *pChr = GetPlayerChar(ClientId);

	if(!pChr)
		return;

	pChr->Move(vec2((Raw ? 1 : 32) * X, (Raw ? 1 : 32) * Y));
	pChr->ResetVelocity();
	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::ConKillPlayer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	int Victim = pResult->GetVictim(0);

	if(pSelf->m_apPlayers[Victim])
	{
		pSelf->m_apPlayers[Victim]->KillCharacter(WEAPON_GAME);
		char aBuf[512];
		if(pResult->NumArguments() == 2)
			str_format(aBuf, sizeof(aBuf), "%s was killed by authorized player (%s)",
				pSelf->Server()->ClientName(Victim),
				pResult->GetString(1));
		else
			str_format(aBuf, sizeof(aBuf), "%s was killed by authorized player",
				pSelf->Server()->ClientName(Victim));
		pSelf->SendChat(-1, TEAM_ALL, aBuf);
	}
}

void CGameContext::ConNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_NINJA, false);
}

void CGameContext::ConUnNinja(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_NINJA, true);
}

void CGameContext::ConEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetEndlessHook(true);
	}
}

void CGameContext::ConUnEndlessHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetEndlessHook(false);
	}
}

void CGameContext::ConRemoveMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer;
	bool isConnected = false;
	//get the database
	sqlite3 *db;
	const char *sqlStatement;
	std::string dbFileName = "Accounts.sqlite";
	char *errMessage = nullptr;
	int file_exist = (std::filesystem::exists(dbFileName) ? 0 : -1);
	int res = sqlite3_open(dbFileName.c_str(), &db);
	if(res != SQLITE_OK)
	{
		db = nullptr;
		std::cout << "Failed to open Accounts, creating new db file with that name..." << std::endl;
	}
	if(file_exist != 0)
	{
		sqlStatement = "CREATE TABLE ACCOUNTS (ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL , NAME TEXT NOT NULL , PASSWORD TEXT NOT NULL , RANK INTEGER NOT NULL , MONEY INTEGER NOT NULL , LEVEL INTEGER NOT NULL , EXP INTEGER NOT NULL , HOUSE INTEGER NOT NULL , VIP INTEGER NOT NULL , REBIRTH INTEGER NOT NULL);";
		res = sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
	}
	for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
	{
		if(!pSelf->m_apPlayers[i])
			continue;
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(pResult->GetVictim(0) == i)
		{
			isConnected = true;
		}
	}
	if(isConnected)
	{
		pPlayer = pSelf->m_apPlayers[pResult->GetVictim(0)];
		if(pPlayer->money - pResult->GetInteger(1) >= 0 && pResult->GetInteger(1) >= 0)
		{
			pPlayer->money -= pResult->GetInteger(1);
			std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + " WHERE NAME='" + pPlayer->username + "';";
			sqlStatement = strSql.c_str();
			sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
		}
		else
		{
			if(pResult->GetInteger(1) < 0)
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "remove_money",
					"you cant enter negative values...");
			else
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "remove_money",
					"You cant remove this much since players cant have negative values of money...");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "remove_money",
			"This player not exist / not connected!");
	}
}
void CGameContext::ConAddMoney(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer;
	bool isConnected = false;
	//get the database
	sqlite3 *db;
	const char *sqlStatement;
	std::string dbFileName = "Accounts.sqlite";
	char *errMessage = nullptr;
	int file_exist = (std::filesystem::exists(dbFileName) ? 0 : -1);
	int res = sqlite3_open(dbFileName.c_str(), &db);
	if(res != SQLITE_OK)
	{
		db = nullptr;
		std::cout << "Failed to open Accounts, creating new db file with that name..." << std::endl;
	}
	if(file_exist != 0)
	{
		sqlStatement = "CREATE TABLE ACCOUNTS (ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL , NAME TEXT NOT NULL , PASSWORD TEXT NOT NULL , RANK INTEGER NOT NULL , MONEY INTEGER NOT NULL , LEVEL INTEGER NOT NULL , EXP INTEGER NOT NULL , HOUSE INTEGER NOT NULL , VIP INTEGER NOT NULL , REBIRTH INTEGER NOT NULL);";
		res = sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
	}
	for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
	{
		if(!pSelf->m_apPlayers[i])
			continue;
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(pResult->GetVictim(0) == i)
		{
			isConnected = true;
		}
	}
	if(isConnected)
	{
		pPlayer = pSelf->m_apPlayers[pResult->GetVictim(0)];
		if(pPlayer->money + pResult->GetInteger(1) <= 2000000000 && pResult->GetInteger(1) >= 0)
		{
			pPlayer->money += pResult->GetInteger(1);
			std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + " WHERE NAME='" + pPlayer->username + "';";
			sqlStatement = strSql.c_str();
			sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
		}
		else
		{
			if(pResult->GetInteger(1) < 0)
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "add_money",
					"use remove_money commands in order to remove money... (dont enter negative values)");
			else
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "add_money",
				"You cant add this much since max money is 2000000000!");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "add_money",
			"This player not exist / not connected!");
	}
}

void CGameContext::ConSetVip(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer;
	bool isConnected = false;
	//get the database
	sqlite3 *db;
	const char *sqlStatement;
	std::string dbFileName = "Accounts.sqlite";
	char *errMessage = nullptr;
	int file_exist = (std::filesystem::exists(dbFileName) ? 0 : -1);
	int res = sqlite3_open(dbFileName.c_str(), &db);
	if(res != SQLITE_OK)
	{
		db = nullptr;
		std::cout << "Failed to open Accounts, creating new db file with that name..." << std::endl;
	}
	if(file_exist != 0)
	{
		sqlStatement = "CREATE TABLE ACCOUNTS (ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL , NAME TEXT NOT NULL , PASSWORD TEXT NOT NULL , RANK INTEGER NOT NULL , MONEY INTEGER NOT NULL , LEVEL INTEGER NOT NULL , EXP INTEGER NOT NULL , HOUSE INTEGER NOT NULL , VIP INTEGER NOT NULL , REBIRTH INTEGER NOT NULL);";
		res = sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
	}
	for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
	{
		if(!pSelf->m_apPlayers[i])
			continue;
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if(pResult->GetVictim(0) == i)
		{
			isConnected = true;
		}
	}
	if(isConnected)
	{
		pPlayer = pSelf->m_apPlayers[pResult->GetVictim(0)];
		if (pPlayer->id > 0)
		{
			if (pResult->GetInteger(1) <= 5 && pResult->GetInteger(1) >= 0)
			{
				pPlayer->vip = pResult->GetInteger(1);
				std::string strSql = "UPDATE ACCOUNTS SET VIP=" + std::to_string(pPlayer->vip) + " WHERE NAME='" + pPlayer->username + "';";
				sqlStatement = strSql.c_str();
				sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "set_vip",
					"vip max is 5 (u should set between the range 0-5)");
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "set_vip",
				"the player with that id is not logged in!");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "set_vip",
			"the player with that id is not exist!");
	}
}

bool CheckRights(int ClientID, int Victim, CGameContext *GameContext)
{
	if(!CheckClientId(ClientID))
		return /*false*/ true; // allow executing from rcon
	if(!CheckClientId(Victim))
		return false;

	if(ClientID == Victim)
		return true;

	if(!GameContext->m_apPlayers[ClientID] || !GameContext->m_apPlayers[Victim])
		return false;

	return true;
}

void CGameContext::ConFastWeapons(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckRights(pResult->m_ClientId, pResult->GetVictim(0), (CGameContext *)pUserData))
		return;
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim(0);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	CCharacter *pChr = pSelf->m_apPlayers[Victim]->GetCharacter();
	if(!pChr)
		return;

	char aBuf[128];
	if(!pChr->m_FastReload)
	{
		pChr->m_ReloadMultiplier = 10000;
		pChr->m_FastReload = true;

		str_format(aBuf, sizeof(aBuf), "You got FastWeapons by %s.", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTarget(Victim, aBuf);
	}
	else
	{
		pChr->m_ReloadMultiplier = 1000;
		pChr->m_FastReload = false;
		str_format(aBuf, sizeof(aBuf), "%s removed your FastWeapons.", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTarget(Victim, aBuf);
	}

	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::ConBloody(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckRights(pResult->m_ClientId, pResult->GetVictim(0), (CGameContext *)pUserData))
		return;
	if(!pSelf->m_apPlayers[pResult->GetVictim(0)]->GetCharacter()->m_Bloody_item)
		pSelf->m_apPlayers[pResult->GetVictim(0)]->GetCharacter()->m_Bloody_item = true;
	else
		pSelf->m_apPlayers[pResult->GetVictim(0)]->GetCharacter()->m_Bloody_item = false;
}

void CGameContext::ConRainbow(IConsole::IResult *pResult, void *pUserData)
{
	if(!CheckRights(pResult->m_ClientId, pResult->GetVictim(0), (CGameContext *)pUserData))
		return;
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim(0);
	int Rainbowtype = std::clamp(pResult->GetInteger(0), 0, 2);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	CCharacter *pChr = pSelf->m_apPlayers[Victim]->GetCharacter();
	if(!pChr)
		return;

	char aBuf[256];
	if((pSelf->m_apPlayers[Victim]->m_Rainbow == RAINBOW_NONE || pSelf->m_apPlayers[Victim]->m_Rainbow == RAINBOW_BLACKWHITE) && Rainbowtype <= 1)
	{
		pSelf->m_apPlayers[Victim]->m_LastBodyR = pSelf->m_apPlayers[Victim]->TeeInfos().m_ColorBody;
		pSelf->m_apPlayers[Victim]->m_LastFeetR = pSelf->m_apPlayers[Victim]->TeeInfos().m_ColorFeet;
		pSelf->m_apPlayers[Victim]->m_Rainbow = RAINBOW_COLOR;

		str_format(aBuf, sizeof(aBuf), "You got rainbow by %s.", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTarget(Victim, aBuf);
	}
	else if((pSelf->m_apPlayers[Victim]->m_Rainbow == RAINBOW_NONE || pSelf->m_apPlayers[Victim]->m_Rainbow == RAINBOW_COLOR) && Rainbowtype == 2)
	{
		pSelf->m_apPlayers[Victim]->m_Rainbow = RAINBOW_BLACKWHITE;

		str_format(aBuf, sizeof(aBuf), "You got black and white rainbow by %s.", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTarget(Victim, aBuf);
	}
	else
	{
		pSelf->m_apPlayers[Victim]->m_Rainbow = RAINBOW_NONE;
		pSelf->m_apPlayers[Victim]->TeeInfos().m_ColorBody = pSelf->m_apPlayers[Victim]->m_LastBodyR;
		pSelf->m_apPlayers[Victim]->TeeInfos().m_ColorFeet = pSelf->m_apPlayers[Victim]->m_LastFeetR;
		str_format(aBuf, sizeof(aBuf), "%s removed your rainbow.", pSelf->Server()->ClientName(pResult->m_ClientId));
		pSelf->SendChatTarget(Victim, aBuf);
	}
}

bool CGameContext::TryStartEvent(int EventType, int ClientId)
{
	if(!db || EventType < 1 || EventType > 5)
	{
		SendChatTarget(ClientId, "The event could not start because the database is unavailable.");
		return false;
	}
	const sqlite3_int64 Now = static_cast<sqlite3_int64>(std::time(nullptr));
	sqlite3_stmt *pStatement = nullptr;
	if(sqlite3_prepare_v2(db, "SELECT LAST_START_UNIX, ACTIVE_EVENT FROM TT_EVENT_STATE WHERE ID = 1", -1, &pStatement, nullptr) != SQLITE_OK)
	{
		SendChatTarget(ClientId, "The event could not start because its database state could not be read.");
		return false;
	}
	const int StepResult = sqlite3_step(pStatement);
	const sqlite3_int64 LastStart = StepResult == SQLITE_ROW ? sqlite3_column_int64(pStatement, 0) : 0;
	const int ActiveEvent = StepResult == SQLITE_ROW ? sqlite3_column_int(pStatement, 1) : 0;
	sqlite3_finalize(pStatement);
	if(StepResult != SQLITE_ROW)
	{
		SendChatTarget(ClientId, "The event could not start because its database state could not be read.");
		return false;
	}
	if(m_EventState.load() != 0 || ActiveEvent != 0)
	{
		SendChatTarget(ClientId, "Another event is already running. Please wait for it to finish.");
		return false;
	}
	const sqlite3_int64 SecondsLeft = 600 - (Now - LastStart);
	if(LastStart > 0 && SecondsLeft > 0)
	{
		char aMessage[128];
		str_format(aMessage, sizeof(aMessage), "Event votes are on cooldown. Try again in %lld:%02lld.", static_cast<long long>(SecondsLeft / 60), static_cast<long long>(SecondsLeft % 60));
		SendChatTarget(ClientId, aMessage);
		return false;
	}
	if(sqlite3_prepare_v2(db, "UPDATE TT_EVENT_STATE SET LAST_START_UNIX = ?, ACTIVE_EVENT = ? WHERE ID = 1 AND ACTIVE_EVENT = 0", -1, &pStatement, nullptr) != SQLITE_OK)
	{
		SendChatTarget(ClientId, "The event could not start because its database state could not be saved.");
		return false;
	}
	sqlite3_bind_int64(pStatement, 1, Now);
	sqlite3_bind_int(pStatement, 2, EventType);
	const bool Saved = sqlite3_step(pStatement) == SQLITE_DONE && sqlite3_changes(db) == 1;
	sqlite3_finalize(pStatement);
	if(!Saved)
	{
		SendChatTarget(ClientId, "Another event is already running, or the event state could not be saved.");
		return false;
	}
	{
		std::lock_guard<std::mutex> Lock(m_EventPlayersMutex);
		playersJoined.clear();
	}
	ignorePlayers.clear();
	m_EventType = EventType;
	m_EventState = 1;
	m_EventPhase = 1;
	m_EventPhaseEndTick = Server()->Tick() + 30LL * Server()->TickSpeed();
	m_EventNextUpdateTick = Server()->Tick();
	m_EventRemainingSeconds = 30;
	m_EventPlayerCount = 0;
	m_EventWinnerClientId = -1;
	SendChatTarget(-1, "An event is open for registration. Type /join to participate; it starts in 30 seconds.");
	return true;
}

void CGameContext::TickEvents()
{
	if(m_EventPhase == 0 || Server()->Tick() < m_EventNextUpdateTick)
		return;
	const int64_t Now = Server()->Tick();
	const int TickSpeed = std::max(1, Server()->TickSpeed());
	m_EventNextUpdateTick = Now + TickSpeed;
	std::vector<int> Participants;
	{
		std::lock_guard<std::mutex> Lock(m_EventPlayersMutex);
		Participants = playersJoined;
	}
	if(m_EventPhase == 1)
	{
		const int SecondsLeft = static_cast<int>(std::max<int64_t>(0, (m_EventPhaseEndTick - Now + TickSpeed - 1) / TickSpeed));
		if(SecondsLeft != m_EventRemainingSeconds || Now >= m_EventPhaseEndTick)
		{
			m_EventRemainingSeconds = SecondsLeft;
			char aMessage[160];
			str_format(aMessage, sizeof(aMessage), "Event starts in %d seconds. Type /join to enter. [%d/%d]", SecondsLeft, static_cast<int>(Participants.size()), MAX_CLIENTS);
			for(int ClientId = 0; ClientId < Server()->MaxClients(); ++ClientId)
				if(m_apPlayers[ClientId])
					SendBroadcast(aMessage, ClientId);
		}
		if(Now < m_EventPhaseEndTick)
			return;
		Participants.erase(std::remove_if(Participants.begin(), Participants.end(), [this](int ClientId) {
			return ClientId < 0 || ClientId >= Server()->MaxClients() || !m_apPlayers[ClientId] || !m_apPlayers[ClientId]->GetCharacter();
		}), Participants.end());
		{
			std::lock_guard<std::mutex> Lock(m_EventPlayersMutex);
			playersJoined = Participants;
		}
		m_EventPlayerCount = static_cast<int>(Participants.size());
		if(m_EventPlayerCount < 2)
		{
			for(int ClientId : Participants)
			{
				m_apPlayers[ClientId]->hasJoined = false;
				SendChatTarget(ClientId, "The event was cancelled because at least two players must join.");
			}
			FinishEvent(false);
			return;
		}
		const int EventType = m_EventType.load();
		const int TeleIn = EventType == 1 ? 254 : EventType == 2 ? 253 : EventType == 3 ? 252 : EventType == 4 ? 251 : 250;
		const auto &TeleOuts = Collision()->TeleOuts(TeleIn - 1);
		if(TeleOuts.empty())
		{
			for(int ClientId : Participants)
				SendChatTarget(ClientId, "The event map is missing its configured spawn points; the event was cancelled.");
			FinishEvent(false);
			return;
		}
		for(int ClientId : Participants)
		{
			CPlayer *pPlayer = m_apPlayers[ClientId];
			CCharacter *pChr = pPlayer->GetCharacter();
			if(!pChr)
				continue;
			pPlayer->playersInEvent = m_EventPlayerCount;
			pChr->m_Race = false;
			pChr->m_dm = EventType == 3;
			pChr->m_fng = EventType == 5;
			if(EventType == 3)
			{
				pChr->GiveWeapon(3, false);
				pChr->GiveWeapon(0, true);
				pChr->GiveWeapon(1, true);
				pChr->GiveWeapon(2, true);
				pChr->GiveWeapon(4, true);
				pChr->SetActiveWeapon(3);
			}
			else if(EventType == 5)
			{
				pChr->GiveWeapon(4, false);
				pChr->GiveWeapon(0, false);
				pChr->GiveWeapon(1, true);
				pChr->GiveWeapon(2, true);
				pChr->GiveWeapon(3, true);
				pChr->SetActiveWeapon(4);
			}
			SetTeamInvite(ClientId, 0);
			Teleport(pChr, TeleOuts[m_World.m_Core.RandomOr0(TeleOuts.size())]);
			pChr->Freeze();
		}
		m_EventState = 2;
		m_EventPhase = 2;
		m_EventPhaseEndTick = Now + 3LL * TickSpeed;
		m_EventNextUpdateTick = m_EventPhaseEndTick;
		SendChatTarget(-1, "Event participants are in position. The event begins in 3 seconds.");
		return;
	}
	if(m_EventPhase == 2)
	{
		for(int ClientId : Participants)
			if(ClientId >= 0 && ClientId < Server()->MaxClients() && m_apPlayers[ClientId])
				if(CCharacter *pChr = m_apPlayers[ClientId]->GetCharacter())
					pChr->Unfreeze();
		m_EventPhase = 3;
		m_EventRemainingSeconds = m_EventType == 3 || m_EventType == 5 ? 90 : 60;
		m_EventPhaseEndTick = Now + static_cast<int64_t>(m_EventRemainingSeconds) * TickSpeed;
		m_EventNextUpdateTick = Now + TickSpeed;
		SendChatTarget(-1, "The event has started!");
		return;
	}
	if(m_EventPhase != 3)
		return;
	const int EventType = m_EventType.load();
	int AliveCount = 0;
	int WinnerClientId = -1;
	const int SecondsLeft = static_cast<int>(std::max<int64_t>(0, (m_EventPhaseEndTick - Now + TickSpeed - 1) / TickSpeed));
	for(int ClientId : Participants)
	{
		if(ClientId < 0 || ClientId >= Server()->MaxClients() || !m_apPlayers[ClientId])
			continue;
		CCharacter *pChr = m_apPlayers[ClientId]->GetCharacter();
		if(!pChr)
			continue;
		if(pChr->m_survival)
			++AliveCount;
		if((EventType == 2 || EventType == 4) && pChr->m_Race)
			WinnerClientId = ClientId;
		char aMessage[128];
		if(EventType == 1)
			str_format(aMessage, sizeof(aMessage), "Survive for %d more seconds!", SecondsLeft);
		else if(EventType == 2)
			str_format(aMessage, sizeof(aMessage), "Race event ends in %d seconds!", SecondsLeft);
		else if(EventType == 3)
			str_format(aMessage, sizeof(aMessage), "Deathmatch ends in %d seconds!", SecondsLeft);
		else if(EventType == 4)
			str_format(aMessage, sizeof(aMessage), "Freeze Race ends in %d seconds!", SecondsLeft);
		else
			str_format(aMessage, sizeof(aMessage), "FNG ends in %d seconds!", SecondsLeft);
		SendBroadcast(aMessage, ClientId);
	}
	m_EventWinnerClientId = WinnerClientId;
	const bool TimedOut = Now >= m_EventPhaseEndTick;
	if(WinnerClientId >= 0 || AliveCount <= 1 || TimedOut)
		FinishEvent(TimedOut);
}

void CGameContext::FinishEvent(bool TimedOut)
{
	const int EventType = m_EventType.load();
	const int WinnerClientId = m_EventWinnerClientId;
	std::vector<int> Participants;
	{
		std::lock_guard<std::mutex> Lock(m_EventPlayersMutex);
		Participants = playersJoined;
	}
	const auto &SpawnOuts = Collision()->TeleOuts(0);
	for(int ClientId : Participants)
	{
		if(ClientId < 0 || ClientId >= Server()->MaxClients() || !m_apPlayers[ClientId])
			continue;
		CPlayer *pPlayer = m_apPlayers[ClientId];
		CCharacter *pChr = pPlayer->GetCharacter();
		const bool Won = EventType == 2 || EventType == 4 ? ClientId == WinnerClientId : pChr && pChr->m_survival;
		pPlayer->hasJoined = false;
		if(Won && pPlayer->id > 0 && db)
		{
			const int Reward = std::max(0, pPlayer->playersInEvent) * 100;
			const sqlite3_int64 Money = std::min<sqlite3_int64>(2000000000LL, static_cast<sqlite3_int64>(pPlayer->money) + Reward);
			const sqlite3_int64 Exp = std::min<sqlite3_int64>(2147483647LL, static_cast<sqlite3_int64>(pPlayer->exp) + Reward);
			sqlite3_stmt *pStatement = nullptr;
			if(sqlite3_prepare_v2(db, "UPDATE ACCOUNTS SET MONEY = ?, EXP = ? WHERE NAME = ?", -1, &pStatement, nullptr) == SQLITE_OK)
			{
				sqlite3_bind_int64(pStatement, 1, Money);
				sqlite3_bind_int64(pStatement, 2, Exp);
				sqlite3_bind_text(pStatement, 3, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
				if(sqlite3_step(pStatement) == SQLITE_DONE && sqlite3_changes(db) == 1)
				{
					pPlayer->money = static_cast<int>(Money);
					pPlayer->exp = static_cast<int>(Exp);
					char aMessage[128];
					str_format(aMessage, sizeof(aMessage), "You won the event and received $%d and %d EXP.", Reward, Reward);
					SendChatTarget(ClientId, aMessage);
				}
				else
					SendChatTarget(ClientId, "You won the event, but the reward could not be saved.");
				sqlite3_finalize(pStatement);
			}
			else
				SendChatTarget(ClientId, "You won the event, but the reward could not be saved.");
		}
		if(pPlayer->exp >= pPlayer->neededExp && pPlayer->neededExp > 0 && pPlayer->id > 0 && db)
		{
			pPlayer->exp -= pPlayer->neededExp;
			++pPlayer->level;
			pPlayer->m_Score = pPlayer->level;
			pPlayer->neededExp = static_cast<int>(10000 * (pPlayer->level + 1) * 1.5);
			sqlite3_stmt *pStatement = nullptr;
			if(sqlite3_prepare_v2(db, "UPDATE ACCOUNTS SET LEVEL = ?, EXP = ? WHERE NAME = ?", -1, &pStatement, nullptr) == SQLITE_OK)
			{
				sqlite3_bind_int(pStatement, 1, pPlayer->level);
				sqlite3_bind_int(pStatement, 2, pPlayer->exp);
				sqlite3_bind_text(pStatement, 3, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_step(pStatement);
				sqlite3_finalize(pStatement);
				SendChatTarget(ClientId, "Congratulations! You leveled up.");
				ConSetClan(pPlayer);
			}
		}
		if(pChr)
		{
			pChr->m_Race = false;
			pChr->m_dm = false;
			pChr->m_fng = false;
			if(EventType == 2 || EventType == 4)
				pChr->SetSolo(false);
			if(TimedOut && EventType == 3 && pChr->m_survival)
			{
				pChr->Die(0, -1);
				pChr = nullptr;
			}
			if(pChr && !SpawnOuts.empty())
				Teleport(pChr, SpawnOuts[m_World.m_Core.RandomOr0(SpawnOuts.size())]);
		}
		pPlayer->playersInEvent = 0;
	}
	{
		std::lock_guard<std::mutex> Lock(m_EventPlayersMutex);
		playersJoined.clear();
	}
	ignorePlayers.clear();
	m_EventPhase = 0;
	m_EventState = 0;
	m_EventType = 0;
	m_EventPhaseEndTick = 0;
	m_EventNextUpdateTick = 0;
	m_EventRemainingSeconds = 0;
	m_EventPlayerCount = 0;
	m_EventWinnerClientId = -1;
	if(db)
		sqlite3_exec(db, "UPDATE TT_EVENT_STATE SET ACTIVE_EVENT = 0 WHERE ID = 1", nullptr, nullptr, nullptr);
	SendChatTarget(-1, "The event has ended.");
}

void CGameContext::ConStartEventSurvival(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->TryStartEvent(1, pResult->m_ClientId);
}
void CGameContext::ConStartEventRace(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->TryStartEvent(2, pResult->m_ClientId);
}

void CGameContext::ConStartEventDm(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->TryStartEvent(3, pResult->m_ClientId);
}

void CGameContext::ConStartEventFreezeRace(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->TryStartEvent(4, pResult->m_ClientId);
}

void CGameContext::ConStartEventFng(IConsole::IResult *pResult, void *pUserData)
{
	((CGameContext *)pUserData)->TryStartEvent(5, pResult->m_ClientId);
}

//need to add tunning for each player itself instead of server tunning, then i can do it for only one player as i want to.
void CGameContext::ConStrongHammer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int victim;
	//in the future.
	if(pResult->NumArguments() > 1)
	{
		victim = pResult->GetVictim(0);
		pPlayer = pSelf->m_apPlayers[victim];
	}
	//put 20 for all until i do it for only specific id.
	pSelf->GlobalTuning()->m_HammerStrength = 100000;
	std::string msg = std::string(pSelf->Server()->ClientName(pResult->m_ClientId)) + " gave you fast grenade!";
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fast_grenade",
		msg.c_str());
}

//need to add tunning for each player itself instead of server tunning, then i can do it for only one player as i want to.
void CGameContext::ConUnStrongHammer(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int victim;
	//in the future.
	if(pResult->NumArguments() > 1)
	{
		victim = pResult->GetVictim(0);
		pPlayer = pSelf->m_apPlayers[victim];
	}
	//put 20 for all until i do it for only specific id.
	pSelf->GlobalTuning()->m_HammerStrength = 1;
	std::string msg = std::string(pSelf->Server()->ClientName(pResult->m_ClientId)) + " gave you fast grenade!";
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fast_grenade",
		msg.c_str());
}

//need to add tunning for each player itself instead of server tunning, then i can do it for only one player as i want to.
void CGameContext::ConInfHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int victim;
	//in the future.
	if(pResult->NumArguments() > 1)
	{
		victim = pResult->GetVictim(0);
		pPlayer = pSelf->m_apPlayers[victim];
	}
	//put 20 for all until i do it for only specific id.
	pSelf->GlobalTuning()->m_HookLength = 100000;
	std::string msg = std::string(pSelf->Server()->ClientName(pResult->m_ClientId)) + " gave you fast grenade!";
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fast_grenade",
		msg.c_str());
}

//need to add tunning for each player itself instead of server tunning, then i can do it for only one player as i want to.
void CGameContext::ConUnInfHook(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int victim;
	//in the future.
	if(pResult->NumArguments() > 1)
	{
		victim = pResult->GetVictim(0);
		pPlayer = pSelf->m_apPlayers[victim];
	}
	//put 20 for all until i do it for only specific id.
	pSelf->GlobalTuning()->m_HookLength = 380;
	std::string msg = std::string(pSelf->Server()->ClientName(pResult->m_ClientId)) + " gave you fast grenade!";
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fast_grenade",
		msg.c_str());
}

void CGameContext::ConSuper(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr && !pChr->IsSuper())
	{
		pChr->SetSuper(true);
		pChr->Unfreeze();
	}
}

void CGameContext::ConUnSuper(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr && pChr->IsSuper())
	{
		pChr->SetSuper(false);
	}
}

void CGameContext::ConToggleInvincible(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetInvincible(pResult->NumArguments() == 0 ? !pChr->Core()->m_Invincible : pResult->GetInteger(0));
}

void CGameContext::ConSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetSolo(true);
}

void CGameContext::ConUnSolo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetSolo(false);
}

void CGameContext::ConFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->Freeze();
}

void CGameContext::ConUnfreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->Unfreeze();
}

void CGameContext::ConDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetDeepFrozen(true);
}

void CGameContext::ConUnDeep(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
	{
		pChr->SetDeepFrozen(false);
		pChr->Unfreeze();
	}
}

void CGameContext::ConLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetLiveFrozen(true);
}

void CGameContext::ConUnLiveFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetLiveFrozen(false);
}

void CGameContext::ConShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_SHOTGUN, false);
}

void CGameContext::ConGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_GRENADE, false);
}

void CGameContext::ConLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_LASER, false);
}

void CGameContext::ConJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJetpack(true);
}

void CGameContext::ConEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetEndlessJump(true);
}

void CGameContext::ConSetJumps(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJumps(pResult->GetInteger(0));
}

void CGameContext::ConWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, -1, false);
}

void CGameContext::ConUnShotgun(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_SHOTGUN, true);
}

void CGameContext::ConUnGrenade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_GRENADE, true);
}

void CGameContext::ConUnLaser(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, WEAPON_LASER, true);
}

void CGameContext::ConUnJetpack(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetJetpack(false);
}

void CGameContext::ConUnEndlessJump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(pChr)
		pChr->SetEndlessJump(false);
}

void CGameContext::ConSetSwitch(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
	{
		log_info("chatresp", "You can't set switch while you are dead/a spectator.");
		return;
	}
	const int Team = pChr->Team();
	const int Switch = pResult->GetInteger(0);
	if(!in_range(Switch, (int)pSelf->Switchers().size() - 1))
	{
		log_info("chatresp", "Invalid switch ID");
		return;
	}
	const bool State = pResult->NumArguments() == 1 ? !pSelf->Switchers()[Switch].m_aStatus[Team] : pResult->GetInteger(1) != 0;
	const int EndTick = pResult->NumArguments() == 3 ? pSelf->Server()->Tick() + 1 + pResult->GetInteger(2) * pSelf->Server()->TickSpeed() : 0;
	pSelf->Switchers()[Switch].m_aStatus[Team] = State;
	pSelf->Switchers()[Switch].m_aEndTick[Team] = EndTick;
	if(State)
		pSelf->Switchers()[Switch].m_aType[Team] = EndTick ? TILE_SWITCHTIMEDOPEN : TILE_SWITCHOPEN;
	else
		pSelf->Switchers()[Switch].m_aType[Team] = EndTick ? TILE_SWITCHTIMEDCLOSE : TILE_SWITCHCLOSE;
}

void CGameContext::ConUnWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, -1, true);
}

void CGameContext::ConAddWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, pResult->GetInteger(0), false);
}

void CGameContext::ConRemoveWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ModifyWeapons(pResult, pUserData, pResult->GetInteger(0), true);
}

void CGameContext::ModifyWeapons(IConsole::IResult *pResult, void *pUserData,
	int Weapon, bool Remove)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CCharacter *pChr = GetPlayerChar(pResult->m_ClientId);
	if(!pChr)
		return;

	if(std::clamp(Weapon, -1, NUM_WEAPONS - 1) != Weapon)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info",
			"invalid weapon id");
		return;
	}

	if(Weapon == -1)
	{
		pChr->GiveWeapon(WEAPON_SHOTGUN, Remove);
		pChr->GiveWeapon(WEAPON_GRENADE, Remove);
		pChr->GiveWeapon(WEAPON_LASER, Remove);
	}
	else
	{
		pChr->GiveWeapon(Weapon, Remove);
	}

	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::SetTeamInvite(int id, int inviteID)
{
	auto *pController = m_pController;
	pController->Teams().SetForceCharacterTeam(id, inviteID);
}

void CGameContext::Teleport(CCharacter *pChr, vec2 Pos, bool ForceBot)
{
	if(!pChr || (!ForceBot && pChr->GetPlayer()->m_IsBot && IsBotFrozen(pChr)))
		return;
	pChr->SetPosition(Pos);
	pChr->m_Pos = Pos;
	pChr->m_PrevPos = Pos;
	pChr->m_DDRaceState = ERaceState::CHEATED;
}

void CGameContext::ConToTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	unsigned int TeleTo = pResult->GetInteger(0);

	if(!pSelf->Collision()->TeleOuts(TeleTo - 1).empty())
	{
		CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
		if(pChr)
		{
			int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(TeleTo - 1).size());
			pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(TeleTo - 1)[TeleOut]);
		}
	}
}

void CGameContext::ConToCheckTeleporter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	unsigned int TeleTo = pResult->GetInteger(0);

	if(!pSelf->Collision()->TeleCheckOuts(TeleTo - 1).empty())
	{
		CCharacter *pChr = pSelf->GetPlayerChar(pResult->m_ClientId);
		if(pChr)
		{
			int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleCheckOuts(TeleTo - 1).size());
			pSelf->Teleport(pChr, pSelf->Collision()->TeleCheckOuts(TeleTo - 1)[TeleOut]);
			pChr->m_TeleCheckpoint = TeleTo;
		}
	}
}

void CGameContext::ConTeleport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const bool HasConsoleCaller = !CheckClientId(pResult->m_ClientId);
	if(HasConsoleCaller && pResult->NumArguments() != 2)
		return;
	const bool HasSource = pResult->NumArguments() == 2;
	int Tele = HasSource ? pResult->GetVictim(0) : pResult->m_ClientId;
	int TeleTo = pResult->NumArguments() ? pResult->GetVictim(HasSource ? 1 : 0) : pResult->m_ClientId;
	int AuthLevel = HasConsoleCaller ? AUTHED_ADMIN : pSelf->Server()->GetAuthedState(pResult->m_ClientId);

	if(Tele != pResult->m_ClientId && AuthLevel < g_Config.m_SvTeleOthersAuthLevel)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tele", "you aren't allowed to tele others");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(Tele);
	CPlayer *pPlayer = CheckClientId(pResult->m_ClientId) ? pSelf->m_apPlayers[pResult->m_ClientId] : nullptr;

	if(pChr && pPlayer && pSelf->GetPlayerChar(TeleTo))
	{
		// default to view pos when character is not available
		vec2 Pos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
		if(pResult->NumArguments() == 0 && pPlayer && !pPlayer->IsPaused() && pChr->IsAlive())
		{
			vec2 Target = vec2(pChr->Core()->m_Input.m_TargetX, pChr->Core()->m_Input.m_TargetY);
			Pos = pPlayer->m_CameraInfo.ConvertTargetToWorld(pChr->GetPos(), Target);
		}
		pSelf->Teleport(pChr, Pos);
		pChr->ResetJumps();
		pChr->Unfreeze();
		pChr->SetVelocity(vec2(0, 0));
	}
}

void CGameContext::ConKill(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(!pPlayer || (pPlayer->m_LastKill && pPlayer->m_LastKill + pSelf->Server()->TickSpeed() * g_Config.m_SvKillDelay > pSelf->Server()->Tick()))
		return;

	pPlayer->m_LastKill = pSelf->Server()->Tick();
	pPlayer->KillCharacter(WEAPON_SELF);
}

void CGameContext::ConForcePause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Victim = pResult->GetVictim(0);
	int Seconds = 0;
	if(pResult->NumArguments() > 1)
		Seconds = std::clamp(pResult->GetInteger(1), 0, 360);

	CPlayer *pPlayer = pSelf->m_apPlayers[Victim];
	if(!pPlayer)
		return;

	pPlayer->ForcePause(Seconds);
}

void CGameContext::ConModerate(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	bool HadModerator = pSelf->PlayerModerating();

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	pPlayer->m_Moderating = !pPlayer->m_Moderating;

	if(!HadModerator && pPlayer->m_Moderating)
		pSelf->SendChat(-1, TEAM_ALL, "Server kick/spec votes will now be actively moderated.", 0);

	if(!pSelf->PlayerModerating())
		pSelf->SendChat(-1, TEAM_ALL, "Server kick/spec votes are no longer actively moderated.", 0);

	if(pPlayer->m_Moderating)
		pSelf->SendChatTarget(pResult->m_ClientId, "Active moderator mode enabled for you.");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "Active moderator mode disabled for you.");
}

void CGameContext::ConSetDDRTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;

	if(g_Config.m_SvTeam == SV_TEAM_FORBIDDEN || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "join",
			"Teams are disabled");
		return;
	}

	const int Target = pResult->GetVictim(0);
	CPlayer *pPlayer = pSelf->m_apPlayers[Target];
	if(!pPlayer)
		return;

	const int Team = pResult->GetInteger(1);
	if(!pController->Teams().IsValidTeamNumber(Team))
		return;

	CCharacter *pChr = pSelf->GetPlayerChar(Target);

	if((pSelf->GetDDRaceTeam(Target) && pController->Teams().GetDDRaceState(pPlayer) == ERaceState::STARTED) || (pChr && pController->Teams().IsPractice(pChr->Team())))
		pPlayer->KillCharacter(WEAPON_GAME);

	pController->Teams().SetForceCharacterTeam(Target, Team);
	pController->Teams().SetTeamLock(Team, true);
}

void CGameContext::ConUninvite(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;

	const int Target = pResult->GetVictim(0);
	if(!pSelf->m_apPlayers[Target])
		return;

	pController->Teams().SetClientInvited(pResult->GetInteger(1), Target, false);
}

void CGameContext::ConVoteNo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->ForceVote(false);
}

void CGameContext::ConDrySave(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pSelf->Server()->IsRconAuthedAdmin(pResult->m_ClientId))
		return;

	CSaveTeam SavedTeam;
	int Team = pSelf->GetDDRaceTeam(pResult->m_ClientId);
	ESaveResult Result = SavedTeam.Save(pSelf, Team, true);
	if(CSaveTeam::HandleSaveError(Result, pResult->m_ClientId, pSelf))
		return;

	char aTimestamp[32];
	str_timestamp(aTimestamp, sizeof(aTimestamp));
	const char *pSaveState = SavedTeam.GetString();
	if(!pSaveState)
	{
		pSelf->SendChatTarget(pResult->m_ClientId, "Your team is too large to save");
		return;
	}

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s_%s_%s.save", pSelf->Map()->BaseName(), aTimestamp, pSelf->Server()->GetAuthName(pResult->m_ClientId));
	IOHANDLE File = pSelf->Storage()->OpenFile(aBuf, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	io_write(File, pSaveState, str_length(pSaveState));
	io_close(File);
}

void CGameContext::ConReloadCensorlist(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ReadCensorList();
}

void CGameContext::ConDumpAntibot(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Antibot()->ConsoleCommand("dump");
}

void CGameContext::ConAntibot(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Antibot()->ConsoleCommand(pResult->GetString(0));
}

void CGameContext::ConDumpLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int LimitSecs = MAX_LOG_SECONDS;
	if(pResult->NumArguments() > 0)
		LimitSecs = pResult->GetInteger(0);

	if(LimitSecs < 0)
		return;

	int Iterator = pSelf->m_LatestLog;
	for(int i = 0; i < MAX_LOGS; i++)
	{
		CLog *pEntry = &pSelf->m_aLogs[Iterator];
		Iterator = (Iterator + 1) % MAX_LOGS;

		if(!pEntry->m_Timestamp)
			continue;

		int Seconds = (time_get() - pEntry->m_Timestamp) / time_freq();
		if(Seconds > LimitSecs)
			continue;

		char aBuf[sizeof(pEntry->m_aDescription) + 128];
		if(pEntry->m_FromServer)
			str_format(aBuf, sizeof(aBuf), "%s, %d seconds ago", pEntry->m_aDescription, Seconds);
		else
			str_format(aBuf, sizeof(aBuf), "%s, %d seconds ago < addr=<{%s}> name='%s' client=%d",
				pEntry->m_aDescription, Seconds, pEntry->m_aClientAddrStr, pEntry->m_aClientName, pEntry->m_ClientVersion);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "log", aBuf);
	}
}

void CGameContext::LogEvent(const char *Description, int ClientId)
{
	CLog *pNewEntry = &m_aLogs[m_LatestLog];
	m_LatestLog = (m_LatestLog + 1) % MAX_LOGS;

	pNewEntry->m_Timestamp = time_get();
	str_copy(pNewEntry->m_aDescription, Description);
	pNewEntry->m_FromServer = ClientId < 0;
	if(!pNewEntry->m_FromServer)
	{
		pNewEntry->m_ClientVersion = Server()->GetClientVersion(ClientId);
		str_copy(pNewEntry->m_aClientAddrStr, Server()->ClientAddrString(ClientId, false));
		str_copy(pNewEntry->m_aClientName, Server()->ClientName(ClientId));
	}
}

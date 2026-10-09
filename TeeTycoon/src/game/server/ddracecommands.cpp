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
#include <fstream>
#include <iostream>
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

void CGameContext::ConEventThread(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	sqlite3 *db = pSelf->db;
	const char *sqlStatement = nullptr;
	std::string strSql;
	char *errMessage = nullptr;
		CCharacter *pChr;
	CCharacter *pCWinner;
	CPlayer *pPlayer;
	std::string myText;
	std::ofstream eventStartFile;
	std::string msg;
	int TeleIn = 0, count = 0, TeleOut = 0, eventType = 0;
	bool eventEnd = false;
	bool IncTime = false;
	while(true)
	{
		std::ifstream EventTypeFile("EventType.txt");
		std::getline(EventTypeFile, myText);
		EventTypeFile.close();
		if(myText == "1")
		{
			eventType = 1; //survival.
		}
		else if(myText == "2")
		{
			eventType = 2; //race.
		}
		else if(myText == "3")
		{
			eventType = 3; //dm.
		}
		else if (myText == "4")
		{
			eventType = 4; //freeze race.
		}
		else if (myText == "5")
		{
			eventType = 5; //fng.
		}
		std::ifstream MyReadFile("StartingEvent.txt");
		std::getline(MyReadFile, myText);
		MyReadFile.close();
		if(myText == "1")
		{
			//broadcast the current amount of players in the survival(create more thread, with the same while.
			//std::string msg = "there are currently: " + playersCount + " players registered to the event";
			//pSelf->SendBroadcast(msg.c_str(), pResult->m_ClientId);
			//let the players 30 seconds to join.
			for (int i = 30; i > 0; i--)
			{
				for(int j = 0; j < pSelf->Server()->MaxClients(); j++)
				{
					if (pSelf->m_apPlayers[j])
					{
						count = pSelf->playersJoined.size();
						msg = "The event start in " + std::to_string(i) + " seconds! Type /join to enter the event. [" + std::to_string(count) + "/" + std::to_string(MAX_CLIENTS) + "]";
						pSelf->SendBroadcast(msg.c_str(), j);
					}
				}
				_sleep(1000);
			}
			//close the event registeration.
			eventStartFile.open("StartingEvent.txt");
			eventStartFile << "0";
			eventStartFile.close();
			//teleport all the registered players to the event area.
			if (count > 1)
			{
				for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
				{
					if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
					{
						if(eventType == 1)
						{
							//set the tele layer to the survival area tele (255).
							TeleIn = 254;
						}
						else if(eventType == 2)
						{
							//set the tele layer to the Race area tele (255).
							TeleIn = 253;
						}
						else if(eventType == 3)
						{
							//set the tele layer to the Race area tele (255).
							TeleIn = 252;
						}
						else if(eventType == 4)
						{
							TeleIn = 251;
						}
						else if(eventType == 5)
						{
							TeleIn = 250;
						}
						pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
						if(!pPlayer)
						{
							pSelf->ignorePlayers.push_back(*it);
						}
						else
						{
							pPlayer->playersInEvent = 0;
							eventEnd = false;
							pPlayer->hasJoined = false;
							pChr = pSelf->GetPlayerChar(*it);
							pChr->m_Race = false;
							if(eventType == 3)
							{
								pChr->m_dm = true;
								pChr->GiveWeapon(3, false);
								pChr->GiveWeapon(0, true);
								pChr->GiveWeapon(1, true);
								pChr->GiveWeapon(2, true);
								pChr->GiveWeapon(4, true);
								pChr->SetActiveWeapon(3);
							}
							else if(eventType == 5)
							{
								pChr->m_fng = true;
								pChr->GiveWeapon(4, false);
								pChr->GiveWeapon(0, false);
								pChr->GiveWeapon(1, true);
								pChr->GiveWeapon(2, true);
								pChr->GiveWeapon(3, true);
								pChr->SetActiveWeapon(4);
							}
							//now tele this player to the event area.
														TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(TeleIn - 1).size());
							pSelf->SetTeamInvite(pChr->GetPlayer()->GetCid(), 0);
							pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(TeleIn - 1)[TeleOut]);
							pChr->Freeze();
						}
					}
				}
				_sleep(3000);
				//now save the count variable for the prizes.
				for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); it++)
				{
					if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
					{
						//get the player's CPlayer class variable.
						pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
						if(!pPlayer)
						{
							pSelf->ignorePlayers.push_back(*it);
						}
						else
						{
							pChr = pSelf->GetPlayerChar(*it);
							if(pChr)
							{
								pPlayer->playersInEvent = count;
								pChr->Unfreeze();
							}
						}
					}
				}

				//let them 1 minutes for the event.
				for(int i = 60; i > 0; i--)
				{
					if (eventType == 1)
					{
						count = 0;
						for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
						{
							if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
							{
								//get the player's CPlayer class variable.
								pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
								if(!pPlayer)
								{
									pSelf->ignorePlayers.push_back(*it);
								}
								else
								{
									pChr = pPlayer->GetCharacter();
									if(pChr)
									{
										if(pChr->m_survival)
										{
											count++;
											msg = "Survive for: " + std::to_string(i) + " seconds to win!";
											pSelf->SendBroadcast(msg.c_str(), *it);
										}
									}
								}
							}
						}
					}
					else if(eventType == 2)
					{
						count = 0;
						for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
						{
							if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
							{
								//get the player's CPlayer class variable.
								pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
								if(!pPlayer)
								{
									pSelf->ignorePlayers.push_back(*it);
								}
								else
								{
									pChr = pSelf->GetPlayerChar(*it);
									if(pChr)
									{
										if(pChr->m_survival)
										{
											count++;
										}
										msg = "Race Event end in: " + std::to_string(i) + " seconds!";
										pSelf->SendBroadcast(msg.c_str(), *it);
										if(pChr->m_Race)
										{
											pCWinner = pChr;
											eventEnd = true;
											break;
										}
									}
								}
							}
						}
					}
					if(eventType == 3)
					{
						if (!IncTime)
						{
							i += 30;
							IncTime = true;
						}
						count = 0;
						for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
						{
							if (!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
							{
								//get the player's CPlayer class variable.
								pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
								if(!pPlayer)
								{
									pSelf->ignorePlayers.push_back(*it);
								}
								else
								{
									pChr = pSelf->GetPlayerChar(*it);
									if(pChr)
									{
										if(pChr->m_survival)
										{
											count++;
											msg = "Deathmatch event end in: " + std::to_string(i) + " seconds!";
											pSelf->SendBroadcast(msg.c_str(), *it);
										}
									}
								}
							}
						}
					}
					else if(eventType == 4)
					{
						count = 0;
						for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
						{
							if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
							{
								//get the player's CPlayer class variable.
								pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
								if(!pPlayer)
								{
									pSelf->ignorePlayers.push_back(*it);
								}
								else
								{
									pChr = pSelf->GetPlayerChar(*it);
									if(pChr)
									{
										if(pChr->m_survival)
										{
											count++;
										}
										msg = "FreezeRace event end in: " + std::to_string(i) + " seconds!";
										pSelf->SendBroadcast(msg.c_str(), *it);
										if(pChr->m_Race)
										{
											pCWinner = pChr;
											eventEnd = true;
											break;
										}
									}
								}
							}
						}
					}
					if(eventType == 5)
					{
						if(!IncTime)
						{
							i += 30;
							IncTime = true;
						}
						count = 0;
						for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
						{
							if(!(std::find(pSelf->ignorePlayers.begin(), pSelf->ignorePlayers.end(), *it) != pSelf->ignorePlayers.end()))
							{
								//get the player's CPlayer class variable.
								pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
								if(!pPlayer)
								{
									pSelf->ignorePlayers.push_back(*it);
								}
								else
								{
									pChr = pSelf->GetPlayerChar(*it);
									if(pChr)
									{
										if(pChr->m_survival)
										{
											count++;
											msg = "Fng event end in: " + std::to_string(i) + " seconds!";
											pSelf->SendBroadcast(msg.c_str(), *it);
										}
									}
								}
							}
						}
					}
					if (eventType == 1)
					{
						if(count <= 1)
						{
							for(auto it = pSelf->ignorePlayers.begin(); it != pSelf->ignorePlayers.end(); it++)
							{
								pSelf->playersJoined.erase(std::remove(pSelf->playersJoined.begin(), pSelf->playersJoined.end(), *it), pSelf->playersJoined.end());
							}
							break;
						}
						else
						{
							_sleep(1000);
						}
					}
					else if(eventType == 2 || eventType == 4)
					{
						if (eventEnd || count <= 1)
						{
							eventEnd = false;
							for(auto it = pSelf->ignorePlayers.begin(); it != pSelf->ignorePlayers.end(); it++)
							{
								pSelf->playersJoined.erase(std::remove(pSelf->playersJoined.begin(), pSelf->playersJoined.end(), *it), pSelf->playersJoined.end());
							}
							break;
						}
						else
						{
							_sleep(1000);
						}
					}
					else if (eventType == 3 || eventType == 5)
					{
						if(count <= 1)
						{
							IncTime = false;
							for(auto it = pSelf->ignorePlayers.begin(); it != pSelf->ignorePlayers.end(); it++)
							{
								pSelf->playersJoined.erase(std::remove(pSelf->playersJoined.begin(), pSelf->playersJoined.end(), *it), pSelf->playersJoined.end());
							}
							break;
						}
						else
						{
							_sleep(1000);
						}
					}
				}
				if (eventType == 3 && count > 1)
				{
					//kill everyone since the event time over and there is more than one survivor.
					for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
					{
						//get the player's CPlayer class variable.
						pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
						pChr = pSelf->GetPlayerChar(*it);
						if(pChr)
						{
							if (pChr->m_survival)
							{
								pChr->Die(0, -1);
							}
						}
					}
				}
				if (eventType == 2 || eventType == 4)
				{
					//kill all the players that aren't the winner.(in the future tp back to their pos before event, dont kill).
					for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
					{
						//get the player's CPlayer class variable.
						pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
						pChr = pSelf->GetPlayerChar(*it);
						if(pChr)
						{
							if(pChr != pCWinner && !(pChr->m_survival || pChr->m_Race))
							{
								pChr->Die(0, -1);
							}
						}
					}
				}
				//now mark every event as finished.
				for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
				{
					//get the player's CPlayer class variable.
					pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
					if(pPlayer->GetCharacter())
						pChr = pPlayer->GetCharacter();
					pPlayer->hasJoined = false;
					if(((pChr->m_survival && eventType != 4) || (pChr->m_survival && eventType != 2)) || pChr->m_Race)
					{
						if (eventType == 2 || eventType == 4)
						{
							if(pCWinner)
								pCWinner->m_Race = false;
							if (pChr)
							{
								pChr->m_Race = false;
								pChr->SetSolo(false);
							}
						}
						//give the money
						errMessage = nullptr;
						if(pPlayer->money + pPlayer->playersInEvent * 100 <= 2000000000)
							pPlayer->money += pPlayer->playersInEvent * 100;
						pPlayer->exp += pPlayer->playersInEvent * 100;
						strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", EXP=" + std::to_string(pPlayer->exp) + " WHERE NAME='" + pPlayer->username + "';";
						sqlStatement = strSql.c_str();
						sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
						std::string msgWon = pPlayer->username + ", You survived the event and got: " + std::to_string(pPlayer->playersInEvent * 100) + " money and exp!";
						pSelf->SendChatTarget(pPlayer->GetCid(), msgWon.c_str());
						errMessage = nullptr;
						if(pPlayer->exp >= pPlayer->neededExp)
						{
							pPlayer->exp -= pPlayer->neededExp;
							pPlayer->level++;
							pPlayer->m_Score = pPlayer->level;
							strSql = "UPDATE ACCOUNTS SET LEVEL=" + std::to_string(pPlayer->level) + " WHERE NAME='" + pPlayer->username + "';";
							sqlStatement = strSql.c_str();
							sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
							pPlayer->neededExp = 10000 * (pPlayer->level + 1) * 1.5;
							std::string msg = "Congratulations! You Leveled up! your current level is: " + std::to_string(pPlayer->level);
							pSelf->SendChatTarget(pPlayer->GetCid(), msg.c_str());
							pSelf->ConSetClan(pPlayer);
						}
						//tp back to spawn.
						pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
						if (eventType == 3)
						{
							pChr->m_dm = false;
							pChr->GiveWeapon(0, false);
							pChr->GiveWeapon(1, false);
						}
						else if (eventType == 5)
						{
							pChr->m_fng = false;
							pChr->GiveWeapon(0, false);
							pChr->GiveWeapon(1, false);
						}
												int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(0).size());
						pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(0)[TeleOut]);
					}
					else
					{
						pPlayer->playersInEvent = 0;
					}
				}
				//now after the event finished reset the ids file.
				pSelf->playersJoined.clear();
			}
			else
			{
				pSelf->SendBroadcast("were not enough players to start the event!", pResult->m_ClientId);
				//mark as finished.
				for(auto it = pSelf->playersJoined.begin(); it != pSelf->playersJoined.end(); ++it)
				{
					//get the player's CPlayer class variable.
					pPlayer = pSelf->m_apPlayers[*it]; //convert string to int.
					pPlayer->hasJoined = false;
				}
				pSelf->playersJoined.clear();
			}
		}
	}
}

void CGameContext::runThread(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->created)
	{
		std::thread tr(&CGameContext::ConEventThread, pResult, pUserData);
		tr.detach();
		pSelf->created = true;
	}
}

void CGameContext::ConStartEventSurvival(IConsole::IResult* pResult, void* pUserData)
{
	bool isAlreadyRunning = false;
	std::string myText;
	CGameContext *pSelf = (CGameContext *)pUserData;
	std::ifstream MyReadFile("StartingEvent.txt");
	std::getline(MyReadFile, myText);
	MyReadFile.close();
	if (myText == "1")
	{
		pSelf->SendBroadcast("an survival event is still running!", pResult->m_ClientId);
	}
	else
	{
		pSelf->runThread(pResult, pUserData);
		//mark event as starting.
		std::ofstream eventTypeFile;
		eventTypeFile.open("EventType.txt");
		eventTypeFile << "1";
		eventTypeFile.close();
		std::ofstream eventStartFile;
		eventStartFile.open("StartingEvent.txt");
		eventStartFile << "1";
		eventStartFile.close();
	}
}

void CGameContext::ConStartEventRace(IConsole::IResult *pResult, void *pUserData)
{
	bool isAlreadyRunning = false;
	std::string myText;
	CGameContext *pSelf = (CGameContext *)pUserData;
	std::ifstream MyReadFile("StartingEvent.txt");
	std::getline(MyReadFile, myText);
	MyReadFile.close();
	if(myText == "1")
	{
		pSelf->SendBroadcast("an race event is still running!", pResult->m_ClientId);
	}
	else
	{
		pSelf->runThread(pResult, pUserData);
		//mark event as starting.
		std::ofstream eventTypeFile;
		eventTypeFile.open("EventType.txt");
		eventTypeFile << "2";
		eventTypeFile.close();
		std::ofstream eventStartFile;
		eventStartFile.open("StartingEvent.txt");
		eventStartFile << "1";
		eventStartFile.close();
	}
}

void CGameContext::ConStartEventDm(IConsole::IResult *pResult, void *pUserData)
{
	bool isAlreadyRunning = false;
	std::string myText;
	CGameContext *pSelf = (CGameContext *)pUserData;
	std::ifstream MyReadFile("StartingEvent.txt");
	std::getline(MyReadFile, myText);
	MyReadFile.close();
	if(myText == "1")
	{
		pSelf->SendBroadcast("an DeathMatch event is still running!", pResult->m_ClientId);
	}
	else
	{
		pSelf->runThread(pResult, pUserData);
		//mark event as starting.
		std::ofstream eventTypeFile;
		eventTypeFile.open("EventType.txt");
		eventTypeFile << "3";
		eventTypeFile.close();
		std::ofstream eventStartFile;
		eventStartFile.open("StartingEvent.txt");
		eventStartFile << "1";
		eventStartFile.close();
	}
}

void CGameContext::ConStartEventFreezeRace(IConsole::IResult *pResult, void *pUserData)
{
	bool isAlreadyRunning = false;
	std::string myText;
	CGameContext *pSelf = (CGameContext *)pUserData;
	std::ifstream MyReadFile("StartingEvent.txt");
	std::getline(MyReadFile, myText);
	MyReadFile.close();
	if(myText == "1")
	{
		pSelf->SendBroadcast("an race event is still running!", pResult->m_ClientId);
	}
	else
	{
		pSelf->runThread(pResult, pUserData);
		//mark event as starting.
		std::ofstream eventTypeFile;
		eventTypeFile.open("EventType.txt");
		eventTypeFile << "4";
		eventTypeFile.close();
		std::ofstream eventStartFile;
		eventStartFile.open("StartingEvent.txt");
		eventStartFile << "1";
		eventStartFile.close();
	}
}

void CGameContext::ConStartEventFng(IConsole::IResult *pResult, void *pUserData)
{
	bool isAlreadyRunning = false;
	std::string myText;
	CGameContext *pSelf = (CGameContext *)pUserData;
	std::ifstream MyReadFile("StartingEvent.txt");
	std::getline(MyReadFile, myText);
	MyReadFile.close();
	if(myText == "1")
	{
		pSelf->SendBroadcast("an fng event is still running!", pResult->m_ClientId);
	}
	else
	{
		pSelf->runThread(pResult, pUserData);
		//mark event as starting.
		std::ofstream eventTypeFile;
		eventTypeFile.open("EventType.txt");
		eventTypeFile << "5";
		eventTypeFile.close();
		std::ofstream eventStartFile;
		eventStartFile.open("StartingEvent.txt");
		eventStartFile << "1";
		eventStartFile.close();
	}
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

void CGameContext::Teleport(CCharacter *pChr, vec2 Pos)
{
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
	if(!CheckClientId(pResult->m_ClientId))
		return;
	const bool HasSource = pResult->NumArguments() == 2;
	int Tele = HasSource ? pResult->GetVictim(0) : pResult->m_ClientId;
	int TeleTo = pResult->NumArguments() ? pResult->GetVictim(HasSource ? 1 : 0) : pResult->m_ClientId;
	int AuthLevel = pSelf->Server()->GetAuthedState(pResult->m_ClientId);

	if(Tele != pResult->m_ClientId && AuthLevel < g_Config.m_SvTeleOthersAuthLevel)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tele", "you aren't allowed to tele others");
		return;
	}

	CCharacter *pChr = pSelf->GetPlayerChar(Tele);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pChr && pPlayer && pSelf->GetPlayerChar(TeleTo))
	{
		// default to view pos when character is not available
		vec2 Pos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
		if(pResult->NumArguments() == 0 && !pPlayer->IsPaused() && pChr->IsAlive())
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

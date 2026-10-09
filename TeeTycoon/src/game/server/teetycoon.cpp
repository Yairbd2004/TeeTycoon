#include "gamecontext.h"
#include "bot.h"
#include "botengine.h"
#include "player.h"
#include "score.h"

#include <base/log.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/ddnet.h>
#include <game/server/teams.h>
#include <game/team_state.h>
#include <game/teamscore.h>
#include <game/version.h>

#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

void CGameContext::ConTeeTycoonMenuAction(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId] || pSelf->m_apPlayers[ClientId]->m_IsBot)
		return;

	const char *pAction = pResult->GetString(0);
	const char *pCommand = nullptr;
	if(str_comp(pAction, "event_join") == 0)
		pCommand = "event_join";
	else if(str_comp(pAction, "event_start_survival") == 0)
		pCommand = "tt_start_event_survival";
	else if(str_comp(pAction, "event_start_race") == 0)
		pCommand = "tt_start_event_race";
	else if(str_comp(pAction, "event_start_dm") == 0)
		pCommand = "tt_start_event_dm";
	else if(str_comp(pAction, "event_start_freezerace") == 0)
		pCommand = "tt_start_event_freezerace";
	else if(str_comp(pAction, "event_start_fng") == 0)
		pCommand = "tt_start_event_fng";
	else if(str_comp(pAction, "buy_farm") == 0)
		pCommand = "buy farm";
	else if(str_comp(pAction, "buy_house") == 0)
		pCommand = "buy house";
	else if(str_comp(pAction, "buy_vip") == 0)
		pCommand = "buy vip";
	else if(str_comp(pAction, "buy_rebirth") == 0)
		pCommand = "buy rebirth";
	else if(str_comp(pAction, "buy_pet") == 0)
		pCommand = "buy pet";
	else if(str_comp(pAction, "buy_rainbow") == 0)
		pCommand = "buy rainbow";
	else if(str_comp(pAction, "buy_bloody") == 0)
		pCommand = "buy bloody";
	else if(str_comp(pAction, "pet_spawn") == 0)
		pCommand = "pet_spawn";
	else if(str_comp(pAction, "pet_profile") == 0)
		pCommand = "pet_profile";
	else if(str_comp(pAction, "pet_follow") == 0)
		pCommand = "stay enable";
	else if(str_comp(pAction, "pet_stay") == 0)
		pCommand = "stay disable";
	else if(str_comp(pAction, "rainbow_off") == 0)
		pCommand = "unrainbow";
	else if(str_comp(pAction, "bloody_off") == 0)
		pCommand = "unbloody";
	else if(str_comp(pAction, "go_home") == 0)
		pCommand = "home";
	else if(str_comp(pAction, "go_spawn") == 0)
		pCommand = "spawn";

	if(pCommand)
	{
		if(str_startswith(pAction, "event_start_") && !pSelf->Server()->IsRconAuthedAdmin(ClientId))
		{
			pSelf->SendChatTarget(ClientId, "Only an authenticated server admin can start an event.");
			return;
		}
		if((str_comp(pAction, "buy_bloody") == 0 || str_comp(pAction, "bloody_off") == 0 || str_comp(pAction, "go_home") == 0 || str_comp(pAction, "go_spawn") == 0) && !pSelf->GetPlayerChar(ClientId))
		{
			pSelf->SendChatTarget(ClientId, "You need to be alive to use this menu action.");
			return;
		}
		const int OldFlagMask = pSelf->Console()->FlagMask();
		pSelf->Console()->SetFlagMask(CFGFLAG_CHAT | CFGFLAG_SERVER);
		pSelf->Console()->ExecuteLine(pCommand, ClientId, false);
		pSelf->Console()->SetFlagMask(OldFlagMask);
	}
}

void CGameContext::ConTeeTycoonMenuInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const int ClientId = pResult->m_ClientId;
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pSelf->m_apPlayers[ClientId] || pSelf->m_apPlayers[ClientId]->m_IsBot)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	const char *pCategory = pResult->GetString(0);
	if(str_comp(pCategory, "account") == 0)
	{
		if(pPlayer->id <= 0)
		{
			pSelf->SendChatTarget(ClientId, "Log in with /login or create an account with /register to view your private stats and shop prices.");
			return;
		}

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Your account: $%d | Level %d | XP %d/%d", pPlayer->money, pPlayer->level, pPlayer->exp, pPlayer->neededExp);
		pSelf->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "Upgrades: Farm %d, House %d, VIP %d, Rebirth %d", pPlayer->rank, pPlayer->house, pPlayer->vip, pPlayer->rebirth);
		pSelf->SendChatTarget(ClientId, aBuf);
		if(pPlayer->rank < 100)
			str_format(aBuf, sizeof(aBuf), "Next Farm upgrade: $%d", 10000 * (pPlayer->rank + 1));
		else
			str_copy(aBuf, "Farm: maximum level reached.");
		pSelf->SendChatTarget(ClientId, aBuf);
		if(pPlayer->house < 2)
			str_format(aBuf, sizeof(aBuf), "Next House upgrade: $%d", 1000000 * (pPlayer->house + 2));
		else
			str_copy(aBuf, "House: maximum level reached.");
		pSelf->SendChatTarget(ClientId, aBuf);
		if(pPlayer->vip < 5)
			str_format(aBuf, sizeof(aBuf), "Next VIP upgrade: $%d", 50000 * (pPlayer->vip + 1));
		else
			str_copy(aBuf, "VIP: maximum level reached.");
		pSelf->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "Rebirth: $%d (requires House 2); Pet: $1000000; Rainbow: $10000; Bloody: $50000", 1000000 * (pPlayer->rebirth + 1));
		pSelf->SendChatTarget(ClientId, aBuf);
	}
	else if(str_comp(pCategory, "events") == 0)
	{
		pSelf->SendChatTarget(ClientId, "Events: select 'Join the current event' while registration is open. This acts immediately and does not start a vote.");
	}
	else if(str_comp(pCategory, "pet") == 0)
	{
		pSelf->SendChatTarget(ClientId, "Pet: buy one in Shop, spawn it, view its private profile, or choose whether it follows you.");
	}
	else if(str_comp(pCategory, "effects") == 0)
	{
		pSelf->SendChatTarget(ClientId, "Effects: buy Rainbow or Bloody, then use the matching Off option to remove an active effect.");
	}
	else if(str_comp(pCategory, "travel") == 0)
	{
		pSelf->SendChatTarget(ClientId, "Travel: select Home to return to your house or Spawn to return to the public area.");
	}
}

void CGameContext::ConJoinEvent(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int eventType = 0;

	if (pPlayer->id > 0)
	{
		//check if the event is starting (remember to change the value to 0 when i start the event).
		std::ifstream MyReadFile("StartingEvent.txt");
		std::string myText;

		std::getline(MyReadFile, myText);
		MyReadFile.close();
		if(myText == "1")
		{
			if (!pPlayer->hasJoined)
			{
				//add pPlayer id to the joined vector (then when i start the event i will teleport all the registered players to the event area).
				pSelf->playersJoined.push_back(pResult->m_ClientId);
				std::ifstream EventTypeFile("EventType.txt");
				std::getline(EventTypeFile, myText);
				EventTypeFile.close();
				if(myText == "1") //Survival.
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You have been added to the survival event!");
				}
				else if(myText == "2") //Race.
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You have been added to the race event!");
				}
				else if(myText == "3") //dm.
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You have been added to the Deathmatch event!");
				}
				else if(myText == "4") //Freeze Race.
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You have been added to the Freeze Race event!");
				}
				else if(myText == "5") //Race.
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You have been added to the fng event!");
				}
				pPlayer->hasJoined = true;
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You have already registered to the event!");
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"There is no event right now / its already started!");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You are not logged in!");
	}
}

void CGameContext::ConStay(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	std::string arg = pResult->GetString(0);
	std::transform(arg.begin(), arg.end(), arg.begin(),
		[](unsigned char c) { return std::tolower(c); });
	if (pPlayer->m_ownBot)
	{
		if(arg == "enable")
		{
			pSelf->BotStay(pPlayer->botId, true);
		}
		else if (arg == "disable")
		{
			pSelf->BotStay(pPlayer->botId, false);
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"the options are enable / disable! try /stay enable or disable");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"you dont own/spawned a bot!");
	}
}

void CGameContext::ConBuy(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	std::string msg;
	int currMoney = 0;
	const char *sqlStatement;
	char *errMessage = nullptr;
	if (pPlayer->id > 0)
	{
		std::string item = pResult->GetString(0);
		std::transform(item.begin(), item.end(), item.begin(),
			[](unsigned char c) { return std::tolower(c); });
		//now find the item the user want to buy and if he got enough money and the item exist then remove the money and add it.
		if(item == "farm")
		{
			if(pPlayer->rank < 100)
			{
				if(pPlayer->money >= (10000 * (pPlayer->rank + 1)))
				{
					if(pResult->NumArguments() < 2)
					{
						pPlayer->money -= (10000 * (pPlayer->rank + 1));
						pPlayer->rank++;
						std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", RANK=" + std::to_string(pPlayer->rank) + " WHERE NAME='" + pPlayer->username + "';";
						sqlStatement = strSql.c_str();
						sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
						msg = "You bought farm[" + std::to_string(pPlayer->rank) + "]!";
						pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
							msg.c_str());
					}
					else
					{
						if(pResult->GetInteger(1) + pPlayer->rank <= 100)
						{
							currMoney = pPlayer->money;
							for(int i = 0; i < pResult->GetInteger(1); i++)
							{
								currMoney -= (10000 * (pPlayer->rank + 1 + i));
							}
							if(currMoney > 0)
							{
								pPlayer->money = currMoney;
								pPlayer->rank += pResult->GetInteger(1);
								std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", RANK=" + std::to_string(pPlayer->rank) + " WHERE NAME='" + pPlayer->username + "';";
								sqlStatement = strSql.c_str();
								sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
								msg = "You bought rank[" + std::to_string(pPlayer->rank) + "]!";
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									msg.c_str());
							}
							else
							{
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									"You dont have enough money to buy this amount!");
							}
						}
						else
						{
							pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
								"You cant buy this amount since max rank is 100!");
						}
					}
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your farm is already max level! (farm 100)");
			}
		}
		else if(item == "house")
		{
			if(pPlayer->house < 2)
			{
				if(pPlayer->money >= (1000000 * (pPlayer->house + 2)))
				{
					if (pResult->NumArguments() < 2)
					{
						pPlayer->money -= (1000000 * (pPlayer->house + 2));
						pPlayer->house++;
						std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", HOUSE=" + std::to_string(pPlayer->house) + " WHERE NAME='" + pPlayer->username + "';";
						sqlStatement = strSql.c_str();
						sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
						msg = "You bought house[" + std::to_string(pPlayer->house) + "]!";
						pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
							msg.c_str());
					}
					else
					{
						if (pResult->GetInteger(1) + pPlayer->house <= 2)
						{
							currMoney = pPlayer->money;
							for(int i = 0; i < pResult->GetInteger(1); i++)
							{
								currMoney -= (1000000 * (pPlayer->house + 2 + i));
							}
							if(currMoney > 0)
							{
								pPlayer->money = currMoney;
								pPlayer->house += pResult->GetInteger(1);
								std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", HOUSE=" + std::to_string(pPlayer->house) + " WHERE NAME='" + pPlayer->username + "';";
								sqlStatement = strSql.c_str();
								sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
								msg = "You bought house[" + std::to_string(pPlayer->house) + "]!";
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									msg.c_str());
							}
							else
							{
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									"You dont have enough money to buy this amount!");
							}
						}
						else
						{
							pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
								"You cant buy this amount since max house is 2!");
						}
					}
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your house is already max level! (house 2)");
			}
		}
		else if(item == "vip")
		{
			if(pPlayer->vip < 5)
			{
				if(pPlayer->money >= (50000 * (pPlayer->vip + 1)))
				{
					if(pResult->NumArguments() < 2)
					{
						pPlayer->money -= (50000 * (pPlayer->vip + 1));
						pPlayer->vip++;
						std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", VIP=" + std::to_string(pPlayer->vip) + " WHERE NAME='" + pPlayer->username + "';";
						sqlStatement = strSql.c_str();
						sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
						msg = "You bought vip[" + std::to_string(pPlayer->vip) + "]!";
						pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
							msg.c_str());
					}
					else
					{
						if(pResult->GetInteger(1) + pPlayer->vip <= 5)
						{
							currMoney = pPlayer->money;
							for(int i = 0; i < pResult->GetInteger(1); i++)
							{
								currMoney -= (50000 * (pPlayer->vip + 1 + i));
							}
							if(currMoney > 0)
							{
								pPlayer->money = currMoney;
								pPlayer->vip += pResult->GetInteger(1);
								std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + ", VIP=" + std::to_string(pPlayer->vip) + " WHERE NAME='" + pPlayer->username + "';";
								sqlStatement = strSql.c_str();
								sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
								msg = "You bought vip[" + std::to_string(pPlayer->vip) + "]!";
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									msg.c_str());
							}
							else
							{
								pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
									"You dont have enough money to buy this amount!");
							}
						}
						else
						{
							pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
								"You cant buy this amount since max vip is 5!");
						}
					}
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You are already at max vip! (vip 5)");
			}
		}
		else if(item == "rebirth")
		{
			if(pPlayer->money >= (1000000 * (pPlayer->rebirth + 1)))
			{
				if(pPlayer->house == 2)
				{
					pPlayer->money = 0;
					pPlayer->rank = 0;
					pPlayer->vip = 0;
					pPlayer->house = 0;
					pPlayer->rebirth++;
					std::string strSql = "UPDATE ACCOUNTS SET MONEY=0, HOUSE=0, RANK=0, VIP=0, REBIRTH=" + std::to_string(pPlayer->rebirth) + " WHERE NAME='" + pPlayer->username + "';";
					sqlStatement = strSql.c_str();
					sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
					msg = "You bought rebirth[" + std::to_string(pPlayer->rebirth) + "]!";
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						msg.c_str());
					ConSpawn(pResult, pUserData);
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Your house has to be max level! (house 2)");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You dont have enough money!");
			}
		}
		else if(item == "pet")
		{
			int isExist = 0;
			std::string statementStr = "SELECT * FROM BOTS WHERE OWNER_NAME='" + pPlayer->username + "';";
			sqlStatement = statementStr.c_str();
			int result = sqlite3_exec(pSelf->db, sqlStatement, callbackCheckExist, &isExist, &errMessage);
			if(!isExist)
			{
				if(pPlayer->money >= 1000000)
				{
					errMessage = nullptr;
					std::string statementStr = "INSERT INTO BOTS VALUES((SELECT seq FROM SQLITE_SEQUENCE WHERE name='BOTS') + 1 , '" + pPlayer->username + "' , 'Pet' , 0 , 0 , 10 , 10 , 0 , 0);";
					sqlStatement = statementStr.c_str();
					sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
					pPlayer->money -= 1000000;
					errMessage = nullptr;
					std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + " WHERE NAME='" + pPlayer->username + "';";
					sqlStatement = strSql.c_str();
					sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You bought a pet! u can use /pet_spawn now!");
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You already have a pet! (use /pet_spawn to spawn it).");
			}
		}
		else if(item == "rainbow")
		{
			if (pPlayer->m_Rainbow == RAINBOW_NONE)
			{
				if(pPlayer->money >= 10000)
				{
					pPlayer->money -= 10000;
					std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + " WHERE NAME='" + pPlayer->username + "';";
					sqlStatement = strSql.c_str();
					sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
					pPlayer->m_LastBodyR = pPlayer->TeeInfos().m_ColorBody;
					pPlayer->m_LastFeetR = pPlayer->TeeInfos().m_ColorFeet;
					pPlayer->m_Rainbow = RAINBOW_COLOR;
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You bought rainbow! (until death)");
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You already have rainbow!");
			}
		}
		else if(item == "bloody")
		{
			if (!pPlayer->GetCharacter()->m_Bloody_item)
			{
				if(pPlayer->money >= 50000)
				{
					pPlayer->money -= 50000;
					std::string strSql = "UPDATE ACCOUNTS SET MONEY=" + std::to_string(pPlayer->money) + " WHERE NAME='" + pPlayer->username + "';";
					sqlStatement = strSql.c_str();
					sqlite3_exec(pSelf->db, sqlStatement, nullptr, nullptr, &errMessage);
					pPlayer->GetCharacter()->m_Bloody_item = true;
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You bought bloody! (until death)");
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You dont have enough money!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You already have bloody!");
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"There is no item with that name!");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"In order to buy an item you has to login first!");
	}
}

void CGameContext::ConShop(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	std::string msg;
	if (pResult->NumArguments() > 0)
	{
		std::string item = pResult->GetString(0);
		std::transform(item.begin(), item.end(), item.begin(),
			[](unsigned char c) { return std::tolower(c); });
		if(item == "farm")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"you get +1 more money in farm tiles every time you upgrade this.");
		}
		else if(item == "house")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"You get bigger house + multi the amount of money that you get!");
		}
		else if(item == "vip")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"you get access to the vip room + multi the amount of money that you get!");
		}
		else if(item == "rebirth")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"reset everything you had, but you get x5 more money every time you buy it! You also has to be max house(2)");
		}
		else if(item == "pet")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"a bot that will follow u and protect you!");
		}
		else if(item == "rainbow")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"cool effect that change your skin colors fast");
		}
		else if(item == "bloody")
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"cool effect (like the death effect but inf)");
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"There is no item with that name!");
		}
	}
	else
	{
		if(pPlayer->id > 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"~Welcome to the Shop~");
			msg = "Farm[" + std::to_string(pPlayer->rank + 1) + "] - " + std::to_string(10000 * (pPlayer->rank + 1)) + "$";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "House[" + std::to_string(pPlayer->house + 1) + "] - " + std::to_string(1000000 * (pPlayer->house + 2)) + "$";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "VIP[" + std::to_string(pPlayer->vip + 1) + "] - " + std::to_string(50000 * (pPlayer->vip + 1)) + "$ (limited)";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "Rebirth[" + std::to_string(pPlayer->rebirth + 1) + "] - " + std::to_string(1000000 * (pPlayer->rebirth + 1)) + "$ + reset all stats expect of level (has to be house 2)";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "pet - " + std::to_string(1000000) + "$ (until disconnect)";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "rainbow - " + std::to_string(10000) + "$ (until death)";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			msg = "bloody - " + std::to_string(50000) + "$ (until death)";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				msg.c_str());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"*pls use /shop item to check the item info!");
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"~Welcome to the Shop~");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"Login to see prices");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"Farm");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"House");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"VIP");
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"Rebirth");
		}
	}
}

void CGameContext::ConLogout(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();

	if (pPlayer->id > 0)
	{
		pPlayer->id = 0;
		pPlayer->rank = 0;
		pPlayer->money = 0;
		pPlayer->exp = 0;
		pPlayer->level = -1;
		pPlayer->username = "";
		pPlayer->password = "";
		pPlayer->m_Score = -9999;
		pSelf->ConSetClan(pPlayer);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You have successfuly logged out!");
		//teleport back to the main map.
						int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(0).size());
		pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(0)[TeleOut]);
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You are not logged in!");
	}
}

void CGameContext::ConCommands(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"~TeeTycoon commands~");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/register <name> <password>");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/login <name> <password>");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/logout");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/spawn");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/home (if u bought home u can use /home 0 /home 1 /home 2)");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/shop (u can use /shop <item> to check the item detailes)");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/buy <item> (u can use /buy <item> <amount>");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/profile (u can use /profile <username>)");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/stay [enable|disable] (the pet follow u or not).");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/pet_profile");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/pet_spawn");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/invite [playerName] (invite player to your house (only if your'e in house)).");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/unrainbow (delete your rainbow).");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"/unbloody (delete your bloody).");
}

void CGameContext::ConPetSpawn(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	const char *sqlStatement;
	char *errMessage = nullptr;

	if(pPlayer->id > 0)
	{
		int isExist = 0;
		std::string statementStr = "SELECT * FROM BOTS WHERE OWNER_NAME='" + pPlayer->username + "';";
		sqlStatement = statementStr.c_str();
		int result = sqlite3_exec(pSelf->db, sqlStatement, callbackCheckExist, &isExist, &errMessage);
		if(isExist)
		{
			if (!pPlayer->m_ownBot)
			{
				int BotNumber = 0;
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					if(!pSelf->m_apPlayers[i])
						continue;
					if(pSelf->m_apPlayers[i]->m_IsBot)
						BotNumber++;
				}
				int LastFreeSlot = pSelf->Server()->MaxClients() - 1;
				for(; LastFreeSlot >= 0; LastFreeSlot--)
					if(!pSelf->m_apPlayers[LastFreeSlot])
						break;
				if(LastFreeSlot >= 0)
				{
					pPlayer->botId = LastFreeSlot;
					pPlayer->m_ownBot = true;
					pSelf->AddBot(LastFreeSlot, pPlayer->GetCid(), 0);
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"sorry but the server is full!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your pet is already in the server!");
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"You dont own a Pet!");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You aren't logged in!");
	}
}

void CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	//if there are 2 arguments then create the acc, otherwise send error message.
	if(pResult->NumArguments() > 1)
	{
		if (pPlayer->id > 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"Your'e already logged in!");
		}
		else
		{
			//database
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
				errMessage = nullptr;
				sqlStatement = "CREATE TABLE BOTS (ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL , OWNER_NAME TEXT NOT NULL , NAME TEXT NOT NULL , LEVEL INTEGER NOT NULL , EXP INTEGER NOT NULL , HEALTH INTEGER NOT NULL , ARMOR INTEGER NOT NULL , WEAPON INTEGER NOT NULL , KILLS INTEGER NOT NULL);"; //weapons: 0-hammer,1-gun,2-shotgun,3-grenade,4-rifle.
				res = sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
			}
			//check if the account is already exist.
			std::string username = pResult->GetString(0);
			std::string password = pResult->GetString(1);
			errMessage = nullptr;
			int isExist = 0;
			std::string statementStr = "SELECT * FROM ACCOUNTS WHERE NAME='" + username + "' AND PASSWORD='" + password + "';";
			sqlStatement = statementStr.c_str();
			int result = sqlite3_exec(db, sqlStatement, callbackCheckExist, &isExist, &errMessage);
			if(isExist)
			{
				//get id.
				int id = 0;
				errMessage = nullptr;
				statementStr = "SELECT ID FROM ACCOUNTS WHERE NAME='" + username + "';";
				sqlStatement = statementStr.c_str();
				result = sqlite3_exec(db, sqlStatement, callbackTele, &id, &errMessage);
				//check if someone else already connected to this account. (if anyone got pPlayer.id == this account id).
				bool isConnected = checkConnected(pResult, pUserData, id);
				if (!isConnected)
				{
					//send login success message.
					std::string msg = "Successfuly logged in as '" + username + "'";
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						msg.c_str());
					//get player info from database.
					int rank = 0, money = 0, level = 0, exp = 0, vip = 0, house = 0, rebirth = 0;
					statementStr = "SELECT RANK FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &rank, &errMessage);
					statementStr = "SELECT MONEY FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &money, &errMessage);
					statementStr = "SELECT LEVEL FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &level, &errMessage);
					statementStr = "SELECT EXP FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &exp, &errMessage);
					statementStr = "SELECT VIP FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &vip, &errMessage);
					statementStr = "SELECT HOUSE FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &house, &errMessage);
					statementStr = "SELECT REBIRTH FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					result = sqlite3_exec(db, sqlStatement, callbackTele, &rebirth, &errMessage);
					pPlayer->id = id;
					pPlayer->username = username;
					pPlayer->password = password;
					pPlayer->rank = rank;
					pPlayer->money = money;
					pPlayer->level = level;
					pPlayer->exp = exp;
					pPlayer->vip = vip;
					pPlayer->house = house;
					pPlayer->rebirth = rebirth;
					pPlayer->m_Score = pPlayer->level;
					pPlayer->neededExp = 10000 * (pPlayer->level + 1) * 1.5;
					pSelf->ConSetClan(pPlayer);
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Someone already connected to this account!");
				}
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"This account is not exist!");
			}
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"Invalid arguments! make sure you did it in the right format!");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"/login [username] [password]");
	}
}

void CGameContext::ConPetProfile(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	//get player info for case that the player check his own pet profile.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	std::string msg;
	bool found = false;
	if (pResult->NumArguments() > 0) //check others pet.
	{
		for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
		{
			if(!pSelf->m_apPlayers[i])
				continue;
			if(strcmp(pResult->GetString(0), pSelf->Server()->ClientName(i)) == 0)
			{
				pPlayer = pSelf->m_apPlayers[i];
				found = true;
				break;
			}
		}
		if (!found)
		{
			return;
		}
	}
	if (pPlayer->m_ownBot)
	{
		msg = std::string(pSelf->Server()->ClientName(pPlayer->GetCid())) + "'s pet profile:";
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "pet name - " + pSelf->m_apPlayers[pPlayer->botId]->username;
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "pet level - " + std::to_string(pSelf->m_apPlayers[pPlayer->botId]->level);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "pet exp - [" + std::to_string(pSelf->m_apPlayers[pPlayer->botId]->exp) + "/" + std::to_string(pSelf->m_apPlayers[pPlayer->botId]->neededExp) + "]";
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		std::string petWeapon;
		if (pSelf->m_apPlayers[pPlayer->botId]->weaponBot == 0)
		{
			petWeapon = "Hammer";
		}
		else if (pSelf->m_apPlayers[pPlayer->botId]->weaponBot == 1)
		{
			petWeapon = "Gun";
		}
		else if(pSelf->m_apPlayers[pPlayer->botId]->weaponBot == 2)
		{
			petWeapon = "Shotgun";
		}
		else if(pSelf->m_apPlayers[pPlayer->botId]->weaponBot == 3)
		{
			petWeapon = "Grenade";
		}
		else if(pSelf->m_apPlayers[pPlayer->botId]->weaponBot == 4)
		{
			petWeapon = "Rifle";
		}
		msg = "pet weapon - " + petWeapon;
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "pet Kills - " + std::to_string(pSelf->m_apPlayers[pPlayer->botId]->kills);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
	}
	else
	{
		if (found)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"This player dont own a pet / didn't spawned it yet!");
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"You dont own a pet / didn't spawned it yet!");
		}
	}
}

void CGameContext::ConProfile(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	//get player info for case that the player check his own profile.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	std::string msg;
	bool found = false;
	//if there is an argument so that means we try to check other player profile so get his CPlayer.
	if (pResult->NumArguments() > 0)
	{
		for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
		{
			if(!pSelf->m_apPlayers[i])
				continue;
			if(strcmp(pResult->GetString(0), pSelf->Server()->ClientName(i)) == 0)
			{
				pPlayer = pSelf->m_apPlayers[i];
				found = true;
				break;
			}
		}
		if (!found)
		{
			return;
		}
	}
	//check if this player is connected and if he is so print his profile.
	if (pPlayer->id > 0)
	{
		msg = pPlayer->username + "'s profile:";
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "Id: " + std::to_string(pPlayer->id);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		if (pPlayer->rank == 100)
		{
			msg = "Farm: " + std::to_string(pPlayer->rank) + " (Max)";
		}
		else
		{
			msg = "Farm: " + std::to_string(pPlayer->rank);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		if (pPlayer->money == 2000000000)
		{
			msg = "money: " + std::to_string(pPlayer->money) + " (Max)";
		}
		else
		{
			msg = "money: " + std::to_string(pPlayer->money);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "level: " + std::to_string(pPlayer->level);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "exp: " + std::to_string(pPlayer->exp);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		if (pPlayer->vip == 5)
		{
			msg = "vip: " + std::to_string(pPlayer->vip) + " (Max)";
		}
		else
		{
			msg = "vip: " + std::to_string(pPlayer->vip);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		if (pPlayer->house == 2)
		{
			msg = "house: " + std::to_string(pPlayer->house) + " (Max)";
		}
		else
		{
			msg = "house: " + std::to_string(pPlayer->house);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
		msg = "rebirths: " + std::to_string(pPlayer->rebirth);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			msg.c_str());
	}
	//means he wasnt logged in, so print message that tell us that.
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"This player not logged in!");
	}
}

void CGameContext::ConAccept(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if (pPlayer->invited)
	{
		pPlayer->accept = true;
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"No one invited u yet!");
	}
}

void CGameContext::ConUnRainbow(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pPlayer->m_Rainbow == RAINBOW_COLOR)
	{
		pPlayer->m_Rainbow = RAINBOW_NONE;
		pPlayer->TeeInfos().m_ColorBody = pPlayer->m_LastBodyR;
		pPlayer->TeeInfos().m_ColorFeet = pPlayer->m_LastFeetR;
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You deleted your rainbow!");
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You dont have rainbow activated!");
	}
}

void CGameContext::ConUnBloody(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(pPlayer->GetCharacter()->m_Bloody || pPlayer->GetCharacter()->m_Bloody_item)
	{
		pPlayer->GetCharacter()->m_Bloody = false;
		pPlayer->GetCharacter()->m_Bloody_item = false;
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You deleted your bloody!");
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You dont have bloody activated!");
	}
}

void CGameContext::ConDecline(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];

	if(pPlayer->invited)
	{
		pPlayer->decline = true;
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"No one invited u yet!");
	}
}

void CGameContext::ConSpawn(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;
	//get player info.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();
	//tp to spawn (totele num 1).
		pController->Teams().SetForceCharacterTeam(pPlayer->GetCid(), 0);
		int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(0).size());
	pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(0)[TeleOut]);
	if(pPlayer->m_ownBot)
	{
		pChr = pSelf->m_apPlayers[pPlayer->botId]->GetCharacter();
		if (pChr)
		{
			pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(0)[TeleOut]);
			pController->Teams().SetForceCharacterTeam(pPlayer->botId, 0);
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"wait... your pet hasn't spawned yet pls try again in a few seconds.");
		}
	}
}

void CGameContext::ConHome(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	//get player info.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();
	int TeleOut = 0;
	auto *pController = pSelf->m_pController;

	//check if user logged in first.
	if (pPlayer->id > 0)
	{
		if (pResult->NumArguments() > 0)
		{
			TeleOut = pResult->GetInteger(0);
			if (TeleOut > pPlayer->house)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You dont own this house yet!");
				TeleOut = pPlayer->house;
			}
		}
		else
		{
			TeleOut = pPlayer->house;
		}
		//the player account has ID var, so TeleIn will be the last ID var + 1. (database...)
		int TeleIn = 2;
						pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(TeleIn - 1)[TeleOut]);
		pController->Teams().SetForceCharacterTeam(pPlayer->GetCid(), pPlayer->GetCid() + 1);
		if (pPlayer->m_ownBot)
		{
			pChr = pSelf->m_apPlayers[pPlayer->botId]->GetCharacter();
			if (pChr)
			{
				pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(TeleIn - 1)[TeleOut]);
				pController->Teams().SetForceCharacterTeam(pSelf->m_apPlayers[pPlayer->botId]->GetCid(), pPlayer->GetCid() + 1);
			}
			else
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"wait... your pet hasn't spawned yet pls try again in a few seconds.");
			}
		}
		else
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"U dont own a bot");
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"Please login in first!");
	}
}

void CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;
	//get player info.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CCharacter *pChr = pPlayer->GetCharacter();
	bool invalidName = false;
	//if there are 2 arguments then create the acc, otherwise send error message.
	if(pResult->NumArguments() > 1)
	{
		if(pPlayer->id > 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
				"Your'e already logged in!");
		}
		else
		{
			std::string username = pResult->GetString(0);
			std::string password = pResult->GetString(1);
			for(int i = 0; i < username.length(); i++)
			{
				if(!(isalpha(username[i]) || isdigit(username[i])))
				{
					i = username.length();
					invalidName = true;
				}
			}
			if(invalidName)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Username must be alphabetic / digits! (a-z,A-Z,0-9)");
			}
			else if (username.length() < 3)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your username has to be at lease 3 characters long!");
			}
			else if (username.length() > 15)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your username is too long!");
			}
			else if (password.length() < 4)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"Your password has to be at lease 4 characters long!");
			}
			else
			{
				//database
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
					errMessage = nullptr;
					sqlStatement = "CREATE TABLE BOTS (ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL , OWNER_NAME TEXT NOT NULL , NAME TEXT NOT NULL , LEVEL INTEGER NOT NULL , EXP INTEGER NOT NULL , HEALTH INTEGER NOT NULL , ARMOR INTEGER NOT NULL , WEAPON INTEGER NOT NULL , KILLS INTEGER NOT NULL);"; //weapons: 0-hammer,1-gun,2-shotgun,3-grenade,4-rifle.
					res = sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
				}
				//check if the account is already exist.
				errMessage = nullptr;
				int isExist = 0;
				std::string statementStr = "SELECT * FROM ACCOUNTS WHERE NAME='" + username + "';";
				sqlStatement = statementStr.c_str();
				int result = sqlite3_exec(db, sqlStatement, callbackCheckExist, &isExist, &errMessage);
				if(!isExist)
				{
					//add the account
					std::string statementStr = "INSERT INTO ACCOUNTS VALUES((SELECT seq FROM SQLITE_SEQUENCE WHERE name='ACCOUNTS') + 1 , '" + username + "' , '" + password + "' , 0 , 0 , 1 , 0 , 0 , 0 , 0);";
					sqlStatement = statementStr.c_str();
					sqlite3_exec(db, sqlStatement, nullptr, nullptr, &errMessage);
					//the player account has ID var, so TeleIn will be the last ID var + 1. (database...)
					int id = 0;
					errMessage = nullptr;
					statementStr = "SELECT ID FROM ACCOUNTS WHERE NAME='" + username + "';";
					sqlStatement = statementStr.c_str();
					int result = sqlite3_exec(db, sqlStatement, callbackTele, &id, &errMessage);
					//set database info in player var.
					pPlayer->id = id; //TeleIn is equal to the player id.
					pPlayer->username = username;
					pPlayer->password = password;
					pPlayer->rank = 0;
					pPlayer->money = 0;
					pPlayer->level = 0;
					pPlayer->exp = 0;
					pPlayer->m_Score = 0;
					pSelf->ConSetClan(pPlayer);
					//tele 1 saved for the main map, so its id + 1 since we start from 2.
					//teleport the player to his tp number(to his own house).
															pController->Teams().SetForceCharacterTeam(pPlayer->GetCid(), pPlayer->GetCid() + 1);
					pSelf->Teleport(pChr, pSelf->Collision()->TeleOuts(1)[0]);
					//print the register information.
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Your Account has been created.");
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Welcome to your own house!");
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Earn money to buy a bigger house and stuff!");
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You can get Money by sitting on the farm tiles!");
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"Account with that name is already Exist!");
				}
			}
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"Invalid arguments! make sure you did it in the right format!");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"/register [username] [password]");
	}
}


void CGameContext::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"TeeTycoon is a mod that created by yair");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"Based on DDNet server code.");
}

void CGameContext::ConInviteHouse(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	auto *pController = pSelf->m_pController;
	CPlayer* pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	CPlayer* pPlayerInvited;
	std::string msg;
	if (pPlayer->id < 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"You need to login first!");
	}
	else
	{
		for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
		{
			if(!pSelf->m_apPlayers[i])
				continue;
			if(strcmp(pResult->GetString(0), pSelf->Server()->ClientName(i)) == 0)
			{
				if(i == pResult->m_ClientId)
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"You cant invite yourself!");
				}
				else
				{
					pPlayerInvited = pSelf->m_apPlayers[i];
					if(!pPlayerInvited->invited)
					{
						if(pPlayerInvited->inviteTick == 0)
						{
							pPlayerInvited->inviteTick = pSelf->Server()->Tick();
							msg = std::string(pSelf->Server()->ClientName(pPlayer->GetCid())) + " invite u to his house (/accept , /decline)";
							pSelf->SendChatTarget(pPlayerInvited->GetCid(), msg.c_str());
							pPlayerInvited->inviteID = pPlayer->GetCid();
							pPlayerInvited->invited = true;
							pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
								"invite sent!");
						}
					}
					else
					{
						pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
							"this player already invited by someone!");
					}
					i = pSelf->Server()->MaxClients();
				}
			}
		}
	}
}


void CGameContext::DeleteBot(int i)
{
	Server()->DelBot(i);
	if(m_apPlayers[i] && m_apPlayers[i]->m_IsBot)
	{
		dbg_msg("context", "Delete bot at slot: %d", i);
		delete m_apPlayers[i];
		m_apPlayers[i] = 0;
	}
}

void CGameContext::DeleteBot()
{
	for(int i = 63; i > 0; i--)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_IsBot)
		{
			if(!m_apPlayers[m_apPlayers[i]->m_pBot->owner])
			{
				Server()->DelBot(i);
				dbg_msg("context", "Delete bot at slot: %d", i);
				delete m_apPlayers[i];
				m_apPlayers[i] = 0;
			}
		}
	}
}

void CGameContext::BotStay(int id, bool isStay)
{
	m_apPlayers[id]->m_pBot->stay = isStay;
}

void CGameContext::OwnerHurt(int id, int enemyId)
{
	m_apPlayers[id]->m_pBot->enemyID = enemyId;
	m_apPlayers[id]->m_pBot->enemyTime = m_apPlayers[enemyId]->GetCharacter()->m_SpawnTick;
	m_apPlayers[id]->m_pBot->ownerAttacked = true;
}

void CGameContext::SetPetData(int id, std::string username)
{
	m_apPlayers[id]->petOwnerName = username;
	std::string name;
	int result, level = 0, exp = 0, health = 0, armor = 0, weapon = 0, kills = 0;
	std::string statementStr = "SELECT NAME FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackName, &name, &errMessage);
	m_apPlayers[id]->username = name;
	statementStr = "SELECT LEVEL FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &level, &errMessage);
	m_apPlayers[id]->level = level;
	statementStr = "SELECT EXP FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &exp, &errMessage);
	m_apPlayers[id]->exp = exp;
	statementStr = "SELECT HEALTH FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &health, &errMessage);
	m_apPlayers[id]->health = health;
	statementStr = "SELECT ARMOR FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &armor, &errMessage);
	m_apPlayers[id]->armor = armor;
	statementStr = "SELECT WEAPON FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &weapon, &errMessage);
	m_apPlayers[id]->weaponBot = weapon;
	statementStr = "SELECT KILLS FROM BOTS WHERE OWNER_NAME='" + username + "';";
	sqlStatement = statementStr.c_str();
	result = sqlite3_exec(db, sqlStatement, callbackTele, &kills, &errMessage);
	m_apPlayers[id]->kills = kills;
	m_apPlayers[id]->neededExp = 10000 * (m_apPlayers[id]->level + 1) * 1.5;
}

bool CGameContext::AddBot(int i, int ownerid, bool UseDropPlayer)
{
	const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(i);
	if(StartTeam == TEAM_SPECTATORS)
		return false;
	if(!m_pBotEngine->InitializeForBot(i))
		return false;
	if(Server()->NewBot(i) == 1)
		return false;
	dbg_msg("context", "Add a bot at slot: %d", i);
	if(!UseDropPlayer || !m_apPlayers[i])
		m_apPlayers[i] = new(i) CPlayer(this, (uint32_t)i, i, StartTeam);
	m_apPlayers[i]->m_IsBot = true;
	m_apPlayers[i]->m_pBot = new CBot(m_pBotEngine, m_apPlayers[i], ownerid);
	//set here all the bot data.
	SetPetData(i, m_apPlayers[ownerid]->username);
	Server()->SetClientName(i, m_apPlayers[i]->username.c_str());
	std::string lvlMsg = "Lv[" + std::to_string(m_apPlayers[i]->level) + "]";
	Server()->SetClientClan(i, lvlMsg.c_str());
	return true;
}

bool CGameContext::ReplacePlayerByBot(int ClientID)
{
	int BotNumber = 0;
	int PlayerCount = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!m_apPlayers[i])
			continue;
		if(m_apPlayers[i]->m_IsBot)
			BotNumber++;
		else
			PlayerCount++;
	}
	if(!PlayerCount || BotNumber >= g_Config.m_SvBotSlots)
		return false;
	return AddBot(ClientID, ClientID, true);
}


int CGameContext::callbackName(void* data, int argc, char** argv, char** azColName)
{
	std::string *c = (std::string *)data;
	*c = argv[0];

	return 0;
}

int CGameContext::callbackTele(void *data, int argc, char **argv, char **azColName)
{
	int *c = (int *)data;
	*c = atoi(argv[0]);

	return 0;
}


int CGameContext::callbackCheckExist(void *data, int argc, char **argv, char **azColName)
{
	int *flag = (int *)data;
	*flag = 1;
	return 1;
}

bool CGameContext::checkConnected(IConsole::IResult *pResult, void *pUserData, int id)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
	{
		if(!pSelf->m_apPlayers[i])
			continue;
		CPlayer *pPlayer = pSelf->m_apPlayers[i];
		if (pPlayer->id == id)
		{
			return true;
		}
	}
	return false;
}

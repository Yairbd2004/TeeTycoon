#include "gamecontext.h"
#include "bot.h"
#include "botengine.h"
#include "bot_ai/brain.h"
#include "player.h"
#include "pet_skill_storage.h"
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
#include <utility>

static bool ResolvePetIngameName(const char *pInput, std::string *pName)
{
	if(!pInput || !pInput[0] || !pName || str_length(pInput) >= MAX_NAME_LENGTH)
		return false;
	*pName = pInput;
	return true;
}

void CGameContext::RefreshTeeTycoonVoteMenu(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId] || m_apPlayers[ClientId]->m_IsBot)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	auto &vOptions = pPlayer->m_vTeeTycoonVoteOptions;
	vOptions.clear();
	auto Add = [&vOptions](std::string Description, const char *pCommand = "") {
		if(Description.size() >= VOTE_DESC_LENGTH)
			Description.resize(VOTE_DESC_LENGTH - 1);
		vOptions.push_back({std::move(Description), pCommand});
	};
	auto AddBack = [&Add]() {
		Add("Refresh this page", "page refresh");
		Add("< Back to main menu", "page main");
	};
	const bool LoggedIn = pPlayer->id > 0;
	using EPage = CPlayer::ETeeTycoonVotePage;
	switch(pPlayer->m_TeeTycoonVotePage)
	{
	case EPage::MAIN:
		// Event starts remain ordinary server-wide votes. All other rows below
		// are private menu actions and never start a ballot.
		for(const CVoteOptionServer *pOption = m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
		{
			if(str_startswith(pOption->m_aCommand, "tt_menu_"))
				continue;
			Add(pOption->m_aDescription, pOption->m_aCommand);
		}
		Add("Join the current event", "tt_menu_action event_join");
		Add("[ Shop and upgrades ]", "page shop");
		Add("[ My pet ]", "page pet");
		Add("[ Cosmetics ]", "page cosmetics");
		Add("[ Travel ]", "page travel");
		Add("[ Account and progress ]", "page account");
		break;
	case EPage::SHOP:
		if(LoggedIn)
		{
			Add("Money: $" + std::to_string(pPlayer->money));
			Add("Level: " + std::to_string(pPlayer->level) + " | XP: " + std::to_string(pPlayer->exp) + "/" + std::to_string(pPlayer->neededExp));
			Add(pPlayer->rank < 100 ? "Money tile Lv " + std::to_string(pPlayer->rank) + " -> " + std::to_string(pPlayer->rank + 1) + " ($" + std::to_string(10000LL * (pPlayer->rank + 1)) + ")" : "Money tile Lv 100 (MAX)", pPlayer->rank < 100 ? "tt_menu_action buy_farm" : "");
			Add(pPlayer->house < 2 ? "House Lv " + std::to_string(pPlayer->house) + " -> " + std::to_string(pPlayer->house + 1) + " ($" + std::to_string(1000000LL * (pPlayer->house + 2)) + ")" : "House Lv 2 (MAX)", pPlayer->house < 2 ? "tt_menu_action buy_house" : "");
			Add(pPlayer->vip < 5 ? "VIP Lv " + std::to_string(pPlayer->vip) + " -> " + std::to_string(pPlayer->vip + 1) + " ($" + std::to_string(50000LL * (pPlayer->vip + 1)) + ")" : "VIP Lv 5 (MAX)", pPlayer->vip < 5 ? "tt_menu_action buy_vip" : "");
			Add("Rebirth " + std::to_string(pPlayer->rebirth) + " -> " + std::to_string(pPlayer->rebirth + 1) + " ($" + std::to_string(1000000LL * (pPlayer->rebirth + 1)) + ")", "tt_menu_action buy_rebirth");
			Add("Rebirth requires House Lv 2; resets upgrades");
		}
		else
			Add("Log in with /login to view money and upgrades");
		AddBack();
		break;
	case EPage::PET:
	{
		if(!LoggedIn)
			Add("Log in with /login to view your pet");
		else
		{
			bool HasPet = false;
			bool PetDataAvailable = false;
			std::string Name;
			int Level = 0, Exp = 0, Health = 0, Armor = 0, Weapon = 0, Kills = 0, FreezeSeconds = 10;
			int HelpCount = 0, BlockCount = 0;
			int aSkills[NUM_PET_SKILLS] = {1, 1, 1, 1, 1};
			if(db)
			{
				sqlite3_stmt *pStatement = nullptr;
				if(sqlite3_prepare_v2(db, "SELECT NAME, LEVEL, EXP, HEALTH, ARMOR, WEAPON, KILLS, SKILL_RACE, SKILL_BLOCKER, SKILL_DEFENSE, SKILL_HELPER, SKILL_AIM, FREEZE_RESPAWN_SECONDS FROM BOTS WHERE OWNER_NAME = ? LIMIT 1", -1, &pStatement, nullptr) == SQLITE_OK)
				{
					sqlite3_bind_text(pStatement, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
					const int StepResult = sqlite3_step(pStatement);
					PetDataAvailable = StepResult == SQLITE_ROW || StepResult == SQLITE_DONE;
					if(StepResult == SQLITE_ROW)
					{
						HasPet = true;
						const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement, 0));
						Name = pName ? pName : "Pet";
						Level = sqlite3_column_int(pStatement, 1);
						Exp = sqlite3_column_int(pStatement, 2);
						Health = sqlite3_column_int(pStatement, 3);
						Armor = sqlite3_column_int(pStatement, 4);
						Weapon = sqlite3_column_int(pStatement, 5);
						Kills = sqlite3_column_int(pStatement, 6);
						for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
							aSkills[Skill] = PetSkillLevel(sqlite3_column_int(pStatement, 7 + Skill));
						FreezeSeconds = sqlite3_column_int(pStatement, 12);
					}
				}
				sqlite3_finalize(pStatement);
				pStatement = nullptr;
				if(sqlite3_prepare_v2(db, "SELECT RELATION, COUNT(*) FROM PET_RELATIONS WHERE OWNER_NAME = ? GROUP BY RELATION", -1, &pStatement, nullptr) == SQLITE_OK)
				{
					sqlite3_bind_text(pStatement, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
					while(sqlite3_step(pStatement) == SQLITE_ROW)
					{
						if(sqlite3_column_int(pStatement, 0) == 1)
							HelpCount = sqlite3_column_int(pStatement, 1);
						else if(sqlite3_column_int(pStatement, 0) == 2)
							BlockCount = sqlite3_column_int(pStatement, 1);
					}
				}
				sqlite3_finalize(pStatement);
			}
			CPlayer *pPet = pPlayer->m_ownBot && pPlayer->botId >= 0 && pPlayer->botId < MAX_CLIENTS ? m_apPlayers[pPlayer->botId] : nullptr;
			const bool Spawned = pPet && pPet->m_IsBot && pPet->m_pBot;
			if(Spawned)
			{
				HasPet = true;
				Name = pPet->username;
				Level = pPet->level;
				Exp = pPet->exp;
				Health = pPet->health;
				Armor = pPet->armor;
				Weapon = pPet->weaponBot;
				Kills = pPet->kills;
				for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
					aSkills[Skill] = PetSkillLevel(pPet->m_aPetSkills[Skill]);
				FreezeSeconds = pPet->m_pBot->m_FreezeRespawnSeconds;
			}
			if(!HasPet && !PetDataAvailable)
				Add("Pet data is unavailable right now");
			else if(!HasPet)
			{
				Add("Pet: none owned");
				Add("Buy a pet ($1000000)", "tt_menu_action buy_pet");
			}
			else
			{
				static const char *s_apWeapons[] = {"Hammer", "Gun", "Shotgun", "Grenade", "Rifle"};
				const char *pWeapon = Weapon >= 0 && Weapon < 5 ? s_apWeapons[Weapon] : "Unknown";
				Add("Pet: " + Name + (Spawned ? " (spawned)" : " (not spawned)"));
				Add("Level: " + std::to_string(Level) + " | XP: " + std::to_string(Exp) + "/" + std::to_string(15000LL * (Level + 1)));
				Add("Health: " + std::to_string(Health) + " | Armor: " + std::to_string(Armor));
				Add(std::string("Weapon: ") + pWeapon + " | Kills: " + std::to_string(Kills));
				int Total = 0;
				for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
					Total += aSkills[Skill];
				Add("Overall skill rating: " + std::to_string(Total) + "/50 | Money: $" + std::to_string(pPlayer->money));
				Add("Targets: " + std::to_string(HelpCount) + " help, " + std::to_string(BlockCount) + " block");
				Add("Show pet target lists", "tt_menu_action pet_relations");
				Add("Use /pet_relation help|block|neutral <name>");
				Add(FreezeSeconds == 0 ? "Freeze respawn: disabled" : "Freeze respawn: " + std::to_string(FreezeSeconds) + " sec");
				Add("Freeze timeout: off", "tt_menu_action pet_freeze_0");
				Add("Freeze timeout: 10 sec", "tt_menu_action pet_freeze_10");
				Add("Freeze timeout: 30 sec", "tt_menu_action pet_freeze_30");
				Add("Custom: /pet_freeze_timeout <0-120>");
				for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
				{
					std::string Label = std::string(PET_SKILL_NAMES[Skill]) + " Lv " + std::to_string(aSkills[Skill]);
					if(aSkills[Skill] < PET_SKILL_MAX_LEVEL)
					{
						Label += " -> " + std::to_string(aSkills[Skill] + 1) + " ($" + std::to_string(PetSkillUpgradeCost(aSkills[Skill])) + ")";
						Add(Label, (std::string("tt_menu_action pet_upgrade_") + PET_SKILL_KEYS[Skill]).c_str());
					}
					else
						Add(Label + " (MAX)");
				}
				if(Spawned)
				{
					const bool Stay = pPet->m_pBot->stay;
					Add(std::string("Follow me ") + (Stay ? "(OFF)" : "(ON)"), "tt_menu_action pet_follow");
					Add(std::string("Stay here ") + (Stay ? "(ON)" : "(OFF)"), "tt_menu_action pet_stay");
					Add("View full pet profile", "tt_menu_action pet_profile");
				}
				else
					Add("Spawn my pet", "tt_menu_action pet_spawn");
			}
		}
		AddBack();
		break;
	}
	case EPage::COSMETICS:
	{
		const CCharacter *pCharacter = pPlayer->GetCharacter();
		const bool Rainbow = pPlayer->m_Rainbow != RAINBOW_NONE;
		const bool Bloody = pCharacter && (pCharacter->m_Bloody || pCharacter->m_Bloody_item);
		Add(std::string("Rainbow: ") + (Rainbow ? "ON" : "OFF") + " | Bloody: " + (Bloody ? "ON" : "OFF"));
		if(LoggedIn)
			Add("Money: $" + std::to_string(pPlayer->money));
		Add("Enable Rainbow ($10000)", "tt_menu_action buy_rainbow");
		Add("Turn Rainbow off", "tt_menu_action rainbow_off");
		Add("Enable Bloody ($50000)", "tt_menu_action buy_bloody");
		Add("Turn Bloody off", "tt_menu_action bloody_off");
		Add("Effects last until death or turning them off");
		AddBack();
		break;
	}
	case EPage::TRAVEL:
		Add(LoggedIn ? "House level: " + std::to_string(pPlayer->house) : "Log in to use your home");
		if(pPlayer->m_HouseVisitHost >= 0)
		{
			Add("Visiting another player's house");
			Add("Leave visited house", "tt_menu_action leave_house");
		}
		Add("Go to my home", "tt_menu_action go_home");
		Add("Go to spawn", "tt_menu_action go_spawn");
		AddBack();
		break;
	case EPage::ACCOUNT:
		if(LoggedIn)
		{
			Add("Account: " + pPlayer->username);
			Add("Level: " + std::to_string(pPlayer->level) + " | XP: " + std::to_string(pPlayer->exp) + "/" + std::to_string(pPlayer->neededExp));
			Add("Money: $" + std::to_string(pPlayer->money) + " | Rebirth: " + std::to_string(pPlayer->rebirth));
			Add("View full profile", "tt_menu_action profile");
		}
		else
			Add("Use /register or /login to save progress");
		AddBack();
		break;
	}

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientId);
	pPlayer->m_SendVoteIndex = 0;
	pPlayer->m_pLastSentVoteOption = nullptr;
}

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
	else if(str_comp(pAction, "pet_relations") == 0)
		pCommand = "pet_relations";
	else if(str_comp(pAction, "pet_freeze_0") == 0)
		pCommand = "pet_freeze_timeout 0";
	else if(str_comp(pAction, "pet_freeze_10") == 0)
		pCommand = "pet_freeze_timeout 10";
	else if(str_comp(pAction, "pet_freeze_30") == 0)
		pCommand = "pet_freeze_timeout 30";
	else if(str_comp(pAction, "pet_upgrade_race") == 0)
		pCommand = "pet_upgrade race";
	else if(str_comp(pAction, "pet_upgrade_blocker") == 0)
		pCommand = "pet_upgrade blocker";
	else if(str_comp(pAction, "pet_upgrade_defense") == 0)
		pCommand = "pet_upgrade defense";
	else if(str_comp(pAction, "pet_upgrade_helper") == 0)
		pCommand = "pet_upgrade helper";
	else if(str_comp(pAction, "pet_upgrade_aim") == 0)
		pCommand = "pet_upgrade aim";
	else if(str_comp(pAction, "pet_follow") == 0)
		pCommand = "stay disable";
	else if(str_comp(pAction, "pet_stay") == 0)
		pCommand = "stay enable";
	else if(str_comp(pAction, "profile") == 0)
		pCommand = "profile";
	else if(str_comp(pAction, "rainbow_off") == 0)
		pCommand = "unrainbow";
	else if(str_comp(pAction, "bloody_off") == 0)
		pCommand = "unbloody";
	else if(str_comp(pAction, "go_home") == 0)
		pCommand = "home";
	else if(str_comp(pAction, "go_spawn") == 0)
		pCommand = "spawn";
	else if(str_comp(pAction, "leave_house") == 0)
		pCommand = "leave_house";

	if(pCommand)
	{
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
		str_format(aBuf, sizeof(aBuf), "Money: $%d | Player level: %d | XP: %d/%d", pPlayer->money, pPlayer->level, pPlayer->exp, pPlayer->neededExp);
		pSelf->SendChatTarget(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "Progress: Money tile (Farm) level %d | House level %d | VIP level %d | Rebirth %d", pPlayer->rank, pPlayer->house, pPlayer->vip, pPlayer->rebirth);
		pSelf->SendChatTarget(ClientId, aBuf);
		if(pPlayer->rank < 100)
			str_format(aBuf, sizeof(aBuf), "Next Money tile level: $%d", 10000 * (pPlayer->rank + 1));
		else
			str_copy(aBuf, "Money tile: maximum level reached.");
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
		str_format(aBuf, sizeof(aBuf), "Next Rebirth: $%d (requires House level 2)", 1000000 * (pPlayer->rebirth + 1));
		pSelf->SendChatTarget(ClientId, aBuf);
		pSelf->SendChatTarget(ClientId, "Shop prices: Pet $1000000 | Rainbow $10000 | Bloody $50000");
	}
	else if(str_comp(pCategory, "events") == 0)
	{
		pSelf->SendChatTarget(ClientId, "Events: select 'Join the current event' while registration is open. This acts immediately and does not start a vote.");
	}
	else if(str_comp(pCategory, "pet") == 0)
	{
		if(pPlayer->id <= 0)
		{
			pSelf->SendChatTarget(ClientId, "Log in to check your pet ownership.");
			return;
		}
		if(!pSelf->db)
		{
			pSelf->SendChatTarget(ClientId, "The account database is unavailable; pet ownership couldn't be checked.");
			return;
		}
		sqlite3_stmt *pCheck = nullptr;
		int DbResult = sqlite3_prepare_v2(pSelf->db, "SELECT 1 FROM BOTS WHERE OWNER_NAME = ? LIMIT 1", -1, &pCheck, nullptr);
		if(DbResult == SQLITE_OK)
		{
			sqlite3_bind_text(pCheck, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
			DbResult = sqlite3_step(pCheck);
		}
		sqlite3_finalize(pCheck);
		if(DbResult == SQLITE_ROW)
			pSelf->SendChatTarget(ClientId, pPlayer->m_ownBot ? "Pet: owned and currently spawned. View your private pet details for its stats." : "Pet: owned but not spawned. Select Spawn my pet to bring it into the server.");
		else if(DbResult == SQLITE_DONE)
			pSelf->SendChatTarget(ClientId, "Pet: you don't own one yet. Select Buy a Pet to purchase one.");
		else
			pSelf->SendChatTarget(ClientId, "Couldn't check pet ownership in the account database.");
	}
	else if(str_comp(pCategory, "effects") == 0)
	{
		const bool BloodyActive = pPlayer->GetCharacter() && (pPlayer->GetCharacter()->m_Bloody || pPlayer->GetCharacter()->m_Bloody_item);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Effects active: Rainbow %s | Bloody %s", pPlayer->m_Rainbow == RAINBOW_NONE ? "Off" : "On", BloodyActive ? "On" : "Off");
		pSelf->SendChatTarget(ClientId, aBuf);
		pSelf->SendChatTarget(ClientId, "Rainbow costs $10000 and Bloody costs $50000. Effects last until death.");
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
	if(pPlayer->m_ownBot && pPlayer->botId >= 0 && pPlayer->botId < MAX_CLIENTS && pSelf->m_apPlayers[pPlayer->botId] && pSelf->m_apPlayers[pPlayer->botId]->m_IsBot && pSelf->m_apPlayers[pPlayer->botId]->m_pBot)
	{
		if(arg == "enable")
		{
			pSelf->BotStay(pPlayer->botId, true);
			pSelf->SendChatTarget(pResult->m_ClientId, "Your pet will stay here.");
			pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
		}
		else if (arg == "disable")
		{
			pSelf->BotStay(pPlayer->botId, false);
			pSelf->SendChatTarget(pResult->m_ClientId, "Your pet will follow you.");
			pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
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
		if(!pSelf->db)
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "The account database is unavailable; no purchase was made.");
			return;
		}
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
			if(!pSelf->db)
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "The account database is unavailable; your pet purchase was not charged.");
				return;
			}
			sqlite3_stmt *pCheck = nullptr;
			int DbResult = sqlite3_prepare_v2(pSelf->db, "SELECT 1 FROM BOTS WHERE OWNER_NAME = ? LIMIT 1", -1, &pCheck, nullptr);
			if(DbResult == SQLITE_OK)
			{
				sqlite3_bind_text(pCheck, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
				DbResult = sqlite3_step(pCheck);
			}
			const bool HasPet = DbResult == SQLITE_ROW;
			sqlite3_finalize(pCheck);
			if(DbResult != SQLITE_ROW && DbResult != SQLITE_DONE)
			{
				pSelf->SendChatTarget(pResult->m_ClientId, "Couldn't check pet ownership; your purchase was not charged.");
				return;
			}
			if(HasPet)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
					"You already have a pet! (use /pet_spawn to spawn it).");
				return;
			}
			if(pPlayer->money < 1000000)
			{
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "You dont have enough money!");
				return;
			}

			bool PurchaseSaved = sqlite3_exec(pSelf->db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK;
			sqlite3_stmt *pInsert = nullptr;
			if(PurchaseSaved)
				PurchaseSaved = sqlite3_prepare_v2(pSelf->db, "INSERT INTO BOTS (OWNER_NAME, NAME, LEVEL, EXP, HEALTH, ARMOR, WEAPON, KILLS) VALUES (?, 'Pet', 0, 0, 10, 10, 0, 0)", -1, &pInsert, nullptr) == SQLITE_OK;
			if(PurchaseSaved)
			{
				sqlite3_bind_text(pInsert, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
				PurchaseSaved = sqlite3_step(pInsert) == SQLITE_DONE;
			}
			sqlite3_finalize(pInsert);

			sqlite3_stmt *pUpdateMoney = nullptr;
			if(PurchaseSaved)
				PurchaseSaved = sqlite3_prepare_v2(pSelf->db, "UPDATE ACCOUNTS SET MONEY = ? WHERE ID = ?", -1, &pUpdateMoney, nullptr) == SQLITE_OK;
			if(PurchaseSaved)
			{
				sqlite3_bind_int(pUpdateMoney, 1, pPlayer->money - 1000000);
				sqlite3_bind_int(pUpdateMoney, 2, pPlayer->id);
				PurchaseSaved = sqlite3_step(pUpdateMoney) == SQLITE_DONE && sqlite3_changes(pSelf->db) == 1;
			}
			sqlite3_finalize(pUpdateMoney);
			if(PurchaseSaved)
				PurchaseSaved = sqlite3_exec(pSelf->db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
			if(!PurchaseSaved)
			{
				sqlite3_exec(pSelf->db, "ROLLBACK", nullptr, nullptr, nullptr);
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "The pet purchase couldn't be saved; your money was not changed.");
				return;
			}

			pPlayer->money -= 1000000;
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "You bought a pet! You can spawn it now.");
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
		if(pPlayer->m_HouseVisitHost >= 0)
			pSelf->EndHouseVisit(pResult->m_ClientId);
		pSelf->EndHouseVisitsOfHost(pResult->m_ClientId);
		if(pPlayer->m_ownBot)
			pSelf->DeleteBot(pPlayer->botId);
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
		pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
		//teleport back to the main map.
						int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(0).size());
		if(pChr && !pSelf->Collision()->TeleOuts(0).empty())
			pSelf->MovePlayerAndPet(pResult->m_ClientId, 0, pSelf->Collision()->TeleOuts(0)[TeleOut]);
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
		"/pet_upgrade [race|blocker|defense|helper|aim] (levels 1-10)");
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

	if(pPlayer->id > 0)
	{
		if(!pSelf->db)
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "The account database is unavailable; your pet can't be loaded right now.");
			return;
		}
		sqlite3_stmt *pCheck = nullptr;
		int DbResult = sqlite3_prepare_v2(pSelf->db, "SELECT 1 FROM BOTS WHERE OWNER_NAME = ? LIMIT 1", -1, &pCheck, nullptr);
		if(DbResult == SQLITE_OK)
		{
			sqlite3_bind_text(pCheck, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
			DbResult = sqlite3_step(pCheck);
		}
		const bool HasPet = DbResult == SQLITE_ROW;
		sqlite3_finalize(pCheck);
		if(DbResult != SQLITE_ROW && DbResult != SQLITE_DONE)
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "Couldn't check pet ownership in the account database.");
			return;
		}
		if(HasPet)
		{
			if (!pPlayer->m_ownBot)
			{
				const int BotId = pSelf->FindFreeBotId(true);
				if(BotId >= 0)
				{
					if(pSelf->AddBot(BotId, pPlayer->GetCid(), false, true))
					{
						pPlayer->botId = BotId;
						pPlayer->m_ownBot = true;
					}
					else
						pSelf->SendChatTarget(pResult->m_ClientId, "Your pet couldn't be spawned. Please try again.");
				}
				else
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
						"The server has no free internal pet IDs. Human connection slots are unaffected.");
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
					pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
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
		int Total = 0;
		for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
		{
			const int Level = PetSkillLevel(pSelf->m_apPlayers[pPlayer->botId]->m_aPetSkills[Skill]);
			Total += Level;
			msg = std::string(PET_SKILL_NAMES[Skill]) + " skill - " + std::to_string(Level) + "/10";
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", msg.c_str());
		}
		msg = "Overall skill rating - " + std::to_string(Total) + "/50";
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", msg.c_str());
	}
	else
	{
		if(pPlayer->id > 0 && pSelf->db)
		{
			sqlite3_stmt *pStatement = nullptr;
			if(sqlite3_prepare_v2(pSelf->db,
				"SELECT NAME, LEVEL, SKILL_RACE, SKILL_BLOCKER, SKILL_DEFENSE, SKILL_HELPER, SKILL_AIM FROM BOTS WHERE OWNER_NAME = ? LIMIT 1",
				-1, &pStatement, nullptr) == SQLITE_OK)
			{
				sqlite3_bind_text(pStatement, 1, pPlayer->username.c_str(), -1, SQLITE_TRANSIENT);
				if(sqlite3_step(pStatement) == SQLITE_ROW)
				{
					const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement, 0));
					msg = pPlayer->username + "'s pet: " + (pName ? pName : "Pet") + " (Lv " + std::to_string(sqlite3_column_int(pStatement, 1)) + ", not spawned)";
					pSelf->SendChatTarget(pResult->m_ClientId, msg.c_str());
					int Total = 0;
					for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
					{
						const int Level = PetSkillLevel(sqlite3_column_int(pStatement, 2 + Skill));
						Total += Level;
						msg = std::string(PET_SKILL_NAMES[Skill]) + " skill: " + std::to_string(Level) + "/10";
						pSelf->SendChatTarget(pResult->m_ClientId, msg.c_str());
					}
					msg = "Overall skill rating: " + std::to_string(Total) + "/50";
					pSelf->SendChatTarget(pResult->m_ClientId, msg.c_str());
					sqlite3_finalize(pStatement);
					return;
				}
			}
			sqlite3_finalize(pStatement);
		}
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
	const int HostId = pPlayer->inviteID;
	if(!pPlayer->invited || HostId < 0 || HostId >= pSelf->Server()->MaxClients() ||
		pSelf->Server()->Tick() - pPlayer->inviteTick > 1000 ||
		!pSelf->m_apPlayers[HostId] || !pSelf->m_apPlayers[HostId]->GetCharacter() ||
		pSelf->m_apPlayers[HostId]->m_HouseVisitHost >= 0 ||
		pSelf->GetDDRaceTeam(HostId) != HostId + 1 ||
		!pPlayer->GetCharacter() || pPlayer->m_HouseVisitHost >= 0)
	{
		pPlayer->invited = false;
		pPlayer->inviteID = -1;
		pSelf->SendChatTarget(pResult->m_ClientId, "The house invitation is no longer available.");
		return;
	}
	pPlayer->m_HouseReturnPos = pPlayer->GetCharacter()->m_Pos;
	pPlayer->m_HouseReturnTeam = pPlayer->GetCharacter()->Team();
	pPlayer->m_HouseVisitHost = HostId;
	pPlayer->invited = false;
	pPlayer->inviteID = -1;
	pSelf->MovePlayerAndPet(pResult->m_ClientId, HostId + 1, pSelf->m_apPlayers[HostId]->GetCharacter()->m_Pos);
	pSelf->SendChatTarget(pResult->m_ClientId, "You are visiting the house. Use /leave_house to return.");
	pSelf->SendChatTarget(HostId, "Your house invitation was accepted.");
	pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
}

void CGameContext::ConPetUpgrade(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	CPlayer *pOwner = ClientId >= 0 && ClientId < MAX_CLIENTS ? pSelf->m_apPlayers[ClientId] : nullptr;
	const int Skill = PetSkillFromKey(pResult->GetString(0));
	if(Skill < 0)
	{
		pSelf->SendChatTarget(ClientId, "Use /pet_upgrade race, blocker, defense, helper, or aim.");
		return;
	}
	if(!pOwner || pOwner->id <= 0 || !pSelf->db)
	{
		pSelf->SendChatTarget(ClientId, "Log in before upgrading a pet.");
		return;
	}
	if(sqlite3_exec(pSelf->db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
	{
		pSelf->SendChatTarget(ClientId, "The pet database is busy. Please try again.");
		return;
	}
	const std::string SelectSql = std::string("SELECT ") + PET_SKILL_COLUMNS[Skill] + " FROM BOTS WHERE OWNER_NAME = ? LIMIT 1";
	sqlite3_stmt *pStatement = nullptr;
	int Level = 0;
	bool Success = sqlite3_prepare_v2(pSelf->db, SelectSql.c_str(), -1, &pStatement, nullptr) == SQLITE_OK;
	if(Success)
	{
		sqlite3_bind_text(pStatement, 1, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		Success = sqlite3_step(pStatement) == SQLITE_ROW;
		if(Success)
			Level = sqlite3_column_int(pStatement, 0);
	}
	sqlite3_finalize(pStatement);
	if(!Success)
	{
		sqlite3_exec(pSelf->db, "ROLLBACK", nullptr, nullptr, nullptr);
		pSelf->SendChatTarget(ClientId, "You do not own a pet, or its data is unavailable.");
		return;
	}
	Level = PetSkillLevel(Level);
	if(Level >= PET_SKILL_MAX_LEVEL)
	{
		sqlite3_exec(pSelf->db, "ROLLBACK", nullptr, nullptr, nullptr);
		pSelf->SendChatTarget(ClientId, "That pet skill is already at level 10.");
		return;
	}
	const int Cost = PetSkillUpgradeCost(Level);
	const std::string UpdateSql = std::string("UPDATE BOTS SET ") + PET_SKILL_COLUMNS[Skill] + " = ? WHERE OWNER_NAME = ? AND " + PET_SKILL_COLUMNS[Skill] + " = ?";
	Success = sqlite3_prepare_v2(pSelf->db, UpdateSql.c_str(), -1, &pStatement, nullptr) == SQLITE_OK;
	if(Success)
	{
		sqlite3_bind_int(pStatement, 1, Level + 1);
		sqlite3_bind_text(pStatement, 2, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(pStatement, 3, Level);
		Success = sqlite3_step(pStatement) == SQLITE_DONE && sqlite3_changes(pSelf->db) == 1;
	}
	sqlite3_finalize(pStatement);
	pStatement = nullptr;
	if(Success)
		Success = sqlite3_prepare_v2(pSelf->db, "UPDATE ACCOUNTS SET MONEY = MONEY - ? WHERE ID = ? AND MONEY >= ?", -1, &pStatement, nullptr) == SQLITE_OK;
	if(Success)
	{
		sqlite3_bind_int(pStatement, 1, Cost);
		sqlite3_bind_int(pStatement, 2, pOwner->id);
		sqlite3_bind_int(pStatement, 3, Cost);
		Success = sqlite3_step(pStatement) == SQLITE_DONE && sqlite3_changes(pSelf->db) == 1;
	}
	sqlite3_finalize(pStatement);
	if(Success)
		Success = sqlite3_exec(pSelf->db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
	if(!Success)
	{
		sqlite3_exec(pSelf->db, "ROLLBACK", nullptr, nullptr, nullptr);
		pSelf->SendChatTarget(ClientId, "Upgrade failed. Check your money and try again; nothing was charged.");
		return;
	}
	pOwner->money -= Cost;
	if(pOwner->m_ownBot && pOwner->botId >= 0 && pOwner->botId < MAX_CLIENTS &&
		pSelf->m_apPlayers[pOwner->botId] && pSelf->m_apPlayers[pOwner->botId]->m_IsBot)
	{
		pSelf->m_apPlayers[pOwner->botId]->m_aPetSkills[Skill] = Level + 1;
		if(pSelf->m_apPlayers[pOwner->botId]->m_pBot)
			pSelf->m_apPlayers[pOwner->botId]->m_pBot->OnSkillUpgrade();
	}
	const std::string Message = std::string("Your pet's ") + PET_SKILL_NAMES[Skill] + " skill is now level " + std::to_string(Level + 1) + ".";
	pSelf->SendChatTarget(ClientId, Message.c_str());
	pSelf->RefreshTeeTycoonVoteMenu(ClientId);
}

void CGameContext::ConPetRelation(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	CPlayer *pOwner = ClientId >= 0 && ClientId < MAX_CLIENTS ? pSelf->m_apPlayers[ClientId] : nullptr;
	if(!pOwner || pOwner->id <= 0 || !pSelf->db)
	{
		pSelf->SendChatTarget(ClientId, "Log in before editing pet targets.");
		return;
	}
	const std::string Mode = pResult->GetString(0);
	const int Relation = Mode == "help" ? 1 : Mode == "block" ? 2 : Mode == "neutral" ? 0 : -1;
	if(Relation < 0)
	{
		pSelf->SendChatTarget(ClientId, "Use /pet_relation help, block, or neutral <in-game name>.");
		return;
	}
	std::string TargetName;
	if(!ResolvePetIngameName(pResult->GetString(1), &TargetName))
	{
		pSelf->SendChatTarget(ClientId, "Enter an in-game name of up to 15 bytes. Quote names with spaces.");
		return;
	}
	if(TargetName == pSelf->Server()->ClientName(ClientId))
	{
		pSelf->SendChatTarget(ClientId, "Your pet always helps you; your own relation cannot be changed.");
		return;
	}
	sqlite3_stmt *pStatement = nullptr;
	bool Saved = sqlite3_prepare_v2(pSelf->db,
		Relation == 0 ? "DELETE FROM PET_RELATIONS WHERE OWNER_NAME = ? AND TARGET_NAME = ?" :
			"INSERT OR REPLACE INTO PET_RELATIONS (OWNER_NAME, TARGET_NAME, RELATION) VALUES (?, ?, ?)",
		-1, &pStatement, nullptr) == SQLITE_OK;
	if(Saved)
	{
		sqlite3_bind_text(pStatement, 1, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pStatement, 2, TargetName.c_str(), -1, SQLITE_TRANSIENT);
		if(Relation != 0)
			sqlite3_bind_int(pStatement, 3, Relation);
		Saved = sqlite3_step(pStatement) == SQLITE_DONE;
	}
	sqlite3_finalize(pStatement);
	if(!Saved)
	{
		pSelf->SendChatTarget(ClientId, "The pet target change could not be saved.");
		return;
	}
	if(pOwner->m_ownBot && pOwner->botId >= 0 && pOwner->botId < MAX_CLIENTS &&
		pSelf->m_apPlayers[pOwner->botId] && pSelf->m_apPlayers[pOwner->botId]->m_pBot)
	{
		CBot *pBot = pSelf->m_apPlayers[pOwner->botId]->m_pBot;
		pBot->m_HelpNames.erase(TargetName);
		pBot->m_BlockNames.erase(TargetName);
		if(Relation == 1)
			pBot->m_HelpNames.insert(TargetName);
		else if(Relation == 2)
			pBot->m_BlockNames.insert(TargetName);
	}
	const std::string Message = "Pet relation for " + TargetName + ": " + Mode + ".";
	pSelf->SendChatTarget(ClientId, Message.c_str());
	pSelf->RefreshTeeTycoonVoteMenu(ClientId);
}

void CGameContext::ConPetRelations(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	CPlayer *pOwner = pSelf->m_apPlayers[ClientId];
	if(!pOwner || pOwner->id <= 0 || !pSelf->db)
	{
		pSelf->SendChatTarget(ClientId, "Log in to view pet targets.");
		return;
	}
	pSelf->SendChatTarget(ClientId, "Your pet always helps you. Other pet targets:");
	sqlite3_stmt *pStatement = nullptr;
	int Count = 0;
	if(sqlite3_prepare_v2(pSelf->db, "SELECT TARGET_NAME, RELATION FROM PET_RELATIONS WHERE OWNER_NAME = ? ORDER BY RELATION, TARGET_NAME", -1, &pStatement, nullptr) == SQLITE_OK)
	{
		sqlite3_bind_text(pStatement, 1, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		while(sqlite3_step(pStatement) == SQLITE_ROW)
		{
			const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement, 0));
			const std::string Message = std::string(sqlite3_column_int(pStatement, 1) == 1 ? "Help: " : "Block: ") + (pName ? pName : "?");
			pSelf->SendChatTarget(ClientId, Message.c_str());
			Count++;
		}
	}
	sqlite3_finalize(pStatement);
	if(Count == 0)
		pSelf->SendChatTarget(ClientId, "No other targets are listed.");
}

void CGameContext::ConPetFreezeTimeout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	CPlayer *pOwner = pSelf->m_apPlayers[ClientId];
	const int Seconds = pResult->GetInteger(0);
	if(Seconds < 0 || Seconds > 120)
	{
		pSelf->SendChatTarget(ClientId, "Choose 0 to disable respawning, or 1-120 seconds.");
		return;
	}
	if(!pOwner || pOwner->id <= 0 || !pSelf->db)
	{
		pSelf->SendChatTarget(ClientId, "Log in before changing pet freeze recovery.");
		return;
	}
	sqlite3_stmt *pStatement = nullptr;
	bool Saved = sqlite3_prepare_v2(pSelf->db,
		"UPDATE BOTS SET FREEZE_RESPAWN_SECONDS = ? WHERE OWNER_NAME = ?", -1, &pStatement, nullptr) == SQLITE_OK;
	if(Saved)
	{
		sqlite3_bind_int(pStatement, 1, Seconds);
		sqlite3_bind_text(pStatement, 2, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		Saved = sqlite3_step(pStatement) == SQLITE_DONE && sqlite3_changes(pSelf->db) == 1;
	}
	sqlite3_finalize(pStatement);
	if(!Saved)
	{
		pSelf->SendChatTarget(ClientId, "You need to own a pet, and the database must be available.");
		return;
	}
	if(pOwner->m_ownBot && pOwner->botId >= 0 && pOwner->botId < MAX_CLIENTS &&
		pSelf->m_apPlayers[pOwner->botId] && pSelf->m_apPlayers[pOwner->botId]->m_pBot)
		pSelf->m_apPlayers[pOwner->botId]->m_pBot->m_FreezeRespawnSeconds = Seconds;
	const std::string Message = Seconds == 0 ? "Your pet will remain frozen until rescued." :
		"Your pet will respawn after " + std::to_string(Seconds) + " seconds of continuous freeze.";
	pSelf->SendChatTarget(ClientId, Message.c_str());
	pSelf->RefreshTeeTycoonVoteMenu(ClientId);
}

void CGameContext::MovePlayerAndPet(int ClientId, int Team, vec2 Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId])
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	m_pController->Teams().SetForceCharacterTeam(ClientId, Team);
	if(CCharacter *pChr = pPlayer->GetCharacter())
	{
		Teleport(pChr, Pos);
	}
	if(pPlayer->m_ownBot && pPlayer->botId >= 0 && pPlayer->botId < MAX_CLIENTS &&
		m_apPlayers[pPlayer->botId] && m_apPlayers[pPlayer->botId]->m_IsBot &&
		m_apPlayers[pPlayer->botId]->m_pBot && m_apPlayers[pPlayer->botId]->m_pBot->owner == ClientId)
	{
		if(CCharacter *pPet = m_apPlayers[pPlayer->botId]->GetCharacter())
		{
			m_pController->Teams().SetForceCharacterTeam(pPlayer->botId, Team);
			if(!IsBotFrozen(pPet))
				Teleport(pPet, Pos);
		}
	}
}

void CGameContext::EndHouseVisit(int ClientId)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients() || !m_apPlayers[ClientId])
		return;
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(pPlayer->m_HouseVisitHost < 0)
		return;
	const vec2 ReturnPos = pPlayer->m_HouseReturnPos;
	const int ReturnTeam = pPlayer->m_HouseReturnTeam;
	pPlayer->m_HouseVisitHost = -1;
	pPlayer->m_HouseReturnPending = !pPlayer->GetCharacter();
	MovePlayerAndPet(ClientId, ReturnTeam, ReturnPos);
	SendChatTarget(ClientId, "You returned to where you were before the house visit.");
	RefreshTeeTycoonVoteMenu(ClientId);
}

void CGameContext::EndHouseVisitsOfHost(int HostId)
{
	for(int ClientId = 0; ClientId < Server()->MaxClients(); ClientId++)
		if(m_apPlayers[ClientId] && m_apPlayers[ClientId]->m_HouseVisitHost == HostId)
			EndHouseVisit(ClientId);
}

void CGameContext::ConLeaveHouse(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || pPlayer->m_HouseVisitHost < 0)
		pSelf->SendChatTarget(pResult->m_ClientId, "You are not visiting a house.");
	else if(!pPlayer->GetCharacter())
		pSelf->SendChatTarget(pResult->m_ClientId, "Respawn before leaving the house.");
	else
		pSelf->EndHouseVisit(pResult->m_ClientId);
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
		const int HostId = pPlayer->inviteID;
		pPlayer->invited = false;
		pPlayer->inviteID = -1;
		pSelf->SendChatTarget(pResult->m_ClientId, "House invitation declined.");
		if(HostId >= 0 && HostId < pSelf->Server()->MaxClients() && pSelf->m_apPlayers[HostId])
			pSelf->SendChatTarget(HostId, "Your house invitation was declined.");
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
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer || !pPlayer->GetCharacter() || pSelf->Collision()->TeleOuts(0).empty())
		return;
	if(pPlayer->m_HouseVisitHost >= 0)
		pSelf->EndHouseVisit(pResult->m_ClientId);
	pSelf->EndHouseVisitsOfHost(pResult->m_ClientId);
	const int TeleOut = pSelf->m_World.m_Core.RandomOr0(pSelf->Collision()->TeleOuts(0).size());
	pSelf->MovePlayerAndPet(pResult->m_ClientId, 0, pSelf->Collision()->TeleOuts(0)[TeleOut]);
}

void CGameContext::ConHome(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	//get player info.
	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	int TeleOut = 0;
	if(!pPlayer || !pPlayer->GetCharacter())
		return;

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
		if(TeleOut < 0 || TeleOut >= (int)pSelf->Collision()->TeleOuts(1).size())
		{
			pSelf->SendChatTarget(pResult->m_ClientId, "This house entrance is unavailable.");
			return;
		}
		if(pPlayer->m_HouseVisitHost >= 0)
			pSelf->EndHouseVisit(pResult->m_ClientId);
		pSelf->EndHouseVisitsOfHost(pResult->m_ClientId);
		pSelf->MovePlayerAndPet(pResult->m_ClientId, pResult->m_ClientId + 1, pSelf->Collision()->TeleOuts(1)[TeleOut]);
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
					pSelf->RefreshTeeTycoonVoteMenu(pResult->m_ClientId);
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
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer || pPlayer->id <= 0)
	{
		pSelf->SendChatTarget(ClientId, "Log in before inviting a player.");
		return;
	}
	if(!pPlayer->GetCharacter() || pPlayer->m_HouseVisitHost >= 0 ||
		pSelf->GetDDRaceTeam(ClientId) != ClientId + 1)
	{
		pSelf->SendChatTarget(ClientId, "Go to your own house before inviting someone.");
		return;
	}
	for(int OtherId = 0; OtherId < pSelf->Server()->MaxClients(); OtherId++)
	{
		CPlayer *pGuest = pSelf->m_apPlayers[OtherId];
		if(!pGuest || pGuest->m_IsBot || str_comp(pResult->GetString(0), pSelf->Server()->ClientName(OtherId)) != 0)
			continue;
		if(OtherId == ClientId)
			pSelf->SendChatTarget(ClientId, "You cannot invite yourself.");
		else if(pGuest->m_HouseVisitHost >= 0)
			pSelf->SendChatTarget(ClientId, "That player is already visiting a house.");
		else if(pGuest->invited && pSelf->Server()->Tick() - pGuest->inviteTick <= 1000)
			pSelf->SendChatTarget(ClientId, "That player already has a house invitation.");
		else
		{
			pGuest->inviteTick = pSelf->Server()->Tick();
			pGuest->inviteID = ClientId;
			pGuest->invited = true;
			const std::string Message = std::string(pSelf->Server()->ClientName(ClientId)) + " invited you to their house. Use /accept or /decline.";
			pSelf->SendChatTarget(OtherId, Message.c_str());
			pSelf->SendChatTarget(ClientId, "House invitation sent.");
		}
		return;
	}
	pSelf->SendChatTarget(ClientId, "That player is not online.");
}


void CGameContext::DeleteBot(int i)
{
	if(i < 0 || i >= MAX_CLIENTS || !m_apPlayers[i] || !m_apPlayers[i]->m_IsBot)
		return;
	const int Owner = m_apPlayers[i]->m_pBot ? m_apPlayers[i]->m_pBot->owner : -1;
	if(Owner >= 0 && Owner < MAX_CLIENTS && m_apPlayers[Owner] && m_apPlayers[Owner]->botId == i)
	{
		m_apPlayers[Owner]->m_ownBot = false;
		m_apPlayers[Owner]->botId = -1;
		RefreshTeeTycoonVoteMenu(Owner);
	}
	Server()->DelBot(i);
	dbg_msg("context", "Delete bot at internal ID: %d", i);
	delete m_apPlayers[i];
	m_apPlayers[i] = nullptr;
}

void CGameContext::DeleteBot()
{
	for(int i = MAX_CLIENTS - 1; i >= 0; i--)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_IsBot && m_apPlayers[i]->m_pBot)
		{
			const int Owner = m_apPlayers[i]->m_pBot->owner;
			if(Owner >= 0 && (Owner >= MAX_CLIENTS || !m_apPlayers[Owner] || m_apPlayers[Owner]->m_IsBot))
				DeleteBot(i);
		}
	}
}

void CGameContext::BotStay(int id, bool isStay)
{
	m_apPlayers[id]->m_pBot->stay = isStay;
}

void CGameContext::OwnerHurt(int id, int enemyId)
{
	if(id < 0 || id >= MAX_CLIENTS || enemyId < 0 || enemyId >= MAX_CLIENTS ||
		!m_apPlayers[id] || !m_apPlayers[id]->m_IsBot || !m_apPlayers[id]->m_pBot ||
		!m_apPlayers[enemyId] || !m_apPlayers[enemyId]->GetCharacter())
		return;
	m_apPlayers[id]->m_pBot->enemyID = enemyId;
	m_apPlayers[id]->m_pBot->enemyTime = m_apPlayers[enemyId]->GetCharacter()->m_SpawnTick;
	m_apPlayers[id]->m_pBot->ownerAttacked = true;
	m_apPlayers[id]->m_pBot->m_ThreatUntilTick = Server()->Tick() + 10 * Server()->TickSpeed();
}

void CGameContext::OnProtectedPlayerHurt(int VictimId, int EnemyId, bool DealtDamage)
{
	for(CPlayer *pPlayer : m_apPlayers)
		if(pPlayer && pPlayer->m_IsBot && pPlayer->m_pBot)
			pPlayer->m_pBot->NotifyProtectedPlayerHurt(VictimId, EnemyId, DealtDamage);
}

void CGameContext::AutoBlockPetAggressor(int OwnerId, int EnemyId)
{
	if(!g_Config.m_SvBotDamageMode || OwnerId < 0 || OwnerId >= MAX_CLIENTS ||
		EnemyId < 0 || EnemyId >= MAX_CLIENTS || OwnerId == EnemyId || !db)
		return;
	CPlayer *pOwner = m_apPlayers[OwnerId];
	CPlayer *pEnemy = m_apPlayers[EnemyId];
	if(!pOwner || pOwner->id <= 0 || !pEnemy || pEnemy->m_IsBot ||
		pOwner->botId < 0 || pOwner->botId >= MAX_CLIENTS ||
		!m_apPlayers[pOwner->botId] || !m_apPlayers[pOwner->botId]->m_pBot)
		return;
	CBot *pPet = m_apPlayers[pOwner->botId]->m_pBot;
	const std::string Name = Server()->ClientName(EnemyId);
	if(Name.empty() || pPet->m_BlockNames.contains(Name))
		return;
	sqlite3_stmt *pStatement = nullptr;
	bool Saved = sqlite3_prepare_v2(db,
		"INSERT OR REPLACE INTO PET_RELATIONS (OWNER_NAME, TARGET_NAME, RELATION) VALUES (?, ?, 2)",
		-1, &pStatement, nullptr) == SQLITE_OK;
	if(Saved)
	{
		sqlite3_bind_text(pStatement, 1, pOwner->username.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(pStatement, 2, Name.c_str(), -1, SQLITE_TRANSIENT);
		Saved = sqlite3_step(pStatement) == SQLITE_DONE;
	}
	sqlite3_finalize(pStatement);
	if(!Saved)
		return;
	pPet->m_HelpNames.erase(Name);
	pPet->m_BlockNames.insert(Name);
	const std::string Message = "Your pet added " + Name + " to its block list after damage.";
	SendChatTarget(OwnerId, Message.c_str());
}

bool CGameContext::IsBotFrozen(const CCharacter *pCharacter)
{
	if(!pCharacter)
		return false;
	if(pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen)
		return true;
	return IsCharacterOnFreezeTile(pCharacter->GetPos());
}

bool CGameContext::IsCharacterOnFreezeTile(vec2 Pos)
{
	return IsCharacterCenterOnFreezeTile(Pos) ||
		IsCharacterCenterOnFreezeTile(Pos + vec2(0, 14)) ||
		IsCharacterCenterOnFreezeTile(Pos + vec2(0, 24)) ||
		IsCharacterCenterOnFreezeTile(Pos + vec2(14, 0)) ||
		IsCharacterCenterOnFreezeTile(Pos - vec2(14, 0));
}

bool CGameContext::IsCharacterCenterOnFreezeTile(vec2 Pos)
{
	const int Index = Collision()->GetPureMapIndex(Pos);
	const auto IsFreezeTile = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};
	return IsFreezeTile(Collision()->GetTileIndex(Index)) ||
		IsFreezeTile(Collision()->GetFrontTileIndex(Index)) ||
		IsFreezeTile(Collision()->GetSwitchType(Index));
}

void CGameContext::UpdateFreezeTileStates()
{
	const int Now = Server()->Tick();
	for(int ClientId = 0; ClientId < Server()->MaxClients(); ClientId++)
	{
		SFreezeTileState &State = m_aFreezeTileState[ClientId];
		CCharacter *pCharacter = m_apPlayers[ClientId] ? m_apPlayers[ClientId]->GetCharacter() : nullptr;
		const bool Frozen = pCharacter && (pCharacter->m_FreezeTime > 0 ||
			pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen);
		if(!Frozen ||
			!IsCharacterCenterOnFreezeTile(pCharacter->GetPos()))
		{
			State = {};
			continue;
		}
		if(State.m_StartTick < 0 || State.m_SpawnTick != pCharacter->m_SpawnTick)
		{
			State.m_StartTick = Now;
			State.m_SpawnTick = pCharacter->m_SpawnTick;
		}
	}
}

bool CGameContext::IsPlayerFreezeLocked(int ClientId)
{
	if(ClientId < 0 || ClientId >= Server()->MaxClients() || !m_apPlayers[ClientId] ||
		!m_apPlayers[ClientId]->GetCharacter())
		return false;
	const CCharacter *pCharacter = m_apPlayers[ClientId]->GetCharacter();
	if((pCharacter->m_FreezeTime <= 0 && !pCharacter->Core()->m_DeepFrozen &&
			!pCharacter->Core()->m_LiveFrozen) ||
		!IsCharacterCenterOnFreezeTile(pCharacter->GetPos()))
		return false;
	const SFreezeTileState &State = m_aFreezeTileState[ClientId];
	return State.m_SpawnTick == pCharacter->m_SpawnTick &&
		bot_ai::FreezeTileLocked(Server()->Tick(), State.m_StartTick, Server()->TickSpeed());
}

void CGameContext::SetPetData(int id, std::string username)
{
	m_apPlayers[id]->petOwnerName = username;
	sqlite3_stmt *pStatement = nullptr;
	if(db && sqlite3_prepare_v2(db, "SELECT NAME, LEVEL, EXP, HEALTH, ARMOR, WEAPON, KILLS, SKILL_RACE, SKILL_BLOCKER, SKILL_DEFENSE, SKILL_HELPER, SKILL_AIM, FREEZE_RESPAWN_SECONDS FROM BOTS WHERE OWNER_NAME = ? LIMIT 1", -1, &pStatement, nullptr) == SQLITE_OK)
	{
		sqlite3_bind_text(pStatement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
		if(sqlite3_step(pStatement) == SQLITE_ROW)
		{
			const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(pStatement, 0));
			m_apPlayers[id]->username = pName ? pName : "Pet";
			m_apPlayers[id]->level = sqlite3_column_int(pStatement, 1);
			m_apPlayers[id]->exp = sqlite3_column_int(pStatement, 2);
			m_apPlayers[id]->health = sqlite3_column_int(pStatement, 3);
			m_apPlayers[id]->armor = sqlite3_column_int(pStatement, 4);
			m_apPlayers[id]->weaponBot = sqlite3_column_int(pStatement, 5);
			m_apPlayers[id]->kills = sqlite3_column_int(pStatement, 6);
			for(int Skill = 0; Skill < NUM_PET_SKILLS; Skill++)
				m_apPlayers[id]->m_aPetSkills[Skill] = PetSkillLevel(sqlite3_column_int(pStatement, 7 + Skill));
			m_apPlayers[id]->m_pBot->m_FreezeRespawnSeconds = std::clamp(sqlite3_column_int(pStatement, 12), 0, 120);
		}
	}
	sqlite3_finalize(pStatement);
	pStatement = nullptr;
	if(db && sqlite3_prepare_v2(db, "SELECT TARGET_NAME, RELATION FROM PET_RELATIONS WHERE OWNER_NAME = ?", -1, &pStatement, nullptr) == SQLITE_OK)
	{
		sqlite3_bind_text(pStatement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
		while(sqlite3_step(pStatement) == SQLITE_ROW)
		{
			const char *pTarget = reinterpret_cast<const char *>(sqlite3_column_text(pStatement, 0));
			if(!pTarget)
				continue;
			if(sqlite3_column_int(pStatement, 1) == 1)
				m_apPlayers[id]->m_pBot->m_HelpNames.insert(pTarget);
			else if(sqlite3_column_int(pStatement, 1) == 2)
				m_apPlayers[id]->m_pBot->m_BlockNames.insert(pTarget);
		}
	}
	sqlite3_finalize(pStatement);
	m_apPlayers[id]->neededExp = 10000 * (m_apPlayers[id]->level + 1) * 1.5;
}

int CGameContext::FindFreeBotId(bool Virtual) const
{
	const int MaxRealClients = Server()->MaxClients();
	if(Virtual)
	{
		for(int i = MaxRealClients; i < MAX_CLIENTS; i++)
			if(!m_apPlayers[i] && Server()->ClientSlotEmpty(i))
				return i;
	}
	else
	{
		for(int i = MaxRealClients - 1; i >= 0; i--)
			if(!m_apPlayers[i] && Server()->ClientSlotEmpty(i))
				return i;
	}
	return -1;
}

bool CGameContext::AddBot(int i, int ownerid, bool UseDropPlayer, bool Virtual)
{
	if(i < 0 || i >= MAX_CLIENTS || (Virtual ? i < Server()->MaxClients() : i >= Server()->MaxClients()) || (ownerid >= 0 && (ownerid >= MAX_CLIENTS || !m_apPlayers[ownerid])))
		return false;
	if((!UseDropPlayer && m_apPlayers[i]) || !Server()->ClientSlotEmpty(i))
		return false;
	const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(i);
	if(StartTeam == TEAM_SPECTATORS)
		return false;
	if(!m_pBotEngine->InitializeForBot(i))
		return false;
	if(Server()->NewBot(i, Virtual) == 1)
		return false;
	dbg_msg("context", "Add a %s bot at internal ID: %d", Virtual ? "virtual" : "slot-backed", i);
	if(!UseDropPlayer || !m_apPlayers[i])
		m_apPlayers[i] = new(i) CPlayer(this, (uint32_t)i, i, StartTeam);
	m_apPlayers[i]->m_IsBot = true;
	m_apPlayers[i]->m_IsBlocker = ownerid < 0;
	m_apPlayers[i]->m_pBot = new CBot(m_pBotEngine, m_apPlayers[i], ownerid);
	if(ownerid >= 0)
		SetPetData(i, m_apPlayers[ownerid]->username);
	else
	{
		m_apPlayers[i]->username = "Blocker";
		m_apPlayers[i]->level = 0;
		m_apPlayers[i]->neededExp = 15000;
		m_apPlayers[i]->health = 10;
		m_apPlayers[i]->armor = 10;
	}
	Server()->SetClientName(i, m_apPlayers[i]->username.c_str());
	std::string lvlMsg = ownerid < 0 ? (Virtual ? "Blocker virtual" : "Blocker slot") : "Lv[" + std::to_string(m_apPlayers[i]->level) + "]";
	Server()->SetClientClan(i, lvlMsg.c_str());
	return true;
}

void CGameContext::ConBlockerSlot(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int BotId = pSelf->FindFreeBotId(false);
	if(BotId < 0 || !pSelf->AddBot(BotId, -1, false, false))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "No free connection slot is available for a slot-backed blocker.");
		return;
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Spawned slot-backed blocker at ID %d.", BotId);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", aBuf);
}

void CGameContext::ConBlockerVirtual(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int BotId = pSelf->FindFreeBotId(true);
	if(BotId < 0 || !pSelf->AddBot(BotId, -1, false, true))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "No free internal ID is available for a virtual blocker.");
		return;
	}
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Spawned virtual blocker at internal ID %d.", BotId);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", aBuf);
}

void CGameContext::ConBlockerRemove(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int BotId = pResult->GetInteger(0);
	if(BotId < 0 || BotId >= MAX_CLIENTS || !pSelf->m_apPlayers[BotId] || !pSelf->m_apPlayers[BotId]->m_IsBlocker)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "That ID is not an active blocker.");
		return;
	}
	pSelf->DeleteBot(BotId);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Blocker removed.");
}

void CGameContext::ConBlockerList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->m_apPlayers[i] || !pSelf->m_apPlayers[i]->m_IsBlocker)
			continue;
		char aBuf[128];
		const CBot *pBot = pSelf->m_apPlayers[i]->m_pBot;
		str_format(aBuf, sizeof(aBuf), "Blocker %d: %s, %d protected, freeze timeout %d sec", i,
			i >= pSelf->Server()->MaxClients() ? "virtual" : "slot-backed",
			pBot ? static_cast<int>(pBot->m_HelpNames.size()) : 0,
			pBot ? pBot->m_FreezeRespawnSeconds : 10);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", aBuf);
		Count++;
	}
	if(!Count)
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "No blockers are active.");
}

void CGameContext::ConBlockerWhitelist(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int BotId = pResult->GetInteger(0);
	if(BotId < 0 || BotId >= MAX_CLIENTS || !pSelf->m_apPlayers[BotId] ||
		!pSelf->m_apPlayers[BotId]->m_IsBlocker || !pSelf->m_apPlayers[BotId]->m_pBot)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "That blocker ID is not active.");
		return;
	}
	CBot *pBot = pSelf->m_apPlayers[BotId]->m_pBot;
	const std::string Action = pResult->GetString(1);
	if(Action == "list")
	{
		if(pBot->m_HelpNames.empty())
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Whitelist is empty; everyone is blocked.");
		for(const auto &Name : pBot->m_HelpNames)
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", Name.c_str());
		return;
	}
	if(Action != "add" && Action != "remove")
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Use: tt_blocker_whitelist <id> <add|remove|list> [in-game name]");
		return;
	}
	std::string Name;
	if(!ResolvePetIngameName(pResult->NumArguments() > 2 ? pResult->GetString(2) : "", &Name))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Enter an in-game name of up to 15 bytes. Quote names with spaces.");
		return;
	}
	if(Action == "add")
		pBot->m_HelpNames.insert(Name);
	else
		pBot->m_HelpNames.erase(Name);
	const std::string Message = Name + (Action == "add" ? " is protected by this blocker." : " is blocked by this blocker.");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", Message.c_str());
}

void CGameContext::ConBlockerFreezeTimeout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = static_cast<CGameContext *>(pUserData);
	const int BotId = pResult->GetInteger(0);
	const int Seconds = pResult->GetInteger(1);
	if(BotId < 0 || BotId >= MAX_CLIENTS || !pSelf->m_apPlayers[BotId] ||
		!pSelf->m_apPlayers[BotId]->m_IsBlocker || !pSelf->m_apPlayers[BotId]->m_pBot ||
		Seconds < 0 || Seconds > 120)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Use an active blocker ID and 0-120 seconds.");
		return;
	}
	pSelf->m_apPlayers[BotId]->m_pBot->m_FreezeRespawnSeconds = Seconds;
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "blocker", "Blocker freeze timeout updated.");
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

/*
* Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
* Copyright (C) 2008-2010 TrinityCore <http://www.trinitycore.org/>
* Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
* Copyright (C) 1985-2010 {VAS} KalCorp  <http://vasserver.dyndns.org/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/*
* Script Name: AutoBalance
* Original Authors: KalCorp and Vaughner
* Maintainer(s): CVMagic
* Original Script Name: VAS.AutoBalance
* Description: This script is intended to scale based on number of players, instance mobs & world bosses' health, mana, and damage.
*/


#include "Unit.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "MapManager.h"
#include "World.h"
#include "Map.h"
#include "Config/Config.h"
#include "ScriptMgr.h"
#include "Log.h"
#include <vector>

struct AutoBalanceCreatureInfo
{
    uint32 instancePlayerCount;
    float DamageMultiplier;
    uint32 instanceId;
    uint32 creatureId;
};

static std::map<uint32, AutoBalanceCreatureInfo> CreatureDetails; // A hook should be added to remove the mapped entry when the creature is dead or this should be added into the creature object
static std::map<int, int> forcedCreatureIds;                   // The map values correspond with the VAS.AutoBalance.XX.Name entries in the configuration file.
static std::map<int, int> blockedCreatureIds;
static std::map<int, int> fullDamageCreatureIds;
static std::map<int, int> logCreatureIds;                      // Used to log updates to selected creatures for debugging purposes.
static int8 PlayerCountDifficultyOffset; //cheaphack for difficulty server-wide. Another value TODO in player class for the party leader's value to determine dungeon difficulty.
static float MaxDamagePct;               // Minimize total damage allowed by a pct of the player's health.
static int8 lockPlayerCount;            // Sets the maximum player count for scaling.
int GetValidDebugLevel()
{
    int debugLevel = sConfig.GetIntDefault("VASAutoBalance.DebugLevel", 2);

    if ((debugLevel < 0) || (debugLevel > 3))
    {
        return 1;
    }
    return debugLevel;
}

void LoadMaxHealthPctForDamage() 
{
    MaxDamagePct = sConfig.GetIntDefault("VASAutoBalance.MaxHealthPctForDamage", 15);

    if (MaxDamagePct > 100)
    {
        MaxDamagePct = 100;
    } 
    else if (MaxDamagePct < 10)  
    {
        MaxDamagePct = 10;
    }
    MaxDamagePct = MaxDamagePct * .01;
}

void LoadLockPlayerCount() {
    lockPlayerCount = sConfig.GetIntDefault("VASAutoBalance.LockPlayerCount", 0);
}

void LoadForcedCreatureIdsFromString(std::string creatureIds, int forcedPlayerCount) // Used for reading the string from the configuration file to for those creatures who need to be scaled for XX number of players.
{
    std::string delimitedValue;
    std::stringstream creatureIdsStream;

    creatureIdsStream.str(creatureIds);
    while (std::getline(creatureIdsStream, delimitedValue, ',')) // Process each Creature ID in the string, delimited by the comma - ","
    {
        int creatureId = atoi(delimitedValue.c_str());
        if (creatureId >= 0)
        {
            forcedCreatureIds[creatureId] = forcedPlayerCount;
        }
    }
}

void LoadBlockedCreatureIdsFromString(std::string creatureIds, int forcedPlayerCount)
{
    std::string delimitedValue;
    std::stringstream creatureIdsStream;

    creatureIdsStream.str(creatureIds);
    while (std::getline(creatureIdsStream, delimitedValue, ',')) // Process each Creature ID in the string, delimited by the comma - ","
    {
        int creatureId = atoi(delimitedValue.c_str());
        if (creatureId >= 0)
        {
            blockedCreatureIds[creatureId] = forcedPlayerCount;
        }
    }
}
void LoadFullDamageCreatureIdsFromString(std::string creatureIds, int forcedPlayerCount)
{
    std::string delimitedValue;
    std::stringstream creatureIdsStream;

    creatureIdsStream.str(creatureIds);
    while (std::getline(creatureIdsStream, delimitedValue, ',')) // Process each GObject ID in the string, delimited by the comma - ","
    {
        int gameObjectId = atoi(delimitedValue.c_str());
        if (gameObjectId >= 0)
        {
            fullDamageCreatureIds[gameObjectId] = forcedPlayerCount;
        }
    }
}
void LoadLogCreatureIdsFromString(std::string creatureIds)
{
    std::string delimitedValue;
    std::stringstream creatureIdsStream;

    creatureIdsStream.str(creatureIds);
    while (std::getline(creatureIdsStream, delimitedValue, ',')) // Process each Creature ID in the string, delimited by the comma - ","
    {
        int creatureId = atoi(delimitedValue.c_str());
        if (creatureId >= 0)
        {
            logCreatureIds[creatureId] = 1;
        }
    }
}

int GetForcedCreatureId(int creatureId)
{
    if (forcedCreatureIds.find(creatureId) == forcedCreatureIds.end()) // Don't want the forcedCreatureIds map to blowup to a massive empty array
    {
        return 0;
    }
    return forcedCreatureIds[creatureId];
}
bool IsBlockedCreatureId(int creatureId)
{
    if (blockedCreatureIds.find(creatureId) == blockedCreatureIds.end()) // Don't want the blockedCreatureIds map to blowup to a massive empty array
    {
        return false;
    }
    return true;
}
bool IsFullDamageCreatureId(int creatureId)
{
    if (fullDamageCreatureIds.find(creatureId) == fullDamageCreatureIds.end()) // Don't want the blockedGameObjectIds map to blowup to a massive empty array
    {
        return false;
    }
    return true;
}
bool IsLogCreatureId(int creatureId)
{
    if (logCreatureIds.find(creatureId) == logCreatureIds.end()) // Don't want the logCreatureIds map to blowup to a massive empty array
    {
        return false;
    }
    return true;
}

class VAS_AutoBalance_WorldScript : public WorldScript
{
public:
    VAS_AutoBalance_WorldScript()
        : WorldScript("VAS_AutoBalance_WorldScript")
    {
    }

    /*void OnBeforeConfigLoad(bool reload) override
    {
        // from skeleton module
        if (!reload) {
            std::string conf_path = _CONF_DIR;
            std::string cfg_file = conf_path+"/VASAutoBalance.conf";
#ifdef WIN32
                cfg_file = "VASAutoBalance.conf";
#endif
            std::string cfg_def_file = cfg_file + ".dist";
            sConfig.LoadMore(cfg_def_file.c_str());

            sConfig.LoadMore(cfg_file.c_str());
        }
        // end from skeleton module
    }*/
    void OnStartup()
    {
    }

    void SetInitialWorldSettings()
    {
        forcedCreatureIds.clear();
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID40", ""), 40);
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID25", ""), 25);
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID20", ""), 20);
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID10", ""), 10);
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID5", ""), 5);
        LoadForcedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.ForcedID2", ""), 2);
        blockedCreatureIds.clear();
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID40", ""), 40);
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID25", ""), 25);
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID20", ""), 20);
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID10", ""), 10);
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID5", ""), 5);
        LoadBlockedCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.BlockedID2", ""), 2);
        fullDamageCreatureIds.clear();
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature40", ""), 40);
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature25", ""), 25);
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature20", ""), 20);
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature10", ""), 10);
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature5", ""), 5);
        LoadFullDamageCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.FullDamageCreature2", ""), 2);
        logCreatureIds.clear();
        LoadLogCreatureIdsFromString(sConfig.GetStringDefault("VASAutoBalance.LogCreature", ""));
        LoadMaxHealthPctForDamage();
        LoadLockPlayerCount();

        PlayerCountDifficultyOffset = 0;
    }
};

class VAS_AutoBalance_PlayerScript : public PlayerScript
{
public:
    VAS_AutoBalance_PlayerScript()
        : PlayerScript("VAS_AutoBalance_PlayerScript")
    {
    }

    void OnLogin(Player *player, bool /*firstLogin*/)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("This server is running a VAS_AutoBalance Module.");
    }
};

class VAS_AutoBalance_UnitScript : public CreatureScript
{
public:
    VAS_AutoBalance_UnitScript()
        : CreatureScript("VAS_AutoBalance_UnitScript")
    {
    }

    void OnDamage(Creature* attacker, Unit* victim, uint32& damage) {
        if (attacker && victim)
        {
            if ((attacker->GetMap()->IsDungeon() && victim->GetMap()->IsDungeon()) || sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1)
            {
                if (attacker->GetTypeId() != TYPEID_PLAYER)
                {
                    uint32 maxDamageThreshold = damage;
                    if (MaxDamagePct < 100)
                        maxDamageThreshold = victim->GetMaxHealth() * MaxDamagePct;
                    damage = VAS_Modifer_DealDamage(attacker, damage, maxDamageThreshold);
                }
            }
        }
    }

    void ModHeal(Unit* healer, Creature* receiver, uint32& gain)
    {
        if ((healer->GetMap()->IsDungeon() && receiver->GetMap()->IsDungeon()) || sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1)
        {
            if (!IsBlockedCreatureId(healer->GetEntry()))
            {
                if (CreatureDetails[healer->GetObjectGuid()].instanceId == healer->GetMap()->GetInstanceId())
                {
                    gain = VAS_Modifer_DealDamage(receiver, gain, gain);
                }
            }
        }
    }

    uint32 HandlePeriodicDamageAurasTick(Unit *target, Creature *caster, int32 damage)
    {
        if ((caster->GetMap()->IsDungeon() && target->GetMap()->IsDungeon()) || sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1)
            if (caster->GetTypeId() != TYPEID_PLAYER)
            {
                if (!(caster->IsPet() && caster->IsControlledByPlayer()))
                    damage = (float)damage * (float)CreatureDetails[caster->GetObjectGuid()].DamageMultiplier;
            }
        return damage;
    }

    void CalculateSpellDamageTaken(SpellNonMeleeDamage *damageInfo, int32 /*damage*/, SpellEntry* const /*spellInfo*/, WeaponAttackType/* attackType*/, bool /*crit*/)
    {
        if (sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1 || (damageInfo->attacker->GetMap()->IsDungeon() && damageInfo->target->GetMap()->IsDungeon()) || (damageInfo->attacker->GetMap()->IsBattleGround() && damageInfo->target->GetMap()->IsBattleGround()))
        {
            if (damageInfo->attacker->GetTypeId() != TYPEID_PLAYER && damageInfo->attacker->ToCreature())
            {
                if (damageInfo->attacker->ToCreature()->IsPet() && damageInfo->attacker->IsControlledByPlayer())
                    return;
                damageInfo->damage = (float)damageInfo->damage * (float)CreatureDetails[damageInfo->attacker->GetObjectGuid()].DamageMultiplier;
            }
        }
        return;
    }

    void CalculateMeleeDamage(Unit* /*playerVictim*/, uint32 /*damage*/, CalcDamageInfo *damageInfo, WeaponAttackType /*attackType*/)
    {
        // Make sure the Attacker and the Victim are in the same location, in addition that the attacker is not player.
        if ((sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1 || (damageInfo->attacker->GetMap()->IsDungeon() && damageInfo->target->GetMap()->IsDungeon()) || (damageInfo->attacker->GetMap()->IsBattleGround() && damageInfo->target->GetMap()->IsBattleGround())) && (damageInfo->attacker->GetTypeId() != TYPEID_PLAYER))
            if (damageInfo->attacker->ToCreature())
                if (!(damageInfo->attacker->ToCreature()->IsPet() && damageInfo->attacker->IsControlledByPlayer())) // Make sure that the attacker Is not a Pet of some sort
                {
                    damageInfo->damage = (float)damageInfo->damage * (float)CreatureDetails[damageInfo->attacker->GetObjectGuid()].DamageMultiplier;
                }
        return;
    }

    uint32 VAS_Modifer_DealDamage(Creature* AttackerUnit, uint32 damage, uint32 maxDamageThreshold)
    {
        if (AttackerUnit->IsPet() && AttackerUnit->IsControlledByPlayer())
            return damage;
        if (IsFullDamageCreatureId(AttackerUnit->ToCreature()->GetEntry()))
            return damage;

        float damageMultiplier = CreatureDetails[AttackerUnit->GetObjectGuid()].DamageMultiplier;

        uint32 damageResult = damage * damageMultiplier;
        
        if (damageResult > maxDamageThreshold) {
            return maxDamageThreshold;
        }
        return damageResult;
    }
};


class VAS_AutoBalance_AllMapScript : public AllMapScript
{
public:
    VAS_AutoBalance_AllMapScript()
        : AllMapScript("VAS_AutoBalance_AllMapScript")
    {
    }

    void OnPlayerEnterAll(Map* map, Player* player)
    {
        int instancePlayerCount = map->GetPlayersCountExceptGMs() - 1;
        if (lockPlayerCount > 0 && lockPlayerCount < instancePlayerCount) {
            instancePlayerCount = lockPlayerCount;
        }

        if (sConfig.GetIntDefault("VASAutoBalance.PlayerChangeNotify", 1) > 0)
        {
            if ((map->IsDungeon()) && !player->isGameMaster())
            {
                Map::PlayerList const &playerList = map->GetPlayers();
                if (!playerList.isEmpty())
                {
                    for (Map::PlayerList::const_iterator playerIteration = playerList.begin(); playerIteration != playerList.end(); ++playerIteration)
                    {
                        if (Player* playerHandle = playerIteration->getSource())
                        {
                            ChatHandler chatHandle = ChatHandler(playerHandle->GetSession());
                            chatHandle.PSendSysMessage("|cffFF0000 [AutoBalance]|r|cffFF8000 %s entered the Instance %s. Auto setting player count to %u (Player Difficulty Offset = %u) |r", player->GetName(), map->GetMapName(), instancePlayerCount + PlayerCountDifficultyOffset, PlayerCountDifficultyOffset);
                        }
                    }
                }
            }
        }
    }

    void OnPlayerLeaveAll(Map* map, Player* player)
    {
        int instancePlayerCount = map->GetPlayersCountExceptGMs() - 1;
        if (lockPlayerCount > 0 && lockPlayerCount < instancePlayerCount) {
            instancePlayerCount = lockPlayerCount;
        }

        if (instancePlayerCount >= 1)
        {
            if (sConfig.GetIntDefault("VASAutoBalance.PlayerChangeNotify", 1) > 0)
            {
                if ((map->IsDungeon()) && !player->isGameMaster())
                {
                    Map::PlayerList const &playerList = map->GetPlayers();
                    if (!playerList.isEmpty())
                    {
                        for (Map::PlayerList::const_iterator playerIteration = playerList.begin(); playerIteration != playerList.end(); ++playerIteration)
                        {
                            if (Player* playerHandle = playerIteration->getSource())
                            {
                                ChatHandler chatHandle = ChatHandler(playerHandle->GetSession());
                                chatHandle.PSendSysMessage("|cffFF0000 [VAS-AutoBalance]|r|cffFF8000 %s left the Instance %s. Auto setting player count to %u (Player Difficulty Offset = %u) |r", player->GetName(), map->GetMapName(), instancePlayerCount, PlayerCountDifficultyOffset);
                            }
                        }
                    }
                }
            }
        }
    }
};


class VAS_AutoBalance_AllCreatureScript : public AllCreatureScript
{
public:
    VAS_AutoBalance_AllCreatureScript()
        : AllCreatureScript("VAS_AutoBalance_AllCreatureScript")
    {
    }


    void Creature_SelectLevel(const CreatureInfo* /*creatureTemplate*/, Creature* creature)
    {

        if (creature->GetMap()->IsDungeon() || sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1)
        {
            int instancePlayerCount = creature->GetMap()->GetPlayersCountExceptGMs();
            if (lockPlayerCount > 0 && lockPlayerCount < instancePlayerCount) {
                instancePlayerCount = lockPlayerCount;
            }

            ModifyCreatureAttributes(creature);
            CreatureDetails[creature->GetObjectGuid()].instancePlayerCount = instancePlayerCount + PlayerCountDifficultyOffset;
            CreatureDetails[creature->GetObjectGuid()].instanceId = creature->GetMap()->GetInstanceId();
        }
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/)
    {
        bool log = IsLogCreatureId(creature->GetEntry());
        if (log) {
           // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, is being checked OnAllCreatureUpdate.", creature->GetEntry(), creature->GetName());
        }
        if (!IsBlockedCreatureId(creature->GetEntry())) {
            Map *map = creature->GetMap();
            if (!map)
                map = creature->GetMap();
           // if (log) {
               // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, is not on the blocked list.", creature->GetEntry(), creature->GetName());
               // if (!map)
                   // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, has no map information.", creature->GetEntry(), creature->GetName());
               // else if (!map->GetPlayersCountExceptGMs())
                   // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, has no player count information.", creature->GetEntry(), creature->GetName());
               // else {
                   // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, has map %s and playerCount %u.", creature->GetEntry(), creature->GetName(), map->GetMapName(), map->GetPlayersCountExceptGMs());
               // }
           // }
            if (creature->IsDead() && CreatureDetails[creature->GetObjectGuid()].creatureId)
                CreatureDetails.erase(creature->GetObjectGuid());
            if (DoCreatureUpdate(creature, map, log))
            {
                if (log) {
                   // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, has passed DoCreatureUpdate checks.", creature->GetEntry(), creature->GetName());
                }
                if (map->IsDungeon() || map->IsBattleGround() || sConfig.GetIntDefault("VASAutoBalance.DungeonsOnly", 1) < 1)
                {
                    if (log) {
                       // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, is having attributes modified.", creature->GetEntry(), creature->GetName());
                    }
                    ModifyCreatureAttributes(creature);
                }
                CreatureDetails[creature->GetObjectGuid()].instancePlayerCount = map->GetPlayersCountExceptGMs() + PlayerCountDifficultyOffset;
                CreatureDetails[creature->GetObjectGuid()].instanceId = map->GetInstanceId();
                CreatureDetails[creature->GetObjectGuid()].creatureId = creature->GetEntry();
            }
        }
        else
        {
            if (log) {
               // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, is on the blocked list.", creature->GetEntry(), creature->GetName());
            }
        }
    }

    bool DoCreatureUpdate(Creature* creature, Map* map, bool /*log*/) {
        if (CreatureDetails[creature->GetObjectGuid()].instanceId != map->GetInstanceId()) {
           // if (log)
               // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, update for new instanceid %u.", creature->GetEntry(), creature->GetName(), map->GetInstanceId());
            return true;
        }
        if (CreatureDetails[creature->GetObjectGuid()].creatureId != creature->GetEntry()) {
           // if (log)
               // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, update for altered creature_id %u to %u.", creature->GetEntry(), creature->GetName(), CreatureInfo[creature->GetGUID()].creatureId, creature->GetEntry());
            return true;
        }
        if (CreatureDetails[creature->GetObjectGuid()].instancePlayerCount != (map->GetPlayersCountExceptGMs() + PlayerCountDifficultyOffset)) {
           // if (log)
               // TC_LOG_DEBUG("creature.log", "VAS_Autobalance: CreatureId %u, name %s, update for altered player count.", creature->GetEntry(), creature->GetName());
            return true;
        }

        return false;
    }

    void ModifyCreatureAttributes(Creature* creature)
    {
        if ((creature->IsPet() && creature->IsControlledByPlayer()) || (creature->GetMap()->IsDungeon() && sConfig.GetIntDefault("VASAutoBalance.Instances", 1) < 1) || creature->GetMap()->GetPlayersCountExceptGMs() <= 0)
        {
            return;
        }

        CreatureInfo const *creatureTemplate = creature->GetCreatureInfo();
        CreatureClassLvlStats const* creatureStats = sObjectMgr->GetCreatureClassLvlStats(creature->getLevel(), creatureTemplate->UnitClass);

        float damageMultiplier = 1.0f;
        float healthMultiplier = 1.0f;

        uint32 baseHealth = creatureStats->BaseHealth;
        uint32 baseMana = creatureStats->BaseMana;
        int instancePlayerCount = creature->GetMap()->GetPlayersCountExceptGMs();
        if (lockPlayerCount > 0 && lockPlayerCount < instancePlayerCount) {
            instancePlayerCount = lockPlayerCount;
        }
        uint32 maxNumberOfPlayers = (sMapMgr->FindMap(creature->GetMapId(), creature->GetInstanceId()))->GetMaxPlayers();
        uint32 scaledHealth = 0;
        uint32 scaledMana = 0;

        //   VAS SOLO  - By MobID
        if (GetForcedCreatureId(creatureTemplate->Entry) > 0)
        {
            maxNumberOfPlayers = GetForcedCreatureId(creatureTemplate->Entry); // Force maxNumberOfPlayers to be changed to match the Configuration entry.
        }

        // (tanh((X-2.2)/1.5) +1 )/2    // 5 Man formula X = Number of Players
        // (tanh((X-5)/2) +1 )/2        // 10 Man Formula X = Number of Players
        // (tanh((X-16.5)/6.5) +1 )/2   // 25 Man Formula X = Number of players
        //
        // Note: The 2.2, 5, and 16.5 are the number of players required to get 50% health.
        //       It's not required this be a whole number, you'd adjust this to raise or lower
        //       the hp modifier for per additional player in a non-whole group. These
        //       values will eventually be part of the configuration file once I finalize the mod.
        //
        //       The 1.5, 2, and 6.5 modify the rate of percentage increase between
        //       number of players. Generally the closer to the value of 1 you have this
        //       the less gradual the rate will be. For example in a 5 man it would take 3
        //       total players to face a mob at full health.
        //
        //       The +1 and /2 values raise the TanH function to a positive range and make
        //       sure the modifier never goes above the value or 1.0 or below 0.
        //
        //       Lastly this formula has one side effect on full groups Bosses and mobs will
        //       never have full health, this can be tested against by making sure the number
        //       of players match the maxNumberOfPlayers variable.

        switch (maxNumberOfPlayers)
        {
        case 40:
            healthMultiplier = (float)instancePlayerCount / (float)maxNumberOfPlayers; // 40 Man Instances oddly enough scale better with the old formula
            break;
        case 25:
            healthMultiplier = (tanh((instancePlayerCount - 16.5f) / 1.5f) + 1.0f) / 2.0f;
            break;
        case 10:
            healthMultiplier = (tanh((instancePlayerCount - 4.5f) / 1.5f) + 1.0f) / 2.0f;
            break;
        case 2:
            healthMultiplier = (float)instancePlayerCount / (float)maxNumberOfPlayers;                   // Two Man Creatures are too easy if handled by the 5 man formula, this would only
            break;                                                                         // apply in the situation where it's specified in the configuration file.
        default:
            healthMultiplier = (tanh((instancePlayerCount - 2.2f) / 1.5f) + 1.0f) / 2.0f;    // default to a 5 man group
        }

        //   VAS SOLO  - Map 0,1 and 530 ( World Mobs )                                                               // This may be where VAS_AutoBalance_CheckINIMaps might have come into play. None the less this is
        if ((creature->GetMapId() == 0 || creature->GetMapId() == 1 || creature->GetMapId() == 530) && (creature->IsElite() || creature->IsWorldBoss()))  // specific to World Bosses and elites in those Maps, this is going to use the entry XPlayer in place of instancePlayerCount.
        {
            if (baseHealth > 800000) {
                healthMultiplier = (tanh((sConfig.GetFloatDefault("VASAutoBalance.numPlayer", 1.0f) - 5.0f) / 1.5f) + 1.0f) / 2.0f;

            }
            else {
                healthMultiplier = (tanh((sConfig.GetFloatDefault("VASAutoBalance.numPlayer", 1.0f) - 2.2f) / 1.5f) + 1.0f) / 2.0f; // Assuming a 5 man configuration, as World Bosses have been relatively retired since BC so unless the boss has some substantial baseHealth
            }

        }

        // Ensure that the healthMultiplier is not lower than the configuration specified value. -- This may be Deprecated later.
        if (healthMultiplier <= sConfig.GetFloatDefault("VASAutoBalance.MinHPModifier", 0.1f))
        {
            healthMultiplier = sConfig.GetFloatDefault("VASAutoBalance.MinHPModifier", 0.1f);
        }

        //Getting the list of Classes in this group - this will be used later on to determine what additional scaling will be required based on the ratio of tank/dps/healer
        //GetPlayerClassList(creature, playerClassList); // Update playerClassList with the list of all the participating Classes


        scaledHealth = uint32((baseHealth * healthMultiplier) + 1.0f);
        // Now adjusting Mana, Mana is something that can be scaled linearly
        if (maxNumberOfPlayers == 0) {
            scaledMana = uint32((baseMana * healthMultiplier) + 1.0f);
            // Now Adjusting Damage, this too is linear for now .... this will have to change I suspect.
            damageMultiplier = healthMultiplier;
        }
        else {
            scaledMana = ((baseMana / maxNumberOfPlayers) * instancePlayerCount);
            // Now Adjusting Damage, this too is linear for now .... this will have to change I suspect.
            damageMultiplier = (float)instancePlayerCount / (float)maxNumberOfPlayers;

        }

        // Can not be less then Min_D_Mod
        if (damageMultiplier <= sConfig.GetFloatDefault("VASAutoBalance.MinDamageModifier", 0.1f))
        {
            damageMultiplier = sConfig.GetFloatDefault("VASAutoBalance.MinDamageModifier", 0.1f);
        }

        creature->SetCreateHealth(scaledHealth);
        creature->SetMaxHealth(scaledHealth);
        creature->ResetPlayerDamageReq();
        creature->SetCreateMana(scaledMana);
        creature->SetMaxPower(POWER_MANA, scaledMana);
        creature->SetPower(POWER_MANA, scaledMana);
        creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, (float)scaledHealth);
        creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, (float)scaledMana);
        CreatureDetails[creature->GetObjectGuid()].DamageMultiplier = damageMultiplier;
    }
};
//class VAS_AutoBalance_CommandScript : public CommandScript
//{
//public:
//    VAS_AutoBalance_CommandScript() : CommandScript("VAS_AutoBalance_CommandScript") { }
//
//    ChatCommandTable GetCommands() const override
//    {
//        static ChatCommandTable vasCommandTable =
//        {
//            { "setoffset",        HandleVasSetOffsetCommand,                 rbac::RBAC_ROLE_GAMEMASTER,                        Console::Yes },
//            { "getoffset",        HandleVasGetOffsetCommand,                 rbac::RBAC_ROLE_GAMEMASTER,                        Console::Yes },
//            { "clear",        HandleVasClearCommand,                 rbac::RBAC_ROLE_GAMEMASTER,                        Console::Yes },
//        };
//
//        static ChatCommandTable commandTable =
//        {
//            { "vas",     vasCommandTable },
//        };
//        return commandTable;
//    }
//
//    static bool HandleVasSetOffsetCommand(ChatHandler* handler, char args)
//    {
//        if (!args)
//        {
//            handler->PSendSysMessage(".vas setoffset #");
//            handler->PSendSysMessage("Sets the Player Difficulty Offset for instances. Example: (You + offset(1) = 2 player difficulty).");
//            return false;
//        }
//        char* offset = strtok(&args, " ");
//        int32 offseti = -1;
//
//        if (offset)
//        {
//            offseti = (uint32)atoi(offset);
//            handler->PSendSysMessage("Changing Player Difficulty Offset to %i.", offseti);
//            PlayerCountDifficultyOffset = offseti;
//            return true;
//        }
//        else
//            handler->PSendSysMessage("Error changing Player Difficulty Offset! Please try again.");
//        return false;
//    }
//    static bool HandleVasGetOffsetCommand(ChatHandler* handler, char /*args*/)
//    {
//        handler->PSendSysMessage("Current Player Difficulty Offset = %i", PlayerCountDifficultyOffset);
//        return true;
//    }
//    static bool HandleVasClearCommand(ChatHandler* handler, char /*args*/)
//    {
//        handler->PSendSysMessage("Clearing all creature info.");
//        CreatureDetails.clear();
//        return true;
//    }
//};
void AddSC_VAS_AutoBalance()
{
    new VAS_AutoBalance_WorldScript;
    new VAS_AutoBalance_PlayerScript;
    new VAS_AutoBalance_UnitScript;
    new VAS_AutoBalance_AllCreatureScript;
    new VAS_AutoBalance_AllMapScript;
    //new VAS_AutoBalance_CommandScript;
}

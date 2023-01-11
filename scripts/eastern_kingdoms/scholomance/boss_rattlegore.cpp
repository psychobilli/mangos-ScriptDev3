/**
 * ScriptDev3 is an extension for mangos providing enhanced features for
 * area triggers, creatures, game objects, instances, items, and spells beyond
 * the default database scripting in mangos.
 *
 * Copyright (C) 2006-2013  ScriptDev2 <http://www.scriptdev2.com/>
 * Copyright (C) 2014-2022 MaNGOS <https://getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

 /**
  * ScriptData
  * SDName:      Boss_Dathrohan_Balnazzar
  * SD%Complete: 95
  * SDComment:   Possibly need to fix/improve summons after death
  * SDCategory:  Stratholme
  * EndScriptData
  */

#include "precompiled.h"

enum
{
    NPC_DISEASED_GHOUL = 10495,
    NPC_RISEN_ABERATION = 10485,
    NPC_REANIMATED_CORPSE = 10481
};

struct ClearDef
{
    uint32 m_uiGuid;
};

static ClearDef m_aClearPoint[] =
{
    {154689},
    {154675},
    {154680},
    {154685},
    {154686},
    {154677},
    {154712},
    {154682},
    {154683},
    {154690},
    {154691},
    {154695},
    {154696},
    {154700},
    {154705},
    {154706},
    {154710},
    {154679},
    {154714},
    {154704},
    {154711},
    {154687},
    {154692},
    {154693},
    {154697},
    {154698},
    {154702},
    {154707},
    {154708},
    {154676},
    {154701},
    {154681}
};

ObjectGuid testCreature;

uint64 const killMob1 = 0;

struct RattlegoreSummonDef
{
    uint32 m_uiEntry;
    float m_fX, m_fY, m_fZ, m_fOrient;
};

RattlegoreSummonDef m_aSummonPoint[] =
{
    {NPC_DISEASED_GHOUL, 223.455f, 98.909f, 104.715f, 0.0f},
    {NPC_DISEASED_GHOUL, 221.354f, 103.765f, 104.715f, 0.0f},
    {NPC_DISEASED_GHOUL, 218.742f, 69.255f, 104.715f, 0.0f},
    {NPC_DISEASED_GHOUL, 185.984f, 76.433f, 104.715f, 0.0f},
    {NPC_DISEASED_GHOUL, 183.381f, 93.023f, 104.715f, 0.0f},
    {NPC_DISEASED_GHOUL, 177.522f, 93.452f, 104.715f, 0.0f},

    {NPC_RISEN_ABERATION, 216.345f, 97.855f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 213.789f, 103.556f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 213.132f, 70.051f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 220.04f, 76.69f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 184.048f, 69.474f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 181.199f, 79.071f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 178.075f, 99.190f, 104.715f, 0.0f},
    {NPC_RISEN_ABERATION, 185.617f, 101.218f, 104.715f, 0.0f},

    {NPC_REANIMATED_CORPSE, 215.657f, 77.333f, 104.715f, 0.0f},
    {NPC_REANIMATED_CORPSE, 179.723f, 72.857f, 104.715f, 0.0f}
};

struct boss_rattlegore : public CreatureScript
{
    boss_rattlegore() : CreatureScript("boss_rattlegore") {}

    struct boss_rattlegoreAI : public ScriptedAI
    {
        boss_rattlegoreAI(Creature* pCreature) : ScriptedAI(pCreature) { }

        void JustDied(Unit* /*Victim*/) override
        {
            std::list<Creature*> despawnCreatures;
            DespawnCreatures(despawnCreatures, m_creature, NPC_DISEASED_GHOUL);
            DespawnCreatures(despawnCreatures, m_creature, NPC_REANIMATED_CORPSE);
            DespawnCreatures(despawnCreatures, m_creature, NPC_RISEN_ABERATION);
            for (uint32 i = 0; i < countof(m_aSummonPoint); ++i)
                m_creature->SummonCreature(m_aSummonPoint[i].m_uiEntry,
                    m_aSummonPoint[i].m_fX, m_aSummonPoint[i].m_fY, m_aSummonPoint[i].m_fZ, m_aSummonPoint[i].m_fOrient,
                    TEMPSPAWN_TIMED_DESPAWN, HOUR * IN_MILLISECONDS);
        }
    };

private:
    static void DespawnCreatures(std::list<Creature*> despawnCreatures, Creature* creature, uint32 uiEntry) {
        GetCreatureListWithEntryInGrid(despawnCreatures, creature, uiEntry, 220.0f);
        for (uint32 i = 0; i < countof(m_aClearPoint); i++)
            for (std::list<Creature*>::iterator iter = despawnCreatures.begin(); iter != despawnCreatures.end(); ++iter)
                if ((*iter)->GetGUIDLow() == m_aClearPoint[i].m_uiGuid)
                    (*iter)->RemoveFromWorld();
        despawnCreatures.clear();
    }

    CreatureAI* GetAI(Creature* pCreature) override
    {
        return new boss_rattlegoreAI(pCreature);
    }
};

void AddSC_boss_rattlegore()
{
    CreatureScript* s;
    s = new boss_rattlegore();
    s->RegisterSelf();
}

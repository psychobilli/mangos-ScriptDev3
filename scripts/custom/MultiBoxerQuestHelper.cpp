#include "ScriptMgr.h"

static std::map<int, int> groupItemData;

class MultiBoxerQuestHelper_LootScript : public LootScript
{
public:
    MultiBoxerQuestHelper_LootScript()
        : LootScript("MultiBoxerQuestHelper_LootScript")
    {
    }

    // Adds additional counts to a quest item during loot.
    // If ClearModQuestLoot is not called before another quest item of the same type is added to a loot table, this method will fail to do anything.
    // Most players will gather quest items as they drop, so this is a low risk bug.
    uint8 ModQuestLoot(Player* pPlayer, LootItem item)
    {
        uint32 groupSize = 0;
        if (pPlayer->GetGroup())
            groupSize = pPlayer->GetGroup()->GetMembersCount();
        bool isDungeon = pPlayer->GetMap()->IsDungeon();
        if (groupSize < 3 || isDungeon || groupItemData[item.itemid] == pPlayer->GetGroup()->GetId())
            return 0;

        uint8 additionalItems = groupSize - 2; // Add 3 items for a 5 man, 2 for 4, 1 for 3. Dual boxers get no love.

        ItemPrototype const* itemPro = sObjectMgr.GetItemPrototype(item.itemid);
        uint32 maxCount = itemPro->MaxCount;
        if (maxCount > 0 && maxCount < additionalItems + 1)
            return 0; // Don't add more items than the player can carry.
        uint32 stackable = itemPro->Stackable;
        if (stackable > 0 && stackable < additionalItems + 1)
            return stackable - 1; // Don't add more items than the player can stack.
        groupItemData[item.itemid] = pPlayer->GetGroup()->GetId();

        return additionalItems;
    }

    // When a player in a given group loots anything, wipe out any data in groupItemData for that group.
    // It doesn't matter if all data is cleared at this point, although if a player loots a creature, sees a quest item, then fails to loot
    // that item before viewing loot in another corpse, the other corpse won't be affected by this mod.
    // In other words, ModQuestLoot only works once per questItem until that questItem is removed from a corpse/container.
    bool ClearModQuestLoot(Player* pPlayer)
    {
        uint32 groupId = 0;
        if (pPlayer->GetGroup())
            groupId = pPlayer->GetGroup()->GetId();
        else
            return true;
        std::map<int, int>::iterator it;
        for (it = groupItemData.begin(); it != groupItemData.end(); it++) {
            if (it->second == groupId)
                groupItemData[it->first] = -1;
        }
        return true;
    }
};
void AddSC_MultiBoxerQuestHelper()
{
    Script* s;
    s = new MultiBoxerQuestHelper_LootScript;
    s->RegisterSelf();
}
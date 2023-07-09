#include "Log.h"
#include "ScriptMgr.h"
#include "Config.h"
#include "Chat.h"
#include "Player.h"
#include "ScriptedGossip.h"

enum AoeLootString
{
    AOE_ACORE_STRING_MESSAGE = 50000,
    AOE_ITEM_IN_THE_MAIL
};

class AoeLoot : public PlayerScript
{
public:
    AoeLoot() : PlayerScript("AoeLoot") {}

    void OnCreatureLootAOE(Player* player)
    {
        bool _enable = sConfigMgr->GetOption<bool>("AOELoot.Enable", true);

        float range = 45.0f;
        uint32 gold = 0;

        std::list<Creature*> creatureDie;
        player->GetDeadCreatureListInGrid(creatureDie, range);

        Group* group = player->GetGroup();
        std::vector<Player*> groupMembers;

        if (group)
        {
            // Get all group members within range
            Group::MemberSlotList const& members = group->GetMemberSlots();
            for (auto const& slot : members)
            {
                Player* groupMember = ObjectAccessor::GetPlayer(*player, slot.guid);
                if (groupMember && groupMember->IsWithinDistInMap(player, range))
                {
                    groupMembers.push_back(groupMember);
                }
            }
        }

        // If the player is not in a group, add the player itself to the groupMembers list
        if (groupMembers.empty())
        {
            groupMembers.push_back(player);
        }

        for (auto const& creature : creatureDie)
        {
            Loot* loot = &creature->loot;
            gold += loot->gold;
            loot->gold = 0;
            uint32 maxSlot = loot->GetMaxSlotInLootFor(player);

            for (uint32 i = 0; i < maxSlot; ++i)
            {
                if (LootItem* item = loot->LootItemInSlot(i, player))
                {
                    bool distributed = false;

                    // Distribute the item to each group member
                    for (auto const& groupMember : groupMembers)
                    {
                        if (groupMember->AddItem(item->itemid, item->count))
                        {
                            groupMember->SendNotifyLootItemRemoved(i);
                            groupMember->SendLootRelease(player->GetLootGUID());
                            distributed = true;
                        }
                    }

                    // If the item couldn't be distributed, send it to the player via mail
                    if (!distributed)
                    {
                        player->SendItemRetrievalMail(item->itemid, item->count);
                        ChatHandler(player->GetSession()).SendSysMessage(AOE_ITEM_IN_THE_MAIL);
                    }
                }
            }

            if (!loot->empty())
            {
                if (!creature->IsAlive())
                {
                    creature->AllLootRemovedFromCorpse();
                    creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
                    loot->clear();

                    if (creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))
                    {
                        creature->RemoveUnitFlag(UNIT_FLAG_SKINNABLE);
                    }
                }
            }
            else
            {
                creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
                creature->AllLootRemovedFromCorpse();
            }
        }

        // Distribute the gold to each group member
        if (!groupMembers.empty())
        {
            uint32 goldPerPlayer = gold / groupMembers.size();

            for (auto const& groupMember : groupMembers)
            {
                groupMember->ModifyMoney(goldPerPlayer);
                groupMember->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);

                WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
                data << uint32(goldPerPlayer);
                data << uint8(0); // Controls the text displayed in chat. 0 is "Your share is..."
                groupMember->GetSession()->SendPacket(&data);
            }
        }
    }

    void OnCreatureKilledByPet(Player* petOwner, Creature* killed) override
    {
        OnCreatureLootAOE(petOwner);

        // Remove items from the creature's loot
        Loot* loot = &killed->loot;
        loot->clear();

        killed->AllLootRemovedFromCorpse();
        killed->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);

        if (killed->HasUnitFlag(UNIT_FLAG_SKINNABLE))
        {
            killed->RemoveUnitFlag(UNIT_FLAG_SKINNABLE);
        }
    }

    void OnCreatureKill(Player* player, Creature* creature) override
    {
        OnCreatureLootAOE(player);
    }

    void OnAfterCreatureLoot(Player* player) override
    {
        OnCreatureLootAOE(player);
    }

    void OnAfterCreatureLootMoney(Player* player) override
    {
        OnCreatureLootAOE(player);
    }
};

void AddSC_aoe_lootScripts()
{
    new AoeLoot();
}


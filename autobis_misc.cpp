#include "autobis_misc.h"

#include <map>

#include "Bag.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "WorldSession.h"

// https://github.com/Road-block/Pawn/blob/master/Wowhead.lua
//  I subtract 20 from HIT_RATING because we tend to get flooded with hit rating, overcapping the 8% limit...

static const AutoBis::ScoreWeightMap ret_paladin_map = {
    {-1, 470}, // melee_DPS
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_STRENGTH, 80},
    {ITEM_MOD_EXPERTISE_RATING, 66},
    {ITEM_MOD_CRIT_RATING, 40},
    {ITEM_MOD_ATTACK_POWER, 34},
    {ITEM_MOD_AGILITY, 32},
    {ITEM_MOD_HASTE_RATING, 30},
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 22},
    {ITEM_MOD_SPELL_POWER, 9},
    {ITEM_MOD_STAMINA, 0.1},
    {-2, 0.01}, // armor
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap prot_paladin_map = {
    {ITEM_MOD_STAMINA, 100},
    {ITEM_MOD_AGILITY, 60},
    {ITEM_MOD_EXPERTISE_RATING, 59},
    {ITEM_MOD_DODGE_RATING, 55},
    {ITEM_MOD_DEFENSE_SKILL_RATING, 45},
    {ITEM_MOD_PARRY_RATING, 30},
    {ITEM_MOD_STRENGTH, 16},
    {8, 0.01}, // armor
    {ITEM_MOD_BLOCK_RATING, 7},
    {ITEM_MOD_BLOCK_VALUE, 6},
    {-1, 0.0001}, // melee_DPS
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap fury_warrior_map = {
    {ITEM_MOD_EXPERTISE_RATING, 100},
    {ITEM_MOD_STRENGTH, 82},
    {ITEM_MOD_CRIT_RATING, 66},
    {ITEM_MOD_AGILITY, 53},
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 52},
    {ITEM_MOD_HIT_RATING, 48},
    {ITEM_MOD_HASTE_RATING, 36},
    {ITEM_MOD_ATTACK_POWER, 31},
    {-2, 5}, // armor
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.01}, // melee_DPS
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap frost_mage_map = {
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_HASTE_RATING, 42},
    {ITEM_MOD_SPELL_POWER, 39},
    {ITEM_MOD_CRIT_RATING, 19},
    {ITEM_MOD_INTELLECT, 6},
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.0001}, // melee_DPS
    {-2, 0.0001}, // armor
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap bm_hunter_map = {
    {-3, 213}, // ranged_DPS
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_AGILITY, 58},
    {ITEM_MOD_CRIT_RATING, 40},
    {ITEM_MOD_INTELLECT, 37},
    {ITEM_MOD_ATTACK_POWER, 30},
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 28},
    {ITEM_MOD_HASTE_RATING, 21},
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.0001}, // melee_DPS
    {-2, 0.0001}, // armor
};

static const AutoBis::ScoreWeightMap boomkin_map = {
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_SPELL_POWER, 66},
    {ITEM_MOD_HASTE_RATING, 54},
    {ITEM_MOD_CRIT_RATING, 43},
    {ITEM_MOD_SPIRIT, 22},
    {ITEM_MOD_INTELLECT, 22},
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.0001}, // melee_DPS
    {-2, 0.0001}, // armor
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap cat_druid_map = {
    {ITEM_MOD_AGILITY, 100},
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 90},
    {ITEM_MOD_STRENGTH, 80},
    {ITEM_MOD_CRIT_RATING, 55},
    {ITEM_MOD_EXPERTISE_RATING, 50},
    {ITEM_MOD_HIT_RATING, 50},
    {ITEM_MOD_ATTACK_POWER, 40},
    {ITEM_MOD_HASTE_RATING, 35},
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.01}, // melee_DPS
    {-2, 0.0001}, // armor
    {-3, 0.0001}, // ranged_DPS
};

static const AutoBis::ScoreWeightMap enh_shaman_map = {
    {-1, 135}, // melee_DPS
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_EXPERTISE_RATING, 84},
    {ITEM_MOD_AGILITY, 55},
    {ITEM_MOD_INTELLECT, 55},
    {ITEM_MOD_CRIT_RATING, 55},
    {ITEM_MOD_HASTE_RATING, 42},
    {ITEM_MOD_STRENGTH, 35},
    {ITEM_MOD_ATTACK_POWER, 32},
    {ITEM_MOD_SPELL_POWER, 29},
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 26},
    {ITEM_MOD_STAMINA, 0.1},
    {-2, 0.01}, // armor
    {-3, 0.0001}, // ranged_DPS
};

const AutoBis::ScoreWeightMap& AutoBis::GetScoreWeightMap(Player *player)
{
    if (player->GetClass() == CLASS_PALADIN)
        return prot_paladin_map;
    else if (player->GetClass() == CLASS_WARRIOR)
        return fury_warrior_map;
    else if (player->GetClass() == CLASS_MAGE)
        return frost_mage_map;
    else if (player->GetClass() == CLASS_DRUID) {
        if (player->GetLevel() < 10)
            return boomkin_map;
        else
            return cat_druid_map;
    }
    else if (player->GetClass() == CLASS_SHAMAN)
        return enh_shaman_map;
    return ret_paladin_map;
}

void AutoBis::AdjustInvType(uint32 &inv_type)
{
    if (inv_type == INVTYPE_ROBE) {
        inv_type = INVTYPE_CHEST;
    } else if (inv_type == INVTYPE_HOLDABLE || inv_type == INVTYPE_WEAPONOFFHAND) {
        inv_type = INVTYPE_SHIELD;
    }
}

double AutoBis::ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate)
{
    // ...
    uint32 itemId = itemTemplate->ItemId;
    std::string myStm = "SELECT dmg_min1, dmg_max1, armor, StatsCount FROM item_template WHERE entry = "
                        + std::to_string(itemId) + ";";
    std::shared_ptr<ResultSet> result = WorldDatabase.Query(myStm.c_str());
    if (!result) {
        printf("INTERNAL ERROR: entry = %u not found in item_template !?\n", itemId);
        return -1.0;
    }
    Field* fields = result->Fetch();
    double dmg_min1 = fields[0].GetFloat();
    double dmg_max1 = fields[1].GetFloat();
    uint32 armor = fields[2].GetUInt32();
    uint32 StatsCount = fields[3].GetUInt32();
    //
    double totalScore = 0.0, totalWeight = 0.0;
    for (auto entry : score_weights) {
        totalWeight += entry.second;
    }
    if (itemTemplate->Class == ITEM_CLASS_WEAPON) {
        double dps = 1000 * (dmg_min1 + dmg_max1) / 2 / itemTemplate->Delay;
        uint32 invtype = itemTemplate->InventoryType;
        if (invtype == INVTYPE_RANGED || invtype == INVTYPE_THROWN || invtype == INVTYPE_RANGEDRIGHT) {
            if (dps > 0 && score_weights.find(-3) != score_weights.end())
                totalScore += dps * score_weights.at(-3) / totalWeight;
        } else {
            if (dps > 0 && score_weights.find(-1) != score_weights.end())
                totalScore += dps * score_weights.at(-1) / totalWeight;
        }
    }
    if (armor > 0 && score_weights.find(-2) != score_weights.end())
        totalScore += armor * score_weights.at(-2) / totalWeight;
    for (int32 idx = 0; idx < StatsCount; ++idx) {
        int32 statId = itemTemplate->ItemStat[idx].ItemStatType;
        int32 statVal = itemTemplate->ItemStat[idx].ItemStatValue;
        auto fiter = score_weights.find(statId);
        if (fiter != score_weights.end()) {
            totalScore += statVal * fiter->second / totalWeight;
        }
    }
    return totalScore;
}

bool AutoBis::PlayerCanUseItem(Player *player, ItemTemplate const* itemTemplate)
{
    if (!itemTemplate)
        return false; // INTERNAL ERROR
    uint32 inv_type = itemTemplate->InventoryType;
    if (inv_type == 0)
        return false;
    if (EQUIP_ERR_OK != player->CanUseItem(itemTemplate))
        return false;
    if (inv_type == INVTYPE_SHIELD) {
        // only shamans, warriors, and paladins can use shields:
        if (player->GetClass() != CLASS_WARRIOR && player->GetClass() != CLASS_PALADIN
            && player->GetClass() != CLASS_SHAMAN)
            return false;
    }
    if (itemTemplate->Class == ITEM_CLASS_WEAPON) {
        const static uint32 item_weapon_skills[MAX_ITEM_SUBCLASS_WEAPON] = {
            SKILL_AXES,     SKILL_2H_AXES,  SKILL_BOWS,          SKILL_GUNS,      SKILL_MACES,
            SKILL_2H_MACES, SKILL_POLEARMS, SKILL_SWORDS,        SKILL_2H_SWORDS, 0,
            SKILL_STAVES,   0,              0,                   SKILL_FIST_WEAPONS,   0,
            SKILL_DAGGERS,  SKILL_THROWN,   SKILL_ASSASSINATION, SKILL_CROSSBOWS, SKILL_WANDS,
            SKILL_FISHING
        }; //Copy from function Item::GetSkill()
        if (player->GetSkillValue(item_weapon_skills[itemTemplate->SubClass]) == 0)
            return false; // player cannot equip this item
    }
    if (itemTemplate->Class == 4) {
        if (player->GetClass() == CLASS_WARRIOR || player->GetClass() == CLASS_PALADIN) {
            // plate doesn't start to show up til lvl 40. Don't do checks for pal/warr/DKs..
        } else if (player->GetClass() == CLASS_SHAMAN || player->GetClass() == CLASS_HUNTER) {
            if (itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_PLATE)
                return false;
            if (player->GetLevel() < 40 && itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_MAIL)
                return false;
        } else if (player->GetClass() == CLASS_DRUID || player->GetClass() == CLASS_ROGUE) {
            if (itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_PLATE
             || itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_MAIL)
                return false;
        } else {
            // player is mage/wlock/priest:
            if (itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_PLATE
              || itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_MAIL
              || itemTemplate->SubClass == ITEM_SUBCLASS_ARMOR_LEATHER)
                return false;
        }
    }
    return true;
}

void AutoBis::PopulateSingleHaveItem(Player *player, const ScoreWeightMap &swm, Item *item, ItemSlotMap &have_items,
                                     bool test_canuse)
{
    ItemTemplate const *itemTemplate = item->GetTemplate();
    if (!PlayerCanUseItem(player, itemTemplate))
        return;
    uint32 inv_type = itemTemplate->InventoryType;
    AdjustInvType(inv_type);
    double score = ComputePawnScore(swm, itemTemplate);
    have_items[inv_type].push_back({itemTemplate, score});
}

void AutoBis::PopulateHaveItems(Player *player, ItemSlotMap &have_items)
{
    const ScoreWeightMap &swm = GetScoreWeightMap(player);
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i) {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!item)
            continue;
        PopulateSingleHaveItem(player, swm, item, have_items, true);
    }
    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++) {
        Bag* bag = player->GetBagByPos(i);
        if (!bag)
            continue;
        for (uint32 j = 0; j < bag->GetBagSize(); j++) {
            Item* item = bag->GetItemByPos(j);
            if (!item)
                continue;
            PopulateSingleHaveItem(player, swm, item, have_items, true);
        }
    }
    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; i++) {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!item)
            continue;
        PopulateSingleHaveItem(player, swm, item, have_items, true);
    }
    for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++) {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!item)
            continue;
        PopulateSingleHaveItem(player, swm, item, have_items, true);
    }
    // in bank bags
    for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++) {
        Bag* bag = player->GetBagByPos(i);
        if (!bag)
            continue;
        for (uint32 j = 0; j < bag->GetBagSize(); j++) {
            Item* item = bag->GetItemByPos(j);
            if (!item)
                continue;
            PopulateSingleHaveItem(player, swm, item, have_items, true);
        }
    }
}

bool AutoBis::Process(ChatHandler* handler, char const* args)
{
    Player* player = handler->GetSession()->GetPlayer();
    uint8 playerLvl = player->GetLevel();
    if (playerLvl < 2)
        return true;
    //if (!*args)
    //    return false;
    struct ItemCompare {
        bool operator()(const ItemScore &left, const ItemScore &right) {
            return (left.second > right.second);
        }
    };
    // First, populate "have_items" map with player's inventory + bank:
    ItemSlotMap have_items;
    PopulateHaveItems(player, have_items);
    for (auto &have_slots : have_items) {
        SlotItems &slot_items = have_slots.second;
        //printf("%u: %lu\n", have_slots.first, slot_items.size());
        std::sort(slot_items.begin(), slot_items.end(), ItemCompare());
    }
    //
    #if 0
    // Test:
    //  50730: Glorenzelg, High-Blade of the Silver Hand (Heroic)
    double item_score = ComputePawnScore(50730);
    printf("50730: expected score == 215.20; got: %f\n", item_score);
    #endif
    std::string myStm = "SELECT entry FROM item_template WHERE (class=2 || class=4) && (Quality<=3) && "
                        "(FlagsExtra!=8192) && (ItemLevel<200) && (RequiredReputationFaction = 0) && "
                        "(SellPrice > 0) && "
                        "(RequiredLevel=" + std::to_string(playerLvl) + ");";
    std::vector<ItemTemplate const*> item_templates;
    std::shared_ptr<ResultSet> result = WorldDatabase.Query(myStm.c_str());
    if (!result) {
        printf("%s\n", myStm.c_str());
        printf("no results found (playerLvl = %u) ?!\n", playerLvl);
        return true;
    }
    do {
        Field* fields = result->Fetch();
        uint32 item_id = fields->GetUInt32();
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(item_id);
        if (!itemTemplate) {
            printf("INTERNAL ERROR: entry = %u not found in sObjectMgr !?\n", item_id);
            continue;
        }
        if (!PlayerCanUseItem(player, itemTemplate))
            continue;
        item_templates.push_back(itemTemplate);
    } while (result->NextRow());
    if (!item_templates.size())
        return true;
    const ScoreWeightMap &swm = GetScoreWeightMap(player);
    ItemSlotMap next_items;
    for (ItemTemplate const* item_template : item_templates) {
        uint32 inv_type = item_template->InventoryType;
        AdjustInvType(inv_type);
        SlotItems &have_si = have_items[inv_type];
        auto fiter = have_si.begin();
        for (; fiter != have_si.end(); ++fiter) {
            if (fiter->first == item_template)
                break;
        }
        if (fiter == have_si.end()) {
            double score = ComputePawnScore(swm, item_template);
            next_items[inv_type].push_back({item_template, score});
        }
    }
    for (auto &next_slots : next_items) {
        SlotItems &slot_items = next_slots.second;
        assert(slot_items.size() > 0);
        std::sort(slot_items.begin(), slot_items.end(), ItemCompare());
        ItemTemplate const* cur_have = nullptr;
        double prevscore = 0.0;
        SlotItems &cur_items = have_items[next_slots.first];
        if (cur_items.size() > 0) {
            cur_have = cur_items.begin()->first;
            prevscore = cur_items.begin()->second;
        }
        ItemTemplate const* next_item_templ = slot_items.begin()->first;
        double nextscore = slot_items.begin()->second;
        if (!cur_have || prevscore < nextscore) {
            uint32 item_id = next_item_templ->ItemId;
            ItemPosCountVec dest;
            InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item_id, 1);
            if (msg == EQUIP_ERR_OK) {
                Item* item = player->StoreNewItem(dest, item_id, true, GenerateItemRandomPropertyId(item_id));
                player->SendNewItem(item, 1, false, true);
            } else {
                handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, item_id, 1);
                return false;
            }
        }
    }
    //printf("item_templates.size() : %lu\n", item_templates.size());
    return true;
}

#include "autobis.h"

#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "Bag.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "Language.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

// NOTE: some items have "Equip: Increase your X by Y". We need to map those auras to ITEM_MODs for the stat calc.
//  If we're not calculating a specific stat, set RHS to INT_MIN.
// FIXME: Is there a "smarter" way of doing this that uses code written prior? I'm having to manually encode the mappings here...
static int32 ModRatingToItemMod(const SpellEffectInfo& sei)
{
    for (int idx = 0; idx < MAX_COMBAT_RATING; ++idx) {
        if (!(sei.MiscValue & (1 << idx)))
            continue;
        switch (idx) {
            case CR_WEAPON_SKILL:
                return INT_MIN;
            case CR_DEFENSE_SKILL:
                return ITEM_MOD_DEFENSE_SKILL_RATING;
            case CR_DODGE:
                return ITEM_MOD_DODGE_RATING;
            case CR_PARRY:
                return ITEM_MOD_PARRY_RATING;
            case CR_BLOCK:
                return ITEM_MOD_BLOCK_RATING;
            case CR_HIT_MELEE:
            case CR_HIT_RANGED:
            case CR_HIT_SPELL:
                return ITEM_MOD_HIT_RATING;
            case CR_CRIT_MELEE:
            case CR_CRIT_RANGED:
            case CR_CRIT_SPELL:
                return ITEM_MOD_CRIT_RATING;
            case CR_HIT_TAKEN_MELEE:
            case CR_HIT_TAKEN_RANGED:
            case CR_HIT_TAKEN_SPELL:
                return INT_MIN;
            case CR_CRIT_TAKEN_MELEE:
            case CR_CRIT_TAKEN_RANGED:
            case CR_CRIT_TAKEN_SPELL:
                return INT_MIN;
            case CR_HASTE_MELEE:
            case CR_HASTE_RANGED:
            case CR_HASTE_SPELL:
                return ITEM_MOD_HASTE_RATING;
            case CR_WEAPON_SKILL_MAINHAND:
            case CR_WEAPON_SKILL_OFFHAND:
            case CR_WEAPON_SKILL_RANGED:
                return INT_MIN;
            case CR_EXPERTISE:
                return ITEM_MOD_EXPERTISE_RATING;
            case CR_ARMOR_PENETRATION:
                return ITEM_MOD_ARMOR_PENETRATION_RATING;
            default:
                return INT_MIN;
        }
    }
    return INT_MIN;
}
int32 SpellEffectInfoToItemMod(const SpellEffectInfo& sei)
{
    if (sei.Effect != SPELL_EFFECT_APPLY_AURA)
        return INT_MIN;
    int value = sei.CalcValue();
    if (value <= 0)
        return INT_MIN;
    switch (sei.ApplyAuraName) {
        case SPELL_AURA_PERIODIC_HEAL:
        case SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT: // These function the same exact way in WOTLK.
            return ITEM_MOD_HEALTH_REGEN;
        case SPELL_AURA_MOD_RATING:
            return ModRatingToItemMod(sei);
        case SPELL_AURA_MOD_ATTACK_POWER:
            return ITEM_MOD_ATTACK_POWER;
        case SPELL_AURA_MOD_RANGED_ATTACK_POWER:
            return INT_MIN; // NOTE: In WOTLK, this stat is redundant to the above.
        case SPELL_AURA_MOD_DAMAGE_DONE:
        case SPELL_AURA_MOD_HEALING_DONE: // Hooray for WOTLK item stat changes! :D
            return ITEM_MOD_SPELL_POWER;
        case SPELL_AURA_PROC_TRIGGER_SPELL:             // Pawn doesn't rate these abilities; neither will we.
        case SPELL_AURA_MOD_MELEE_ATTACK_POWER_VERSUS:  // Don't weigh. Example: "Do extra damage against elementals/beasts/etc.."
        case SPELL_AURA_MOD_RANGED_ATTACK_POWER_VERSUS: // same idea as above...
        case SPELL_AURA_MOD_FLAT_SPELL_DAMAGE_VERSUS:   // same idea...
        case SPELL_AURA_MOD_SKILL:                      // Unimportant skill, don't weigh. But, should we?!
        case SPELL_AURA_MOD_INCREASE_SWIM_SPEED:        // ditto...
        case SPELL_AURA_MOD_RESISTANCE:
        case SPELL_AURA_MOD_STEALTH_LEVEL:
        case SPELL_AURA_DUMMY:
        case SPELL_AURA_OVERRIDE_CLASS_SCRIPTS: // ditto, but important. See: "Totem of Rage" (22395)
        case SPELL_AURA_ADD_FLAT_MODIFIER:      // ditto, but important. See: "Idol of Ferocity" (22397)
        case SPELL_AURA_DAMAGE_SHIELD:          // Thorns-esque effect. Don't weigh.
            return INT_MIN;
        case SPELL_AURA_MOD_POWER_REGEN:
            return ITEM_MOD_MANA_REGENERATION;
        case SPELL_AURA_MOD_SHIELD_BLOCKVALUE:
            return ITEM_MOD_BLOCK_VALUE;
        default: {
            LOG_DEBUG("module.autobis", "autobis: unhandled ApplyAuraName={}", uint32(sei.ApplyAuraName));
            return INT_MIN;
        }
    }
}

// https://github.com/Road-block/Pawn/blob/master/Wowhead.lua
//  I subtract 20 from HIT_RATING because we tend to get flooded with hit rating, overcapping the 8% limit...
static const AutoBis::ScoreWeightMap ret_paladin_map = {
    {-1, 470}, // melee_DPS
    {ITEM_MOD_HIT_RATING, 70},
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
    {-2, 8}, // armor
    {ITEM_MOD_BLOCK_RATING, 7},
    {ITEM_MOD_BLOCK_VALUE, 6},
    {-1, 0.00001}, // melee_DPS
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

static const AutoBis::ScoreWeightMap combat_rogue_map = {
    {-1, 220}, // melee_DPS
    {ITEM_MOD_ARMOR_PENETRATION_RATING, 100},
    {ITEM_MOD_AGILITY, 100},
    {ITEM_MOD_EXPERTISE_RATING, 82},
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_CRIT_RATING, 75},
    {ITEM_MOD_HASTE_RATING, 73},
    {ITEM_MOD_STRENGTH, 55},
    {ITEM_MOD_ATTACK_POWER, 50},
    {ITEM_MOD_STAMINA, 0.1},
    {-2, 0.01}, // armor
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
    {ITEM_MOD_HIT_RATING, 70},
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

static const AutoBis::ScoreWeightMap shadow_priest_map = {
    {ITEM_MOD_HIT_RATING, 80},
    {ITEM_MOD_SPELL_POWER, 76},
    {ITEM_MOD_CRIT_RATING, 54},
    {ITEM_MOD_HASTE_RATING, 50},
    {ITEM_MOD_SPIRIT, 16},
    {ITEM_MOD_INTELLECT, 16},
    {ITEM_MOD_STAMINA, 0.1},
    {-1, 0.0001}, // melee_DPS
    {-2, 0.0001}, // armor
    {-3, 0.0001}, // ranged_DPS
};

const AutoBis::ScoreWeightMap& AutoBis::GetScoreWeightMap(Player *player)
{
    if (player->getClass() == CLASS_PALADIN) {
        if (Item* offhand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            return prot_paladin_map;
        else
            return ret_paladin_map;
    } else if (player->getClass() == CLASS_HUNTER)
        return bm_hunter_map;
    else if (player->getClass() == CLASS_WARRIOR)
        return fury_warrior_map;
    else if (player->getClass() == CLASS_MAGE)
        return frost_mage_map;
    else if (player->getClass() == CLASS_DRUID) {
        if (player->GetLevel() < 10)
            return boomkin_map;
        else
            return cat_druid_map;
    }
    else if (player->getClass() == CLASS_SHAMAN)
        return enh_shaman_map;
    else if (player->getClass() == CLASS_PRIEST)
        return shadow_priest_map;
    else if (player->getClass() == CLASS_ROGUE)
        return combat_rogue_map;
    return ret_paladin_map;
}

static bool CanOneDualWield(Player* player)
{
    // Warriors: look for off hand weapon specialization:
    if (player->getClass() == CLASS_WARRIOR)
        return (player->HasSpell(23584) || player->HasSpell(23585) || player->HasSpell(23586) || player->HasSpell(23587)
             || player->HasSpell(23588));
    return ((player->HasSpell(674) || player->HasSpell(30798)) && !player->HasSpell(46917));
}

void AutoBis::AdjustInvType(Player* player, uint32 &inv_type)
{
    if (inv_type == INVTYPE_WEAPONMAINHAND) {
        if (CanOneDualWield(player))
            return;
        inv_type = INVTYPE_WEAPON;
    } else if (inv_type == INVTYPE_ROBE) {
        inv_type = INVTYPE_CHEST;
    } else if (inv_type == INVTYPE_HOLDABLE || inv_type == INVTYPE_WEAPONOFFHAND) {
        inv_type = INVTYPE_SHIELD;
    } else if (inv_type == INVTYPE_THROWN || inv_type == INVTYPE_RANGEDRIGHT) {
        inv_type = INVTYPE_RANGED;
    }
}


//
// see src/server/game/Entities/Item/ItemEnchantmentMgr.cpp
//
// Unlike GenerateItemRandomPropertyId(), we don't care about the chances (except to initially populate the unordered_map).
typedef std::vector<uint32> EnchStoreList;
struct AbEnchantmentStore {
    void LoadRandomEnchantmentsTable();
    std::unordered_map<uint32, EnchStoreList> _store;
    bool _needs_init = true;
};

static AbEnchantmentStore randomItemEnch;

void AbEnchantmentStore::LoadRandomEnchantmentsTable()
{
    if (!_needs_init)
        return;
    _needs_init = false;
    _store.clear();
    //                                                 0      1      2
    QueryResult result = WorldDatabase.Query("SELECT entry, ench, chance FROM item_enchantment_template");
    if (result) {
        do {
            Field* fields = result->Fetch();
            uint32 entry = fields[0].Get<uint32>();
            uint32 ench = fields[1].Get<uint32>();
            float chance = fields[2].Get<float>();
            if (chance > 0.000001f && chance <= 100.0f)
                _store[entry].push_back(ench);
        } while (result->NextRow());
    }
}

double AutoBis::CalculateBestRandomEnchant(const ScoreWeightMap &score_weights, ItemTemplate const* itemProto, int32& enchId)
{
    randomItemEnch.LoadRandomEnchantmentsTable();
    enchId = 0;
    // Items with random suffixes/properties must have one of the two:
    bool do_debug = false; // (itemProto->ItemId == 25117);
    double totalWeight = 0.0;
    for (auto entry : score_weights) {
        totalWeight += entry.second;
    }
    // RandomSuffix and RandomProperty _should_ be mutually exclusive.
    //  If, for some reason, both are set. Just take from "RandomProperty":
    if (itemProto->RandomProperty || itemProto->RandomSuffix) {
        bool rand_suffix = false;
        uint32 ench_idx = itemProto->RandomProperty;
        if (!ench_idx) {
            ench_idx = itemProto->RandomSuffix;
            rand_suffix = true;
        }
        auto riefiter = randomItemEnch._store.find(ench_idx);
        if (riefiter == randomItemEnch._store.end())
            return 0; // Internal error!?
        const EnchStoreList& list = riefiter->second;
        double best_score = -1000.0; // because we want at least 1 enchant
        for (uint32 ench : list) {
            double cur_score = 0;
            if (!rand_suffix) {
                ItemRandomPropertiesEntry const* propEntry = sItemRandomPropertiesStore.LookupEntry(ench);
                if (!propEntry) {
                    if (do_debug)
                        LOG_DEBUG("module.autobis", "  ench={} doesn't have ItemRandomPropertiesEntry", ench);
                    continue; // Internal error!?
                }
                for (unsigned idx = 0; idx < MAX_ITEM_ENCHANTMENT_EFFECTS; ++idx) {
                    uint32 subench = propEntry->Enchantment[idx];
                    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(subench);
                    if (!pEnchant)
                        continue; // Internal error!?
                    for (uint8 tt = 0; tt < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++tt) {
                        if (do_debug) {
                            LOG_DEBUG("module.autobis", "       enchId = {}:{}, type = {}, stat = {}, amount = {}",
                                      ench, subench, pEnchant->type[tt], pEnchant->spellid[tt],
                                      pEnchant->amount[tt]);
                        }
                        if (pEnchant->type[tt] != ITEM_ENCHANTMENT_TYPE_STAT)
                            continue;
                        int32 statId = pEnchant->spellid[tt];
                        auto fiter = score_weights.find(statId);
                        if (fiter != score_weights.end()) {
                            cur_score += pEnchant->amount[tt] * fiter->second / totalWeight;
                        }
                    }
                }
            } else {
                // Items with RandomSuffix are handled way differently than RandomProp. Suffixes are scaled based
                //  on a scaling factor, which can be queried for. We need to calculate how much value per item:
                ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(ench);
                if (!item_rand) {
                    if (do_debug)
                        LOG_DEBUG("module.autobis", "  ench={} doesn't have ItemRandomSuffixEntry", ench);
                    continue; // internal error?!
                }
                uint32 scalefact = GenerateEnchSuffixFactor(itemProto->ItemId);
                for (int k = 0; k < MAX_ITEM_ENCHANTMENT_EFFECTS; ++k) {
                    uint32 myEnchantment = item_rand->Enchantment[k];
                    if (!myEnchantment)
                        continue;
                    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(myEnchantment);
                    if (!pEnchant) {
                        if (do_debug)
                            LOG_DEBUG("module.autobis", "  ench={}:{}, Enchantment={} doesn't have pEnchant", ench_idx, ench, myEnchantment);
                        continue;
                    }
                    uint32 enchant_amount = uint32((item_rand->AllocationPct[k] * scalefact) / 10000);
                    if (do_debug) {
                        LOG_DEBUG("module.autobis", "      enchId={}:{}, Enchantment = {}, AllocationPct = {}, scale = {}, amount={}",
                              ench_idx, ench, myEnchantment, item_rand->AllocationPct[k], scalefact, enchant_amount);
                    }
                    for (uint8 tt = 0; tt < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++tt) {
                        if (pEnchant->type[tt] != ITEM_ENCHANTMENT_TYPE_STAT)
                            continue;
                        if (do_debug) {
                            LOG_DEBUG("module.autobis", "           type = {}, stat = {}, amount = {}",
                                      pEnchant->type[tt], pEnchant->spellid[tt], pEnchant->amount[tt]);
                        }
                        int32 statId = pEnchant->spellid[tt];
                        auto fiter = score_weights.find(statId);
                        if (fiter != score_weights.end()) {
                            cur_score += enchant_amount * fiter->second / totalWeight;
                        }
                    }
                }
            }
            if (do_debug)
                LOG_DEBUG("module.autobis", "       enchId = {}, score = {}", ench, cur_score);
            if (cur_score > best_score) {
                if (!rand_suffix)
                    enchId = ((int32) ench);
                else
                    enchId = -((int32) ench);
                best_score = cur_score;
            }
        }
        if (best_score <= 0) {
            // Internal error!? print something out..
            enchId = Item::GenerateItemRandomPropertyId(itemProto->ItemId);
            return 0;
        }
        return best_score;
    } else
        return 0;
}

double AutoBis::ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate)
{
    // ...
    // AzerothCore keeps dmg_min1/dmg_max1 in the item template, so no per-item SQL query is needed:
    double dmg_min1 = itemTemplate->Damage[0].DamageMin;
    double dmg_max1 = itemTemplate->Damage[0].DamageMax;
    uint32 armor = itemTemplate->Armor;
    //
    double totalScore = 0.0, totalWeight = 0.0;
    for (auto entry : score_weights) {
        totalWeight += entry.second;
    }
    int block = itemTemplate->Block;
    if (block) {
        auto fiter = score_weights.find(ITEM_MOD_BLOCK_VALUE);
        if (fiter != score_weights.end()) {
            totalScore += block * fiter->second / totalWeight;
        }
    }
    if (itemTemplate->Class == ITEM_CLASS_WEAPON && itemTemplate->Delay > 0) {
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
    for (uint32 idx = 0; idx < itemTemplate->StatsCount; ++idx) {
        int32 statId = itemTemplate->ItemStat[idx].ItemStatType;
        int32 statVal = itemTemplate->ItemStat[idx].ItemStatValue;
        if (statVal <= 0)
            continue;
        auto fiter = score_weights.find(statId);
        if (fiter != score_weights.end()) {
            totalScore += statVal * fiter->second / totalWeight;
        }
    }
    // Items can also have: "Equip: Increase X by Y" attributes. These are separate:
    std::unordered_set<int> seen_stat_ids;
    for (uint32 idx = 0; idx < MAX_ITEM_PROTO_SPELLS; ++idx) {
        uint32 spellid = itemTemplate->Spells[idx].SpellId;
        if (spellid <= 0 || itemTemplate->Spells[idx].SpellTrigger != ITEM_SPELLTRIGGER_ON_EQUIP)
            continue;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid);
        if (!spellInfo)
            continue; // Internal error??
        for (uint8 jdx = 0; jdx < MAX_SPELL_EFFECTS; ++jdx) {
            const SpellEffectInfo& sei = spellInfo->Effects[jdx];
            int statId = SpellEffectInfoToItemMod(sei);
            if (statId == INT_MIN)
                continue; // we're not weighing this Equip stat
            int value = sei.CalcValue();
            // FIXME: For some reason, this loop will iterate the same power/value twice. Prevent this:
            if (seen_stat_ids.find(statId) != seen_stat_ids.end())
                continue;
            seen_stat_ids.insert(statId);
            auto fiter = score_weights.find(statId);
            if (fiter != score_weights.end()) {
                totalScore += value * fiter->second / totalWeight;
            }
        }
    }
    int32 enchId;
    totalScore += CalculateBestRandomEnchant(score_weights, itemTemplate, enchId);
    return totalScore;
}

bool AutoBis::PlayerCanUseItem(Player *player, ItemTemplate const* itemTemplate)
{
    bool titans_grip = player->getClass() == CLASS_WARRIOR && player->HasSpell(46917);
    if (!itemTemplate)
        return false; // INTERNAL ERROR
    uint32 inv_type = itemTemplate->InventoryType;
    uint32 subclass = itemTemplate->SubClass;
    if (inv_type == 0)
        return false;
    if (EQUIP_ERR_OK != player->CanUseItem(itemTemplate))
        return false;
    if (inv_type == INVTYPE_SHIELD) {
        // only shamans, warriors, and paladins can use shields:
        if (player->getClass() != CLASS_WARRIOR && player->getClass() != CLASS_PALADIN
            && player->getClass() != CLASS_SHAMAN)
            return false;
    }
    if (itemTemplate->Class == ITEM_CLASS_WEAPON) {
        uint32 skill = itemTemplate->GetSkill();
        if (!skill || player->GetSkillValue(skill) == 0)
            return false; // player cannot equip this item
        // If a warrior has Titan's Grip, they can't onehand polearms nor staffs:
        if (titans_grip && (subclass == ITEM_SUBCLASS_WEAPON_SPEAR || subclass == ITEM_SUBCLASS_WEAPON_STAFF))
            return false;
    }
    if (itemTemplate->Class == ITEM_CLASS_ARMOR) {
        if (player->getClass() == CLASS_WARRIOR || player->getClass() == CLASS_PALADIN
            || player->getClass() == CLASS_DEATH_KNIGHT) {
            // plate doesn't start to show up til lvl 40. Don't do checks for pal/warr/DKs..
        } else if (player->getClass() == CLASS_SHAMAN || player->getClass() == CLASS_HUNTER) {
            if (subclass == ITEM_SUBCLASS_ARMOR_PLATE)
                return false;
            if (player->GetLevel() < 40 && subclass == ITEM_SUBCLASS_ARMOR_MAIL)
                return false;
        } else if (player->getClass() == CLASS_DRUID || player->getClass() == CLASS_ROGUE) {
            if (subclass == ITEM_SUBCLASS_ARMOR_PLATE
             || subclass == ITEM_SUBCLASS_ARMOR_MAIL)
                return false;
        } else {
            // player is mage/wlock/priest:
            if (subclass == ITEM_SUBCLASS_ARMOR_PLATE
              || subclass == ITEM_SUBCLASS_ARMOR_MAIL
              || subclass == ITEM_SUBCLASS_ARMOR_LEATHER)
                return false;
        }
        // Totems, Sigils, etc need to be checked against player class:
        if (subclass == ITEM_SUBCLASS_ARMOR_LIBRAM) {
            if (player->getClass() != CLASS_PALADIN)
                return false;
        } else if (subclass == ITEM_SUBCLASS_ARMOR_IDOL) {
            if (player->getClass() != CLASS_DRUID)
                return false;
        } else if (subclass == ITEM_SUBCLASS_ARMOR_TOTEM) {
            if (player->getClass() != CLASS_SHAMAN)
                return false;
        } else if (subclass == ITEM_SUBCLASS_ARMOR_SIGIL) {
            if (player->getClass() != CLASS_DEATH_KNIGHT)
                return false;
        }
    }
    return true;
}

void AutoBis::PopulateSingleHaveItem(Player *player, const ScoreWeightMap &swm, Item *item, ItemSlotMap &have_items,
                                     [[maybe_unused]] bool test_canuse)
{
    ItemTemplate const *itemTemplate = item->GetTemplate();
    if (!PlayerCanUseItem(player, itemTemplate))
        return;
    uint32 inv_type = itemTemplate->InventoryType;
    AdjustInvType(player, inv_type);
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

bool AutoBis::Process(ChatHandler* handler)
{
    Player* player = handler->GetPlayer();
    bool titans_grip = player->getClass() == CLASS_WARRIOR && player->HasSpell(46917);
    bool oh_dual = CanOneDualWield(player);
    uint8 playerLvl = player->GetLevel();
    if (playerLvl < 2) {
        handler->SendSysMessage("autobis: you must be at least level 2.");
        return true;
    }
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
        std::sort(slot_items.begin(), slot_items.end(), ItemCompare());
    }
    // Same filter the TrinityCore version ran as SQL, applied to the in-memory item template store instead:
    //   (class=2 || class=4) && Quality<=3 && !(FlagsExtra & 0x2000) && ItemLevel<200 &&
    //   RequiredReputationFaction=0 && SellPrice>0 && RequiredLevel=<player level>
    // Plus: skip deprecated items (e.g. "Deprecated Worn Leather Shoulderpads"). The original
    // FlagsExtra!=8192 compare was meant to do that, but in AC's data those items carry
    // ITEM_FLAG_DEPRECATED instead. The 0x2000 (ITEM_FLAG2_SHOW_BEFORE_DISCOVERED) bit is kept
    // since it mostly marks monster/test items.
    std::vector<ItemTemplate const*> item_templates;
    for (auto const& [itemId, itemTemplate] : *sObjectMgr->GetItemTemplateStore()) {
        if (itemTemplate.Class != ITEM_CLASS_WEAPON && itemTemplate.Class != ITEM_CLASS_ARMOR)
            continue;
        if (itemTemplate.Quality > ITEM_QUALITY_RARE || itemTemplate.HasFlag(ITEM_FLAG_DEPRECATED)
            || itemTemplate.HasFlag2(ITEM_FLAG2_SHOW_BEFORE_DISCOVERED)
            || itemTemplate.ItemLevel >= 200 || itemTemplate.RequiredReputationFaction != 0
            || itemTemplate.SellPrice == 0 || itemTemplate.RequiredLevel != playerLvl)
            continue;
        if (!PlayerCanUseItem(player, &itemTemplate))
            continue;
        item_templates.push_back(&itemTemplate);
    }
    // The SQL query returned rows ordered by entry; keep that order so results are deterministic:
    std::sort(item_templates.begin(), item_templates.end(),
              [](ItemTemplate const* l, ItemTemplate const* r) { return l->ItemId < r->ItemId; });
    if (!item_templates.size())
        return true;
    const ScoreWeightMap &swm = GetScoreWeightMap(player);
    ItemSlotMap next_items;
    for (ItemTemplate const* item_template : item_templates) {
        uint32 inv_type = item_template->InventoryType;
        // In the loop prior we used "PlayerCanUseItem()", so don't use it here.
        AdjustInvType(player, inv_type);
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
        uint32 invtype = next_slots.first;
        if (invtype == INVTYPE_SHIELD || invtype == INVTYPE_WEAPONMAINHAND) {
            // Don't bother with "Main Hand" and "Shield/Offhand" items. "One Handed" items are sufficient.
            if (oh_dual || titans_grip)
                continue;
        }
        // don't give one-handed weapons to warriors with Titan's Grip
        if (invtype == INVTYPE_WEAPON && titans_grip)
            continue;
        SlotItems &slot_items = next_slots.second;
        ASSERT(!slot_items.empty());
        std::sort(slot_items.begin(), slot_items.end(), ItemCompare());
        ItemTemplate const* cur_have = nullptr;
        double prevscore = 0.0;
        SlotItems &cur_items = have_items[invtype];
        if (cur_items.size() > 0) {
            cur_have = cur_items.begin()->first;
            prevscore = cur_items.begin()->second;
        }
        ItemTemplate const* next_item_templ = slot_items.begin()->first;
        double nextscore = slot_items.begin()->second;
        bool second_best = false;
        bool use_two = (invtype == INVTYPE_FINGER || invtype == INVTYPE_TRINKET || (invtype == INVTYPE_WEAPON && oh_dual)
                        || (invtype == INVTYPE_2HWEAPON && titans_grip));
        // We don't beat the 1st item, but let's see if we beat the 2nd item:
        if (cur_have && prevscore >= nextscore && use_two) {
            // don't give duplicates:
            if (cur_have->ItemId != next_item_templ->ItemId) {
                if (cur_items.size() > 1) {
                    if (cur_items[1].first->ItemId != next_item_templ->ItemId && nextscore > cur_items[1].second)
                        second_best = true;
                } else
                    second_best = true;
            }
        }
        if (!cur_have || prevscore < nextscore || second_best) {
            uint32 item_id = next_item_templ->ItemId;
            ItemPosCountVec dest;
            InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item_id, 1);
            if (msg == EQUIP_ERR_OK) {
                int32 enchId;
                CalculateBestRandomEnchant(swm, next_item_templ, enchId);
                Item* item = player->StoreNewItem(dest, item_id, true, enchId);
                player->SendNewItem(item, 1, false, true);
            } else {
                handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, item_id, 1);
                return false;
            }
            // let's see if we can add two items!
            if (!second_best && use_two && slot_items.size() > 1) {
                ItemTemplate const* next_next_proto = slot_items[1].first;
                double nextnext = slot_items[1].second;
                // We need to beat out the best item we already have (if it exists, otherwise win automatically).
                if (cur_have) {
                    if (next_next_proto->ItemId != cur_have->ItemId && nextnext > prevscore)
                        second_best = true;
                } else
                    second_best = true;
                if (second_best) {
                    ItemPosCountVec dest2;
                    uint32 item_id2 = next_next_proto->ItemId;
                    msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest2, item_id2, 1);
                    if (msg == EQUIP_ERR_OK) {
                        int32 enchId;
                        CalculateBestRandomEnchant(swm, next_next_proto, enchId);
                        Item* item = player->StoreNewItem(dest2, item_id2, true, enchId);
                        player->SendNewItem(item, 1, false, true);
                    } else {
                        handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, item_id2, 1);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

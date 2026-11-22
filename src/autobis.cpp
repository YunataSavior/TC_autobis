#include "autobis.h"

#include <algorithm>
#include <climits>
#include <functional>
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

// I halved the expertise rating, as otherwise gems will only default to expertise
static const AutoBis::ScoreWeightMap fury_warrior_map = {
    {ITEM_MOD_EXPERTISE_RATING, 50},
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

//
// Gems
//

static double TotalWeight(const AutoBis::ScoreWeightMap& swm)
{
    double totalWeight = 0.0;
    for (auto const& entry : swm)
        totalWeight += entry.second;
    return totalWeight;
}

// Weapon procs (and a few auras) that the generic scorer can't value, as rough "always on" stat
//  equivalents (proc value x uptime). Key: SpellItemEnchantment id. -1 is weapon DPS, -2 is armor,
//  like in the weight maps. An enchant listed here is scored from this table only.
// These are hand-tuned guesses: adjust freely.
static std::unordered_map<uint32, std::vector<std::pair<int32, double>>> const proc_enchant_values = {
    {803,  {{-1, 4}}},                                                // Fiery Weapon
    {1894, {{-1, 1}}},                                                // Icy Chill
    {1898, {{-1, 3}}},                                                // Lifestealing
    {1899, {{-1, 4}}},                                                // Unholy Weapon
    {1900, {{ITEM_MOD_STRENGTH, 35}}},                                // Crusader
    {2673, {{ITEM_MOD_AGILITY, 40}, {ITEM_MOD_HASTE_RATING, 15}}},    // Mongoose
    {3225, {{ITEM_MOD_ARMOR_PENETRATION_RATING, 40}}},                // Executioner
    {3239, {{-1, 4}}},                                                // Icebreaker
    {3789, {{ITEM_MOD_ATTACK_POWER, 120}}},                           // Berserking
    {3869, {{ITEM_MOD_PARRY_RATING, 50}}},                            // Blade Ward
    // Death knight runeforging (only when the DK knows the rune):
    {3368, {{ITEM_MOD_STRENGTH, 60}}},                                // Rune of the Fallen Crusader
    {3369, {{-1, 3}}},                                                // Rune of Cinderglacier
    {3370, {{-1, 5}}},                                                // Rune of Razorice
    {3847, {{ITEM_MOD_STAMINA, 25}, {-2, 400}}},                      // Rune of the Stoneskin Gargoyle
};

// Score of a single SpellItemEnchantment (gem, socket bonus, permanent enchant), using the same weights
//  as item stats. "proto" is the item it goes on (needed for "+X weapon damage"). If "scorable" is set,
//  it's cleared when part of the enchant can't be valued (unknown proc, on-use effect, unmapped aura).
static double ScoreEnchantment(const AutoBis::ScoreWeightMap& swm, uint32 enchId,
                               ItemTemplate const* proto = nullptr, bool* scorable = nullptr)
{
    if (!enchId)
        return 0.0;
    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchId);
    double totalWeight = TotalWeight(swm);
    if (!pEnchant || totalWeight <= 0.0)
        return 0.0;
    auto weight = [&](int32 statId) {
        auto fiter = swm.find(statId);
        return fiter != swm.end() ? fiter->second : 0.0;
    };
    double score = 0.0;
    auto ptable = proc_enchant_values.find(enchId);
    if (ptable != proc_enchant_values.end()) {
        for (auto const& [statId, amount] : ptable->second)
            score += amount * weight(statId) / totalWeight;
        return score;
    }
    for (uint8 tt = 0; tt < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++tt) {
        switch (pEnchant->type[tt]) {
            case ITEM_ENCHANTMENT_TYPE_NONE:
                break;
            case ITEM_ENCHANTMENT_TYPE_STAT:
                score += pEnchant->amount[tt] * weight(int32(pEnchant->spellid[tt])) / totalWeight;
                break;
            case ITEM_ENCHANTMENT_TYPE_RESISTANCE:
                // spellid is the school; physical "resistance" is armor:
                if (pEnchant->spellid[tt] == SPELL_SCHOOL_NORMAL)
                    score += pEnchant->amount[tt] * weight(-2) / totalWeight;
                break;
            case ITEM_ENCHANTMENT_TYPE_DAMAGE:
                // "+X weapon damage": convert to DPS like the weapon itself.
                if (proto && proto->Class == ITEM_CLASS_WEAPON && proto->Delay > 0) {
                    uint32 invtype = proto->InventoryType;
                    bool ranged = invtype == INVTYPE_RANGED || invtype == INVTYPE_THROWN || invtype == INVTYPE_RANGEDRIGHT;
                    score += 1000.0 * pEnchant->amount[tt] / proto->Delay * weight(ranged ? -3 : -1) / totalWeight;
                }
                break;
            case ITEM_ENCHANTMENT_TYPE_EQUIP_SPELL: {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(pEnchant->spellid[tt]);
                bool mapped = false;
                if (spellInfo) {
                    // Spell power spells carry both "damage done" and "healing done" effects; count once.
                    std::unordered_set<int32> seen;
                    for (uint8 jdx = 0; jdx < MAX_SPELL_EFFECTS; ++jdx) {
                        int32 statId = SpellEffectInfoToItemMod(spellInfo->Effects[jdx]);
                        if (statId == INT_MIN || !seen.insert(statId).second)
                            continue;
                        mapped = true;
                        score += spellInfo->Effects[jdx].CalcValue() * weight(statId) / totalWeight;
                    }
                }
                if (!mapped && scorable)
                    *scorable = false;
                break;
            }
            default: // procs not in the table above, on-use effects, totems, sockets
                if (scorable)
                    *scorable = false;
                break;
        }
    }
    return score;
}

// Can the player use this gem at all (level, skill, not unique/limited)? Fills "out" if so.
// Meta gems: always presume the player has enough colors to activate them, and make sure a meta
//  socket always wins (head gear with a meta gem beats head gear without). The advised meta for the
//  class/spec beats any other meta, and a meta that's already socketed beats both (it may have been
//  chosen by the player, e.g. a bear druid socketing a tank meta).
static constexpr double META_SCORE = 1000000.0;

bool AutoBis::GemUsableBy(Player* player, ItemTemplate const* gem, GemCandidate& out, const ScoreWeightMap& swm,
                          uint32 recMeta)
{
    if (!gem || gem->Class != ITEM_CLASS_GEM || !gem->GemProperties)
        return false;
    GemPropertiesEntry const* gemProps = sGemPropertiesStore.LookupEntry(gem->GemProperties);
    if (!gemProps || !gemProps->spellitemenchantement || !gemProps->color)
        return false;
    // Keep it simple: no unique-equipped or limit-category (e.g. jewelcrafter-only) gems.
    if (gem->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) || gem->ItemLimitCategory)
        return false;
    if (gem->HasFlag(ITEM_FLAG_DEPRECATED) || gem->HasFlag2(ITEM_FLAG2_SHOW_BEFORE_DISCOVERED))
        return false;
    // No item level gating: any gem up to WotLK may be used (e.g. TBC gear gets WotLK gems), as long
    //  as the game's own requirements (required level/skill, enchant required level) allow it.
    if (player->CanUseItem(gem) != EQUIP_ERR_OK)
        return false;
    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(gemProps->spellitemenchantement);
    if (!pEnchant || pEnchant->requiredLevel > player->GetLevel())
        return false;
    if (pEnchant->requiredSkill && player->GetSkillValue(pEnchant->requiredSkill) < pEnchant->requiredSkillValue)
        return false;
    out.gem = gem;
    out.enchId = gemProps->spellitemenchantement;
    out.color = gemProps->color;
    out.score = ScoreEnchantment(swm, out.enchId);
    if (out.color & SOCKET_COLOR_META)
        out.score += (gem->ItemId == recMeta) ? 2 * META_SCORE : META_SCORE;
    return true;
}

// Which talent tree has the most points? -1 if no talents spent (then use the class fallback).
static int8 InferTalentTree(Player* player)
{
    if (player->CalculateTalentsPoints() <= player->GetFreeTalentPoints())
        return -1;
    return int8(player->GetMostPointsTalentTree());
}

// Meta gems people advise per class/spec. Each list is in order of preference: WotLK first, then the
//  TBC equivalent as a fallback in case the WotLK one isn't usable.
static std::vector<uint32> const& AdvisedMetaGems(Player* player)
{
    static const std::vector<uint32> physical = {41398 /* Relentless Earthsiege */, 32409 /* Relentless Earthstorm */};
    static const std::vector<uint32> tank     = {41380 /* Austere Earthsiege */,    25896 /* Powerful Earthstorm */};
    static const std::vector<uint32> caster   = {41285 /* Chaotic Skyflare */,      34220 /* Chaotic Skyfire */};
    static const std::vector<uint32> healer   = {41401 /* Insightful Earthsiege */, 25901 /* Insightful Earthstorm */};
    static const std::vector<uint32> hot_heal = {41376 /* Revitalizing Skyflare */, 25901 /* Insightful Earthstorm */};
    int8 tree = InferTalentTree(player); // talent tab page: 0, 1, 2
    switch (player->getClass()) {
        case CLASS_WARRIOR:      // Arms, Fury, Protection
            return tree == 2 ? tank : physical;
        case CLASS_PALADIN:      // Holy, Protection, Retribution
            if (tree == 0) return healer;
            if (tree == 1) return tank;
            return physical;
        case CLASS_DEATH_KNIGHT: // Blood, Frost, Unholy
            return tree == 0 ? tank : physical;
        case CLASS_PRIEST:       // Discipline, Holy, Shadow
            if (tree == 0) return healer;
            if (tree == 1) return hot_heal;
            return caster;
        case CLASS_SHAMAN:       // Elemental, Enhancement, Restoration
            if (tree == 0) return caster;
            if (tree == 2) return hot_heal;
            return physical;     // fallback matches the enhancement weight map
        case CLASS_DRUID:        // Balance, Feral, Restoration
            if (tree == 0) return caster;
            if (tree == 2) return hot_heal;
            return physical;     // fallback matches the cat weight map
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            return caster;
        default:                 // Hunter, Rogue
            return physical;
    }
}

uint32 AutoBis::RecommendedMetaGem(Player* player, const ScoreWeightMap& swm)
{
    for (uint32 gemId : AdvisedMetaGems(player)) {
        GemCandidate cand;
        if (GemUsableBy(player, sObjectMgr->GetItemTemplate(gemId), cand, swm, gemId))
            return gemId;
    }
    return 0;
}

// Gems autobis may hand out for free: uncommon/rare (no epics).
AutoBis::GemPool AutoBis::BuildGemPool(Player* player, const ScoreWeightMap& swm, uint32 recMeta)
{
    GemPool pool;
    for (auto const& [itemId, gemTemplate] : *sObjectMgr->GetItemTemplateStore()) {
        if (gemTemplate.Class != ITEM_CLASS_GEM)
            continue;
        if (gemTemplate.Quality > ITEM_QUALITY_RARE && itemId != recMeta)
            continue;
        GemCandidate cand;
        if (!GemUsableBy(player, &gemTemplate, cand, swm, recMeta) || cand.score <= 0.0)
            continue;
        cand.source = GemSource::Pool;
        pool.push_back(cand);
    }
    // Deterministic tie-breaking:
    std::sort(pool.begin(), pool.end(),
              [](GemCandidate const& l, GemCandidate const& r) { return l.gem->ItemId < r.gem->ItemId; });
    return pool;
}

static void ForEachOwnedItem(Player* player, std::function<void(Item*)> const& fn)
{
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            fn(item);
    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
        if (Bag* bag = player->GetBagByPos(i))
            for (uint32 j = 0; j < bag->GetBagSize(); j++)
                if (Item* item = bag->GetItemByPos(j))
                    fn(item);
    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            fn(item);
    for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            fn(item);
    for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
        if (Bag* bag = player->GetBagByPos(i))
            for (uint32 j = 0; j < bag->GetBagSize(); j++)
                if (Item* item = bag->GetItemByPos(j))
                    fn(item);
}

// Gems the player already owns (any quality, e.g. epics from the AH). These get consumed when used.
AutoBis::GemPool AutoBis::BuildOwnedGems(Player* player, const ScoreWeightMap& swm, OwnedGemCounts& counts,
                                         uint32 recMeta)
{
    counts.clear();
    ForEachOwnedItem(player, [&counts](Item* item) {
        if (item->GetTemplate()->Class == ITEM_CLASS_GEM && !item->IsInTrade())
            counts[item->GetEntry()] += item->GetCount();
    });
    GemPool owned;
    for (auto const& [gemId, count] : counts) {
        GemCandidate cand;
        if (!GemUsableBy(player, sObjectMgr->GetItemTemplate(gemId), cand, swm, recMeta) || cand.score <= 0.0)
            continue;
        cand.source = GemSource::Owned;
        owned.push_back(cand);
    }
    return owned;
}

static bool GemFitsSocket(uint32 socketColor, uint32 gemColor)
{
    // Meta sockets only take meta gems, and meta gems only go in meta sockets:
    if (socketColor & SOCKET_COLOR_META)
        return (gemColor & SOCKET_COLOR_META) != 0;
    return !(gemColor & SOCKET_COLOR_META);
}

// Pick gems for every socket of "proto". Two layouts:
//  (a) every gem matches its socket color (pure or mixed, e.g. orange fits red and yellow), so the
//      socket bonus is earned. This is always used when possible;
//  (b) otherwise, the best-scoring gem in every socket.
// "owned" gems are limited by "ownedCounts"; "current" are the gems already in this item (kept if best).
AutoBis::GemPlan AutoBis::PlanSockets(const ScoreWeightMap& swm, ItemTemplate const* proto, GemPool const& pool,
                                      GemPool const* owned, OwnedGemCounts const* ownedCounts,
                                      std::array<GemCandidate, 3> const* current)
{
    GemPlan best;
    bool have_best = false;
    for (int pass = 0; pass < 2; ++pass) {
        bool match_colors = (pass == 0);
        OwnedGemCounts counts = ownedCounts ? *ownedCounts : OwnedGemCounts();
        GemPlan plan;
        bool valid = true, all_match = true, has_socket = false;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i) {
            uint32 socketColor = proto->Socket[i].Color;
            if (!socketColor)
                continue;
            has_socket = true;
            GemCandidate const* pick = nullptr;
            auto consider = [&](GemCandidate const& cand) {
                if (!cand.gem || !GemFitsSocket(socketColor, cand.color))
                    return;
                if (match_colors && !(socketColor & cand.color))
                    return;
                if (cand.source == GemSource::Owned && counts[cand.gem->ItemId] == 0)
                    return;
                // strictly better only: ties prefer what's already socketed, then free gems, then owned.
                if (!pick || cand.score > pick->score)
                    pick = &cand;
            };
            if (current)
                consider((*current)[i]);
            for (GemCandidate const& cand : pool)
                consider(cand);
            if (owned)
                for (GemCandidate const& cand : *owned)
                    consider(cand);
            if (!pick) {
                if (match_colors)
                    valid = false;
                all_match = false;
                continue;
            }
            if (pick->source == GemSource::Owned)
                --counts[pick->gem->ItemId];
            if (!(socketColor & pick->color))
                all_match = false;
            plan.gems[i] = *pick;
            plan.score += pick->score;
        }
        if (!has_socket)
            return GemPlan();
        if (!valid)
            continue;
        if (all_match) {
            plan.score += ScoreEnchantment(swm, proto->socketBonus);
            plan.bonus = true;
        }
        if (match_colors)
            return plan; // always activate the socket bonus when we can
        if (!have_best || plan.score > best.score) {
            best = plan;
            have_best = true;
        }
    }
    return best;
}

// Socket the best gems into "item", replacing worse ones. Mirrors WorldSession::HandleSocketOpcode.
uint32 AutoBis::AutoGemItem(Player* player, Item* item, const ScoreWeightMap& swm, GemPool const& pool,
                            GemPool const& owned, OwnedGemCounts& ownedCounts, uint32 recMeta)
{
    ItemTemplate const* proto = item->GetTemplate();
    bool has_socket = false;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
        has_socket |= proto->Socket[i].Color != 0;
    if (!has_socket)
        return 0;
    // What's in there now:
    std::array<GemCandidate, 3> current{};
    double current_score = 0.0;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i) {
        uint32 enchId = item->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + i));
        SpellItemEnchantmentEntry const* pEnchant = enchId ? sSpellItemEnchantmentStore.LookupEntry(enchId) : nullptr;
        if (!pEnchant)
            continue;
        ItemTemplate const* gemProto = sObjectMgr->GetItemTemplate(pEnchant->GemID);
        GemPropertiesEntry const* gemProps = gemProto ? sGemPropertiesStore.LookupEntry(gemProto->GemProperties) : nullptr;
        if (!gemProps)
            continue;
        current[i].gem = gemProto;
        current[i].enchId = enchId;
        current[i].color = gemProps->color;
        current[i].score = ScoreEnchantment(swm, enchId);
        if (current[i].color & SOCKET_COLOR_META)
            current[i].score += 3 * META_SCORE; // keep whatever meta is already socketed
        current[i].source = GemSource::Current;
        current_score += current[i].score;
    }
    bool current_bonus = item->GemsFitSockets();
    if (current_bonus)
        current_score += ScoreEnchantment(swm, proto->socketBonus);

    GemPlan plan = PlanSockets(swm, proto, pool, &owned, &ownedCounts, &current);
    // Re-gem if it scores better, or if it activates the socket bonus without destroying any epic
    //  (or better) gem the player socketed themselves. Never downgrade otherwise.
    bool replaces_epic = false;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i) {
        if (current[i].gem && plan.gems[i].source != GemSource::Current && current[i].gem->Quality > ITEM_QUALITY_RARE)
            replaces_epic = true;
    }
    bool better = plan.score > current_score + 1e-9;
    bool gains_bonus = plan.bonus && !current_bonus && !replaces_epic;
    if (!better && !gains_bonus)
        return 0;

    bool equipped = item->IsEquipped();
    uint8 slot = item->GetSlot();
    if (equipped)
        player->ToggleMetaGemsActive(slot, false);
    for (uint32 es = SOCK_ENCHANTMENT_SLOT; es < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++es)
        player->ApplyEnchantment(item, EnchantmentSlot(es), false);
    player->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, false);

    uint32 socketed = 0;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i) {
        GemCandidate const& gem = plan.gems[i];
        if (!gem.gem || gem.source == GemSource::Current)
            continue;
        item->SetEnchantment(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + i), gem.enchId, 0, 0, player->GetGUID());
        if (gem.source == GemSource::Owned) {
            player->DestroyItemCount(gem.gem->ItemId, 1, true);
            --ownedCounts[gem.gem->ItemId];
        }
        ++socketed;
    }

    for (uint32 es = SOCK_ENCHANTMENT_SLOT; es < SOCK_ENCHANTMENT_SLOT + MAX_GEM_SOCKETS; ++es)
        player->ApplyEnchantment(item, EnchantmentSlot(es), true);
    item->SetEnchantment(BONUS_ENCHANTMENT_SLOT, item->GemsFitSockets() ? proto->socketBonus : 0, 0, 0, player->GetGUID());
    player->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, true);
    if (equipped)
        player->ToggleMetaGemsActive(slot, true);

    player->RemoveTradeableItem(item);
    item->ClearSoulboundTradeable(player);
    item->SendUpdateSockets();
    return socketed;
}

//
// Permanent enchants
//
// Sources (built once, see LoadEnchantSources):
//  - Enchanting recipes that can be learned from a trainer, an auto-learn, or a recipe item that does
//    not need reputation.
//  - Items with an on-use permanent enchant (armor kits, scopes, spurs, leg armors, spellthreads, ...)
//    that a profession can craft from such a recipe. This leaves out reputation, quest and test items.
//    Quality is capped at rare, like the free gems.
// Gate per source: the enchant's required level, the item's required level (if any), and the level
//  needed to train the profession up to the recipe's skill requirement (see LevelForProfessionSkill).
// On top of that, the player's own known enchant spells are always usable (e.g. profession perks
//  like Fur Lining or Lightweave Embroidery, death knight runes).

namespace {
struct EnchantSource {
    SpellInfo const* spell = nullptr;
    uint32 enchId = 0;
    uint32 reqLevel = 0;
    ItemTemplate const* item = nullptr; // set if the enchant is applied from an item
};
std::vector<EnchantSource> enchant_sources;
bool enchant_sources_loaded = false;
}

// Player level needed to train a profession up to "skill" (Apprentice .. Grand Master). Taken from
//  AzerothCore's trainer_spell data (the profession rank spells' ReqLevel).
static uint32 LevelForProfessionSkill(uint32 skill)
{
    static constexpr std::pair<uint32, uint32> ranks[] = {
        {75, 5}, {150, 10}, {225, 20}, {300, 35}, {375, 50}, {450, 65},
    };
    for (auto const& [cap, level] : ranks)
        if (skill <= cap)
            return level;
    return DEFAULT_MAX_LEVEL;
}

// The permanent enchant a spell applies, or 0. Skips enchants autobis never applies: on-use effects
//  (they can't go on items that already have one) and sockets.
static uint32 PermEnchantOf(SpellInfo const* spellInfo)
{
    if (!spellInfo)
        return 0;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i) {
        if (spellInfo->Effects[i].Effect != SPELL_EFFECT_ENCHANT_ITEM || spellInfo->Effects[i].MiscValue <= 0)
            continue;
        uint32 enchId = uint32(spellInfo->Effects[i].MiscValue);
        SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchId);
        if (!pEnchant)
            return 0;
        for (uint8 tt = 0; tt < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++tt)
            if (pEnchant->type[tt] == ITEM_ENCHANTMENT_TYPE_USE_SPELL
                || pEnchant->type[tt] == ITEM_ENCHANTMENT_TYPE_PRISMATIC_SOCKET)
                return 0;
        return enchId;
    }
    return 0;
}

static void LoadEnchantSources()
{
    if (enchant_sources_loaded)
        return;
    enchant_sources_loaded = true;
    enchant_sources.clear();

    // Lowest player level at which each profession recipe can be learned without reputation:
    std::unordered_map<uint32, uint32> learn_level;
    auto offer = [&](uint32 spellId, uint32 level) {
        auto [it, inserted] = learn_level.try_emplace(spellId, level);
        if (!inserted && level < it->second)
            it->second = level;
    };
    //                                                   0         1          2
    if (QueryResult result = WorldDatabase.Query("SELECT SpellId, ReqSkillRank, ReqLevel FROM trainer_spell")) {
        do {
            Field* fields = result->Fetch();
            offer(fields[0].Get<uint32>(),
                  std::max(LevelForProfessionSkill(fields[1].Get<uint32>()), uint32(fields[2].Get<uint8>())));
        } while (result->NextRow());
    }
    ItemTemplateContainer const* items = sObjectMgr->GetItemTemplateStore();
    for (auto const& [itemId, proto] : *items) {
        if (proto.Class != ITEM_CLASS_RECIPE || proto.RequiredReputationFaction)
            continue;
        for (uint8 k = 0; k < MAX_ITEM_PROTO_SPELLS; ++k)
            if (proto.Spells[k].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID && proto.Spells[k].SpellId > 0)
                offer(uint32(proto.Spells[k].SpellId),
                      std::max(LevelForProfessionSkill(proto.RequiredSkillRank), proto.RequiredLevel));
    }

    // One pass over all spells: enchanting recipes, and which profession spells craft which item.
    std::unordered_map<uint32, std::vector<uint32>> crafted_by; // item id -> profession spells
    auto add_source = [](SpellInfo const* spellInfo, uint32 enchId, uint32 level, ItemTemplate const* item) {
        SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchId);
        enchant_sources.push_back({spellInfo, enchId, std::max(level, pEnchant->requiredLevel), item});
    };
    for (uint32 id = 0; id < sSpellMgr->GetSpellInfoStoreSize(); ++id) {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
        if (!spellInfo)
            continue;
        bool profession = false, enchanting = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(id);
        for (auto it = bounds.first; it != bounds.second; ++it) {
            SkillLineEntry const* skillLine = sSkillLineStore.LookupEntry(it->second->SkillLine);
            if (!skillLine || (skillLine->categoryId != SKILL_CATEGORY_PROFESSION
                               && skillLine->categoryId != SKILL_CATEGORY_SECONDARY))
                continue;
            profession = true;
            if (it->second->SkillLine == SKILL_ENCHANTING)
                enchanting = true;
            if (it->second->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE
                || it->second->AcquireMethod == SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                offer(id, LevelForProfessionSkill(it->second->MinSkillLineRank));
        }
        if (!profession)
            continue;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_CREATE_ITEM && spellInfo->Effects[i].ItemType)
                crafted_by[spellInfo->Effects[i].ItemType].push_back(id);
        if (!enchanting)
            continue;
        uint32 enchId = PermEnchantOf(spellInfo);
        auto lit = learn_level.find(id);
        if (enchId && lit != learn_level.end())
            add_source(spellInfo, enchId, lit->second, nullptr);
    }

    // Crafted enchant items:
    for (auto const& [itemId, proto] : *items) {
        if (proto.Class == ITEM_CLASS_RECIPE || proto.Quality > ITEM_QUALITY_RARE || proto.RequiredReputationFaction
            || proto.HasFlag(ITEM_FLAG_DEPRECATED) || proto.HasFlag2(ITEM_FLAG2_SHOW_BEFORE_DISCOVERED))
            continue;
        auto cit = crafted_by.find(itemId);
        if (cit == crafted_by.end())
            continue;
        for (uint8 k = 0; k < MAX_ITEM_PROTO_SPELLS; ++k) {
            if (proto.Spells[k].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE || proto.Spells[k].SpellId <= 0)
                continue;
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto.Spells[k].SpellId);
            uint32 enchId = PermEnchantOf(spellInfo);
            if (!enchId)
                continue;
            uint32 level = UINT32_MAX;
            for (uint32 craftSpell : cit->second) {
                auto lit = learn_level.find(craftSpell);
                if (lit != learn_level.end())
                    level = std::min(level, lit->second);
            }
            if (level != UINT32_MAX)
                add_source(spellInfo, enchId, std::max(level, proto.RequiredLevel), &proto);
            break;
        }
    }
    LOG_INFO("module.autobis", "autobis: loaded {} permanent enchant sources.", enchant_sources.size());
}

AutoBis::EnchantPool AutoBis::BuildEnchantPool(Player* player)
{
    LoadEnchantSources();
    uint32 level = player->GetLevel();
    auto enchant_ok = [&](uint32 enchId) {
        SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchId);
        if (!pEnchant || pEnchant->requiredLevel > level)
            return false;
        // e.g. ring enchants (enchanters only), Fur Lining (leatherworkers only):
        return !pEnchant->requiredSkill || player->GetSkillValue(pEnchant->requiredSkill) >= pEnchant->requiredSkillValue;
    };
    EnchantPool pool;
    std::unordered_set<uint32> seen; // spell ids
    for (EnchantSource const& src : enchant_sources) {
        if (src.reqLevel > level || !enchant_ok(src.enchId))
            continue;
        if (src.item && player->CanUseItem(src.item) != EQUIP_ERR_OK)
            continue;
        if (seen.insert(src.spell->Id).second)
            pool.push_back({src.spell, src.enchId, src.item != nullptr});
    }
    // Enchants the player knows (profession perks, death knight runes, recipes they learned themselves):
    for (auto const& [spellId, playerSpell] : player->GetSpellMap()) {
        if (!player->HasSpell(spellId))
            continue;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        uint32 enchId = PermEnchantOf(spellInfo);
        if (enchId && enchant_ok(enchId) && seen.insert(spellId).second)
            pool.push_back({spellInfo, enchId, false});
    }
    std::sort(pool.begin(), pool.end(),
              [](EnchantCandidate const& l, EnchantCandidate const& r) { return l.spell->Id < r.spell->Id; });
    return pool;
}

// Can this enchant go on this item? Mirrors Item::IsFitToSpellRequirements and the item level checks
//  in Spell::CheckItems, but works on a template so it can also be used for scoring.
static bool EnchantFitsItem(AutoBis::EnchantCandidate const& cand, ItemTemplate const* proto)
{
    SpellInfo const* spellInfo = cand.spell;
    if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
        return false;
    if (spellInfo->EquippedItemClass != -1) {
        if (spellInfo->EquippedItemClass != int32(proto->Class))
            return false;
        if (spellInfo->EquippedItemSubClassMask != 0 && !(spellInfo->EquippedItemSubClassMask & (1 << proto->SubClass)))
            return false;
    }
    if (spellInfo->EquippedItemInventoryTypeMask != 0) {
        bool weaponForHand = proto->InventoryType == INVTYPE_WEAPON
            && (spellInfo->EquippedItemInventoryTypeMask & ((1 << INVTYPE_WEAPONMAINHAND) | (1 << INVTYPE_WEAPONOFFHAND)));
        if (!weaponForHand && !(spellInfo->EquippedItemInventoryTypeMask & (1 << proto->InventoryType)))
            return false;
    }
    if (!spellInfo->HasAttribute(SPELL_ATTR2_ALLOW_LOW_LEVEL_BUFF)) {
        uint32 itemLevel = proto->RequiredLevel ? proto->RequiredLevel : proto->ItemLevel;
        if (itemLevel < spellInfo->BaseLevel)
            return false; // e.g. WotLK enchants need a level 60+ item
    }
    if (cand.fromItem && spellInfo->MaxLevel > 0 && proto->ItemLevel > spellInfo->MaxLevel)
        return false;
    return true;
}

AutoBis::EnchantCandidate const* AutoBis::BestEnchantFor(const ScoreWeightMap& swm, ItemTemplate const* proto,
                                                         EnchantPool const& pool, double& bestScore)
{
    EnchantCandidate const* best = nullptr;
    bestScore = 0.0;
    for (EnchantCandidate const& cand : pool) {
        if (!EnchantFitsItem(cand, proto))
            continue;
        double score = ScoreEnchantment(swm, cand.enchId, proto);
        if (score > bestScore) {
            bestScore = score;
            best = &cand;
        }
    }
    return best;
}

bool AutoBis::AutoEnchantItem(Player* player, Item* item, const ScoreWeightMap& swm, EnchantPool const& pool)
{
    ItemTemplate const* proto = item->GetTemplate();
    uint32 current = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
    bool scorable = true;
    double currentScore = ScoreEnchantment(swm, current, proto, &scorable);
    // Never overwrite an enchant we can't value (e.g. a proc we don't know, or a gathering enchant).
    if (!scorable)
        return false;
    double bestScore;
    EnchantCandidate const* best = BestEnchantFor(swm, proto, pool, bestScore);
    if (!best || best->enchId == current || bestScore <= currentScore)
        return false;
    // Same sequence as Spell::EffectEnchantItemPerm:
    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
    item->SetEnchantment(PERM_ENCHANTMENT_SLOT, best->enchId, 0, 0, player->GetGUID());
    player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
    player->RemoveTradeableItem(item);
    item->ClearSoulboundTradeable(player);
    return true;
}

double AutoBis::ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate,
                                 GemPool const* gems, EnchantPool const* enchants)
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
    // Sockets: score the item as if it had the best free (uncommon/rare) gems, so socketed items
    //  are compared fairly against unsocketed ones.
    if (gems)
        totalScore += PlanSockets(score_weights, itemTemplate, *gems, nullptr, nullptr, nullptr).score;
    // Likewise with the best permanent enchant. This mostly matters where the item type decides which
    //  enchants fit (two-hander vs. one-hander, shields, staves).
    if (enchants) {
        double enchScore;
        BestEnchantFor(score_weights, itemTemplate, *enchants, enchScore);
        totalScore += enchScore;
    }
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
                                     GemPool const* gems, EnchantPool const* enchants)
{
    ItemTemplate const *itemTemplate = item->GetTemplate();
    if (!PlayerCanUseItem(player, itemTemplate))
        return;
    uint32 inv_type = itemTemplate->InventoryType;
    AdjustInvType(player, inv_type);
    double score = ComputePawnScore(swm, itemTemplate, gems, enchants);
    have_items[inv_type].push_back({itemTemplate, score});
}

void AutoBis::PopulateHaveItems(Player *player, ItemSlotMap &have_items, GemPool const* gems,
                                EnchantPool const* enchants)
{
    const ScoreWeightMap &swm = GetScoreWeightMap(player);
    ForEachOwnedItem(player, [&](Item* item) { PopulateSingleHaveItem(player, swm, item, have_items, gems, enchants); });
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
    const ScoreWeightMap &swm = GetScoreWeightMap(player);
    // Free gems autobis may socket (uncommon/rare). Also used to score socketed items fairly:
    uint32 rec_meta = RecommendedMetaGem(player, swm);
    GemPool gem_pool = BuildGemPool(player, swm, rec_meta);
    // Permanent enchants autobis may apply. Also used to score items:
    EnchantPool enchant_pool = BuildEnchantPool(player);
    // First, populate "have_items" map with player's inventory + bank:
    ItemSlotMap have_items;
    PopulateHaveItems(player, have_items, &gem_pool, &enchant_pool);
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
            double score = ComputePawnScore(swm, item_template, &gem_pool, &enchant_pool);
            next_items[inv_type].push_back({item_template, score});
        }
    }
    std::vector<Item*> new_items;
    auto grant_items = [&]() -> bool {
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
                    if (item)
                        new_items.push_back(item);
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
                            if (item)
                                new_items.push_back(item);
                        } else {
                            handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, item_id2, 1);
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    };
    bool ok = grant_items();

    // Auto-gem and auto-enchant: equipped gear, newly granted items, and the best item(s) we already had per slot.
    std::unordered_set<ItemTemplate const*> top_have;
    for (auto const& [invtype, slot_items] : have_items)
        for (size_t idx = 0; idx < slot_items.size() && idx < 2; ++idx)
            top_have.insert(slot_items[idx].first);
    std::vector<Item*> gem_targets;
    std::unordered_set<Item*> seen_targets;
    auto add_target = [&](Item* item) {
        if (item && seen_targets.insert(item).second)
            gem_targets.push_back(item);
    };
    // Equipped first, so any epic gems the player owns go into worn gear:
    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        add_target(player->GetItemByPos(INVENTORY_SLOT_BAG_0, i));
    for (Item* item : new_items)
        add_target(item);
    ForEachOwnedItem(player, [&](Item* item) {
        if (top_have.count(item->GetTemplate()))
            add_target(item);
    });
    OwnedGemCounts owned_counts;
    GemPool owned_gems = BuildOwnedGems(player, swm, owned_counts, rec_meta);
    uint32 socketed = 0;
    for (Item* item : gem_targets)
        socketed += AutoGemItem(player, item, swm, gem_pool, owned_gems, owned_counts, rec_meta);
    if (socketed)
        handler->PSendSysMessage("autobis: socketed {} gem(s).", socketed);
    uint32 enchanted = 0;
    for (Item* item : gem_targets)
        if (AutoEnchantItem(player, item, swm, enchant_pool))
            ++enchanted;
    if (enchanted)
        handler->PSendSysMessage("autobis: enchanted {} item(s).", enchanted);
    return ok;
}

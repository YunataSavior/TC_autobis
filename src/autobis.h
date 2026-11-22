#ifndef __AUTOBIS_MISC_H__
#define __AUTOBIS_MISC_H__

#include <array>
#include <map>
#include <utility>
#include <vector>

#include "Chat.h"
#include "Player.h"

class Item;
class SpellEffectInfo;
class SpellInfo;
struct ItemTemplate;

class AutoBis {
    public:
        using ItemScore = std::pair<ItemTemplate const*, double>;
        using SlotItems = std::vector<ItemScore>;
        using ItemSlotMap = std::map<uint32, SlotItems>;
        using ScoreWeightMap = std::map<int32, double>;
        // Gems:
        enum class GemSource { Pool, Owned, Current };
        struct GemCandidate {
            ItemTemplate const* gem = nullptr;
            uint32 enchId = 0;   // SpellItemEnchantment id the gem applies
            uint32 color = 0;    // GemProperties color mask (SOCKET_COLOR_*)
            double score = 0.0;
            GemSource source = GemSource::Pool;
            double hit = 0.0;       // hit rating the gem gives (for the cap budget)
            double expertise = 0.0; // expertise rating the gem gives
        };
        using GemPool = std::vector<GemCandidate>;
        using OwnedGemCounts = std::map<uint32, uint32>; // gem item id -> count in bags/bank
        struct GemPlan {
            double score = 0.0;  // gems + socket bonus (if earned)
            bool bonus = false;  // socket bonus earned
            std::array<GemCandidate, 3> gems{};
            double hit = 0.0, expertise = 0.0; // rating from the gems (and socket bonus)
        };
        // Hit/expertise rating still useful on the worn gear before the same-level caps. While under a
        //  cap, gems value that stat above the map's top stat; past it, at 0. Inactive = plain weights.
        struct CapBudget {
            bool active = false;
            double hit = 0.0, expertise = 0.0;                  // rating left until the cap
            double hitWeight = 0.0, expertiseWeight = 0.0;      // the map's own weights
            double hitCapWeight = 0.0, expertiseCapWeight = 0.0; // weights while under the cap
            double totalWeight = 1.0;
        };
        using WornItems = std::array<Item*, EQUIPMENT_SLOT_END>;
        // Permanent enchants:
        struct EnchantCandidate {
            SpellInfo const* spell = nullptr; // the enchanting spell (its item requirements decide where it fits)
            uint32 enchId = 0;                // SpellItemEnchantment id
            bool fromItem = false;            // applied from an item (armor kit, scope, ...)
        };
        using EnchantPool = std::vector<EnchantCandidate>;
    private:
        static const ScoreWeightMap& GetScoreWeightMap(Player *player);
        static void AdjustInvType(Player* player, uint32 &inv_type);
        // return: score of the best enchant; also populates "enchid" (set to 0 if invalid):
        static double CalculateBestRandomEnchant(const ScoreWeightMap &score_weights, ItemTemplate const* itemProto, int32& enchId);
        static double ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate,
                                       GemPool const* gems = nullptr, EnchantPool const* enchants = nullptr);
        static bool PlayerCanUseItem(Player *player, ItemTemplate const* itemTemplate);
        static void PopulateSingleHaveItem(Player *player, const ScoreWeightMap &swm, Item *item,
                                           ItemSlotMap &have_items, GemPool const* gems, EnchantPool const* enchants);
        static void PopulateHaveItems(Player *player, ItemSlotMap &have_items, GemPool const* gems,
                                      EnchantPool const* enchants);
        // Gems:
        static bool GemUsableBy(Player* player, ItemTemplate const* gem, GemCandidate& out, const ScoreWeightMap& swm,
                                uint32 recMeta);
        // Meta gem advised for the player's class/spec (0 if none usable):
        static uint32 RecommendedMetaGem(Player* player, const ScoreWeightMap& swm);
        static GemPool BuildGemPool(Player* player, const ScoreWeightMap& swm, uint32 recMeta);
        static GemPool BuildOwnedGems(Player* player, const ScoreWeightMap& swm, OwnedGemCounts& counts,
                                      uint32 recMeta);
        static GemPlan PlanSockets(const ScoreWeightMap& swm, ItemTemplate const* proto, GemPool const& pool,
                                   GemPool const* owned, OwnedGemCounts const* ownedCounts,
                                   std::array<GemCandidate, 3> const* current, CapBudget const* budget = nullptr);
        // returns number of gems newly socketed. With a budget, the hit/expertise that ends up in the
        //  item is taken out of it:
        static uint32 AutoGemItem(Player* player, Item* item, const ScoreWeightMap& swm, GemPool const& pool,
                                  GemPool const& owned, OwnedGemCounts& ownedCounts, uint32 recMeta,
                                  CapBudget* budget);
        // Gear the player will wear: equipped items, replaced by better newly granted ones:
        static WornItems ProjectWornItems(Player* player, const ScoreWeightMap& swm, std::vector<Item*> const& newItems,
                                          GemPool const* gems, EnchantPool const* enchants, bool ohDual, bool titansGrip);
        static CapBudget ComputeCapBudget(Player* player, const ScoreWeightMap& swm, WornItems const& worn);
        // Permanent enchants:
        static EnchantPool BuildEnchantPool(Player* player);
        // Best-scoring enchant that fits the item (nullptr if none scores above 0):
        static EnchantCandidate const* BestEnchantFor(const ScoreWeightMap& swm, ItemTemplate const* proto,
                                                      EnchantPool const& pool, double& bestScore);
        // returns true if the item was (re-)enchanted:
        static bool AutoEnchantItem(Player* player, Item* item, const ScoreWeightMap& swm, EnchantPool const& pool);
    public:
        static bool Process(ChatHandler* handler);
};

// returns INT_MIN if sei doesn't map in a valid fashion:
int32 SpellEffectInfoToItemMod(const SpellEffectInfo& sei);

#endif

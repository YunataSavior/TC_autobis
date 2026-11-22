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
        };
        using GemPool = std::vector<GemCandidate>;
        using OwnedGemCounts = std::map<uint32, uint32>; // gem item id -> count in bags/bank
        struct GemPlan {
            double score = 0.0;  // gems + socket bonus (if earned)
            bool bonus = false;  // socket bonus earned
            std::array<GemCandidate, 3> gems{};
        };
    private:
        static const ScoreWeightMap& GetScoreWeightMap(Player *player);
        static void AdjustInvType(Player* player, uint32 &inv_type);
        // return: score of the best enchant; also populates "enchid" (set to 0 if invalid):
        static double CalculateBestRandomEnchant(const ScoreWeightMap &score_weights, ItemTemplate const* itemProto, int32& enchId);
        static double ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate,
                                       GemPool const* gems = nullptr);
        static bool PlayerCanUseItem(Player *player, ItemTemplate const* itemTemplate);
        static void PopulateSingleHaveItem(Player *player, const ScoreWeightMap &swm, Item *item,
                                           ItemSlotMap &have_items, GemPool const* gems);
        static void PopulateHaveItems(Player *player, ItemSlotMap &have_items, GemPool const* gems);
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
                                   std::array<GemCandidate, 3> const* current);
        // returns number of gems newly socketed:
        static uint32 AutoGemItem(Player* player, Item* item, const ScoreWeightMap& swm, GemPool const& pool,
                                  GemPool const& owned, OwnedGemCounts& ownedCounts, uint32 recMeta);
    public:
        static bool Process(ChatHandler* handler);
};

// returns INT_MIN if sei doesn't map in a valid fashion:
int32 SpellEffectInfoToItemMod(const SpellEffectInfo& sei);

#endif

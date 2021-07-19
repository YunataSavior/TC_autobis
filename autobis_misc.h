#ifndef __AUTOBIS_MISC_H__
#define __AUTOBIS_MISC_H__

#include "Chat.h"
#include "Player.h"

class AutoBis {
    public:
        using ItemScore = std::pair<ItemTemplate const*, double>;
        using SlotItems = std::vector<ItemScore>;
        using ItemSlotMap = std::map<uint32, SlotItems>;
        using ScoreWeightMap = std::map<int32, double>;
    private:
        static const ScoreWeightMap& GetScoreWeightMap(Player *player);
        static void AdjustInvType(Player* player, uint32 &inv_type);
        static double ComputePawnScore(const ScoreWeightMap &score_weights, ItemTemplate const* itemTemplate);
        static bool PlayerCanUseItem(Player *player, ItemTemplate const* itemTemplate);
        static void PopulateSingleHaveItem(Player *player, const ScoreWeightMap &swm, Item *item,
                                           ItemSlotMap &have_items, bool test_canuse);
        static void PopulateHaveItems(Player *player, ItemSlotMap &have_items);
    public:
        static bool Process(ChatHandler* handler, char const* args);
};

#endif

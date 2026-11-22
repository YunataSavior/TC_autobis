# TODO

Open items, collected from the `TODO`/`FIXME` comments in the code plus problems found while porting to AzerothCore.

## Open: from the code

- [ ] **Equip-stat "counted twice" workaround is too broad.** `ComputePawnScore` in [src/autobis.cpp](src/autobis.cpp) skips any stat id it has already seen (`seen_stat_ids`).
  - **Likely cause of the double count:** spell-power equip spells have two effects with the same amount, `SPELL_AURA_MOD_DAMAGE_DONE` and `SPELL_AURA_MOD_HEALING_DONE`. `SpellEffectInfoToItemMod` maps both to `ITEM_MOD_SPELL_POWER`. That's expected data, not a loop bug.
  - **Problem:** the dedup set covers the whole item. If an item has two *different* equip spells that give the same stat, the second one is silently dropped.
  - **Fix:** track seen stats per spell (clear the set for each spell), or special-case the damage-done/healing-done pair.
- [ ] **Manual rating to `ITEM_MOD` mapping** (`ModRatingToItemMod`). The original FIXME asks whether existing core code could do this. AC has no reverse table; the forward mapping is a hand-written switch in `Player::_ApplyItemBonuses`, so the switch here is probably as good as it gets.
  - **Smaller issue:** it returns on the *first* bit set in `MiscValue`. A spell whose mask covers several ratings, for example melee, ranged and spell hit together, only counts toward the first one. Check whether this matters for any item that autobis could pick.
- [ ] **Unhandled equip auras.** Unknown `ApplyAuraName` values are now logged at `LOG_DEBUG` under `module.autobis`. They used to be `printf("FIXME: ...")`.
  - To find them: run `.autobis` for a few classes and levels with that logger set to Debug, then decide for each aura whether to weigh it or explicitly ignore it.
- [ ] **README "How it works"** still says `TODO`.

## Done during the AzerothCore port

- [x] "Can we completely get rid of this SQL statement?" Weapon damage now comes from `ItemTemplate::Damage[0]`, and the item list is filtered from `sObjectMgr->GetItemTemplateStore()`. The only remaining query is the `item_enchantment_template` load.
- [x] Copied skill table replaced by `ItemTemplate::GetSkill()`.
- [x] `FlagsExtra != 8192` is now a bit test. Items with `ITEM_FLAG_DEPRECATED` are also skipped, for example "Deprecated Worn Leather Shoulderpads".
- [x] The enchant effect loops could read past the end of the array; they're now bounded by `MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS`.
- [x] A weapon with `Delay == 0` could cause a divide-by-zero; it's now guarded.
- [x] Players below level 2 now get a message instead of nothing.

## Gems (added 2026-11-21)

- [x] **Item scoring:** socketed items are scored with the best free gems (uncommon/rare) plus the socket bonus, so socketed and unsocketed items are compared fairly.
- [x] **Auto-gem:** runs on equipped gear, newly granted items, and the top one or two owned items per slot. Gems the player already owns (any quality, such as Auction House epics) are used and consumed if they're better. Gems already socketed are kept unless something better is available, so it never downgrades.
- [x] **Meta gems:** the advised meta is chosen by class and spec, with a class fallback (`AdvisedMetaGems`).
  - The spec is the talent tree with the most points. With no talents spent, the class fallback is used.
  - Each list has the WotLK diamond first and the TBC equivalent second, as a fallback.
  - Metas are presumed active, and a socketed meta always outweighs any non-meta gem. So head gear with a meta socket beats head gear without, and the advised meta beats any other meta.
  - **A meta that's already socketed is always kept,** for example a tank meta a bear druid chose themselves.
- [x] **Socket bonus:** the color-matching layout (pure or mixed colors) is always used when possible. To earn a bonus it never destroys an epic gem the player socketed, but it does re-arrange uncommon and rare gems.
- [x] **Bear vs. cat:** feral defaults to cat (Relentless). Bear players can socket their own meta, which is then kept.
- [ ] **Future: spec-aware stat weight maps.** Only the meta gem choice uses the talent tree today. The stat weights are still one map per class, plus the paladin off-hand and druid level-10 special cases. The idea is to reuse `InferTalentTree()` to pick a weight map per spec (Holy/Prot/Ret, Arms/Fury/Prot, the casters, and so on), keeping the class map as the fallback.
- [x] **No gem item-level gating:** sockets are scored and filled with the best uncommon/rare gems up to WotLK, so TBC gear can get WotLK gems. Only the game's own requirements apply: the gem's required level and skill, and the enchant's required level.
- [ ] **Unique-equipped and limit-category gems are always skipped**, even ones the player owns, for example the jewelcrafter-only Dragon's Eye. Supporting them needs per-character limit tracking (`CanEquipUniqueItem`).
- [x] **Won't do: extra sockets.** The belt buckle and blacksmith prismatic socket use their own enchant slot (`PRISMATIC_ENCHANTMENT_SLOT`). They're left out on purpose, to keep the logic simple.

## Leveling weights (design, not started)

**For now:** the weight maps are tweaked by hand to under-weight Hit and Expertise (fury warrior Expertise is halved). The layer below is a later step.

The Pawn maps are tuned for endgame raiding: Stamina and armor are worth almost nothing, ret has no Intellect at all, and Hit/Expertise are weighted for bosses three levels above you. autobis is for leveling: items go up to about ilvl 187, and the player stops using it at level 80, when heroics start. So the whole range it covers is leveling.

**Decisions so far**
- **Applies at every level the module covers.** No fade toward endgame weights.
- **No player override** (no `.autobis raid` or similar).
- **Offense or healing stays dominant.** Survivability and sustain are added, but they must never outweigh the spec's main stats.

**Proposed shape**
1. **A leveling layer on top of the class (later spec) map,** one small table per role:
   - **Stamina** gets a weight of about 20–25% of the map's highest offensive or healing weight, never more.
     - Example for ret: Strength 80, Stamina 20. A plate piece built for DPS still clearly beats a tank piece.
   - **Mana users** (ret, enhancement, hunter): add Intellect at about 15–20% of the top weight, for less drinking.
   - **Healers and casters:** a small boost to Spirit and MP5. Healing stats stay on top.
   - **Leather and mail wearers:** a small boost to armor.
2. **Hit and Expertise caps against same-level mobs:** 5% melee hit, about 4% spell hit, and about 5% dodge (roughly 20 Expertise). Converted to rating at the player's level and used for gems first, items later.
   - After that, restore the fury warrior Expertise weight to 100. The halved weight is a stopgap.
   - **Gem shuffling:** re-gem so the player just reaches the same-level Hit and Expertise caps, and no further. This **must count talents** as well as gear, for example Precision and Weapon Expertise for warriors, Dual Wield Specialization, Combat Expertise, and the spell-hit talents.
3. **Order:** spec map (see the Gems section), then the leveling layer, then caps.
4. **Guard rule:** after the layer is applied, check that no survivability stat outweighs the role's top stat. Assert or clamp to enforce it.

## Enchants (added 2026-11-22)

- [x] **Auto-enchant** (`PERM_ENCHANTMENT_SLOT` only) on the same targets as auto-gem: equipped, newly granted, top owned per slot.
- [x] **Sources** are built once (`LoadEnchantSources`):
  - Enchanting recipes learnable from a trainer (`trainer_spell`), an auto-learn, or a recipe item that needs no reputation. With the base data, 245 recipes qualify and 20 rep-gated ones are left out.
  - Crafted enchant items (armor kits, scopes, spurs, chains, shield spikes, leg armors, spellthreads), rare quality or below. This leaves out rep, quest and test items, and the epic leg armors.
  - The player's own known enchant spells: profession perks (Fur Lining, embroideries, Master's Inscription) and death knight runes. Recipes the player learned themselves count too, rep-gated ones included.
- [x] **Level gate:** the enchant's or item's required level. Without one, the level needed to train the profession up to the recipe's skill (`LevelForProfessionSkill`): 75→5, 150→10, 225→20, 300→35, 375→50, 450→65, from AC's `trainer_spell`. Trainer rows can raise it further (most WotLK recipes need level 60).
  - The game's own target checks also apply: the item must meet the spell's `BaseLevel` (WotLK enchants need a level 60+ item), and item enchants have a `MaxLevel`.
- [x] **Fit:** template version of `Item::IsFitToSpellRequirements`.
- [x] **Scoring:** stats, armor, "+X weapon damage" (converted to DPS) and mapped equip auras. Weapon procs and DK runes use the hand-set table `proc_enchant_values`.
- [x] **Item scoring** includes the best enchant that fits.
- [x] **Existing enchants:** replaced only by a strictly better score. Enchants that can't be valued (unknown procs, on-use tinkers, gathering enchants) are never overwritten.
- [ ] **Tune `proc_enchant_values`.** The numbers are rough guesses: Crusader as 35 Str, Mongoose as 40 Agi + 15 Haste, Berserking as 120 AP, and so on. Unlisted procs (Deathfrost, Battlemaster, Lifeward, Giant Slayer, Demonslaying, the DK sword/spell runes) are never picked.
- [ ] **Speed and utility enchants score 0** (Minor Speed boots, Surefooted, gathering). A leveling weight for run speed would fit the leveling layer above.
- [ ] **Check in-game:** the trainer data in the live DB may differ from the base SQL. The worldserver log line "autobis: loaded N permanent enchant sources" shows how many were found.

## Open: other

- [ ] **Test `.autobis` in-game** on the AzerothCore build, with a few classes and specs (dual wield, Titan's Grip, casters) and a few levels.
- [ ] **`CalculateBestRandomEnchant`:** when the best score is `<= 0`, the comment says "Internal error!? print something out..." but nothing is printed. Add a `LOG_DEBUG`, or figure out when this actually happens.
- [ ] **The `FlagsExtra` and `ITEM_FLAG_DEPRECATED` filter was checked against AC's base `item_template.sql`, not the live world DB.** Check the live DB if you see odd picks.
- [ ] **Optional:** restyle to the AzerothCore code style (Allman braces). The port kept the original K&R style.
- [ ] **Optional:** a custom RBAC permission instead of `SEC_GAMEMASTER`. That would need auth DB changes, so it's deferred.

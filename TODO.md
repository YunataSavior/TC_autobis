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
- [ ] **Extra sockets are ignored:** the belt buckle and blacksmith prismatic socket (`PRISMATIC_ENCHANTMENT_SLOT`).

## Open: other

- [ ] **Test `.autobis` in-game** on the AzerothCore build, with a few classes and specs (dual wield, Titan's Grip, casters) and a few levels.
- [ ] **`CalculateBestRandomEnchant`:** when the best score is `<= 0`, the comment says "Internal error!? print something out..." but nothing is printed. Add a `LOG_DEBUG`, or figure out when this actually happens.
- [ ] **The `FlagsExtra` and `ITEM_FLAG_DEPRECATED` filter was checked against AC's base `item_template.sql`, not the live world DB.** Check the live DB if you see odd picks.
- [ ] **Optional:** restyle to the AzerothCore code style (Allman braces). The port kept the original K&R style.
- [ ] **Optional:** a custom RBAC permission instead of `SEC_GAMEMASTER`. That would need auth DB changes, so it's deferred.

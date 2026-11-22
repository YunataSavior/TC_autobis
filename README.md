# TC_autobis
AzerothCore module (originally a TrinityCore custom command) for giving players the best items possible at any level.

"autobis" is a command you can use on your own compiled installation of AzerothCore to give yourself the "best theoretical" items at any given level.

# Installation Instructions (AzerothCore)

autobis is packaged as an AzerothCore module, so no core files need to be edited.

## Linux Instructions
1. Put this repository in your AzerothCore `modules/` directory **under the name `mod-autobis`**
   (the module loader derives the entry-point name `Addmod_autobisScripts` from the directory name):
   ```
   cd <Path_to_your_azerothcore>/modules
   git clone <this repo> mod-autobis          # or: ln -s /path/to/TC_autobis mod-autobis
   ```
2. Re-run CMake and rebuild/install, e.g. from your build directory:
   ```
   cmake .. <your usual flags>
   make -j$(nproc) && make install
   ```
3. Start `worldserver`. With database auto-updates enabled (the default `Updates.EnableDatabases`), the file
   `data/sql/db-world/autobis_command.sql` is applied to the world database automatically; it only adds the
   help text for `.help autobis`. If you have auto-updates disabled, source that file into `acore_world` manually.
4. Congrats! Enjoy!

The command requires GM security level (`SEC_GAMEMASTER`, i.e. account `gmlevel` 2 or higher).
No auth-database / permission changes are needed.

## TrinityCore
The original TrinityCore version of these instructions (patching `cs_misc.cpp` and `RBAC.h`) lives on the `main` branch.

# How to use
In-game, provided you are at:
* At least Level 2
* Below Level 80
* Have GM permissions

Type the following command:
```
.autobis
```

# How it works
TODO. If you understand C++, feel free to read the code.

If you don't understand the code, or don't want to, let me summarize how autobis works:

* When you execute "autobis", the server will loop over all items you have that you can use.
* It will also loop over all items that you don't have that have a "Requires Level X" equal to your current level.
* If then computes a "score" for each of the aforementioned items based on stat weights. These stat weights were generated via "Pawn" scores. These scores can be found here:
  * https://github.com/Road-block/Pawn/blob/master/Wowhead.lua
  * Stat weights are (currently) class-based. It'll (currently) use a hard-coded C++ table to translate stats to points. See ``src/autobis.cpp`` to view the code yourself.
* Using these scores, the server will compare the item you currently have versus available items you don't have on a per-slot basis.
* If the "don't have" item has a higher score, then the server will add that item to your inventory.
* You can thus equip your new item and become a lot stronger!

# Known Issues
1. Players can repeatedly run this command, sell all their gear, rerun this command, sell, and repeat for infinite gold.
  1. I have a wishlist item that would prevent this from occurring: put a cap at 1 execution per-level.
2. This command is quite intensive to run; if there are thousands of players running the command at once, it might cause the server to be unresponsive. Again, mitigated if players can only run this once per level (but even then malicious players might constantly create new characters, level them up to 5 while running this command once per level, delete, repeat...).
3. Not all of the weight tables are filled out. Feel free to read the code I wrote to figure out how to insert those tables, then have your class-of-choice use those tables.

# Wishlist
## The code itself
1. Make sure players can only execute this command ONCE per level.
2. Don't hardcode these item weights; be able to download "Wowhead.lua" and read that file to automatically create the weights.
3. Automatically choose the most appropriate stat weight based on a player's talent choices (e.g. if a player specs into Bear Tank, then give them the Bear Tank weights; if a player specs into Cat DPS, then give them the Cat DPS table). This might be a little tricky.
4. Allow players to set their own custom weights.

## This README file
1. Write Windows instructions.

# Design Decisions
1. I've excluded ALL items that don't have a sell value. This is to prevent adding items that might be granted as part of a quest, or might feel too cheaty (e.g. items gained from redeeming tokens dropped by TBC raid bosses).
2. I've excluded all Epics and above. When you hit 60 and 70, those items will last with you for a long while. Giving lesser upgrades (yet they still provide a hefty power bonus) feels more appropriate to smoothen the power progression of the character with this cheat.
3. I'll let players grind for better gear at max level; just don't give it to them when they get there.

If you want to disable any of these design decisions, you can always modify the code.

# License
I pretty much used the same exact license agreement as does the base TrinityCore repository. Feel free to copy, modify, or do whatever you wish with this software. I give the TrinityCore and AzerothCore teams permission to integrate this command into the source code (if they ever think this command would be valuable to have).

# TC_autobis
TrinityCore custom C++ command for giving players the best items possible at any level.

"autobis" is a command you can use on your own compiled installation of TrinityCore to give yourself the "best theoretical" items at any given level.

# Installation Instructions
Note: these instructions are very crude at the moment. These should work fine with any latest build of TrinityCore, but if not, please let me know.

1. Download/clone this repository to your computer.
2. Copy the files ``autobis_misc.cpp`` and ``autobis_misc.h`` to ``<Path_to_your_TC_clone>/src/server/scripts/Commands/``
3. In that same folder, open up the file named ``cs_misc.cpp``. We're going to make the following changes to that file:
  1. Below all the lines that start with ``#include `` at the top of the file, insert the following line: ``#include <autobis_misc.h>``
  2. Look for the following table in the same file: ``static std::vector<ChatCommand> commandTable``. Insert the row listed as "Table row" below this list.
  3. In the same file, but below aforementioned table, insert the function labelled as "Autobis Entry Function" as a member function of the ``misc_commandscript`` class. Ideally, put this function between ``HandleAddItemSetCommand`` and ``HandleBankCommand``.
4. Now, open up the file ``<Path_to_your_TC_clone>/src/server/game/Accounts/RBAC.h``, search for the table named ``enum RBACPermissions``, and add the following line to the end of the table: ``RBAC_PERM_COMMAND_AUTOBIS = 1222,``
5. Now, log into MySQL, and source the file ``insert_autobis.sql`` found in this repository.
6. Congrats! Enjoy!

NOTE: If TrinityCore ends up using "1222" for another command down the line, please let me know ASAP. I chose this number because it's far greater than whatever other number is being used currently, but you never know....

# Code you need to insert yourself

## Table row
```
            { "autobis",          rbac::RBAC_PERM_COMMAND_AUTOBIS,          false, &HandleAutoBisCommand,          "" },
```

## Autobis Entry Function
```
    static bool HandleAutoBisCommand(ChatHandler* handler, char const* args)
    {
        return AutoBis::Process(handler, args);
    }
```

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
  * Stat weights are (currently) class-based. It'll (currently) use a hard-coded C++ table to translate stats to points. See ``autobis_misc.cpp`` to view the code yourself.
* Using these scores, the server will compare the item you currently have versus available items you don't have on a per-slot basis.
* If the "don't have" item has a higher score, then the server will add that item to your inventory.
* You can thus equip your new item and become a lot stronger!

# Known Issues
1. Players can repeatedly run this command, sell all their gear, rerun this command, sell, and repeat for infinite gold.
  1. I have a wishlist item that would prevent this from occurring: put a cap at 1 execution per-level.
2. This command is quite intensive to run; if there are thousands of players running the command at once, it might cause the server to be unresponsive. Again, mitigated if players can only run this once per level (but even then malicious players might constantly create new characters, level them up to 5 while running this command once per level, delete, repeat...).
3. Not all of the weight tables are filled out. Feel free to read the code I wrote to figure out how to insert those tables, then have your class-of-choice use those tables.

# Wishlist
1. Make sure players can only execute this command ONCE per level.
2. Don't hardcode these item weights; be able to download "Wowhead.lua" and read that file to automatically create the weights.
3. Automatically choose the most appropriate stat weight based on a player's talent choices (e.g. if a player specs into Bear Tank, then give them the Bear Tank weights; if a player specs into Cat DPS, then give them the Cat DPS table). This might be a little tricky.
4. Allow players to set their own custom weights.

# Design Decisions
1. I've excluded ALL items that don't have a sell value. This is to prevent adding items that might be granted as part of a quest, or might feel too cheaty (e.g. items gained from redeeming tokens dropped by TBC raid bosses).
2. I've excluded all Epics and above. When you hit 60 and 70, those items will last with you for a long while. Giving lesser upgrades (yet they still provide a hefty power bonus) feels more appropriate to smoothen the power progression of the character with this cheat.
3. I'll let players grind for better gear at max level; just don't give it to them when they get there.

If you want to disable any of these design decisions, you can always modify the code.

# License
I pretty much used the same exact license agreement as does the base TrinityCore repository. Feel free to copy, modify, or do whatever you wish with this software. I give the TrinityCore team permission to integrate this command into the source code (if they ever think this command would be valuable to have).

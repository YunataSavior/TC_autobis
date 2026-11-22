#include "autobis.h"

#include "Chat.h"
#include "CommandScript.h"

using namespace Acore::ChatCommands;

class autobis_commandscript : public CommandScript
{
public:
    autobis_commandscript() : CommandScript("autobis_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "autobis", HandleAutoBisCommand, SEC_GAMEMASTER, Console::No },
        };
        return commandTable;
    }

    static bool HandleAutoBisCommand(ChatHandler* handler)
    {
        return AutoBis::Process(handler);
    }
};

void AddSC_autobis_commandscript()
{
    new autobis_commandscript();
}

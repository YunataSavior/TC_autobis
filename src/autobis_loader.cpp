// AzerothCore module entry point. The function name must match the module directory name
// ("mod-autobis" -> Addmod_autobisScripts), so clone/symlink this repo as modules/mod-autobis.

void AddSC_autobis_commandscript();

void Addmod_autobisScripts()
{
    AddSC_autobis_commandscript();
}

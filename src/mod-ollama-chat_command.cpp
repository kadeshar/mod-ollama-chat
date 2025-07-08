#include "mod-ollama-chat_command.h"
#include "mod-ollama-chat_config.h"
#include "Chat.h"
#include "Config.h"

using namespace Acore::ChatCommands;

OllamaChatConfigCommand::OllamaChatConfigCommand()
    : CommandScript("OllamaChatConfigCommand")
{
}

ChatCommandTable OllamaChatConfigCommand::GetCommands() const
{
    static ChatCommandTable ollamaReloadCommandTable =
    {
        { "reload", HandleOllamaReloadCommand, SEC_ADMINISTRATOR, Console::Yes }
    };

    static ChatCommandTable commandTable =
    {
        { "ollama", ollamaReloadCommandTable }
    };

    return commandTable;
}

bool OllamaChatConfigCommand::HandleOllamaReloadCommand(ChatHandler* handler)
{
    sConfigMgr->Reload();
    LoadOllamaChatConfig();
    LoadBotPersonalityList();
    LoadBotConversationHistoryFromDB();
    handler->SendSysMessage("OllamaChat: Configuration reloaded from conf!");
    return true;
}

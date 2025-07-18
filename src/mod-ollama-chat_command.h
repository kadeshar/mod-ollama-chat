#ifndef MOD_OLLAMA_CHAT_COMMAND_H
#define MOD_OLLAMA_CHAT_COMMAND_H

#include "ScriptMgr.h"
#include "Chat.h"

class OllamaChatConfigCommand : public CommandScript
{
public:
    OllamaChatConfigCommand();
    Acore::ChatCommands::ChatCommandTable GetCommands() const override;

    static bool HandleOllamaReloadCommand(ChatHandler* handler); // Added static method declaration
};

#endif // MOD_OLLAMA_CHAT_COMMAND_H

#ifndef MOD_OLLAMA_CHAT_CONFIG_H
#define MOD_OLLAMA_CHAT_CONFIG_H

#include <string>
#include <cstdint>
#include <vector>
#include "ScriptMgr.h"  // Ensure WorldScript is defined

extern float            g_SayDistance;
extern float            g_YellDistance;
extern float            g_GeneralDistance;
extern uint32_t         g_PlayerReplyChance;
extern uint32_t         g_BotReplyChance;
extern uint32_t         g_MaxBotsToPick;
extern std::string      g_OllamaUrl;
extern std::string      g_OllamaModel;
extern uint32_t         g_OllamaNumPredict;
extern float            g_OllamaTemperature;
extern float            g_OllamaTopP;
extern float            g_OllamaRepeatPenalty;
extern uint32_t         g_OllamaNumCtx;
extern std::string      g_OllamaStop;
extern std::string      g_OllamaSystemPrompt;
extern std::string      g_OllamaSeed;

extern uint32_t         g_MaxConcurrentQueries;

extern bool             g_Enable;
extern bool             g_DisableRepliesInCombat;
extern bool             g_EnableRandomChatter;
extern bool             g_DebugEnabled;
extern uint32_t         g_MinRandomInterval;
extern uint32_t         g_MaxRandomInterval;
extern float            g_RandomChatterRealPlayerDistance;
extern uint32_t         g_RandomChatterBotCommentChance;
extern uint32_t         g_RandomChatterMaxBotsPerPlayer;

extern bool             g_EnableRPPersonalities;

extern std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::deque<std::pair<std::string, std::string>>>> g_BotConversationHistory;
extern std::mutex       g_ConversationHistoryMutex;
extern uint32_t         g_MaxConversationHistory;
extern uint32_t         g_ConversationHistorySaveInterval;
extern time_t           g_LastHistorySaveTime;

extern std::string      g_ChatHistoryHeaderTemplate;
extern std::string      g_ChatHistoryLineTemplate;
extern std::string      g_ChatHistoryFooterTemplate;
extern bool             g_EnableChatHistory;

extern std::string      g_RandomChatterPromptTemplate;

extern std::unordered_map<uint64_t, std::string> g_BotPersonalityList;
extern std::unordered_map<std::string, std::string> g_PersonalityPrompts;
extern std::vector<std::string> g_PersonalityKeys;

extern std::string      g_ChatPromptTemplate;
extern std::string      g_ChatExtraInfoTemplate;

extern std::string      g_ChatBotSnapshotTemplate;

extern std::vector<std::string> g_BlacklistCommands;

extern std::string      g_DefaultPersonalityPrompt;

extern std::vector<std::string> g_EnvCommentCreature;
extern std::vector<std::string> g_EnvCommentGameObject;
extern std::vector<std::string> g_EnvCommentEquippedItem;
extern std::vector<std::string> g_EnvCommentBagItem;
extern std::vector<std::string> g_EnvCommentBagItemSell;
extern std::vector<std::string> g_EnvCommentSpell;
extern std::vector<std::string> g_EnvCommentQuestArea;
extern std::vector<std::string> g_EnvCommentVendor;
extern std::vector<std::string> g_EnvCommentQuestgiver;
extern std::vector<std::string> g_EnvCommentBagSlots;
extern std::vector<std::string> g_EnvCommentDungeon;
extern std::vector<std::string> g_EnvCommentUnfinishedQuest;

// Loads configuration
void LoadOllamaChatConfig();

void LoadPersonalityTemplatesFromDB();


// Declaration of the configuration WorldScript.
class OllamaChatConfigWorldScript : public WorldScript
{
public:
    OllamaChatConfigWorldScript();
    void OnStartup() override;
};

#endif // MOD_OLLAMA_CHAT_CONFIG_H

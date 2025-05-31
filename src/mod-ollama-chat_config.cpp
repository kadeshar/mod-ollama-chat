#define CURL_STATICLIB
#include "mod-ollama-chat_config.h"
#include "Config.h"
#include "Log.h"
#include "mod-ollama-chat_api.h"
#include <fmt/core.h>
#include <sstream>
#include <curl/curl.h>
#include <fstream>


// Global configuration variable definitions...
float      g_SayDistance       = 30.0f;
float      g_YellDistance      = 100.0f;
float      g_GeneralDistance   = 600.0f;
uint32_t   g_PlayerReplyChance = 90;
uint32_t   g_BotReplyChance    = 10;
uint32_t   g_MaxBotsToPick     = 2;
std::string g_OllamaUrl        = "http://localhost:11434/api/generate";
std::string g_OllamaModel      = "llama3.2:1b";

// New configuration option: API max concurrent queries (0 means no limit)
uint32_t   g_MaxConcurrentQueries = 0;

bool       g_Enable                          = true;
bool       g_EnableRandomChatter             = true;
uint32_t   g_MinRandomInterval               = 45;
uint32_t   g_MaxRandomInterval               = 180;
float      g_RandomChatterRealPlayerDistance = 40.0f;
uint32_t   g_RandomChatterBotCommentChance   = 25;

bool       g_EnableRPPersonalities           = false;

std::string g_RandomChatterPromptTemplate;

std::unordered_map<uint64_t, std::string> g_BotPersonalityList;
std::unordered_map<std::string, std::string> g_PersonalityPrompts;
std::vector<std::string> g_PersonalityKeys;

std::string g_ChatPromptTemplate;
std::string g_ChatExtraInfoTemplate;

bool        g_DebugEnabled = false;

std::string g_DefaultPersonalityPrompt;

// Default blacklist commands; these are prefixes that indicate the message is a command.
std::vector<std::string> g_BlacklistCommands = {
    ".playerbots",
    "playerbot",
};

// Environment random chatter message templates (populated from config).
std::vector<std::string> g_EnvCommentCreature;
std::vector<std::string> g_EnvCommentGameObject;
std::vector<std::string> g_EnvCommentEquippedItem;
std::vector<std::string> g_EnvCommentBagItem;
std::vector<std::string> g_EnvCommentBagItemSell;
std::vector<std::string> g_EnvCommentSpell;
std::vector<std::string> g_EnvCommentQuestArea;
std::vector<std::string> g_EnvCommentVendor;
std::vector<std::string> g_EnvCommentQuestgiver;
std::vector<std::string> g_EnvCommentBagSlots;
std::vector<std::string> g_EnvCommentDungeon;
std::vector<std::string> g_EnvCommentUnfinishedQuest;


static std::vector<std::string> SplitString(const std::string& str, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        // Trim whitespace from token
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos)
            tokens.push_back(token.substr(start, end - start + 1));
    }
    return tokens;
}

// Load Bot Personalities from Database
static void LoadBotPersonalityList()
{    
    // Let's make sure our user has sourced the required sql file to add the new table
    QueryResult tableExists = CharacterDatabase.Query("SELECT * FROM information_schema.tables WHERE table_schema = 'acore_characters' AND table_name = 'mod_ollama_chat_personality' LIMIT 1");
    if (!tableExists)
    {
        LOG_ERROR("server.loading", "[Ollama Chat] Please source the required database table first");
        return;
    }

    QueryResult result = CharacterDatabase.Query("SELECT guid,personality FROM mod_ollama_chat_personality");

    if (!result)
    {
        return;
    }
    if (result->GetRowCount() == 0)
    {
        return;
    }    

    if(g_DebugEnabled)
    {
        LOG_INFO("server.loading", "[Ollama Chat] Fetching Bot Personality List into array");
    }

    do
    {
        uint64_t personalityBotGUID = result->Fetch()[0].Get<uint64_t>();
        std::string personalityKey = result->Fetch()[1].Get<std::string>();
        g_BotPersonalityList[personalityBotGUID] = personalityKey;
    } while (result->NextRow());
}

std::string GetMultiLineConfigValue(const std::string& configFilePath, const std::string& key)
{
    std::ifstream infile(configFilePath);
    if (!infile) return "";

    std::string line;
    std::string value;
    bool foundKey = false;
    while (std::getline(infile, line))
    {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        if (trimmed.empty() || trimmed[0] == '#')
            continue;
        size_t pos = trimmed.find('=');
        if (!foundKey && pos != std::string::npos) {
            std::string possibleKey = trimmed.substr(0, pos);
            possibleKey.erase(possibleKey.find_last_not_of(" \t\r\n") + 1);
            if (possibleKey == key) {
                foundKey = true;
                std::string afterEq = trimmed.substr(pos + 1);
                afterEq.erase(0, afterEq.find_first_not_of(" \t\r\n"));
                value += afterEq;
                continue;
            }
        }
        else if (foundKey) {
            // New config key or section
            if (trimmed.find('=') != std::string::npos && trimmed.find('[') == std::string::npos)
                break;
            if (!value.empty()) value += "\n";
            value += trimmed;
        }
    }

    return value;
}

void LoadOllamaChatConfig()
{
    g_SayDistance       = sConfigMgr->GetOption<float>("OllamaChat.SayDistance", 30.0f);
    g_YellDistance      = sConfigMgr->GetOption<float>("OllamaChat.YellDistance", 100.0f);
    g_GeneralDistance   = sConfigMgr->GetOption<float>("OllamaChat.GeneralDistance", 600.0f);
    g_PlayerReplyChance = sConfigMgr->GetOption<uint32_t>("OllamaChat.PlayerReplyChance", 90);
    g_BotReplyChance    = sConfigMgr->GetOption<uint32_t>("OllamaChat.BotReplyChance", 10);
    g_MaxBotsToPick     = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxBotsToPick", 2);
    g_OllamaUrl         = sConfigMgr->GetOption<std::string>("OllamaChat.Url", "http://localhost:11434/api/generate");
    g_OllamaModel       = sConfigMgr->GetOption<std::string>("OllamaChat.Model", "llama3.2:1b");

    g_MaxConcurrentQueries            = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxConcurrentQueries", 0);

    g_Enable                          = sConfigMgr->GetOption<bool>("OllamaChat.Enable", true);
    g_EnableRandomChatter             = sConfigMgr->GetOption<bool>("OllamaChat.EnableRandomChatter", true);

    g_DebugEnabled                    = sConfigMgr->GetOption<bool>("OllamaChat.DebugEnabled", false);

    g_MinRandomInterval               = sConfigMgr->GetOption<uint32_t>("OllamaChat.MinRandomInterval", 45);
    g_MaxRandomInterval               = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxRandomInterval", 180);
    g_RandomChatterRealPlayerDistance = sConfigMgr->GetOption<float>("OllamaChat.RandomChatterRealPlayerDistance", 40.0f);
    g_RandomChatterBotCommentChance   = sConfigMgr->GetOption<uint32_t>("OllamaChat.RandomChatterBotCommentChance", 25);
    
    g_MaxConcurrentQueries            = sConfigMgr->GetOption<uint32_t>("OllamaChat.MaxConcurrentQueries", 0);

    g_EnableRPPersonalities           = sConfigMgr->GetOption<bool>("OllamaChat.EnableRPPersonalities", false);

    g_RandomChatterPromptTemplate     = sConfigMgr->GetOption<std::string>("OllamaChat.RandomChatterPromptTemplate", "You are a World of Warcraft player in the Wrath of the Lich King expansion. Your name is {}. You are a level {} {}, Race: {}, Gender: {}, Talent Spec: {}, Faction: {}. You are currently located in {}, inside the zone '{}' on map '{}'. Your Personality is '{}'. {} Make it a short statement (under 15 words) using casual WoW-style slang and attitude. Respond as a real WoW player would.");

    g_ChatPromptTemplate              = sConfigMgr->GetOption<std::string>("OllamaChat.ChatPromptTemplate", "You are a World of Warcraft player in the Wrath of the Lich King expansion version of the game, but you are also knowledgeable about Vanilla WoW and The Burning Crusade. Make sure your responses are relevant to the game lore and context. Your character's name is {} and you are a level {} {}. Your Personality is '{}'. A level {} {} named {} said '{}' in the game chat. Reply (under 15 words) relevant to the message and context. {} Keep your responses natural and unfiltered, just like real WoW players; embrace common slang, faction pride, and rivalry. If someone jokes, joke back. If they’re rude, don’t hold back being rude to them. Show respect to high-level players but be snooty and entitled over lower levels. When giving directions, be precise, using landmarks, flight paths, and major cities for clarity. Keep responses accurate, short and to the point. Be factual about everything like your location, race, class, etc. Do not say you're in a location or are a class or race that you are not. Always prioritize sounding like a real human player.");
    
    g_ChatExtraInfoTemplate           = sConfigMgr->GetOption<std::string>("OllamaChat.ChatExtraInfoTemplate", "Your info: Race: {}, Gender: {}, Talent Spec: {}, Faction: {}, Guild: {}, Group: {}, Gold: {}. Other players info: Race: {}, Gender: {}, Talent Spec: {}, Faction: {}, Guild: {}, Group: {}, Gold: {}. Approximate distance between you and other player: {:.1f} yards. {}");

    g_DefaultPersonalityPrompt        = sConfigMgr->GetOption<std::string>("OllamaChat.DefaultPersonalityPrompt", "Talk like a standard WoW player.");

    
    // Load extra blacklist commands from config (comma-separated list)
    std::string extraBlacklist = sConfigMgr->GetOption<std::string>("OllamaChat.BlacklistCommands", "");
    if (!extraBlacklist.empty())
    {
        std::vector<std::string> extraList = SplitString(extraBlacklist, ',');
        for (const auto& cmd : extraList)
        {
            g_BlacklistCommands.push_back(cmd);
        }
    }

    g_PersonalityPrompts.clear();
    g_PersonalityKeys.clear();

    LoadPersonalityTemplatesFromDB();

    g_queryManager.setMaxConcurrentQueries(g_MaxConcurrentQueries);

    // Loads the environment random chatter message templates for each type.
    // Each config option is a pipe-separated list of string templates,
    // using {} as a placeholder for named substitutions.
    // Helper to load a multi-line config option into a std::vector<std::string>
    auto LoadEnvCommentVector = [](const char* key, const std::vector<std::string>& defaults = {}) -> std::vector<std::string>
    {
        std::string val = sConfigMgr->GetOption<std::string>(key, "");
        std::vector<std::string> result;
        std::istringstream iss(val);
        std::string line;
        while (std::getline(iss, line)) {
            // Trim whitespace (both ends)
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos)
                result.push_back(line.substr(start, end - start + 1));
        }
        if (result.empty() && !defaults.empty())
            return defaults;
        return result;
    };


    g_EnvCommentCreature        = LoadEnvCommentVector("OllamaChat.EnvCommentCreature", { "You spot a creature named '{}'." });
    g_EnvCommentGameObject      = LoadEnvCommentVector("OllamaChat.EnvCommentGameObject", { "You see {} nearby." });
    g_EnvCommentEquippedItem    = LoadEnvCommentVector("OllamaChat.EnvCommentEquippedItem", { "Talk about your equipped item {}." });
    g_EnvCommentBagItem         = LoadEnvCommentVector("OllamaChat.EnvCommentBagItem", { "You notice a {} in your bag." });
    g_EnvCommentBagItemSell     = LoadEnvCommentVector("OllamaChat.EnvCommentBagItemSell", { "You are trying persuasively to sell {} of this item {}." });
    g_EnvCommentSpell           = LoadEnvCommentVector("OllamaChat.EnvCommentSpell", { "Talk about use cases or strategies for your spell '{}'." });
    g_EnvCommentQuestArea       = LoadEnvCommentVector("OllamaChat.EnvCommentQuestArea", { "Suggest you could go questing around {}." });
    g_EnvCommentVendor          = LoadEnvCommentVector("OllamaChat.EnvCommentVendor", { "You spot {} selling wares nearby." });
    g_EnvCommentQuestgiver      = LoadEnvCommentVector("OllamaChat.EnvCommentQuestgiver", { "{} looks like they have {} quests for anyone brave enough." });
    g_EnvCommentBagSlots        = LoadEnvCommentVector("OllamaChat.EnvCommentBagSlots", { "You have {} free bag slots left." });
    g_EnvCommentDungeon         = LoadEnvCommentVector("OllamaChat.EnvCommentDungeon", { "You're in a Dungeon instance named '{}' talk about the Dungeon or one of its Bosses." });
    g_EnvCommentUnfinishedQuest = LoadEnvCommentVector("OllamaChat.EnvCommentUnfinishedQuest", { "Say the name of and talk about your un-finished quest '{}'." });

    LOG_INFO("server.loading",
             "[mod-ollama-chat] Config loaded: Enabled = {}, SayDistance = {}, YellDistance = {}, "
             "GeneralDistance = {}, PlayerReplyChance = {}%, BotReplyChance = {}%, MaxBotsToPick = {}, "
             "Url = {}, Model = {}, MaxConcurrentQueries = {}, EnableRandomChatter = {}, MinRandInt = {}, MaxRandInt = {}, RandomChatterRealPlayerDistance = {}, "
             "RandomChatterBotCommentChance = {}. MaxConcurrentQueries = {}. Extra blacklist commands: {}",
             g_Enable, g_SayDistance, g_YellDistance, g_GeneralDistance,
             g_PlayerReplyChance, g_BotReplyChance, g_MaxBotsToPick,
             g_OllamaUrl, g_OllamaModel, g_MaxConcurrentQueries,
             g_EnableRandomChatter, g_MinRandomInterval, g_MaxRandomInterval, g_RandomChatterRealPlayerDistance,
             g_RandomChatterBotCommentChance, g_MaxConcurrentQueries, extraBlacklist);
}

void LoadPersonalityTemplatesFromDB()
{
    g_PersonalityPrompts.clear();
    g_PersonalityKeys.clear();

    QueryResult result = CharacterDatabase.Query("SELECT `key`, `prompt` FROM `mod_ollama_chat_personality_templates`");
    if (!result)
    {
        LOG_ERROR("server.loading", "[Ollama Chat] No personality templates found in the database!");
        return;
    }

    do
    {
        std::string key = (*result)[0].Get<std::string>();
        std::string prompt = (*result)[1].Get<std::string>();
        g_PersonalityPrompts[key] = prompt;
        g_PersonalityKeys.push_back(key);
        LOG_INFO("server.loading", "Loaded personality key: '{}' value: '{}'", key, prompt);
    } while (result->NextRow());
}


// Definition of the configuration WorldScript.
OllamaChatConfigWorldScript::OllamaChatConfigWorldScript() : WorldScript("OllamaChatConfigWorldScript") { }

void OllamaChatConfigWorldScript::OnStartup()
{
    curl_global_init(CURL_GLOBAL_ALL);
    LoadOllamaChatConfig();
    LoadBotPersonalityList();
}

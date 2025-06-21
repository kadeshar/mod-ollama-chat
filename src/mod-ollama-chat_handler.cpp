#include "Log.h"
#include "Player.h"
#include "Chat.h"
#include "Channel.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Config.h"
#include "Common.h"
#include "Guild.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "AiFactory.h"
#include "ChannelMgr.h"
#include <sstream>
#include <vector>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <algorithm>
#include <random>
#include <cctype>
#include <chrono>
#include <ctime>
#include "DatabaseEnv.h"
#include "mod-ollama-chat_handler.h"
#include "mod-ollama-chat_api.h"
#include "mod-ollama-chat_personality.h"
#include "mod-ollama-chat_config.h"

// For AzerothCore range checks
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Map.h"
#include "GridNotifiers.h"

// Forward declarations for internal helper functions.
static bool IsBotEligibleForChatChannelLocal(Player* bot, Player* player,
                                             ChatChannelSourceLocal source, Channel* channel = nullptr);
static std::string GenerateBotPrompt(Player* bot, std::string playerMessage, Player* player);

const char* ChatChannelSourceLocalStr[] =
{
    "Undefined",
    "Say",
    "Party",
    "Raid",
    "Guild",
    "Yell",
    "General"
};

std::string GetConversationEntryKey(uint64_t botGuid, uint64_t playerGuid, const std::string& playerMessage, const std::string& botReply)
{
    // Use a combination that guarantees uniqueness
    return fmt::format("{}:{}:{}:{}", botGuid, playerGuid, playerMessage, botReply);
}

std::string rtrim(const std::string& s)
{
    const std::string whitespace = " \t\n\r,.!?;:";
    size_t end = s.find_last_not_of(whitespace);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

ChatChannelSourceLocal GetChannelSourceLocal(uint32_t type)
{
    switch (type)
    {
        case 1:
            return SRC_SAY_LOCAL;
        case 51:
            return SRC_PARTY_LOCAL;
        case 3:
            return SRC_RAID_LOCAL;
        case 5:
            return SRC_GUILD_LOCAL;
        case 6:
            return SRC_YELL_LOCAL;
        case 17:
            return SRC_GENERAL_LOCAL;
        default:
            return SRC_UNDEFINED_LOCAL;
    }
}

Channel* GetValidChannel(uint32_t teamId, const std::string& channelName, Player* player)
{
    ChannelMgr* cMgr = ChannelMgr::forTeam(static_cast<TeamId>(teamId));
    Channel* channel = cMgr->GetChannel(channelName, player);
    if (!channel)
    {
        if(g_DebugEnabled)
        {
            LOG_ERROR("server.loading", "Channel '{}' not found for team {}", channelName, teamId);
        }
    }
    return channel;
}

void PlayerBotChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg)
{
    if (!g_Enable)
        return;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr);
}

void PlayerBotChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Group* /*group*/)
{
    if (!g_Enable)
        return;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, nullptr);
}

void PlayerBotChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Channel* channel)
{
    if (!g_Enable)
        return;

    ChatChannelSourceLocal sourceLocal = GetChannelSourceLocal(type);
    ProcessChat(player, type, lang, msg, sourceLocal, channel);
}

void AppendBotConversation(uint64_t botGuid, uint64_t playerGuid, const std::string& playerMessage, const std::string& botReply)
{
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);
    auto& playerHistory = g_BotConversationHistory[botGuid][playerGuid];
    playerHistory.push_back({ playerMessage, botReply });
    while (playerHistory.size() > g_MaxConversationHistory)
    {
        playerHistory.pop_front();
    }

}

void SaveBotConversationHistoryToDB()
{
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);

    for (const auto& [botGuid, playerMap] : g_BotConversationHistory) {
        for (const auto& [playerGuid, history] : playerMap) {
            for (const auto& pair : history) {
                const std::string& playerMessage = pair.first;
                const std::string& botReply = pair.second;

                std::string escPlayerMsg = playerMessage;
                CharacterDatabase.EscapeString(escPlayerMsg);

                std::string escBotReply = botReply;
                CharacterDatabase.EscapeString(escBotReply);

                CharacterDatabase.Execute(fmt::format(
                    "INSERT IGNORE INTO mod_ollama_chat_history (bot_guid, player_guid, timestamp, player_message, bot_reply) "
                    "VALUES ({}, {}, NOW(), '{}', '{}')",
                    botGuid, playerGuid, escPlayerMsg, escBotReply));
            }
        }
    }

    // Cleanup: keep only the N most recent entries per bot/player pair
    std::string cleanupQuery = R"SQL(
        WITH ranked_history AS (
            SELECT
                bot_guid,
                player_guid,
                timestamp,
                ROW_NUMBER() OVER (
                    PARTITION BY bot_guid, player_guid
                    ORDER BY timestamp DESC
                ) as rn
            FROM mod_ollama_chat_history
        )
        DELETE FROM mod_ollama_chat_history
        WHERE (bot_guid, player_guid, timestamp) IN (
            SELECT bot_guid, player_guid, timestamp
            FROM ranked_history
            WHERE rn > {}
        );
    )SQL";
    CharacterDatabase.Execute(fmt::format(cleanupQuery, g_MaxConversationHistory));
}


std::string GetBotHistoryPrompt(uint64_t botGuid, uint64_t playerGuid, std::string playerMessage)
{
    if(!g_EnableChatHistory)
    {
        return "";
    }
    
    std::lock_guard<std::mutex> lock(g_ConversationHistoryMutex);

    std::string result;
    const auto botIt = g_BotConversationHistory.find(botGuid);
    if (botIt == g_BotConversationHistory.end())
        return result;
    const auto playerIt = botIt->second.find(playerGuid);
    if (playerIt == botIt->second.end())
        return result;

    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(playerGuid));
    std::string playerName = player ? player->GetName() : "The player";

    result += fmt::format(g_ChatHistoryHeaderTemplate, playerName);

    for (const auto& entry : playerIt->second) {
        result += fmt::format(g_ChatHistoryLineTemplate, playerName, entry.first, entry.second);
    }

    result += fmt::format(g_ChatHistoryFooterTemplate, playerName, playerMessage);

    return result;
}


void PlayerBotChatHandler::ProcessChat(Player* player, uint32_t /*type*/, uint32_t /*lang*/, std::string& msg, ChatChannelSourceLocal sourceLocal, Channel* channel)
{
    std::string chanName = (channel != nullptr) ? channel->GetName() : "Unknown";
    uint32_t channelId = (channel != nullptr) ? channel->GetChannelId() : 0;
    if(g_DebugEnabled)
    {
        LOG_INFO("server.loading",
                "Player {} sent msg: '{}' | Channel Name: {} | Channel ID: {}",
                player->GetName(), msg, chanName, channelId);
    }

    std::string trimmedMsg = rtrim(msg);
    for (const std::string& blacklist : g_BlacklistCommands)
    {
        if (trimmedMsg.find(blacklist) == 0)
        {
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "Message starts with '{}' (blacklisted). Skipping bot responses.", blacklist);
            }
            return;
        }
    }
             
    PlayerbotAI* senderAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    bool senderIsBot = (senderAI && senderAI->IsBotAI());
    
    std::vector<Player*> eligibleBots;
    if (channel != nullptr)
    {
        ChannelMgr* cMgr = ChannelMgr::forTeam(static_cast<TeamId>(player->GetTeamId()));
        Channel* senderChannel = cMgr->GetChannel(channel->GetName(), player);
        if (senderChannel)
        {
            auto const& allPlayers = ObjectAccessor::GetPlayers();
            for (auto const& itr : allPlayers)
            {
                Player* candidate = itr.second;
                if (!candidate->IsInWorld())
                    continue;
                ChannelMgr* candidateCM = ChannelMgr::forTeam(static_cast<TeamId>(candidate->GetTeamId()));
                Channel* candidateChannel = candidateCM->GetChannel(channel->GetName(), candidate);
                if (candidateChannel && candidateChannel->GetChannelId() == senderChannel->GetChannelId())
                {
                    eligibleBots.push_back(candidate);
                }
            }
        }
    }
    else
    {
        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* candidate = itr.second;
            if (candidate->IsInWorld())
                eligibleBots.push_back(candidate);
        }
    }
    
    std::vector<Player*> candidateBots;
    for (Player* bot : eligibleBots)
    {
        if (IsBotEligibleForChatChannelLocal(bot, player, sourceLocal, channel))
            candidateBots.push_back(bot);
    }
    
    uint32_t chance = senderIsBot ? g_BotReplyChance : g_PlayerReplyChance;
    if (senderIsBot)
    {
        bool realPlayerNearby = false;
        for (auto const& itr : ObjectAccessor::GetPlayers())
        {
            Player* candidate = itr.second;
            if (candidate == player)
                continue;
            if (!candidate->IsInWorld())
                continue;
            if (player->GetDistance(candidate) > g_GeneralDistance)
                continue;
            if (!sPlayerbotsMgr->GetPlayerbotAI(candidate))
            {
                realPlayerNearby = true;
                break;
            }
        }
        if (!realPlayerNearby)
            chance = 0;
    }
    
    std::vector<Player*> finalCandidates;
    std::vector<std::pair<size_t, Player*>> mentionedBots;
    for (Player* bot : candidateBots)
    {
        size_t pos = msg.find(bot->GetName());
        if (pos != std::string::npos)
            mentionedBots.push_back({ pos, bot });
    }
    if (!mentionedBots.empty())
    {
        std::sort(mentionedBots.begin(), mentionedBots.end(),
                  [](auto &a, auto &b) { return a.first < b.first; });
        Player* chosenBot = mentionedBots.front().second;
        finalCandidates.clear();
        if (!senderIsBot)
        {
            finalCandidates.push_back(chosenBot);
            if(g_DebugEnabled)
            {
                LOG_INFO("server.loading", "Non-bot player mentioned bot '{}', forcing reply.", chosenBot->GetName());
            }
        }
        else
        {
            uint32_t roll = urand(0, 99);
            if (roll < chance)
                finalCandidates.push_back(chosenBot);
        }
    }
    else
    {
        for (Player* bot : candidateBots)
        {
            uint32_t roll = urand(0, 99);
            if (roll < chance)
                finalCandidates.push_back(bot);
        }
    }
    
    if (finalCandidates.empty())
    {
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "No eligible bots found to respond to message '{}'.", msg);
        }
        return;
    }
    
    if (finalCandidates.size() > g_MaxBotsToPick)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(finalCandidates.begin(), finalCandidates.end(), g);
        uint32_t countToPick = urand(1, g_MaxBotsToPick);
        finalCandidates.resize(countToPick);
    }
    
    uint64_t senderGuid = player->GetGUID().GetRawValue();
    
    for (Player* bot : finalCandidates)
    {
        float distance = player->GetDistance(bot);
        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "Bot {} (distance: {}) is set to respond.", bot->GetName(), distance);
        }
        std::string prompt = GenerateBotPrompt(bot, msg, player);
        uint64_t botGuid = bot->GetGUID().GetRawValue();
        
        std::thread([botGuid, senderGuid, prompt, sourceLocal, channelId = (channel ? channel->GetChannelId() : 0), msg]() {
            try {
                // Use the QueryManager to submit the query.
                std::future<std::string> responseFuture = SubmitQuery(prompt);
                std::string response = responseFuture.get();

                // Reacquire pointers by GUID.
                Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                Player* senderPtr = ObjectAccessor::FindPlayer(ObjectGuid(senderGuid));
                if (!botPtr)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "Failed to reacquire bot from GUID {}", botGuid);
                    }
                    return;
                }
                if (!senderPtr)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "Failed to reacquire sender from GUID {}", senderGuid);
                    }
                    return;
                }
                if (response.empty())
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "Bot {} received empty response from Ollama API.", botPtr->GetName());
                    }
                    return;
                }
                PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(botPtr);
                if (!botAI)
                {
                    if(g_DebugEnabled)
                    {
                        LOG_ERROR("server.loading", "No PlayerbotAI found for bot {}", botPtr->GetName());
                    }
                    return;
                }
                // Route the response.
                if (channelId != 0)
                {
                    ChatChannelId chanId = static_cast<ChatChannelId>(channelId);
                    botAI->SayToChannel(response, chanId);
                }
                else
                {
                    switch (sourceLocal)
                    {
                        case SRC_GUILD_LOCAL: botAI->SayToGuild(response); break;
                        case SRC_PARTY_LOCAL: botAI->SayToParty(response); break;
                        case SRC_RAID_LOCAL:  botAI->SayToRaid(response); break;
                        case SRC_SAY_LOCAL:   botAI->Say(response); break;
                        case SRC_YELL_LOCAL:  botAI->Yell(response); break;
                        default:              botAI->Say(response); break;
                    }
                }
                AppendBotConversation(botGuid, senderGuid, msg, response);
                float respDistance = senderPtr->GetDistance(botPtr);
                if(g_DebugEnabled)
                {
                    LOG_INFO("server.loading", "Bot {} (distance: {}) responded: {}", botPtr->GetName(), respDistance, response);
                }
            }
            catch (const std::exception& ex)
            {
                if(g_DebugEnabled)
                {
                    LOG_ERROR("server.loading", "Exception in bot response thread: {}", ex.what());
                }
            }
        }).detach();

    }
}

static bool IsBotEligibleForChatChannelLocal(Player* bot, Player* player, ChatChannelSourceLocal source, Channel* channel)
{
    if (!bot || !player || bot == player)
        return false;
    if (bot->GetTeamId() != player->GetTeamId())
        return false;
    if (!sPlayerbotsMgr->GetPlayerbotAI(bot))
        return false;
    if (channel && !bot->IsInChannel(channel))
        return false;
    
    bool isInParty = (player->GetGroup() && bot->GetGroup() && (player->GetGroup() == bot->GetGroup()));
    float threshold = 0.0f;
    switch (source)
    {
        case SRC_SAY_LOCAL:    threshold = g_SayDistance;     break;
        case SRC_YELL_LOCAL:   threshold = g_YellDistance;    break;
        case SRC_GENERAL_LOCAL:threshold = g_GeneralDistance; break;
        default:               threshold = 0.0f;              break;
    }
    switch (source)
    {
        case SRC_GUILD_LOCAL:
            return (player->GetGuild() && bot->GetGuildId() == player->GetGuildId());
        case SRC_PARTY_LOCAL:
            return isInParty;
        case SRC_RAID_LOCAL:
            return isInParty;
        case SRC_SAY_LOCAL:
            return (threshold > 0.0f && player->GetDistance(bot) > threshold) ? false : true;
        case SRC_YELL_LOCAL:
            return (threshold > 0.0f && player->GetDistance(bot) > threshold) ? false : true;
        case SRC_GENERAL_LOCAL:
            return (threshold > 0.0f && player->GetDistance(bot) > threshold) ? false : true;
        default:
            return false;
    }
}

std::string GenerateBotPrompt(Player* bot, std::string playerMessage, Player* player)
{  
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);

    AreaTableEntry const* botCurrentArea = botAI->GetCurrentArea();
    AreaTableEntry const* botCurrentZone = botAI->GetCurrentZone();

    uint64_t botGuid                = bot->GetGUID().GetRawValue();
    uint64_t playerGuid             = player->GetGUID().GetRawValue();

    std::string personality         = GetBotPersonality(bot);
    std::string personalityPrompt   = GetPersonalityPromptAddition(personality);
    std::string botName             = bot->GetName();
    uint32_t botLevel               = bot->GetLevel();
    uint8_t botGenderByte           = bot->getGender();
    std::string botAreaName         = botCurrentArea ? botAI->GetLocalizedAreaName(botCurrentArea): "UnknownArea";
    std::string botZoneName         = botCurrentZone ? botAI->GetLocalizedAreaName(botCurrentZone): "UnknownZone";
    std::string botMapName          = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";
    std::string botClass            = botAI->GetChatHelper()->FormatClass(bot->getClass());
    std::string botRace             = botAI->GetChatHelper()->FormatRace(bot->getRace());
    std::string botRole             = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
    std::string botGender           = (botGenderByte == 0 ? "Male" : "Female");
    std::string botFaction          = (bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
    std::string botGuild            = (bot->GetGuild() ? bot->GetGuild()->GetName() : "No Guild");
    std::string botGroupStatus      = (bot->GetGroup() ? "In a group" : "Solo");
    uint32_t botGold                = bot->GetMoney() / 10000;

    std::string playerName          = player->GetName();
    uint32_t playerLevel            = player->GetLevel();
    std::string playerClass         = botAI->GetChatHelper()->FormatClass(player->getClass());
    std::string playerRace          = botAI->GetChatHelper()->FormatRace(player->getRace());
    std::string playerRole          = ChatHelper::FormatClass(player, AiFactory::GetPlayerSpecTab(player));
    uint8_t playerGenderByte        = player->getGender();
    std::string playerGender        = (playerGenderByte == 0 ? "Male" : "Female");
    std::string playerFaction       = (player->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
    std::string playerGuild         = (player->GetGuild() ? player->GetGuild()->GetName() : "No Guild");
    std::string playerGroupStatus   = (player->GetGroup() ? "In a group" : "Solo");
    uint32_t playerGold             = player->GetMoney() / 10000;
    float playerDistance            = player->IsInWorld() && bot->IsInWorld() ? player->GetDistance(bot) : -1.0f;

    std::string chatHistory         = GetBotHistoryPrompt(botGuid, playerGuid, playerMessage);

    std::string extraInfo = fmt::format(
        g_ChatExtraInfoTemplate,
        botRace, botGender, botRole, botFaction, botGuild, botGroupStatus, botGold,
        playerRace, playerGender, playerRole, playerFaction, playerGuild, playerGroupStatus, playerGold,
        playerDistance, botAreaName, botZoneName, botMapName
    );
    
    std::string prompt = fmt::format(
        g_ChatPromptTemplate,
        botName, botLevel, botClass, personalityPrompt,
        playerLevel, playerClass, playerName, playerMessage,
        chatHistory, extraInfo
    );

    return prompt;
}
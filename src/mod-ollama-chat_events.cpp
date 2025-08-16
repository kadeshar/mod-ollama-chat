#include "mod-ollama-chat_events.h"
#include "mod-ollama-chat_config.h"
#include "mod-ollama-chat_random.h"
#include "mod-ollama-chat_api.h"
#include "mod-ollama-chat-utilities.h"
#include "mod-ollama-chat_personality.h"
#include "mod-ollama-chat_sentiment.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Guild.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "AiFactory.h"
#include "SpellMgr.h"
#include "AchievementMgr.h"
#include "GameObject.h"
#include <vector>
#include <thread>
#include <random>
#include <fmt/core.h>

static OllamaBotEventChatter eventChatter;

void OllamaBotEventChatter::DispatchGameEvent(Player* source, std::string type, std::string detail)
{
    if (!g_Enable || !g_EnableEventChatter)
        return;
    
    if (!source)
    {
       return;
    }

    bool isSourceBot = sPlayerbotsMgr->GetPlayerbotAI(source) != nullptr;
    bool hasNearbyRealPlayer = false;
    bool isGuildEvent = false;

    // Check if this is a guild event (level up, gear, etc.)
    if (source->GetGuild() && g_EnableGuildRandomChatter)
    {
        // Check if there are real players in the guild
        Guild* guild = source->GetGuild();
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!player || !player->IsInWorld())
                continue;
                
            if (sPlayerbotsMgr->GetPlayerbotAI(player))
                continue;
                
            if (player->GetGuild() && player->GetGuild()->GetId() == guild->GetId())
            {
                isGuildEvent = true;
                break;
            }
        }
    }

    // Check for nearby real players (original logic)
    for (auto const& pair : source->GetMap()->GetPlayers())
    {
        Player* player = pair.GetSource();
        if (player == source)
            continue;

        if (!sPlayerbotsMgr->GetPlayerbotAI(player) && player->IsWithinDist(source, g_EventChatterRealPlayerDistance, false))
        {
            hasNearbyRealPlayer = true;
            break;
        }
    }

    if (isSourceBot && !hasNearbyRealPlayer && !isGuildEvent)
    {
        if (g_DebugEnabled)
        {
            //LOG_INFO("server.loading", "[OllamaChat] Skipping bot source {} - no real players nearby", source->GetName());
        }
        return;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] DispatchGameEvent from {} | type={} | detail={}", source->GetName(), type, detail);

    float maxDist = g_EventChatterRealPlayerDistance;
    bool disableInCombat = g_DisableRepliesInCombat;

    std::vector<Player*> candidateBots;
    
    if (isGuildEvent)
    {
        // For guild events, get guild bots
        Guild* guild = source->GetGuild();
        for (auto const& pair : ObjectAccessor::GetPlayers())
        {
            Player* player = pair.second;
            if (!player || !player->IsInWorld())
                continue;
                
            PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(player);
            if (!ai)
                continue;
                
            if (!player->GetGuild() || player->GetGuild()->GetId() != guild->GetId())
                continue;
                
            // For guild events, just check if there are any real players online in the guild
            bool hasRealPlayerInGuild = false;
            for (auto const& pair2 : ObjectAccessor::GetPlayers())
            {
                Player* guildPlayer = pair2.second;
                if (!guildPlayer || !guildPlayer->IsInWorld())
                    continue;
                    
                if (sPlayerbotsMgr->GetPlayerbotAI(guildPlayer))
                    continue;
                    
                if (guildPlayer->GetGuild() && guildPlayer->GetGuild()->GetId() == guild->GetId())
                {
                    hasRealPlayerInGuild = true;
                    break;
                }
            }
            
            if (hasRealPlayerInGuild)
                candidateBots.push_back(player);
        }
    }
    else
    {
        // For regular events, get nearby bots
        for (auto const& pair : source->GetMap()->GetPlayers())
        {
            Player* player = pair.GetSource();
            if (player->IsWithinDist(source, maxDist, false)) {
                candidateBots.push_back(player);
                if (g_DebugEnabled)
                    LOG_INFO("server.loading", "[OllamaChat] Nearby player {} within {:.1f} yards", player->GetName(), maxDist);
            }
        }
    }

    uint32_t responses = 0;

    for (Player* bot : candidateBots)
    {
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - not a bot", bot->GetName());
            continue;
        }

        if (disableInCombat && bot->IsInCombat()) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - in combat", bot->GetName());
            continue;
        }

        uint32_t chance = 0;
        if(source == bot)
        {
            chance = g_EventChatterBotSelfCommentChance;
        } 
        else if (isGuildEvent)
        {
            chance = g_GuildChatterBotCommentChance;
        }
        else 
        {
            chance = g_EventChatterBotCommentChance;
        }

        if (urand(1, 100) > chance) {
            if (g_DebugEnabled)
                LOG_INFO("server.loading", "[OllamaChat] Skipping {} - failed chance roll", bot->GetName());
            continue;
        }

        if (g_DebugEnabled)
            LOG_INFO("server.loading", "[OllamaChat] Queueing event for bot {}", bot->GetName());

        QueueEvent(bot, type, detail, source->GetName(), isGuildEvent);

        responses++;
        uint32_t maxBots = isGuildEvent ? g_GuildChatterMaxBotsPerEvent : g_EventChatterMaxBotsPerPlayer;
        if (maxBots > 0 && responses >= maxBots)
            break;
    }

    if (g_DebugEnabled)
        LOG_INFO("server.loading", "[OllamaChat] Dispatch complete. {} bots responded", responses);
}


void OllamaBotEventChatter::QueueEvent(Player* bot, std::string type, std::string detail, std::string actorName, bool isGuildEvent)
{
    if (!g_Enable || !g_EnableEventChatter || !bot)
        return;

    uint64_t botGuid = bot->GetGUID().GetRawValue();

    std::thread([this, botGuid, type, detail, actorName, isGuildEvent]()
    {
        try
        {
            Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;

            std::string prompt = BuildPrompt(botPtr, g_EventChatterPromptTemplate, type, detail, actorName);
            if (prompt.empty()) return;

            std::string response = QueryOllamaAPI(prompt);
            if (response.empty()) return;

            // reacquire pointers before use
            botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;
            PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(botPtr);
            if (!botAI) return;

            if (isGuildEvent && botPtr->GetGuild())
                botAI->SayToGuild(response);
            else if (botPtr->GetGroup())
                botAI->SayToParty(response);
            else
                botAI->Say(response);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("server.loading", "[OllamaChat] Exception in QueueEvent thread: {}", e.what());
        }
    }).detach();
}


std::string OllamaBotEventChatter::BuildPrompt(Player* bot, std::string promptTemplate, std::string eventType, std::string eventDetail, std::string actorName)
{
    if (!bot) return "";

    PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
    if (!ai) return "";

    std::string personality = GetBotPersonality(bot);
    std::string personalityPrompt = GetPersonalityPromptAddition(personality);
    std::string botName = bot->GetName();
    std::string botClass = ai->GetChatHelper() ? ai->GetChatHelper()->FormatClass(bot->getClass()) : "UnknownClass";
    uint32_t botLevel = bot->GetLevel();
    std::string botRace = ai->GetChatHelper() ? ai->GetChatHelper()->FormatRace(bot->getRace()) : "UnknownRace";
    std::string botRole = ai->GetChatHelper() ? ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot)) : "UnknownRole";
    std::string botGender = bot->getGender() == GENDER_MALE ? "Male" : "Female";
    std::string botFaction = bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde";

    AreaTableEntry const* botCurrentArea = ai->GetCurrentArea();
    AreaTableEntry const* botCurrentZone = ai->GetCurrentZone();
    std::string botAreaName = botCurrentArea ? ai->GetLocalizedAreaName(botCurrentArea) : "UnknownArea";
    std::string botZoneName = botCurrentZone ? ai->GetLocalizedAreaName(botCurrentZone) : "UnknownZone";
    std::string botMapName = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";

    // Try to get sentiment information if the actor is a player
    std::string sentimentInfo = "";
    if (g_EnableSentimentTracking && !actorName.empty())
    {
        // Try to find the actor player by name
        Player* actorPlayer = ObjectAccessor::FindPlayerByName(actorName);
        if (actorPlayer)
        {
            sentimentInfo = GetSentimentPromptAddition(bot, actorPlayer);
        }
    }

    return SafeFormat(
        promptTemplate,
        fmt::arg("bot_name", botName),
        fmt::arg("bot_level", botLevel),
        fmt::arg("bot_class", botClass),
        fmt::arg("bot_race", botRace),
        fmt::arg("bot_gender", botGender),
        fmt::arg("bot_role", botRole),
        fmt::arg("bot_faction", botFaction),
        fmt::arg("bot_area", botAreaName),
        fmt::arg("bot_zone", botZoneName),
        fmt::arg("bot_map", botMapName),
        fmt::arg("bot_personality", personalityPrompt),
        fmt::arg("event_type", eventType),
        fmt::arg("event_detail", eventDetail),
        fmt::arg("actor_name", actorName),
        fmt::arg("sentiment_info", sentimentInfo)
    );
}

// === Script Hooks ===

ChatOnKill::ChatOnKill() : PlayerScript("ChatOnKill") {}

void ChatOnKill::OnPlayerCreatureKill(Player* killer, Creature* victim)
{
    if (!killer || !victim)
    {
        return;
    }
    eventChatter.DispatchGameEvent(killer, g_EventTypeDefeated, victim->GetName());
}

void ChatOnKill::OnPlayerPVPKill(Player* killer, Player* killed)
{
    if (!killer || !killed)
    {
        return;
    }
    eventChatter.DispatchGameEvent(killer, g_EventTypeDefeatedPlayer, killed->GetName());
}

void ChatOnKill::OnPlayerCreatureKilledByPet(Player* owner, Creature* victim)
{
    if (!owner || !victim)
    {
        return;
    }
    eventChatter.DispatchGameEvent(owner, g_EventTypePetDefeated, victim->GetName());
}

ChatOnLoot::ChatOnLoot() : PlayerScript("ChatOnLoot") {}

void ChatOnLoot::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!player || !item || !item->GetTemplate())
    {
        return;
    }
    if (item->GetTemplate()->Quality >= ITEM_QUALITY_UNCOMMON)
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeGotItem, item->GetTemplate()->Name1);
    }
    
    // Guild-specific gear events
    if (player->GetGuild() && g_EnableGuildRandomChatter)
    {
        if (item->GetTemplate()->Quality == ITEM_QUALITY_EPIC && !g_GuildEventTypeEpicGear.empty())
        {
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeEpicGear, item->GetTemplate()->Name1);
        }
        else if (item->GetTemplate()->Quality == ITEM_QUALITY_RARE && !g_GuildEventTypeRareGear.empty())
        {
            // Only announce rare gear if it's equipment (not consumables, etc.)
            if (item->GetTemplate()->Class == ITEM_CLASS_WEAPON || 
                item->GetTemplate()->Class == ITEM_CLASS_ARMOR)
            {
                eventChatter.DispatchGameEvent(player, g_GuildEventTypeRareGear, item->GetTemplate()->Name1);
            }
        }
    }
}

ChatOnDeath::ChatOnDeath() : PlayerScript("ChatOnDeath") {}

void ChatOnDeath::OnPlayerJustDied(Player* player)
{
    if (!player)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeDied, "");
}

ChatOnQuest::ChatOnQuest() : PlayerScript("ChatOnQuest") {}

void ChatOnQuest::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!player || !quest)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeCompletedQuest, quest->GetTitle());
    
    // Guild-specific dungeon completion events
    if (player->GetGuild() && g_EnableGuildRandomChatter && !g_GuildEventTypeDungeonComplete.empty())
    {
        // Check if this is a dungeon quest
        if (player->GetMap() && player->GetMap()->IsDungeon())
        {
            std::string dungeonInfo = SafeFormat("{} in {}", quest->GetTitle(), player->GetMap()->GetMapName());
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeDungeonComplete, dungeonInfo);
        }
    }
}

ChatOnLearn::ChatOnLearn() : PlayerScript("ChatOnLearn") {}

void ChatOnLearn::OnPlayerLearnSpell(Player* player, uint32 spellID)
{
    if (!player)
    {
        return;
    }
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
    if (spellInfo)
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeLearnedSpell, spellInfo->SpellName[0]);
    }
    else
    {
        eventChatter.DispatchGameEvent(player, g_EventTypeLearnedSpell, std::to_string(spellID));
    }
}

ChatOnDuel::ChatOnDuel() : PlayerScript("ChatOnDuel") {}

void ChatOnDuel::OnPlayerDuelRequest(Player* target, Player* challenger)
{
    if (!challenger || !target)
    {
        return;
    }
    eventChatter.DispatchGameEvent(challenger, g_EventTypeRequestedDuel, target->GetName());
}

void ChatOnDuel::OnPlayerDuelStart(Player* player1, Player* player2)
{
    if (!player1 || !player2)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player1, g_EventTypeStartedDueling, player2->GetName());
}

void ChatOnDuel::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType /*type*/)
{
    if (!winner || !loser)
    {
        return;
    }
    eventChatter.DispatchGameEvent(winner, g_EventTypeWonDuel, loser->GetName());
}

ChatOnLevelUp::ChatOnLevelUp() : PlayerScript("ChatOnLevelUp") {}

void ChatOnLevelUp::OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!player)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeLeveledUp, std::to_string(player->GetLevel()));
    
    // Guild-specific level up events
    if (player->GetGuild() && g_EnableGuildRandomChatter && !g_GuildEventTypeLevelUp.empty())
    {
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeLevelUp, std::to_string(player->GetLevel()));
    }
}

ChatOnAchievement::ChatOnAchievement() : PlayerScript("ChatOnAchievement") {}

void ChatOnAchievement::OnPlayerCompleteAchievement(Player* player, AchievementEntry const* achievement)
{
    if (!player || !achievement)
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeAchievement, achievement->name[0]);
}

ChatOnGameObjectUse::ChatOnGameObjectUse() : PlayerScript("ChatOnGameObjectUse") {}

void ChatOnGameObjectUse::OnGameObjectUse(Player* player, GameObject* go)
{
    if (!player || !go || !go->GetGOInfo())
    {
        return;
    }
    eventChatter.DispatchGameEvent(player, g_EventTypeUsedObject, go->GetGOInfo()->name);
}

ChatOnGuildMemberChange::ChatOnGuildMemberChange() : PlayerScript("ChatOnGuildMemberChange") {}

void ChatOnGuildMemberChange::OnGuildMemberJoin(Player* player, Guild* /*guild*/)
{
    if (!player || !player->GetGuild() || !g_EnableGuildRandomChatter)
        return;
        
    if (!g_GuildEventTypeGuildJoin.empty())
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildJoin, player->GetGuild()->GetName());
}

void ChatOnGuildMemberChange::OnGuildMemberLeave(Player* player, Guild* guild)
{
    if (!player || !guild || !g_EnableGuildRandomChatter)
        return;
        
    if (!g_GuildEventTypeGuildLeave.empty())
        eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildLeave, guild->GetName());
}

void ChatOnGuildMemberChange::OnGuildMemberRankChange(Player* player, Guild* /*guild*/, uint8 /*oldRank*/, uint8 newRank)
{
    if (!player || !player->GetGuild() || !g_EnableGuildRandomChatter)
        return;
        
    if (newRank < player->GetGuild()->GetMember(player->GetGUID())->GetRankId())
    {
        // Promotion
        if (!g_GuildEventTypeGuildPromotion.empty())
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildPromotion, std::to_string(newRank));
    }
    else
    {
        // Demotion
        if (!g_GuildEventTypeGuildDemotion.empty())
            eventChatter.DispatchGameEvent(player, g_GuildEventTypeGuildDemotion, std::to_string(newRank));
    }
}


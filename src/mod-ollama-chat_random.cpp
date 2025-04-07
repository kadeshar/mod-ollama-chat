#include "mod-ollama-chat_random.h"
#include "mod-ollama-chat_config.h"  // Added to declare configuration variables.
#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "fmt/core.h"
#include "mod-ollama-chat_api.h"
#include "mod-ollama-chat_personality.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Map.h"
#include "GridNotifiers.h"
#include <vector>
#include <random>
#include <thread>
#include <ctime>
#include "Item.h"
#include "Bag.h"
#include "SpellMgr.h"

OllamaBotRandomChatter::OllamaBotRandomChatter() : WorldScript("OllamaBotRandomChatter") {}

std::unordered_map<uint64_t, time_t> nextRandomChatTime;

void OllamaBotRandomChatter::OnUpdate(uint32 diff)
{
    if (!g_Enable || !g_EnableRandomChatter)
        return;

    static uint32_t timer = 0;
    if (timer <= diff)
    {
        timer = 30000; // ~30-second check
        HandleRandomChatter();
    }
    else
    {
        timer -= diff;
    }
}

void OllamaBotRandomChatter::HandleRandomChatter()
{
    auto const& allPlayers = ObjectAccessor::GetPlayers();
    for (auto const& itr : allPlayers)
    {
        Player* bot = itr.second;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai)
            continue; // not a bot

        if (!bot->IsInWorld() || bot->IsBeingTeleported())
            continue;

        uint64_t guid = bot->GetGUID().GetRawValue();
        time_t now  = time(nullptr);

        // If not set, set random next time
        if (nextRandomChatTime.find(guid) == nextRandomChatTime.end())
        {
            nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
            continue;
        }

        // Check if time is up
        if (now < nextRandomChatTime[guid])
            continue;

        // Only talk if a real (non-bot) player is within g_RandomChatterRealPlayerDistance.
        bool realPlayerNearby = false;
        for (auto const& itr : ObjectAccessor::GetPlayers())
        {
            Player* candidate = itr.second;
            if (candidate == bot)
                continue;
            if (!candidate->IsInWorld())
                continue;
            if (bot->GetDistance(candidate) > g_RandomChatterRealPlayerDistance)
                continue;
            // If candidate is not a bot, then it is a real player.
            if (!sPlayerbotsMgr->GetPlayerbotAI(candidate))
            {
                realPlayerNearby = true;
                break;
            }
        }

        if (!realPlayerNearby)
        {
            // no real player => skip
            nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
            continue;
        }

        // Random chance to comment for the bot.
        if(urand(0, 99) > g_RandomChatterBotCommentChance)
        {
            continue;
        }

        // If there's a real player, do environment-based chatter
        std::string environmentInfo;
        std::vector<std::string> candidateComments;

        // Check for nearby creature within g_SayDistance
        {
            Unit* unitInRange = nullptr;
            Acore::AnyUnitInObjectRangeCheck creatureCheck(bot, g_SayDistance);
            Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> creatureSearcher(bot, unitInRange, creatureCheck);
            Cell::VisitGridObjects(bot, creatureSearcher, g_SayDistance);
            if (unitInRange && unitInRange->GetTypeId() == TYPEID_UNIT)
                candidateComments.push_back(fmt::format("You spot a creature named '{}'", unitInRange->ToCreature()->GetName()));
        }

        // Check for nearby game object within g_SayDistance
        {
            Acore::GameObjectInRangeCheck goCheck(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), g_SayDistance);
            GameObject* goInRange = nullptr;
            Acore::GameObjectSearcher<Acore::GameObjectInRangeCheck> goSearcher(bot, goInRange, goCheck);
            Cell::VisitGridObjects(bot, goSearcher, g_SayDistance);
            if (goInRange)
                candidateComments.push_back(fmt::format("You see a {} nearby", goInRange->GetName()));
        }

        // Check for a random equipped item
        {
            std::vector<Item*> equippedItems;
            for (uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (Item* item = bot->GetItemByPos(slot))
                    equippedItems.push_back(item);
            }
            if (!equippedItems.empty())
            {
                Item* randomEquipped = equippedItems[urand(0, equippedItems.size() - 1)];
                candidateComments.push_back(fmt::format("You glance at your {}.", randomEquipped->GetTemplate()->Name1));
            }
        }

        // Check for a random bag item (iterating over bag slots 0 to 4)
        {
            std::vector<Item*> bagItems;
            for (uint32_t bagSlot = 0; bagSlot < 5; ++bagSlot)
            {
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                {
                    for (uint32_t i = 0; i < bag->GetBagSize(); ++i)
                    {
                        if (Item* bagItem = bag->GetItemByPos(i))
                            bagItems.push_back(bagItem);
                    }
                }
            }
            if (!bagItems.empty())
            {
                Item* randomBagItem = bagItems[urand(0, bagItems.size() - 1)];
                candidateComments.push_back(fmt::format("You notice a {} in your bag.", randomBagItem->GetCount(), ai->GetChatHelper()->FormatItem(randomBagItem->GetTemplate(), randomBagItem->GetCount())));
                candidateComments.push_back(fmt::format("You are trying persuasively to sell {} of this item {}.", randomBagItem->GetCount(), ai->GetChatHelper()->FormatItem(randomBagItem->GetTemplate(), randomBagItem->GetCount())));
            }
        }

        // Check for a random known spell
        {
            std::vector<uint32_t> spellIds;
            for (auto itr : bot->GetSpellMap())
                spellIds.push_back(itr.first);
            if (!spellIds.empty())
            {
                uint32_t randomSpellId = spellIds[urand(0, spellIds.size() - 1)];
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(randomSpellId);
                if (spellInfo)
                    candidateComments.push_back(fmt::format("You recall your spell '{}'.", spellInfo->SpellName[0]));
            }
        }

        if (!candidateComments.empty())
        {
            uint32_t index = urand(0, candidateComments.size() - 1);
            environmentInfo = candidateComments[index];
        }
        else
        {
            environmentInfo = "Nothing special stands out nearby...";
        }

        // Build a short prompt
        auto prompt = [bot, &environmentInfo]()
        {
            BotPersonalityType personality = GetBotPersonality(bot);
            std::string personalityPrompt = GetPersonalityPromptAddition(personality);
            std::string botName   = bot->GetName();
            uint32_t botLevel   = bot->GetLevel();
            PlayerbotAI* botAI  = sPlayerbotsMgr->GetPlayerbotAI(bot);
            if (!botAI)
                return std::string("Error, no bot AI");
            std::string botClass  = botAI->GetChatHelper()->FormatClass(bot->getClass());
            return fmt::format(
                "You are a WoW player named {} (level {} {}). Personality='{}'. "
                "Looking around, {}. Comment in character (under 15 words).",
                botName, botLevel, botClass,
                personalityPrompt,
                environmentInfo
            );
        }();

        LOG_INFO("server.loading", "Random Message Prompt: {} ", prompt);

        uint64_t botGuid = bot->GetGUID().GetRawValue();

        // spawn a thread so we don't block
        std::thread([botGuid, prompt]()
        {
            Player* botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;
            std::string response = QueryOllamaAPI(prompt);
            if (response.empty()) return;
            botPtr = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
            if (!botPtr) return;
            PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(botPtr);
            if (!botAI) return;
            if (botPtr->GetGroup())
            {
                botAI->SayToParty(response);
            }
            else
            {
                std::vector<std::string> channels = {"General", "Say"};
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> dist(0, channels.size() - 1);
                std::string selectedChannel = channels[dist(gen)];
                if (selectedChannel == "Say")
                {
                    LOG_INFO("server.loading", "Bot Random Chatter Say: {}", response);
                    botAI->Say(response);
                }
                else if (selectedChannel == "General")
                {
                    LOG_INFO("server.loading", "Bot Random Chatter General: {}", response);
                    botAI->SayToChannel(response, ChatChannelId::GENERAL);
                }
            }
        }).detach();

        // schedule next random chatter time
        nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
    }
}

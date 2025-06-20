#include "mod-ollama-chat_random.h"
#include "mod-ollama-chat_config.h"
#include "mod-ollama-chat_handler.h"
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
#include "AiFactory.h"
#include "ObjectMgr.h"
#include "QuestDef.h"


OllamaBotRandomChatter::OllamaBotRandomChatter() : WorldScript("OllamaBotRandomChatter") {}

std::unordered_map<uint64_t, time_t> nextRandomChatTime;

void OllamaBotRandomChatter::OnUpdate(uint32 diff)
{
    if (!g_Enable || !g_EnableRandomChatter)
        return;

    if (g_ConversationHistorySaveInterval > 0)
    {
        time_t now = time(nullptr);
        if (difftime(now, g_LastHistorySaveTime) >= g_ConversationHistorySaveInterval * 60)
        {
            SaveBotConversationHistoryToDB();
            g_LastHistorySaveTime = now;
        }
    }

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
                if (!g_EnvCommentCreature.empty()) {
                    std::string templ = g_EnvCommentCreature[urand(0, g_EnvCommentCreature.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, unitInRange->ToCreature()->GetName()));
                }
        }

        // Check for nearby game object within g_SayDistance
        {
            Acore::GameObjectInRangeCheck goCheck(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), g_SayDistance);
            GameObject* goInRange = nullptr;
            Acore::GameObjectSearcher<Acore::GameObjectInRangeCheck> goSearcher(bot, goInRange, goCheck);
            Cell::VisitGridObjects(bot, goSearcher, g_SayDistance);
            if (goInRange)
                if (!g_EnvCommentGameObject.empty()) {
                    std::string templ = g_EnvCommentGameObject[urand(0, g_EnvCommentGameObject.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, goInRange->GetName()));
                }

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
                if (!g_EnvCommentEquippedItem.empty()) {
                    std::string templ = g_EnvCommentEquippedItem[urand(0, g_EnvCommentEquippedItem.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, randomEquipped->GetTemplate()->Name1));
                }

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

                if (!g_EnvCommentBagItem.empty()) {
                    std::string templ = g_EnvCommentBagItem[urand(0, g_EnvCommentBagItem.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, randomBagItem->GetCount(), ai->GetChatHelper()->FormatItem(randomBagItem->GetTemplate(), randomBagItem->GetCount())));
                }
                if (!g_EnvCommentBagItemSell.empty()) {
                    std::string templ = g_EnvCommentBagItemSell[urand(0, g_EnvCommentBagItemSell.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, randomBagItem->GetCount(), ai->GetChatHelper()->FormatItem(randomBagItem->GetTemplate(), randomBagItem->GetCount())));
                }

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
                    if (!g_EnvCommentSpell.empty()) {
                        std::string templ = g_EnvCommentSpell[urand(0, g_EnvCommentSpell.size() - 1)];
                        candidateComments.push_back(fmt::format(templ, spellInfo->SpellName[0]));
                    }

            }
        }

        // Check for an area to quest in.
        {
            std::vector<std::string> questAreas;
            for (auto const& qkv : sObjectMgr->GetQuestTemplates())
            {
                Quest const* qt = qkv.second;
                if (!qt) continue;
        
                int32 qlevel = qt->GetQuestLevel();
                int32 plevel = bot->GetLevel();
                if (qlevel < plevel - 2 || qlevel > plevel + 2)
                    continue;
        
                uint32 zone = qt->GetZoneOrSort();
                if (zone == 0) continue;
                if (auto const* area = sAreaTableStore.LookupEntry(zone))
                {
                    if (!g_EnvCommentQuestArea.empty()) {
                        std::string templ = g_EnvCommentQuestArea[urand(0, g_EnvCommentQuestArea.size() - 1)];
                        questAreas.push_back(fmt::format(templ, area->area_name[LocaleConstant::LOCALE_enUS]));
                    }
                }
            }
            if (!questAreas.empty())
                candidateComments.push_back(
                    questAreas[urand(0, questAreas.size() - 1)]
                );
        }

        // Check for Vendor nearby
        {
            Unit* unit = nullptr;
            Acore::AnyUnitInObjectRangeCheck check(bot, g_SayDistance);
            Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, unit, check);
            Cell::VisitGridObjects(bot, searcher, g_SayDistance);

            if (unit && unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* vendor = unit->ToCreature();
                if (vendor->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
                {
                    if (!g_EnvCommentVendor.empty()) {
                        std::string templ = g_EnvCommentVendor[urand(0, g_EnvCommentVendor.size() - 1)];
                        candidateComments.push_back(fmt::format(templ, vendor->GetName()));
                    }
                }
            }
        }


        // Check for Questgiver nearby
        {
            Unit* unit = nullptr;
            Acore::AnyUnitInObjectRangeCheck check(bot, g_SayDistance);
            Acore::UnitSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, unit, check);
            Cell::VisitGridObjects(bot, searcher, g_SayDistance);

            if (unit && unit->GetTypeId() == TYPEID_UNIT)
            {
                Creature* giver = unit->ToCreature();
                if (giver->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                {
                    auto bounds = sObjectMgr->GetCreatureQuestRelationBounds(giver->GetEntry());
                    int n       = std::distance(bounds.first, bounds.second);
                    if (!g_EnvCommentQuestgiver.empty()) {
                        std::string templ = g_EnvCommentQuestgiver[urand(0, g_EnvCommentQuestgiver.size() - 1)];
                        candidateComments.push_back(fmt::format(templ, giver->GetName(), n));
                    }
                }
            }
        }

        // Check for Free bag slots (manual count)
        {
            int freeSlots = 0;
            // backpack slots 0..18
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                if (!bot->GetItemByPos(i))
                    ++freeSlots;
            // each equipped bag (slots 19..22) has its own GetFreeSlots()
            for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
                if (Bag* bag = bot->GetBagByPos(b))
                    freeSlots += bag->GetFreeSlots();

            if (!g_EnvCommentBagSlots.empty()) {
                std::string templ = g_EnvCommentBagSlots[urand(0, g_EnvCommentBagSlots.size() - 1)];
                candidateComments.push_back(fmt::format(templ, freeSlots));
            }

        }

        // Check for Dungeon context
        {
            if (bot->GetMap() && bot->GetMap()->IsDungeon())
            {
                std::string name = bot->GetMap()->GetMapName();
                if (!g_EnvCommentDungeon.empty()) {
                    std::string templ = g_EnvCommentDungeon[urand(0, g_EnvCommentDungeon.size() - 1)];
                    candidateComments.push_back(fmt::format(templ, name));
                }
            }
        }

        // Check for Random incomplete quest in log
        {
            std::vector<std::string> unfinished;
            for (auto const& qs : bot->getQuestStatusMap())
            {
                if (qs.second.Status == QUEST_STATUS_INCOMPLETE)
                {
                    if (auto* qt = sObjectMgr->GetQuestTemplate(qs.first))
                        if (!g_EnvCommentUnfinishedQuest.empty()) {
                            std::string templ = g_EnvCommentUnfinishedQuest[urand(0, g_EnvCommentUnfinishedQuest.size() - 1)];
                            unfinished.push_back(fmt::format(templ, qt->GetTitle()));
                        }
                }
            }
            if (!unfinished.empty())
                candidateComments.push_back(
                    unfinished[urand(0, unfinished.size() - 1)]
                );
        }

        if (!candidateComments.empty())
        {
            uint32_t index = urand(0, candidateComments.size() - 1);
            environmentInfo = candidateComments[index];
        }
        else
        {
            environmentInfo = "";
        }

        // Build a rich context prompt
        auto prompt = [bot, &environmentInfo]()
        {
            PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
            if (!botAI)
                return std::string("Error, no bot AI");

            std::string personality         = GetBotPersonality(bot);
            std::string personalityPrompt   = GetPersonalityPromptAddition(personality);
            std::string botName             = bot->GetName();
            uint32_t botLevel               = bot->GetLevel();
            std::string botClass            = botAI->GetChatHelper()->FormatClass(bot->getClass());
            std::string botRace             = botAI->GetChatHelper()->FormatRace(bot->getRace());
            std::string botRole             = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            std::string botGender           = (bot->getGender() == 0 ? "Male" : "Female");
            std::string botFaction          = (bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");

            AreaTableEntry const* botCurrentArea = botAI->GetCurrentArea();
            AreaTableEntry const* botCurrentZone = botAI->GetCurrentZone();
            std::string botAreaName = botCurrentArea ? botAI->GetLocalizedAreaName(botCurrentArea) : "UnknownArea";
            std::string botZoneName = botCurrentZone ? botAI->GetLocalizedAreaName(botCurrentZone) : "UnknownZone";
            std::string botMapName  = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";

            return fmt::format(
                g_RandomChatterPromptTemplate,
                botName, botLevel, botClass, botRace, botGender, botRole, botFaction,
                botAreaName, botZoneName, botMapName,
                personalityPrompt,
                environmentInfo
            );
        }();


        if(g_DebugEnabled)
        {
            LOG_INFO("server.loading", "Random Message Prompt: {} ", prompt);
        }

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
                    if(g_DebugEnabled)
                    {
                        LOG_INFO("server.loading", "Bot Random Chatter Say: {}", response);
                    }
                    botAI->Say(response);
                }
                else if (selectedChannel == "General")
                {
                    if(g_DebugEnabled)
                    {
                        LOG_INFO("server.loading", "Bot Random Chatter General: {}", response);
                    }
                    botAI->SayToChannel(response, ChatChannelId::GENERAL);
                }
            }
        }).detach();

        // schedule next random chatter time
        nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
    }
}

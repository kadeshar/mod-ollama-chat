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

    // Find all real players
    std::vector<Player*> realPlayers;
    for (auto const& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player->IsInWorld()) continue;
        if (!sPlayerbotsMgr->GetPlayerbotAI(player))
            realPlayers.push_back(player);
    }

    std::unordered_set<uint64_t> processedBotsThisTick;

    // For each real player, process up to N bots in range
    for (Player* realPlayer : realPlayers)
    {
        // Gather all bots within range of this real player
        std::vector<Player*> botsInRange;
        for (auto const& itr : allPlayers)
        {
            Player* bot = itr.second;
            PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
            if (!ai) continue;
            if (!bot->IsInWorld() || bot->IsBeingTeleported()) continue;
            if (bot->GetDistance(realPlayer) > g_RandomChatterRealPlayerDistance) continue;
            if (processedBotsThisTick.count(bot->GetGUID().GetRawValue())) continue; // No double-processing
            botsInRange.push_back(bot);
        }

        std::shuffle(botsInRange.begin(), botsInRange.end(), std::mt19937(std::random_device()()));

        uint32_t botsToProcess = std::min((uint32_t)botsInRange.size(), g_RandomChatterMaxBotsPerPlayer);
        for (uint32_t i = 0; i < botsToProcess; ++i)
        {
            Player* bot = botsInRange[i];
            PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
            uint64_t guid = bot->GetGUID().GetRawValue();

            processedBotsThisTick.insert(guid);

            time_t now = time(nullptr);

            if (nextRandomChatTime.find(guid) == nextRandomChatTime.end())
            {
                nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
                continue;
            }

            if (now < nextRandomChatTime[guid])
                continue;

            if(urand(0, 99) > g_RandomChatterBotCommentChance)
                continue;

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
                // Build a vector of valid "active" spells for this bot.
                struct NamedSpell
                {
                    uint32 id;
                    std::string name;
                    std::string effect;
                    std::string cost;
                };
                std::vector<NamedSpell> validSpells;
                for (const auto& spellPair : bot->GetSpellMap())
                {
                    uint32 spellId = spellPair.first;
                    const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                    if (!spellInfo) continue;
                    if (spellInfo->Attributes & SPELL_ATTR0_PASSIVE)
                        continue;
                    if (spellInfo->SpellFamilyName == SPELLFAMILY_GENERIC)
                        continue;
                    if (bot->HasSpellCooldown(spellId))
                        continue;

                    std::string effectText;
                    for (int i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    {
                        if (!spellInfo->Effects[i].IsEffect())
                            continue;
                        switch (spellInfo->Effects[i].Effect)
                        {
                            case SPELL_EFFECT_SCHOOL_DAMAGE: effectText = "Deals damage"; break;
                            case SPELL_EFFECT_HEAL: effectText = "Heals the target"; break;
                            case SPELL_EFFECT_APPLY_AURA: effectText = "Applies an effect"; break;
                            case SPELL_EFFECT_DISPEL: effectText = "Dispels magic"; break;
                            case SPELL_EFFECT_THREAT: effectText = "Generates threat"; break;
                            default: continue;
                        }
                        if (!effectText.empty())
                            break;
                    }
                    if (effectText.empty())
                        continue;

                    const char* name = spellInfo->SpellName[0];
                    if (!name || !*name)
                        continue;

                    std::string costText;
                    if (spellInfo->ManaCost || spellInfo->ManaCostPercentage)
                    {
                        switch (spellInfo->PowerType)
                        {
                            case POWER_MANA: costText = std::to_string(spellInfo->ManaCost) + " mana"; break;
                            case POWER_RAGE: costText = std::to_string(spellInfo->ManaCost) + " rage"; break;
                            case POWER_FOCUS: costText = std::to_string(spellInfo->ManaCost) + " focus"; break;
                            case POWER_ENERGY: costText = std::to_string(spellInfo->ManaCost) + " energy"; break;
                            case POWER_RUNIC_POWER: costText = std::to_string(spellInfo->ManaCost) + " runic power"; break;
                            default: costText = std::to_string(spellInfo->ManaCost) + " unknown resource"; break;
                        }
                    }
                    else
                    {
                        costText = "no cost";
                    }

                    validSpells.push_back({spellId, name, effectText, costText});
                }

                if (!validSpells.empty())
                {
                    const NamedSpell& randomSpell = validSpells[urand(0, validSpells.size() - 1)];
                    if (!g_EnvCommentSpell.empty())
                    {
                        std::string templ = g_EnvCommentSpell[urand(0, g_EnvCommentSpell.size() - 1)];
                        // Format: spell name, effect, cost
                        candidateComments.push_back(fmt::format(
                            templ, 
                            randomSpell.name, 
                            randomSpell.effect, 
                            randomSpell.cost 
                        ));
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
                    candidateComments.push_back(questAreas[urand(0, questAreas.size() - 1)]);
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
                for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
                    if (!bot->GetItemByPos(i))
                        ++freeSlots;
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
                    candidateComments.push_back(unfinished[urand(0, unfinished.size() - 1)]);
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

            nextRandomChatTime[guid] = now + urand(g_MinRandomInterval, g_MaxRandomInterval);
        }
    }
}

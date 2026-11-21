/*
 * mod-temucraft-legacy: the Legacy system.
 *
 * Legacy points are earned once per account, 1 per challenge:
 *   - each class reaching level 30, 60 and 80 (death knights: 60, 70 and 80)
 *   - each crafting profession reaching 150, 300 and 450 skill (Alchemy, Blacksmithing, Enchanting, Engineering,
 *     Jewelcrafting, Inscription, Leatherworking, Tailoring); unlearning it later keeps the point
 *   - PvP: 100, 1000 and 10000 honorable kills
 *   - achievements: exploring each continent, the dungeonmaster and raider achievements, Molten Core, Blackwing Lair,
 *     Onyxia's Lair and the Black Temple
 * The rewards track (.legacy, "Legacy rewards") gives each account an item at 15, 25, 40 and 55 points earned.
 * Every character of the account can spend the account's points on perks, each character its own way, up to
 * Temucraft.Legacy.MaxSpent points. .legacy opens the menu: perks, challenges done, and a free reset.
 *
 * Perks (5 ranks each):
 *   Well Rested         +2% experience per rank
 *   Working Overtime    +6% chance per rank of an extra skill point when crafting
 *   Bountiful Harvest   +6% chance per rank of an extra skill point when gathering
 *   Reinforce           repairs cost 4% less per rank
 *   Diplomat            +2% reputation per rank
 *   Field Guide         +2% quest experience per rank
 *   Thrill of Adventure +3% money looted per rank
 *   Swift Flights       flight paths 5% faster per rank (with mod-temucraft-travel)
 */

#include "Chat.h"
#include "TemucraftModuleText.h"
#include "TemucraftFlightBonus.h"
#include "ObjectMgr.h"
#include "LootMgr.h"
#include "Item.h"
#include "AchievementMgr.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "World.h"
#include "WorldSession.h"

#include <array>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr uint32 LEGACY_MENU = 601630;
    enum Sender : uint32
    {
        SENDER_MAIN = 1,
        SENDER_PERK = 2,
        SENDER_ADD = 3,
        SENDER_CHALLENGES = 4,
        SENDER_RESET = 5,
        SENDER_INFO = 6,
        SENDER_REWARDS = 7,
        SENDER_CLAIM = 8,
    };

    enum Perk : uint8
    {
        PERK_WELL_RESTED,
        PERK_WORKING_OVERTIME,
        PERK_BOUNTIFUL_HARVEST,
        PERK_REINFORCE,
        PERK_DIPLOMAT,
        PERK_FIELD_GUIDE,
        PERK_THRILL,
        PERK_SWIFT_FLIGHTS,
        PERK_COUNT
    };

    struct PerkInfo
    {
        char const* key;    // Temucraft.Legacy.<key>.MaxRank / .PerRank
        char const* name;
        char const* effect;
        uint8 maxRank;
        float perRank;
    };

    std::array<PerkInfo, PERK_COUNT> PERKS = { {
        { "WellRested", "Well Rested",       "+{}% experience",                       5, 2.0f },
        { "WorkingOvertime", "Working Overtime",  "{}% chance of an extra crafting skill point", 5, 6.0f },
        { "BountifulHarvest", "Bountiful Harvest", "{}% chance of an extra gathering skill point", 5, 6.0f },
        { "Reinforce", "Reinforce",         "repairs cost {}% less",                  5, 4.0f },
        { "Diplomat", "Diplomat",          "+{}% reputation",                        5, 2.0f },
        { "FieldGuide", "Field Guide",       "+{}% quest experience",                  5, 2.0f },
        { "ThrillOfAdventure", "Thrill of Adventure", "+{}% money looted",                    5, 3.0f },
        { "SwiftFlights", "Swift Flights",     "flight paths {}% faster",                5, 5.0f },
    } };

    // Achievement challenges: id 20000 + achievement. Honorable kills: 30000 + step.
    constexpr std::array<uint32, 13> ACHIEVEMENTS = { 42, 43, 44, 45, 1283, 1284, 1288, 1285, 1286, 686, 685, 4396, 697 };
    constexpr std::array<uint32, 3> HONOR_KILLS = { 100, 1000, 10000 };

    // The rewards track: points earned -> item (per account, once)
    struct Reward
    {
        uint32 points;
        uint32 item;
    };
    std::vector<Reward> REWARDS = { { 15, 46725 }, { 25, 49343 }, { 40, 40643 }, { 55, 49283 } }; // Temucraft.Legacy.Rewards

    struct Challenge
    {
        uint32 id;
        std::string name;
    };

    // Challenge ids: class * 100 + step (1-3); 10000 + skill * 10 + step (1-3)
    constexpr std::array<uint32, 8> PROFESSIONS = { 171, 164, 333, 202, 755, 773, 165, 197 };

    uint32 ClassChallenge(uint8 cls, uint8 step) { return cls * 100 + step; }
    uint32 ProfessionChallenge(uint32 skill, uint8 step) { return 10000 + skill * 10 + step; }

    std::array<uint8, 3> ClassLevels(uint8 cls)
    {
        return cls == CLASS_DEATH_KNIGHT ? std::array<uint8, 3>{ 60, 70, 80 } : std::array<uint8, 3>{ 30, 60, 80 };
    }
    constexpr std::array<uint32, 3> SKILL_STEPS = { 150, 300, 450 };
    constexpr std::array<char const*, 3> STEP_NAMES = { "Novice", "Experienced", "Master" };

    std::string ChallengeName(uint32 id)
    {
        if (id >= 30000)
            return Acore::StringFormat("{} honorable kills", HONOR_KILLS[std::min<uint32>(id - 30001, 2)]);
        if (id >= 20000)
        {
            AchievementEntry const* achievement = sAchievementStore.LookupEntry(id - 20000);
            return achievement ? std::string(achievement->name[0]) : Acore::StringFormat("achievement {}", id - 20000);
        }
        if (id >= 10000)
        {
            uint32 skill = (id - 10000) / 10;
            uint8 step = (id - 10000) % 10;
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
            return Acore::StringFormat("{} {} ({})", STEP_NAMES[step - 1], line ? line->name[0] : "?", SKILL_STEPS[step - 1]);
        }
        uint8 cls = id / 100, step = id % 100;
        ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(cls);
        return Acore::StringFormat("{} {} (level {})", STEP_NAMES[step - 1], entry ? entry->name[0] : "?", ClassLevels(cls)[step - 1]);
    }

    struct Settings
    {
        bool enable = true;
        uint32 maxSpent = 16;
    } legacy;

    struct Account
    {
        std::set<uint32> challenges;
    };

    std::mutex sLock;
    std::unordered_map<uint32, Account> sAccounts;                          // account -> challenges done
    std::unordered_map<ObjectGuid::LowType, std::array<uint8, PERK_COUNT>> sPerks; // character -> ranks

    uint32 Earned(Player* player)
    {
        std::lock_guard lock(sLock);
        auto itr = sAccounts.find(player->GetSession()->GetAccountId());
        return itr == sAccounts.end() ? 0 : uint32(itr->second.challenges.size());
    }

    std::array<uint8, PERK_COUNT> Ranks(Player* player)
    {
        std::lock_guard lock(sLock);
        auto itr = sPerks.find(player->GetGUID().GetCounter());
        return itr == sPerks.end() ? std::array<uint8, PERK_COUNT>{} : itr->second;
    }

    uint32 Spent(std::array<uint8, PERK_COUNT> const& ranks)
    {
        uint32 total = 0;
        for (uint8 rank : ranks)
            total += rank;
        return total;
    }

    float PerkValue(Player* player, Perk perk)
    {
        if (!legacy.enable)
            return 0.0f;
        return Ranks(player)[perk] * PERKS[perk].perRank;
    }

    void Load(Player* player)
    {
        uint32 account = player->GetSession()->GetAccountId();
        Account acc;
        if (QueryResult result = CharacterDatabase.Query("SELECT `challenge` FROM `mod_temucraft_legacy_challenge` WHERE `account` = {}", account))
            do acc.challenges.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        std::array<uint8, PERK_COUNT> ranks{};
        if (QueryResult result = CharacterDatabase.Query("SELECT `perk`, `rank` FROM `mod_temucraft_legacy_perk` WHERE `guid` = {}", player->GetGUID().GetCounter()))
            do
            {
                uint8 perk = result->Fetch()[0].Get<uint8>();
                if (perk < PERK_COUNT)
                    ranks[perk] = std::min(result->Fetch()[1].Get<uint8>(), PERKS[perk].maxRank);
            } while (result->NextRow());
        std::lock_guard lock(sLock);
        sAccounts[account] = std::move(acc);
        sPerks[player->GetGUID().GetCounter()] = ranks;
        TemucraftSetFlightBonus(player, "legacy", ranks[PERK_SWIFT_FLIGHTS] * PERKS[PERK_SWIFT_FLIGHTS].perRank);
    }

    void Complete(Player* player, uint32 challenge)
    {
        uint32 account = player->GetSession()->GetAccountId();
        {
            std::lock_guard lock(sLock);
            if (!sAccounts[account].challenges.insert(challenge).second)
                return;
        }
        CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_legacy_challenge` (`account`, `challenge`, `time`) VALUES ({}, {}, UNIX_TIMESTAMP())",
            account, challenge);
        ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-legacy", player->GetSession(), 1, "|cffff8000Legacy challenge complete:|r {}. |cffff8000+1 Legacy point|r "
            "(every character on your account can spend it; .legacy)."), ChallengeName(challenge));
    }

    void CheckHonorKills(Player* player)
    {
        uint32 kills = player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS);
        for (uint8 step = 0; step < HONOR_KILLS.size(); ++step)
            if (kills >= HONOR_KILLS[step])
                Complete(player, 30001 + step);
    }

    void CheckChallenges(Player* player)
    {
        if (!legacy.enable)
            return;
        auto levels = ClassLevels(player->getClass());
        for (uint8 step = 0; step < 3; ++step)
            if (player->GetLevel() >= levels[step])
                Complete(player, ClassChallenge(player->getClass(), step + 1));
        for (uint32 skill : PROFESSIONS)
        {
            uint32 value = player->GetPureSkillValue(skill);
            for (uint8 step = 0; step < 3; ++step)
                if (value >= SKILL_STEPS[step])
                    Complete(player, ProfessionChallenge(skill, step + 1));
        }
        for (uint32 achievement : ACHIEVEMENTS)
            if (player->HasAchieved(achievement))
                Complete(player, 20000 + achievement);
        CheckHonorKills(player);
    }

    void SendMain(Player* player)
    {
        auto ranks = Ranks(player);
        uint32 earned = Earned(player), spent = Spent(ranks);
        uint32 spendable = std::min(earned, legacy.maxSpent);
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 2, "|cff8a4b00Legacy points: {} earned, {} of {} spent.|r"),
            earned, spent, spendable), SENDER_INFO, 0);
        for (uint8 perk = 0; perk < PERK_COUNT; ++perk)
        {
            PerkInfo const& info = PERKS[perk];
            std::string effect = fmt::format(fmt::runtime(info.effect), ranks[perk] ? ranks[perk] * info.perRank : info.perRank);
            AddGossipItemFor(player, ranks[perk] ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_TALK,
                Acore::StringFormat("{} [{}/{}]: {}{}", info.name, ranks[perk], info.maxRank, effect, ranks[perk] ? ""  : TemucraftModuleText("mod-temucraft-legacy", player, 3, " per rank")),
                SENDER_PERK, perk);
        }
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, TemucraftModuleText("mod-temucraft-legacy", player, 4, "Challenges done"), SENDER_CHALLENGES, 0);
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, TemucraftModuleText("mod-temucraft-legacy", player, 5, "Legacy rewards"), SENDER_REWARDS, 0);
        if (spent)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_2, TemucraftModuleText("mod-temucraft-legacy", player, 6, "Take back all my points (free)"), SENDER_RESET, 0,
                TemucraftModuleText("mod-temucraft-legacy", player, 7, "Take back every Legacy point this character spent?"), 0, false);
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(LEGACY_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    void SendPerk(Player* player, uint8 perk)
    {
        auto ranks = Ranks(player);
        PerkInfo const& info = PERKS[perk];
        bool canAdd = ranks[perk] < info.maxRank && Spent(ranks) < std::min(Earned(player), legacy.maxSpent);
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 8, "|cff8a4b00{}: rank {} of {}.|r"), info.name, ranks[perk], info.maxRank),
            SENDER_INFO, 0);
        if (canAdd)
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 9, "Spend a point: {}"), fmt::format(fmt::runtime(info.effect),
                (ranks[perk] + 1) * info.perRank)), SENDER_ADD, perk);
        else
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ranks[perk] >= info.maxRank ? TemucraftModuleText("mod-temucraft-legacy", player, 10, "This perk is at its highest rank." ) :
                TemucraftModuleText("mod-temucraft-legacy", player, 11, "No Legacy points left to spend."), SENDER_INFO, 0);
        AddGossipItemFor(player, GOSSIP_ICON_TALK, TemucraftModuleText("mod-temucraft-legacy", player, 12, "Back"), SENDER_MAIN, 0);
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(LEGACY_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    void SendChallenges(Player* player)
    {
        std::set<uint32> done;
        {
            std::lock_guard lock(sLock);
            done = sAccounts[player->GetSession()->GetAccountId()].challenges;
        }
        ClearGossipMenuFor(player);
        if (done.empty())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, TemucraftModuleText("mod-temucraft-legacy", player, 13, "None yet: level a class to 30, or a profession to 150."), SENDER_INFO, 0);
        uint32 shown = 0;
        for (uint32 id : done)
        {
            if (++shown > 25)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 14, "... and {} more."), done.size() - 25), SENDER_INFO, 0);
                break;
            }
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ChallengeName(id), SENDER_INFO, 0);
        }
        AddGossipItemFor(player, GOSSIP_ICON_TALK, TemucraftModuleText("mod-temucraft-legacy", player, 12, "Back"), SENDER_MAIN, 0);
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(LEGACY_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    std::set<uint32> ClaimedRewards(uint32 account)
    {
        std::set<uint32> claimed;
        if (QueryResult result = CharacterDatabase.Query("SELECT `points` FROM `mod_temucraft_legacy_reward` WHERE `account` = {}", account))
            do claimed.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        return claimed;
    }

    void SendRewards(Player* player)
    {
        uint32 earned = Earned(player);
        std::set<uint32> claimed = ClaimedRewards(player->GetSession()->GetAccountId());
        ClearGossipMenuFor(player);
        for (Reward const& reward : REWARDS)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward.item);
            std::string name = proto ? proto->Name1 : std::to_string(reward.item);
            if (claimed.count(reward.points))
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 15, "{} points: {} (taken)"), reward.points, name), SENDER_INFO, 0);
            else if (earned >= reward.points)
                AddGossipItemFor(player, GOSSIP_ICON_VENDOR, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 16, "{} points: take {}"), reward.points, name), SENDER_CLAIM,
                    reward.points);
            else
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, Acore::StringFormat(TemucraftModuleText("mod-temucraft-legacy", player, 17, "{} points: {}"), reward.points, name), SENDER_INFO, 0);
        }
        AddGossipItemFor(player, GOSSIP_ICON_TALK, TemucraftModuleText("mod-temucraft-legacy", player, 12, "Back"), SENDER_MAIN, 0);
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(LEGACY_MENU);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
    }

    void ClaimReward(Player* player, uint32 points)
    {
        uint32 account = player->GetSession()->GetAccountId();
        for (Reward const& reward : REWARDS)
        {
            if (reward.points != points || Earned(player) < points || ClaimedRewards(account).count(points))
                continue;
            ItemPosCountVec dest;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, reward.item, 1) != EQUIP_ERR_OK)
            {
                ChatHandler(player->GetSession()).SendSysMessage(TemucraftModuleText("mod-temucraft-legacy", player->GetSession(), 18, "|cffff4c4cYour bags are full.|r"));
                return;
            }
            if (Item* item = player->StoreNewItem(dest, reward.item, true))
                player->SendNewItem(item, 1, true, false);
            CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_legacy_reward` (`account`, `points`) VALUES ({}, {})", account, points);
        }
    }

    bool Chance(float percent)
    {
        static thread_local std::mt19937 rng{ std::random_device{}() };
        return std::uniform_real_distribution<float>(0.0f, 100.0f)(rng) < percent;
    }
}

class TemucraftLegacy_Player : public PlayerScript
{
public:
    TemucraftLegacy_Player() : PlayerScript("TemucraftLegacy_Player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_UPDATE_SKILL, PLAYERHOOK_ON_GIVE_EXP, PLAYERHOOK_ON_REPUTATION_CHANGE,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL, PLAYERHOOK_ON_UPDATE_GATHERING_SKILL, PLAYERHOOK_ON_BEFORE_DURABILITY_REPAIR,
        PLAYERHOOK_ON_GOSSIP_SELECT, PLAYERHOOK_ON_ACHI_COMPLETE, PLAYERHOOK_ON_PVP_KILL, PLAYERHOOK_ON_QUEST_COMPUTE_EXP,
        PLAYERHOOK_ON_BEFORE_LOOT_MONEY }) { }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (legacy.enable && achievement && std::find(ACHIEVEMENTS.begin(), ACHIEVEMENTS.end(), achievement->ID) != ACHIEVEMENTS.end())
            Complete(player, 20000 + achievement->ID);
    }

    void OnPlayerPVPKill(Player* killer, Player* /*killed*/) override
    {
        if (legacy.enable && killer)
            CheckHonorKills(killer);
    }

    void OnPlayerQuestComputeXP(Player* player, Quest const* /*quest*/, uint32& xp) override
    {
        if (float bonus = PerkValue(player, PERK_FIELD_GUIDE))
            xp = uint32(xp * (1.0f + bonus / 100.0f));
    }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (loot && loot->gold)
            if (float bonus = PerkValue(player, PERK_THRILL))
                loot->gold = uint32(loot->gold * (1.0f + bonus / 100.0f));
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!legacy.enable)
            return;
        Load(player);
        CheckChallenges(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard lock(sLock);
        sPerks.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        CheckChallenges(player);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/, uint32 newValue) override
    {
        if (!legacy.enable || std::find(PROFESSIONS.begin(), PROFESSIONS.end(), skillId) == PROFESSIONS.end())
            return;
        for (uint8 step = 0; step < 3; ++step)
            if (newValue >= SKILL_STEPS[step])
                Complete(player, ProfessionChallenge(skillId, step + 1));
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (float bonus = PerkValue(player, PERK_WELL_RESTED))
            amount = uint32(amount * (1.0f + bonus / 100.0f));
    }

    bool OnPlayerReputationChange(Player* player, uint32 /*factionId*/, int32& standing, bool incremental) override
    {
        if (incremental && standing > 0)
            if (float bonus = PerkValue(player, PERK_DIPLOMAT))
                standing = int32(standing * (1.0f + bonus / 100.0f));
        return true;
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* /*skill*/, uint32 /*level*/, uint32& gain) override
    {
        if (gain && Chance(PerkValue(player, PERK_WORKING_OVERTIME)))
            ++gain;
    }

    void OnPlayerUpdateGatheringSkill(Player* player, uint32 /*skill*/, uint32 /*current*/, uint32 /*gray*/, uint32 /*green*/,
        uint32 /*yellow*/, uint32& gain) override
    {
        if (gain && Chance(PerkValue(player, PERK_BOUNTIFUL_HARVEST)))
            ++gain;
    }

    void OnPlayerBeforeDurabilityRepair(Player* player, ObjectGuid /*npc*/, ObjectGuid /*item*/, float& discountMod, uint8 /*guildBank*/) override
    {
        if (float discount = PerkValue(player, PERK_REINFORCE))
            discountMod *= 1.0f - discount / 100.0f;
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 sender, uint32 action) override
    {
        if (menuId != LEGACY_MENU || !legacy.enable)
            return;
        switch (sender)
        {
            case SENDER_PERK:
                if (action < PERK_COUNT)
                    SendPerk(player, uint8(action));
                return;
            case SENDER_ADD:
            {
                if (action >= PERK_COUNT)
                    return;
                auto ranks = Ranks(player);
                if (ranks[action] < PERKS[action].maxRank && Spent(ranks) < std::min(Earned(player), legacy.maxSpent))
                {
                    uint8 rank = ranks[action] + 1;
                    {
                        std::lock_guard lock(sLock);
                        sPerks[player->GetGUID().GetCounter()][action] = rank;
                    }
                    CharacterDatabase.Execute("REPLACE INTO `mod_temucraft_legacy_perk` (`guid`, `perk`, `rank`) VALUES ({}, {}, {})",
                        player->GetGUID().GetCounter(), action, rank);
                    if (action == PERK_SWIFT_FLIGHTS)
                        TemucraftSetFlightBonus(player, "legacy", rank * PERKS[PERK_SWIFT_FLIGHTS].perRank);
                }
                SendPerk(player, uint8(action));
                return;
            }
            case SENDER_CHALLENGES:
                SendChallenges(player);
                return;
            case SENDER_REWARDS:
                SendRewards(player);
                return;
            case SENDER_CLAIM:
                ClaimReward(player, action);
                SendRewards(player);
                return;
            case SENDER_RESET:
            {
                {
                    std::lock_guard lock(sLock);
                    sPerks[player->GetGUID().GetCounter()] = {};
                }
                CharacterDatabase.Execute("DELETE FROM `mod_temucraft_legacy_perk` WHERE `guid` = {}", player->GetGUID().GetCounter());
                TemucraftSetFlightBonus(player, "legacy", 0.0f);
                SendMain(player);
                return;
            }
            default:
                SendMain(player);
                return;
        }
    }
};

class TemucraftLegacy_Commands : public CommandScript
{
public:
    TemucraftLegacy_Commands() : CommandScript("TemucraftLegacy_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "legacy", HandleLegacy, SEC_PLAYER, Console::No },
        };
        return commandTable;
    }

    static bool HandleLegacy(ChatHandler* handler)
    {
        if (!legacy.enable)
        {
            handler->SendSysMessage(TemucraftModuleText("mod-temucraft-legacy", handler, 19, "The Legacy system is off on this server."));
            return true;
        }
        SendMain(handler->GetSession()->GetPlayer());
        return true;
    }
};

class TemucraftLegacy_World : public WorldScript
{
public:
    TemucraftLegacy_World() : WorldScript("TemucraftLegacy_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        legacy.enable = sConfigMgr->GetOption<bool>("Temucraft.Legacy.Enable", true);
        legacy.maxSpent = sConfigMgr->GetOption<uint32>("Temucraft.Legacy.MaxSpent", 16);
        static std::array<PerkInfo, PERK_COUNT> const defaults = PERKS;
        for (uint8 i = 0; i < PERK_COUNT; ++i)
        {
            std::string const base = std::string("Temucraft.Legacy.") + defaults[i].key;
            PERKS[i].maxRank = uint8(std::min<uint32>(sConfigMgr->GetOption<uint32>(base + ".MaxRank", defaults[i].maxRank), 255));
            PERKS[i].perRank = sConfigMgr->GetOption<float>(base + ".PerRank", defaults[i].perRank);
        }
        // "points:item points:item ..."
        std::vector<Reward> rewards;
        std::string const rewardList = sConfigMgr->GetOption<std::string>("Temucraft.Legacy.Rewards", "15:46725 25:49343 40:40643 55:49283");
        for (std::string_view pair : Acore::Tokenize(rewardList, ' ', false))
        {
            std::vector<std::string_view> parts = Acore::Tokenize(pair, ':', false);
            if (parts.size() != 2)
                continue;
            Optional<uint32> points = Acore::StringTo<uint32>(parts[0]);
            Optional<uint32> item = Acore::StringTo<uint32>(parts[1]);
            if (points && item)
                rewards.push_back({ *points, *item });
        }
        REWARDS = rewards;
    }
};

void AddTemucraftLegacyScripts()
{
    new TemucraftLegacy_Player();
    new TemucraftLegacy_Commands();
    new TemucraftLegacy_World();
}

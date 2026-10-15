#include "content.hpp"

#include <algorithm>
#include <iterator>

// ----- Events -----
const EventInfo EVENTS[] = {
    {"None",            "",                                  0},
    {"Meteor Shower",   "2x mining yield (you and the bot)", 30},
    {"Drone Overclock", "3x passive income",                 45},
    {"Gold Rush",       "2x bonus/jackpot odds",             20},
    {"Payday Rush",     "your crew works twice as hard, but wants paying when it ends", 60},
};
static_assert(std::size(EVENTS) == EV_COUNT);

EventFx eventFx(int ev) {
    EventFx fx;
    switch (ev) {
        case EV_METEOR:    fx.mining  = 2.0; break;
        case EV_OVERCLOCK: fx.passive = 3.0; break;
        case EV_GOLDRUSH:  fx.odds    = 2.0; break;
        case EV_PAYDAY:    fx.crew    = 2.0; break;
        default:           break; // EV_NONE (and anything corrupt) has no effect
    }
    return fx;
}

// ----- Curios -----
const char* const CURIO_SETS[] = {"Desk Toys", "Relics", "Impossible Things", "Under the Counter"};
static_assert(std::size(CURIO_SETS) == CURIO_SET_COUNT);

const CurioInfo CURIOS[] = {
    // Desk Toys: the early game
    {"Rubber Duck",              0,           1'500, "Listens to your bugs without judgment."},
    {"Newton's Cradle",          0,           4'000, "Click. Click. Click. Click."},
    {"Lava Lamp",                0,          10'000, "The blobs never meet. Neither do your deadlines."},
    {"Tiny Cactus",              0,          25'000, "Low maintenance. Unlike you."},
    // Relics: around the Market and Contracts
    {"Cracked Floppy Disk",      1,         150'000, "1.44 MB of someone's lost thesis."},
    {"The First Token Ever",     1,         600'000, "Slightly dented. Priceless. Well, 600,000."},
    {"Haunted Pickaxe",          1,       2'000'000, "Mines on its own at 3 AM. Do not ask."},
    {"Golden Keyboard",          1,       6'000'000, "Every keystroke sounds like a cash register."},
    // Impossible Things: the late game
    {"Bottled Lightning",        2,      25'000'000, "Shake well before not opening."},
    {"Jar of Fog from Day One",  2,      80'000'000, "Smells like the beginning."},
    {"A Spare Moon",             2,     250'000'000, "In case the first one breaks."},
    {"The Last Token",           2,     750'000'000, "There will never be another. (There will. Shh.)"},
    // Under the Counter: never in the shop (the Curio Dealer sells them at 1.5x this, or a crew expedition finds one)
    {"A Map of Nowhere",         3,       2'000'000, "Every road on it is real. None of them go anywhere."},
    {"Jar of Someone Else's Luck", 3,    20'000'000, "Slightly used. Still works. Mostly."},
    {"Receipt for a Moon",       3,     200'000'000, "Non-refundable. They checked."},
    {"A Token From Tomorrow",    3,   1'500'000'000, "It's already worth less."},
};
static_assert(std::size(CURIOS) == CURIO_COUNT);

std::string theCurio(int curio) {
    std::string name = CURIOS[curio].name;
    if (name.rfind("The ", 0) == 0) return "the " + name.substr(4);
    if (name.rfind("A ", 0) == 0) return "a " + name.substr(2);
    return "the " + name;
}

// ----- Prompt styles -----
const PromptStyle PROMPTS[] = {
    {"classic", "> ",       0},
    {"dollar",  "$ ",     500},
    {"python",  ">>> ", 1'500},
    {"tilde",   "~> ",  3'000},
    {"hash",    "[#] ", 7'500},
    {"heart",   "<3 ", 15'000},
};
static_assert(std::size(PROMPTS) == PROMPT_COUNT);

// ----- Themes -----
// Roles: Text, Muted, Accent, Good, Bad, Warn, Event, Tokens, Shards, Heat,
//        Background, Panel, Button, ButtonText, ButtonOff, ButtonOffText, Infected
const ThemeInfo THEMES[] = {
    {"classic", "Classic", 0, "The original look, in your terminal's own colors.", true,
     {{204, 204, 204}, {118, 118, 118}, {41, 184, 219}, {13, 188, 121}, {205, 49, 49}, {229, 229, 16}, {188, 63, 188},
      {255, 215, 0}, {0, 175, 255}, {255, 135, 0},
      {30, 30, 30}, {48, 48, 48}, {36, 114, 200}, {255, 255, 255}, {68, 68, 68}, {188, 188, 188}, {175, 255, 0}}},
    {"dusk", "Dusk", 0, "Lavender and apricot over a deep violet evening.", false,
     {{230, 225, 242}, {125, 119, 150}, {185, 163, 245}, {143, 214, 176}, {242, 121, 139}, {244, 194, 122}, {245, 159, 214},
      {246, 210, 122}, {140, 200, 242}, {245, 154, 106},
      {29, 27, 44}, {38, 35, 56}, {91, 79, 158}, {244, 240, 255}, {52, 48, 74}, {138, 132, 163}, {196, 232, 120}}},
    {"glacier", "Glacier", 300, "Frost blue and snow, cold and clear.", false,
     {{221, 230, 239}, {111, 127, 144}, {136, 198, 224}, {159, 211, 167}, {229, 124, 134}, {232, 201, 138}, {185, 162, 230},
      {236, 210, 139}, {127, 212, 240}, {224, 154, 115},
      {27, 34, 44}, {35, 44, 56}, {61, 106, 143}, {238, 246, 255}, {44, 54, 68}, {124, 138, 153}, {190, 230, 140}}},
    {"ember", "Ember", 750, "Forge light: amber and coal, with a cold blue spark.", false,
     {{240, 226, 214}, {140, 116, 102}, {255, 157, 92}, {182, 207, 111}, {255, 95, 86}, {255, 200, 87}, {255, 143, 163},
      {255, 207, 92}, {126, 208, 214}, {255, 122, 61},
      {30, 22, 19}, {42, 30, 25}, {156, 61, 30}, {255, 241, 230}, {58, 42, 35}, {154, 130, 115}, {200, 230, 90}}},
    {"verdigris", "Verdigris", 1500, "Old copper gone green: patina, brass and sea glass.", false,
     {{217, 238, 230}, {106, 138, 130}, {95, 209, 179}, {143, 224, 138}, {239, 122, 107}, {230, 197, 107}, {195, 155, 240},
      {232, 180, 90}, {111, 199, 232}, {232, 135, 90},
      {19, 32, 30}, {27, 43, 40}, {47, 122, 104}, {234, 255, 248}, {36, 55, 47}, {122, 153, 144}, {205, 240, 110}}},
    {"neon", "Neon", 3000, "Arcade after dark: hot pink, cyan and a lot of purple.", false,
     {{240, 230, 255}, {122, 108, 153}, {0, 229, 255}, {92, 255, 157}, {255, 79, 123}, {255, 210, 63}, {255, 92, 240},
      {255, 210, 63}, {0, 229, 255}, {255, 138, 61},
      {20, 15, 36}, {29, 22, 51}, {122, 44, 255}, {255, 255, 255}, {42, 33, 64}, {133, 121, 158}, {190, 255, 60}}},
};
static_assert(std::size(THEMES) == THEME_COUNT);

// ----- The Monolith -----
const MonoStage MONO[] = {
    {"Foundation",   5'000, {"    [#################]", nullptr}},
    {"Pillars",     20'000, {"        ___|###|___",     "       |###########|"}},
    {"Spire",       60'000, {"           |###|",        "           |###|"}},
    {"Crown",      150'000, {"           /###\\",       nullptr}},
    {"Beacon",     400'000, {"             *",          "            /|\\"}},
};
static_assert(std::size(MONO) == MONO_STAGE_COUNT);

// ----- The Orrery -----
const OrreryStage ORRERY[] = {
    {"Base Plate",    2'000, "a 5th crew slot",                                             11},
    {"First Ring",    6'000, "Resonance: Drones and the Mining Bot scale with your mining",  2},
    {"Second Ring",  15'000, "a second contract slot",                                       0},
    {"Gearwork",     35'000, "crew can train up to level 8",                                 9},
    {"The Sun",      80'000, "the light at the center (and the last goal)",                  4},
};
static_assert(std::size(ORRERY) == ORRERY_STAGE_COUNT);

// 1 Base Plate, 2 First Ring (inner orbit), 3 Second Ring (outer orbit), 4 Gearwork, 5 The Sun
const ArtRow ORRERY_ART[] = {
    {"      ·       _.-''''●''''-._       *",
     "      5       333333333333333       5"},
    {"  *       .-''               ''-.       ·",
     "  5       3333               3333       5"},
    {"        .'     _.-'''''''-._     '.   ·",
     "        33     2222222222222     33   5"},
    {"  ·    /     .'    \\ | /    '.     \\",
     "  5    3     22    5 5 5    22     3"},
    {"      ●     :    -- (O) --    o     :",
     "      3     2    55 555 55    2     3"},
    {"       \\     '.    / | \\    .'     /",
     "       3     22    5 5 5    22     3"},
    {"        '.     '-.._____..-'     .'",
     "        33     2222222222222     33"},
    {"          '-..       ║       ..-'",
     "          3333       4       3333"},
    {"              '-.____║____.-'",
     "              333333343333333"},
    {"         {@}======={@@@}======={@}",
     "         4444444444444444444444444"},
    {"    ·         ___/       \\___         *",
     "    5         1111       1111         5"},
    {"        ___[===================]___",
     "        111111111111111111111111111"},
};
static_assert(std::size(ORRERY_ART) == ORRERY_ART_ROWS);

namespace {

size_t utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 1;   // a stray continuation byte: take it alone rather than get stuck
}

ArtKind kindOf(const std::string& glyph, int stage) {
    constexpr int SUN = ORRERY_STAGE_COUNT - 1;
    if (stage == SUN) {
        if (glyph == "*" || glyph == "·") return ArtKind::Star;
        if (glyph == "(" || glyph == "O" || glyph == ")") return ArtKind::Core;
    }
    if ((stage == 1 || stage == 2) && (glyph == "●" || glyph == "o")) return ArtKind::Planet;
    return ArtKind::Line;
}

}  // namespace

std::vector<std::vector<ArtRun>> orreryPicture(int stagesBuilt) {
    std::vector<std::vector<ArtRun>> rows;
    for (const ArtRow& row : ORRERY_ART) {
        std::vector<ArtRun> runs;
        std::string glyphs = row.glyphs, stages = row.stages;
        size_t at = 0;
        for (size_t i = 0; at < glyphs.size(); ++i) {
            size_t len = std::min(utf8Length((unsigned char)glyphs[at]), glyphs.size() - at);
            ArtRun cell;
            cell.text = glyphs.substr(at, len);
            at += len;

            char tag = i < stages.size() ? stages[i] : ' ';
            if (cell.text != " " && tag >= '1' && tag < '1' + ORRERY_STAGE_COUNT) {
                cell.stage = tag - '1';
                cell.built = cell.stage < stagesBuilt;
                cell.kind = kindOf(cell.text, cell.stage);
                if (!cell.built) {
                    // Unbuilt: a dotted outline. Stars just aren't out yet.
                    cell.text = cell.kind == ArtKind::Star ? " " : "·";
                    cell.kind = cell.kind == ArtKind::Star ? ArtKind::Blank : ArtKind::Line;
                    if (cell.kind == ArtKind::Blank) cell.stage = -1;
                }
            } else {
                cell.text = " ";
            }

            if (!runs.empty() && runs.back().stage == cell.stage && runs.back().built == cell.built &&
                runs.back().kind == cell.kind) {
                runs.back().text += cell.text;
            } else {
                runs.push_back(std::move(cell));
            }
        }
        rows.push_back(std::move(runs));
    }
    return rows;
}

// ----- Goals -----
constexpr Goal GOALS[] = {
    {"First Steps",    "Mine 10 times",                     Stat::ManualMines,      10,  1},
    {"Pocket Change",  "Earn 1,000 tokens in total",        Stat::Lifetime,      1'000,  1},
    {"Lucky Strike",   "Hit a jackpot",                     Stat::Jackpots,          1,  1},
    {"On a Roll",      "Reach a mining streak of 20",       Stat::BestStreak,       20,  2},
    {"Tinkerer",       "Own 25 upgrade levels in total",    Stat::UpgradeLevels,    25,  2},
    {"Unbroken",       "Reach a mining streak of 250",      Stat::BestStreak,      250,  2},
    {"Hands-Free",     "Let the Mining Bot earn 5,000",     Stat::BotTotal,      5'000,  2},
    {"Storm Chaser",   "Witness 5 random events",           Stat::EventsSeen,        5,  2},
    {"Collector",      "Own 4 curios",                      Stat::CuriosOwned,       4,  2},
    {"Groundbreaking", "Finish the Monolith's Foundation",  Stat::MonolithStage,     1,  2},
    {"Dedicated",      "Play for 60 minutes",               Stat::PlayMinutes,      60,  3},
    {"Jackpot Junkie", "Hit 10 jackpots",                   Stat::Jackpots,         10,  3},
    {"Shard Baron",    "Trade 5,000 shards",                Stat::ShardsTraded,  5'000,  4},
    {"Industrialist",  "Own 75 upgrade levels in total",    Stat::UpgradeLevels,    75,  4},
    {"Grinder",        "Mine 1,000 times",                  Stat::ManualMines,   1'000,  4},
    {"Curator",        "Own every curio in the shop",       Stat::ShopCurios, SHOP_CURIO_COUNT, 5},
    {"Tycoon",         "Earn 1,000,000 tokens in total",    Stat::Lifetime,  1'000'000,  6},
    {"Architect",      "Complete the Monolith",             Stat::MonolithStage, MONO_STAGE_COUNT, 10},
    {"Market Maker",   "Trade 500 shards",                  Stat::ShardsTraded,    500,  2},
    {"Contractor",     "Complete 3 contracts",              Stat::ContractsDone,     3,  2},
    {"Payroll",        "Hire 4 crew members",               Stat::CrewHired,         4,  2},
    {"Crew Boss",      "Let your crew mine 50,000",         Stat::CrewMined,    50'000,  3},
    {"Professional",   "Complete 15 contracts",             Stat::ContractsDone,    15,  4},
    {"Stargazer",      "Finish the Orrery's Base Plate",    Stat::OrreryStage,       1,  3},
    {"Full House",     "Have 5 crew members at once",       Stat::CrewSize,          5,  3},
    {"Clockwork Sky",  "Complete the Orrery",               Stat::OrreryStage, ORRERY_STAGE_COUNT, 12},
    {"Regular",        "Become Trusted by a client",        Stat::BestStanding,      6,  3},
    {"Gadgeteer",      "Build 3 gadgets",                   Stat::GadgetsBuilt,      3,  3},
    {"Explorer",       "Finish 5 crew expeditions",         Stat::Expeditions,       5,  3},
};
static_assert(std::size(GOALS) == GOAL_COUNT);

// ----- Secret goals -----
const SecretGoal SECRETS[] = {
    {"rubberduck",  "Rubber Duck Debugging", "Quack.",                                      "Make the Rubber Duck your very first curio",              25},
    {"coldfeet",    "Cold Feet",             "Some offers are better left unanswered.",     "Say no to 10 offers in a row",                            40},
    {"witching",    "Witching Hour",         "The Haunted Pickaxe knows what time it is.",  "Mine by hand between 3 and 4 in the morning",             66},
    {"sevens",      "Lucky Sevens",          "Some numbers are luckier than others.",       "Mine exactly 777 tokens in one go",                       77},
    {"rested",      "Well Rested",           "Sometimes the best move is to walk away.",    "Come back after 8 hours or more away",                    30},
    {"glasscannon", "Glass Cannon",          "Big risks, hot hands.",                       "Finish a big contract at heat 8 or more",                120},
    {"goldenhour",  "Golden Hour",           "Strike while the gold is rushing.",           "Hit a jackpot during a Gold Rush",                        50},
    {"hoard",       "Dragon's Hoard",        "Just look at them all.",                      "Hold 10,000 shards at once",                             150},
    {"overkill",    "Overkill",              "Past twenty it stops helping. Keep going.",   "Reach a mining streak of 500",                           100},
    {"cleanhands",  "Clean Hands",           "Never let them down.",                        "Finish 10 contracts without ever failing one",            80},
    {"speedrun",    "Speedrunner",           "Hurry.",                                      "Reach the title Baron within 25 minutes of play",         60},
    {"downtozero",  "Down to Zero",          "Every last one.",                             "Spend your way to exactly 0 tokens (after earning 10,000)", 20},
    {"loyal",       "Loyal to a Fault",      "Some stay even when you can't pay.",          "Have a Loyal crew member stay through a missed payday",   40},
    {"undercounter","Under the Counter",     "Not everything is in the shop.",              "Own every curio the Dealer sells",                       200},
    {"firefighter", "Firefighter",           "Stop it before it starts.",                   "Stop 5 disasters before they hit",                       100},
};
static_assert(std::size(SECRETS) == SECRET_COUNT);

// ----- Titles -----
constexpr Title TITLES[] = {
    { 0, "Rookie"},
    { 4, "Prospector"},
    {10, "Hustler"},
    {18, "Baron"},
    {28, "Magnate"},
    {40, "Legend"},
    {55, "Monolith-Maker"},
    {62, "Kingpin"},
    {72, "Overseer"},
    {96, "Celestial"},   // every goal, Orrery included
};
static_assert(std::size(TITLES) == TITLE_COUNT);

// Celestial means every goal: its renown has to be exactly the sum of them all.
constexpr int allRenown() {
    int sum = 0;
    for (const Goal& g : GOALS) sum += g.renown;
    return sum;
}
static_assert(allRenown() == TITLES[TITLE_COUNT - 1].renown, "update Celestial's renown to the sum of the goals");

// ----- Infection -----
const StrainInfo STRAINS[] = {
    {"none",    "",        ""},
    {"fever",   "Fever",   "heat never cools on its own"},
    {"rust",    "Rust",    "upgrades cost twice as much"},
    {"static",  "Static",  "the market swings twice as hard, with twice the spread"},
    {"mutiny",  "Mutiny",  "the crew wants double wages"},
    {"leech",   "Leech",   "1% of your tokens drains away every 5 minutes"},
    {"amnesia", "Amnesia", "your mining streak fades after 6 seconds instead of 15"},
};
static_assert(std::size(STRAINS) == STRAIN_COUNT);

// ----- Unlocks -----
const FeatureInfo FEATURES[] = {
    {"Curios",       "curios",    1, "collectibles for your shelf"},                       // Prospector
    {"the Monolith", "monolith",  2, "a big build that soaks up tokens"},                  // Hustler
    {"the Market",   "market",    3, "shards, a second currency you trade tokens for"},    // Baron
    {"Contracts",    "contracts", 4, "timed jobs that pay tokens and shards"},             // Magnate
    {"the Crew",     "crew",      5, "hires who mine, trade, scout and handle heat"},      // Legend
    {"the Orrery",   "orrery",    6, "the late-game build, made of shards"},               // Monolith-Maker
    {"the Workshop", "workshop",  7, "gadgets built from your curios: trade-offs to equip"}, // Kingpin
    {"the Constellations", "stars", -1, "feats that light up the sky around the Orrery"},  // the Orrery's Sun
    {"Tasks",        "tasks",     8, "rules that run your errands for a fee (with a license)"}, // Overseer
};
static_assert(std::size(FEATURES) == FEATURE_COUNT);

// ----- Gadgets -----
const GadgetInfo GADGETS[] = {
    {"debugduck",    "Debug Duck",      0,  150, "offers come twice as often",                 "you only get 20 seconds to answer them"},
    {"tuningfork",   "Tuning Fork",     1,  250, "your streak lasts 25 seconds (+10)",         "jackpots come half as often"},
    {"lavacore",     "Lava Core",       2,  400, "Drones earn 50% more",                       "they only earn 4 hours while you're away (not 12)"},
    {"forgedpapers", "Forged Papers",   4,  500, "new contracts give you 25% more time",       "they pay 20% less"},
    {"luckypenny",   "Lucky Penny",     5,  650, "bonuses come 50% more often",                "they pay 30% less"},
    {"nightshift",   "Night Shift",     6,  900, "the Mining Bot works twice as fast",         "mining by hand pays 25% less"},
    {"goldenledger", "Golden Ledger",   7, 1200, "the market spread is 2 points tighter",      "audits can start at heat 2 (not 3)"},
    {"lightningrod", "Lightning Rod",   8, 1800, "random events come twice as often",          "they last half as long"},
    {"fogmachine",   "Fog Machine",     9, 2600, "heat cools twice as fast",                   "contracts pay half the shards"},
    {"moonanchor",   "Moon Anchor",    10, 4000, "the shard price swings half as hard",        "your own trades move it twice as much"},
};
static_assert(std::size(GADGETS) == GADGET_COUNT);

// ----- Crew -----
const RoleInfo ROLES[] = {
    {"miner",  "Miner",   8, "mines on their own every 5s"},
    {"broker", "Broker", 10, "tighter market spread, less price impact"},
    {"scout",  "Scout",   6, "finds shards"},
    {"fixer",  "Fixer",  12, "cools heat faster, can stop audits"},
};
static_assert(std::size(ROLES) == ROLE_COUNT);

const TraitInfo TRAITS[] = {
    {"none",         "",              ""},
    {"diligent",     "Diligent",      "works 25% harder"},
    {"greedy",       "Greedy",        "works 50% harder, wants double wages"},
    {"frugal",       "Frugal",        "half the wages, 15% less work"},
    {"quicklearner", "Quick Learner", "training costs half"},
    {"ambitious",    "Ambitious",     "promotions cost 30% less"},
    {"loyal",        "Loyal",         "stays through one missed payday"},
    {"nightowl",     "Night Owl",     "works twice as hard while you're not mining"},
};
static_assert(std::size(TRAITS) == TRAIT_COUNT);

const char* const CREW_NAMES[] = {
    "Mara", "Bo", "Deniz", "Elif", "Kaan", "Ayla", "Rook", "Vex",
    "Juno", "Tess", "Emre", "Lina", "Otto", "Pip", "Selin", "Nox",
    "Ines", "Cato", "Defne", "Hugo", "Iris", "Burak", "Wren", "Zeki",
    "Odette", "Milo", "Yuki", "Rafa", "Sable", "Ozan", "Quill", "Nadia",
};
static_assert(std::size(CREW_NAMES) == CREW_NAME_COUNT);

// ----- Contracts -----
const ContractKindInfo CONTRACT_KINDS[] = {
    {"earn",     "Earn %s tokens",                    "Earn 1 token",               false, ""},
    {"mine",     "Mine %s times",                     "Mine once",                  false, ""},
    {"jackpots", "Hit %s jackpots",                   "Hit a jackpot",              false, ""},
    {"streak",   "Build a fresh streak of %s",        "Build a fresh streak of 1",  false, ""},
    {"bot",      "Let the Mining Bot earn %s",        "Let the Mining Bot earn 1",  false, ""},
    {"trade",    "Trade %s shards on the market",     "Trade a shard on the market", false, ""},
    {"hold",     "Hold %s shards until the end",      "Hold a shard until the end", true,  "dropping below that many shards"},
    {"cooloff",  "Cool your heat down by %s",         "Cool your heat down by 1",   false, ""},
    {"handsoff", "Hands off: don't mine by hand",     "Hands off: don't mine by hand", true, "mining by hand even once"},
};
static_assert(std::size(CONTRACT_KINDS) == CONTRACT_KIND_COUNT);

const ClientStyleInfo CLIENT_STYLES[] = {
    {"Shady",     "pay 40% more, but every job you finish for them adds 1 heat"},
    {"Miners",    "only want mining by hand; pay 25% more shards"},
    {"Merchants", "money and market jobs; 60% more shards, 20% fewer tokens"},
    {"Small fry", "small jobs only, and they forgive easily"},
};
static_assert(std::size(CLIENT_STYLES) == (int)ClientStyle::Count);

const ClientInfo CLIENTS[] = {
    {"foundry",   "The Foundry",          ClientStyle::Miners,    7},
    {"halvard",   "Old Man Halvard",      ClientStyle::Miners,    10},
    {"saffron",   "Saffron & Sons",       ClientStyle::Merchants, 8},
    {"intern",    "a nervous intern",     ClientStyle::SmallFry,  9},
    {"syndicate", "The Syndicate",        ClientStyle::Shady,     11},
    {"kestrel",   "Mother Kestrel",       ClientStyle::Merchants, 6},
    {"burner",    "a burner account",     ClientStyle::Shady,     5},
    {"exchange",  "The Night Exchange",   ClientStyle::Shady,     0},
    {"corvid",    "Corvid Holdings",      ClientStyle::Shady,     2},
    {"landlord",  "your landlord",        ClientStyle::SmallFry,  3},
    {"pellucid",  "Dr. Pellucid",         ClientStyle::Merchants, 1},
    {"guild",     "the Guild of Pickaxes", ClientStyle::Miners,   4},
};
static_assert(std::size(CLIENTS) == CLIENT_COUNT);

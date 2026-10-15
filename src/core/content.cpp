#include "content.hpp"

#include <algorithm>
#include <iterator>

// ----- Events -----
const EventInfo EVENTS[] = {
    {"None",            "",                                  0},
    {"Meteor Shower",   "2x mining yield (you and the bot)", 30},
    {"Drone Overclock", "3x passive income",                 45},
    {"Gold Rush",       "2x bonus/jackpot odds",             20},
};
static_assert(std::size(EVENTS) == EV_COUNT);

EventFx eventFx(int ev) {
    EventFx fx;
    switch (ev) {
        case EV_METEOR:    fx.mining  = 2.0; break;
        case EV_OVERCLOCK: fx.passive = 3.0; break;
        case EV_GOLDRUSH:  fx.odds    = 2.0; break;
        default:           break; // EV_NONE (and anything corrupt) has no effect
    }
    return fx;
}

// ----- Curios -----
const char* const CURIO_SETS[] = {"Desk Toys", "Relics", "Impossible Things"};
static_assert(std::size(CURIO_SETS) == CURIO_SET_COUNT);

const CurioInfo CURIOS[] = {
    {"Rubber Duck",              0,    300, "Listens to your bugs without judgment."},
    {"Newton's Cradle",          0,    600, "Click. Click. Click. Click."},
    {"Lava Lamp",                0,  1'000, "The blobs never meet. Neither do your deadlines."},
    {"Tiny Cactus",              0,  1'500, "Low maintenance. Unlike you."},
    {"Cracked Floppy Disk",      1,  2'500, "1.44 MB of someone's lost thesis."},
    {"The First Token Ever",     1,  4'000, "Slightly dented. Priceless. Well, 4,000."},
    {"Haunted Pickaxe",          1,  6'500, "Mines on its own at 3 AM. Do not ask."},
    {"Golden Keyboard",          1, 10'000, "Every keystroke sounds like a cash register."},
    {"Bottled Lightning",        2, 20'000, "Shake well before not opening."},
    {"Jar of Fog from Day One",  2, 35'000, "Smells like the beginning."},
    {"A Spare Moon",             2, 55'000, "In case the first one breaks."},
    {"The Last Token",           2, 80'000, "There will never be another. (There will. Shh.)"},
};
static_assert(std::size(CURIOS) == CURIO_COUNT);

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
const Goal GOALS[] = {
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
    {"Curator",        "Own every curio",                   Stat::CuriosOwned, CURIO_COUNT, 5},
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
};
static_assert(std::size(GOALS) == GOAL_COUNT);

// ----- Titles -----
const Title TITLES[] = {
    { 0, "Rookie"},
    { 4, "Prospector"},
    {10, "Hustler"},
    {18, "Baron"},
    {28, "Magnate"},
    {40, "Legend"},
    {55, "Monolith-Maker"},
    {68, "Kingpin"},
    {87, "Celestial"},   // every goal, Orrery included
};
static_assert(std::size(TITLES) == TITLE_COUNT);

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
};
static_assert(std::size(FEATURES) == FEATURE_COUNT);

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
    {"earn",     "Earn %s tokens",                    "Earn 1 token"},
    {"mine",     "Mine %s times",                     "Mine once"},
    {"jackpots", "Hit %s jackpots",                   "Hit a jackpot"},
    {"streak",   "Build a fresh streak of %s",        "Build a fresh streak of 1"},
    {"bot",      "Let the Mining Bot earn %s",        "Let the Mining Bot earn 1"},
};
static_assert(std::size(CONTRACT_KINDS) == CONTRACT_KIND_COUNT);

const char* const CLIENTS[] = {
    "The Foundry", "Old Man Halvard", "Saffron & Sons", "a nervous intern", "The Syndicate",
    "Mother Kestrel", "a burner account", "The Night Exchange", "Corvid Holdings",
    "your landlord", "Dr. Pellucid", "the Guild of Pickaxes",
};
static_assert(std::size(CLIENTS) == CLIENT_COUNT);

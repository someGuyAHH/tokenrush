// Ways to spend tokens: the shop, curios, prompt styles and the Monolith.
// Rule for every menu here: never hold the game lock while waiting for input.
// Lock to draw the menu, unlock to read the player's choice, then lock again and
// re-check, because the workers may have changed the balance in between.

#include <algorithm>
#include <bit>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "commands.hpp"
#include "core/actions.hpp"
#include "core/content.hpp"
#include "core/game.hpp"
#include "core/util.hpp"
#include "term.hpp"

// ----- Shop -----
enum class ShopItem { Back, Pickaxe, Magnet, Drone, Bot, License, Invalid };

static ShopItem parseShopItem(const std::string& s) {
    static const std::unordered_map<std::string, ShopItem> table = {
        {"0", ShopItem::Back},    {"back", ShopItem::Back},       {"b", ShopItem::Back},
        {"1", ShopItem::Pickaxe}, {"pickaxe", ShopItem::Pickaxe}, {"p", ShopItem::Pickaxe},
        {"2", ShopItem::Magnet},  {"magnet", ShopItem::Magnet},
        {"3", ShopItem::Drone},   {"drone", ShopItem::Drone},     {"d", ShopItem::Drone},
        {"4", ShopItem::Bot},     {"bot", ShopItem::Bot},
        {"5", ShopItem::License}, {"license", ShopItem::License}, {"auto", ShopItem::License},
    };
    auto it = table.find(s);
    return it == table.end() ? ShopItem::Invalid : it->second;
}

// One shop line, like "1", "pickaxe 10" or "0". False when it means "back".
static bool shopLine(Shared& s, const std::string& line) {
    std::stringstream ss(line);
    std::string word, countWord;
    ss >> word >> countWord;
    ShopItem item = parseShopItem(lower(word));
    if (item == ShopItem::Back) return false;

    long long count = 1;   // "1 10" buys ten Pickaxe levels
    if (!countWord.empty() && (!parseLongLong(countWord, count) || count < 1 || count > MAX_LEVEL)) {
        out() << paint(Style::Warn, "Buy how many? Try something like '1 10'.") << "\n";
        return true;
    }

    Notes notes;
    {
        std::lock_guard lk(s.m);
        GameState& g = s.g;
        switch (item) {
            case ShopItem::Back:    break; // handled above
            case ShopItem::Pickaxe: notes = buyUpgrades(g, 0, (int)count); break;
            case ShopItem::Magnet:  notes = buyUpgrades(g, 1, (int)count); break;
            case ShopItem::Drone:   notes = buyUpgrades(g, 2, (int)count); break;
            case ShopItem::Bot:     notes = buyUpgrades(g, 3, (int)count); break;
            case ShopItem::License: notes = buyLicense(g);    break;
            case ShopItem::Invalid: notes = {{Sev::Warn, "Choose 0-5."}}; break;
        }
    }
    printNotes(notes);
    return true;
}

static void printShop(Shared& s) {
    std::lock_guard lk(s.m);
    GameState& g = s.g;
    auto ups = upgrades(g);

    out() << "\n" << paint(Style::Header, "=== SHOP ===") << "\n";
    out() << "Tokens: " << paint(Style::Tokens, num(g.tokens)) << "\n";
    for (size_t i = 0; i < ups.size(); ++i) {
        bool afford = g.tokens >= ups[i].cost;
        out() << (i + 1) << ") " << padRight(ups[i].name, 11)
              << "(lvl " << *ups[i].lvl << ")  cost: " << paint(afford ? Style::Plain : Style::Muted, num(ups[i].cost)) << "\n";
    }
    out() << "5) Auto-Buyer license  ";
    if (g.autoUnlocked) out() << "(owned)\n";
    else                out() << "cost: " << num(AUTOBUY_LICENSE_COST) << "\n";
    out() << "0) Back\n";
    out() << paint(Style::Muted, "Add a number to buy several levels at once: '1 10' buys 10 Pickaxe levels.") << "\n";
}

void doShop(Shared& s, const std::string& args) {
    if (!trim(args).empty()) { shopLine(s, args); return; }   // "shop 1 10" skips the menu
    while (true) {
        printShop(s);
        out() << "> " << std::flush;   // (the lock is released before waiting on input)
        std::string line;
        if (!std::getline(in(), line) || !shopLine(s, line)) return;
    }
}

// ----- Curios -----
static void buyCurioNumber(Shared& s, long long pick) {
    if (pick < 1 || pick > CURIO_COUNT) { out() << "No such curio.\n"; return; }
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = buyCurio(s.g, (int)pick - 1);
    }
    printNotes(notes);
}

void doCurios(Shared& s, const std::string& arg) {
    if (!arg.empty()) {   // "curios 3" buys number 3 without the menu
        long long pick;
        if (!parseLongLong(arg, pick)) out() << "Usage: curios [number]\n";
        else buyCurioNumber(s, pick);
        return;
    }
    while (true) {
        {
            std::lock_guard lk(s.m);
            const GameState& g = s.g;
            out() << "\n" << paint(Style::Header, "=== CURIOS ===") << "  Tokens: " << paint(Style::Tokens, num(g.tokens))
                      << "   Shelf: " << std::popcount(g.curiosOwned) << "/" << CURIO_COUNT << "\n";
            for (int set = 0; set < CURIO_SET_COUNT; ++set) {
                out() << paint(Style::Bold, CURIO_SETS[set]) << "\n";
                for (int i = 0; i < CURIO_COUNT; ++i) {
                    if (CURIOS[i].set != set) continue;
                    bool owned = hasBit(g.curiosOwned, i);
                    std::string row = padLeft(std::to_string(i + 1), 2) + ") " + (owned ? "[x] " : "[ ] ") + padRight(CURIOS[i].name, 25);
                    if (owned) out() << "  " << paint(Style::Good, row) << paint(Style::Muted, std::string("\"") + CURIOS[i].flavor + "\"") << "\n";
                    else       out() << "  " << row << padLeft(num(CURIOS[i].cost), 11) << "\n";
                }
            }
            out() << "Buy which? (number, 0 to go back)\n> " << std::flush;
        } // unlock before waiting on input

        std::string line;
        if (!std::getline(in(), line)) return;

        long long pick;
        if (!parseLongLong(line, pick)) { out() << "Enter a number.\n"; continue; }
        if (pick == 0) return;
        buyCurioNumber(s, pick);
    }
}

// ----- Prompt styles -----
void doPrompt(Shared& s, const std::string& arg) {
    if (arg.empty()) {
        std::lock_guard lk(s.m);
        const GameState& g = s.g;
        out() << paint(Style::Header, "=== PROMPT STYLES ===") << "  Tokens: " << paint(Style::Tokens, num(g.tokens)) << "\n";
        for (int i = 0; i < PROMPT_COUNT; ++i) {
            out() << "  " << (i + 1) << ") " << padRight(PROMPTS[i].text, 6);
            if (i == g.promptSel)                 out() << paint(Style::Good, "(in use)") << "\n";
            else if (hasBit(g.promptsOwned, i))   out() << "(owned)\n";
            else                                  out() << num(PROMPTS[i].cost) << "\n";
        }
        out() << "  " << (PROMPT_COUNT + 1) << ") ";
        if (!g.customPromptOwned) {
            out() << padRight("?????", 6) << num(customPromptCost()) << paint(Style::Muted, "   your own, up to 5 characters") << "\n";
        } else {
            out() << padRight(g.customPrompt + " ", 6)
                      << (g.promptSel == PROMPT_COUNT ? paint(Style::Good, "(in use)") : std::string("(owned)"))
                      << paint(Style::Muted, "   change it any time, free") << "\n";
        }
        out() << "Use 'prompt <n>' to buy or switch, 'prompt custom <text>' to make your own.\n";
        return;
    }

    // "custom <text>": the text keeps its case, so it's taken from the raw argument.
    if (lower(arg.substr(0, 6)) == "custom" && (arg.size() == 6 || arg[6] == ' ')) {
        std::string text = trim(arg.substr(6));
        Notes out;
        {
            std::lock_guard lk(s.m);
            out = setCustomPrompt(s.g, text);
        }
        printNotes(out);
        return;
    }

    long long n;
    if (!parseLongLong(arg, n) || n < 1 || n > PROMPT_COUNT + 1) {
        out() << "Usage: prompt [1-" << (PROMPT_COUNT + 1) << "]  or  prompt custom <text>\n";
        return;
    }
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = choosePrompt(s.g, (int)n - 1);
    }
    printNotes(out);
}

// ----- The Monolith -----
static void printMonolith(const GameState& g) {
    out() << paint(Style::Header, "=== THE MONOLITH ===") << "\n";
    for (int st = MONO_STAGE_COUNT - 1; st >= 0; --st) {
        bool built = st < g.monolithStage;
        for (int li = 0; li < 2 && MONO[st].art[li] != nullptr; ++li) {
            std::string line = MONO[st].art[li];
            if (!built) for (char& ch : line) if (ch != ' ') ch = '.'; // unbuilt stages are a dotted outline
            line.resize(26, ' ');
            out() << (built ? paint(Style::Tokens, line) : paint(Style::Muted, line));
            if (li == 0) {
                out() << "<- " << MONO[st].name;
                if (st == g.monolithStage) out() << "  (building)";
            }
            out() << "\n";
        }
    }

    if (g.monolithStage >= MONO_STAGE_COUNT) {
        out() << "Complete. It hums quietly.\n";
        return;
    }
    const MonoStage& m = MONO[g.monolithStage];
    out() << "Stage " << (g.monolithStage + 1) << "/" << MONO_STAGE_COUNT << ": " << m.name << "  "
              << bar(g.monolithProgress, m.cost, 20) << " " << num(g.monolithProgress) << " / " << num(m.cost) << "\n";
}

static void printEnding(const GameState& g) {
    long long h = g.playSeconds / 3600, mins = (g.playSeconds / 60) % 60;
    out() <<
R"(
********************************************
*        THE MONOLITH IS COMPLETE          *
********************************************
The Beacon flickers on. Every miner within a
hundred servers looks up at once.
)";
    out() << "  Play time:       " << h << "h " << mins << "m\n"
              << "  Tokens earned:   " << num(g.lifetimeEarned) << "\n"
              << "  Times mined:     " << num(g.manualMines) << "\n"
              << "  Jackpots:        " << num(g.jackpots) << "\n"
              << "  Curios:          " << std::popcount(g.curiosOwned) << "/" << CURIO_COUNT << "\n"
              << "You can keep playing. There are still goals left, maybe.\n";
}

void doBuild(Shared& s, const std::string& arg) {
    std::lock_guard lk(s.m);
    GameState& g = s.g;

    if (arg.empty()) {
        printMonolith(g);
        if (g.monolithStage < MONO_STAGE_COUNT) out() << "Use 'build <amount>' or 'build all' to contribute.\n";
        return;
    }

    long long amount = 0;
    if (arg == "all") amount = g.tokens;
    else if (!parseLongLong(arg, amount)) { out() << "Usage: build <amount|all>\n"; return; }

    BuildResult r = buildMonolith(g, amount);
    printNotes(r.notes);
    if (r.poured > 0) printMonolith(g);
    if (r.finished) printEnding(g);
}

// ----- The Orrery -----
static std::string orreryRunText(const ArtRun& run) {
    if (run.kind == ArtKind::Blank) return run.text;
    if (!run.built) return paint(Style::Muted, run.text);
    static const int stageColor[] = {180, 39, 75, 249, 220};   // tan, blue, steel blue, silver, gold
    switch (run.kind) {
        case ArtKind::Planet: return paint256(216, run.text, true);
        case ArtKind::Star:   return paint256(227, run.text);
        case ArtKind::Core:   return paint256(220, run.text, true);
        default:              return paint256(stageColor[std::clamp(run.stage, 0, 4)], run.text);
    }
}

static void printOrrery(const GameState& g) {
    constexpr size_t ART_WIDTH = 44;
    out() << paint(Style::Header, "=== THE ORRERY ===") << "  Shards: " << paint(Style::Shards, num(g.shards)) << "\n";
    int r = 0;
    for (const std::vector<ArtRun>& runs : orreryPicture(g.orreryStage)) {
        std::string line, plain;
        for (const ArtRun& run : runs) { line += orreryRunText(run); plain += run.text; }
        out() << line;
        for (int st = 0; st < ORRERY_STAGE_COUNT; ++st) {
            if (ORRERY[st].labelRow != r) continue;
            size_t w = visibleWidth(plain);
            out() << std::string(w < ART_WIDTH ? ART_WIDTH - w : 1, ' ');
            if (st < g.orreryStage) out() << paint(Style::Bold, padRight(ORRERY[st].name, 13)) << paint(Style::Good, ORRERY[st].perk);
            else out() << paint(Style::Muted, padRight(ORRERY[st].name, 13) + num(ORRERY[st].cost) + " shards: " + ORRERY[st].perk);
        }
        out() << "\n";
        ++r;
    }
    if (g.orreryStage >= ORRERY_STAGE_COUNT) {
        out() << paint(Style::Shards, "Complete. The little sun turns, and so does everything else.") << "\n";
        return;
    }
    const OrreryStage& st = ORRERY[g.orreryStage];
    out() << "Stage " << (g.orreryStage + 1) << "/" << ORRERY_STAGE_COUNT << ": " << st.name << "  "
              << bar(g.orreryProgress, st.cost, 20) << " " << num(g.orreryProgress) << " / " << num(st.cost) << " shards\n";
}

void doOrrery(Shared& s, const std::string& arg) {
    std::lock_guard lk(s.m);
    GameState& g = s.g;

    if (arg.empty()) {
        printOrrery(g);
        if (g.orreryStage < ORRERY_STAGE_COUNT)
            out() << "Use 'orrery <shards>' or 'orrery all' to build. Shards come from the market, Scouts and contracts.\n";
        return;
    }

    long long amount = 0;
    if (arg == "all") {
        amount = orrerySpareShards(g);   // keeps the next payday's wages, or the crew walks
        if (amount < g.shards) out() << paint(Style::Muted, "(Keeping " + count(g.shards - amount, "shard") + " for payday.)") << "\n";
    }
    else if (!parseLongLong(arg, amount)) { out() << "Usage: orrery <shards|all>\n"; return; }

    BuildResult r = buildOrrery(g, amount);
    printNotes(r.notes);
    if (r.poured > 0) printOrrery(g);
    if (r.finished) out() << paint(Style::Event, "*** THE ORRERY IS COMPLETE ***") << "\n";
}

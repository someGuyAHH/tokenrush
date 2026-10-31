// Ways to spend tokens: the shop, curios, prompt styles and the Monolith.
// Rule for every menu here: never hold the game lock while waiting for input.
// Lock to draw the menu, unlock to read the player's choice, then lock again and
// re-check, because the workers may have changed the balance in between.

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "commands.hpp"
#include "core/actions.hpp"
#include "core/content.hpp"
#include "core/game.hpp"
#include "core/stars.hpp"
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
        if (!std::getline(in(), line)) return;
        std::string first;
        std::stringstream(line) >> first;
        if (parseShopItem(lower(first)) == ShopItem::Invalid && menuHandsBack(s, line, "shop")) return;   // 'status', 'curios 3'...
        if (!shopLine(s, line)) return;
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
                out() << paint(Style::Bold, CURIO_SETS[set]) << (set == DEALER_SET ? paint(Style::Muted, "   not in any shop: the Curio Dealer, or a lucky expedition") : "") << "\n";
                for (int i = 0; i < CURIO_COUNT; ++i) {
                    if (CURIOS[i].set != set) continue;
                    bool owned = hasBit(g.curiosOwned, i);
                    std::string row = padLeft(std::to_string(i + 1), 2) + ") " + (owned ? "[x] " : "[ ] ") + padRight(owned || inShop(i) ? CURIOS[i].name : "???", 27);
                    if (owned)           out() << "  " << paint(Style::Good, row) << paint(Style::Muted, std::string("\"") + CURIOS[i].flavor + "\"") << "\n";
                    else if (inShop(i))  out() << "  " << row << padLeft(num(CURIOS[i].cost), 11) << "\n";
                    else                 out() << "  " << paint(Style::Muted, row) << "\n";
                }
            }
            if (!g.keepsakes.empty()) {
                out() << paint(Style::Bold, "Keepsakes") << paint(Style::Muted, "   left by your crew") << "\n";
                for (const Keepsake& k : g.keepsakes)
                    out() << "  " << padRight(k.item, 42) << paint(Style::Muted, k.why) << "\n";
            }
            out() << "Buy which? (number, 0 to go back)\n> " << std::flush;
        } // unlock before waiting on input

        std::string line;
        if (!std::getline(in(), line)) return;

        long long pick;
        if (!parseLongLong(line, pick)) {
            if (menuHandsBack(s, line, "curio shelf")) return;
            out() << "Enter a number (0 to go back).\n";
            continue;
        }
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
        out() << "  Color: ";
        if (!g.promptColorOwned) out() << num(promptColorCost()) << " shards" << paint(Style::Muted, "   draws any prompt in a color of yours") << "\n";
        else if (!g.promptColorSet) out() << "the theme's" << paint(Style::Muted, "   'prompt color #ff8800' picks one, free") << "\n";
        else out() << paintRgb(g.promptColor, hexColor(g.promptColor)) << (g.promptColorOn ? "" : paint(Style::Muted, " (off)"))
                   << paint(Style::Muted, "   change it any time, free") << "\n";
        out() << "Use 'prompt <n>' to buy or switch, 'prompt custom <text>' to make your own, 'prompt color #ff8800' to color it.\n";
        return;
    }

    // "color <#rrggbb|off|on>"
    if (lower(arg.substr(0, 5)) == "color" && (arg.size() == 5 || arg[5] == ' ')) {
        std::string text = trim(arg.substr(5));
        if (text.empty()) {
            std::lock_guard lk(s.m);
            const GameState& g = s.g;
            if (!g.promptColorOwned) out() << "'prompt color #ff8800' draws your prompt in that color (" << num(promptColorCost()) << " shards, once; then free to change).\n";
            else if (!g.promptColorSet) out() << "Your prompt has the theme's color. 'prompt color #ff8800' picks one (free: you own it).\n";
            else out() << "Your prompt's color: " << paintRgb(g.promptColor, hexColor(g.promptColor)) << (g.promptColorOn ? "" : " (off: 'prompt color on')") << ".\n";
            return;
        }
        Notes out;
        {
            std::lock_guard lk(s.m);
            out = setPromptColor(s.g, text);
        }
        printNotes(out);
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
        out() << "Usage: prompt [1-" << (PROMPT_COUNT + 1) << "], prompt custom <text>, or prompt color <#rrggbb|off|on|reset>\n";
        return;
    }
    Notes out;
    {
        std::lock_guard lk(s.m);
        out = choosePrompt(s.g, (int)n - 1);
    }
    printNotes(out);
}

// ----- The Constellations -----
void doStars(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;
    out() << paint(Style::Header, "=== THE CONSTELLATIONS ===") << "  " << std::popcount(g.starsLit) << "/" << CONSTELLATION_COUNT << " lit\n\n";
    for (const std::vector<SkyGlyph>& row : skyMap(g)) {
        std::string line;
        for (const SkyGlyph& c : row) {
            switch (c.kind) {
                case SkyCell::Empty: line += " "; break;
                case SkyCell::Dust:  line += paint(Style::Muted, c.text); break;
                case SkyCell::Star:
                case SkyCell::Label: line += c.lit ? paintHue(CONSTELLATIONS[c.constellation].hue, c.text, c.kind == SkyCell::Star)
                                                   : paint(Style::Muted, c.text); break;
            }
        }
        out() << "  " << line << "\n";
    }
    out() << "\n";
    for (int i = 0; i < CONSTELLATION_COUNT; ++i) {
        const ConstellationInfo& c = CONSTELLATIONS[i];
        bool lit = hasBit(g.starsLit, i);
        std::string progress = lit ? "" : starProgress(g, i);
        out() << "  " << (lit ? paintHue(c.hue, "[*] " + padRight(c.name, 15), true) : paint(Style::Muted, "[ ] ") + padRight(c.name, 15))
              << (lit ? paint(Style::Muted, c.feat) : std::string(c.feat))
              << (progress.empty() ? "" : paint(Style::Muted, "   (" + progress + ")")) << "\n";
    }
    out() << paint(Style::Muted, "They give nothing but the sky. Each one is a feat you pulled off.") << "\n";
}

// ----- Themes -----
// One line in a theme's own colors, on its own background: what the game would look like.
static std::string swatch(int theme) {
    bool classic = themeClassic(theme);
    Rgb bgc = themeRgb(theme, Hue::Background), fgc = themeRgb(theme, Hue::Text);
    struct Part { Style st; const char* text; };
    static const Part parts[] = {
        {Style::Header, "Heading"}, {Style::Plain, "  "}, {Style::Tokens, "1,234 tokens"}, {Style::Plain, "  "},
        {Style::Shards, "56 shards"}, {Style::Plain, "  "}, {Style::Good, "good"}, {Style::Plain, " "},
        {Style::Bad, "bad"}, {Style::Plain, " "}, {Style::Warn, "warning"}, {Style::Plain, " "},
        {Style::Event, "EVENT"}, {Style::Plain, " "}, {Style::Heat, "heat"}, {Style::Plain, " "}, {Style::Muted, "quiet"},
    };
    if (!colorOn()) return "";
    std::string bg = classic ? "" : "\x1b[48;2;" + std::to_string(bgc.r) + ";" + std::to_string(bgc.g) + ";" + std::to_string(bgc.b) + "m";
    std::string text = classic ? "" : "\x1b[38;2;" + std::to_string(fgc.r) + ";" + std::to_string(fgc.g) + ";" + std::to_string(fgc.b) + "m";
    std::string out = bg + text + " ";
    for (const Part& p : parts) {
        std::string painted = paintIn(theme, p.st, p.text);
        // paintIn() ends with a full reset, which would drop the background: put it back after each part.
        out += painted + (painted.size() > std::strlen(p.text) ? bg + text : "");
    }
    return out + " \x1b[0m";
}

// "Button (off)" -> "buttonoff": how a color is named after 'theme custom'.
static std::string hueKey(const std::string& s) {
    std::string k;
    for (char c : s) if (std::isalpha((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
    return k;
}

static int hueByName(const std::string& word) {
    std::string w = hueKey(word);
    if (w == "muted" || w == "quiet") return (int)Hue::Muted;
    if (w == "warn") return (int)Hue::Warn;
    if (w == "bg") return (int)Hue::Background;
    for (int h = 0; h < HUE_COUNT; ++h) if (w == hueKey(HUES[h].name)) return h;
    return -1;
}

static void printCustomColors(const GameState& g) {
    out() << paint(Style::Bold, "Your custom theme") << paint(Style::Muted, "   ('theme custom <color> <#hex>' to change one)") << "\n";
    for (int h = 0; h < HUE_COUNT; ++h) {
        Rgb c = g.customTheme[(size_t)h];
        out() << "  " << paintRgb(c, "██") << " " << padRight(hueKey(HUES[h].name), 15) << padRight(hexColor(c), 9)
              << paint(Style::Muted, HUES[h].use) << "\n";
    }
}

// theme custom [ <color> <#hex> | from <theme> ]
static void doCustomTheme(Shared& s, const std::string& args) {
    std::stringstream ss(args);
    std::string a, b;
    ss >> a >> b;
    Notes notes;
    int now;
    {
        std::lock_guard lk(s.m);
        GameState& g = s.g;
        if (a.empty()) {
            notes = chooseTheme(g, CUSTOM_THEME);
        } else if (lower(a) == "from") {
            int base = -1;
            for (int i = 0; i < THEME_COUNT; ++i) if (lower(b) == THEMES[i].id) base = i;
            if (base < 0) { out() << "Usage: theme custom from <classic|dusk|glacier|...>\n"; return; }
            notes = copyIntoCustom(g, base);
        } else {
            int h = hueByName(a);
            Rgb c;
            if (h < 0) { out() << "No color called " << a << ". 'theme custom' lists them (text, quiettext, accent, ...).\n"; return; }
            if (!parseHexColor(b, c)) { out() << "Usage: theme custom " << hueKey(HUES[h].name) << " #rrggbb\n"; return; }
            notes = setCustomColor(g, (Hue)h, c);
        }
        now = g.themeSel;
        if (g.customThemeOwned) setCustomPalette(g.customTheme);
    }
    setTheme(now);
    printNotes(notes);
    std::lock_guard lk(s.m);
    if (s.g.customThemeOwned && (a.empty() || lower(a) == "from")) printCustomColors(s.g);
}

void doTheme(Shared& s, const std::string& args) {
    std::string arg = lower(trim(args));
    if (arg == "custom" || arg.rfind("custom ", 0) == 0) { doCustomTheme(s, trim(args.substr(6))); return; }
    if (arg.empty()) {
        std::lock_guard lk(s.m);
        const GameState& g = s.g;
        out() << paint(Style::Header, "=== THEMES ===") << "  Shards: " << paint(Style::Shards, num(g.shards))
              << paint(Style::Muted, "   (a theme belongs to this profile)") << "\n";
        for (int i = 0; i < THEME_COUNT; ++i) {
            const ThemeInfo& t = THEMES[i];
            std::string state = i == g.themeSel ? paint(Style::Good, "in use") : themeOwned(g, i) ? std::string(t.cost ? "owned" : "free")
                                                                                : paint(Style::Shards, count(t.cost, "shard"));
            out() << "  " << (i + 1) << ") " << paint(Style::Bold, padRight(t.name, 11)) << padVisible(state, 14)
                  << paint(Style::Muted, t.blurb) << "\n";
            if (std::string sw = swatch(i); !sw.empty()) out() << "     " << sw << "\n";
        }
        std::string state = g.themeSel == CUSTOM_THEME ? paint(Style::Good, "in use") : g.customThemeOwned ? std::string("owned")
                                                                                         : paint(Style::Shards, count(CUSTOM_THEME_COST, "shard"));
        out() << "  " << THEME_CHOICES << ") " << paint(Style::Bold, padRight("Custom", 11)) << padVisible(state, 14)
              << paint(Style::Muted, "every color picked by hand ('theme custom')") << "\n";
        if (g.customThemeOwned) if (std::string sw = swatch(CUSTOM_THEME); !sw.empty()) out() << "     " << sw << "\n";
        out() << "Use 'theme <n>' (or its name) to buy or switch. Classic uses your terminal's own colors; the others are 24-bit color.\n";
        return;
    }

    int idx = -1;
    long long n;
    if (parseLongLong(arg, n) && n == THEME_CHOICES) { doCustomTheme(s, ""); return; }
    if (parseLongLong(arg, n) && n >= 1 && n <= THEME_COUNT) idx = (int)n - 1;
    for (int i = 0; i < THEME_COUNT && idx < 0; ++i)
        if (lower(arg) == THEMES[i].id) idx = i;
    if (idx < 0) { out() << "Usage: theme [1-" << THEME_CHOICES << " or a name]  (see 'theme')\n"; return; }

    Notes notes;
    int now;
    {
        std::lock_guard lk(s.m);
        notes = chooseTheme(s.g, idx);
        now = s.g.themeSel;
    }
    setTheme(now);   // from here on, everything prints in it
    printNotes(notes);
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
    if (g.orreryAssembly > 0)
        out() << paint(Style::Shards, "Assembling: " + duration(g.orreryAssembly) + " left") << paint(Style::Muted, " (while the game is open)") << "\n";
}

void doOrrery(Shared& s, const std::string& arg) {
    std::lock_guard lk(s.m);
    GameState& g = s.g;

    if (arg.empty()) {
        printOrrery(g);
        if (g.orreryStage < ORRERY_STAGE_COUNT)
            out() << "Use 'orrery <shards>' or 'orrery all' to build. Shards come from the market, Scouts and contracts.\n"
                  << paint(Style::Muted, "A funded stage takes time to assemble: " + duration(orreryBuildSeconds(0)) + " for the first, " +
                                         std::to_string(ORRERY_BUILD_GROWTH) + "x more each stage after.") << "\n";
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
}

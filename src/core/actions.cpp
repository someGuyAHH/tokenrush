#include "actions.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "crew.hpp"
#include "stars.hpp"
#include "game.hpp"
#include "util.hpp"

// ----- Mining -----
MineResult mine(GameState& g, std::time_t t, std::mt19937& rng) {
    normalizeStreak(g, t);
    EventFx fx = fxNow(g, t);

    MineResult r;
    r.base = baseYield(g);
    std::uniform_int_distribution<int> extra(0, r.base);
    int raw = r.base + extra(rng);

    int streakForMult = std::min(g.streak, 20);
    r.mult = 1.0 + 0.05 * (double)streakForMult;   // up to 2.0x at streak 20+

    double bonusP   = bonusChance(g, fx.odds);
    double jackpotP = jackpotChance(g, fx.odds);

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double roll = uni(rng);

    long long gain = toTokens((double)raw * r.mult);
    if (roll < jackpotP) {
        r.jackpot = true;
        gain = addTokens(gain, toTokens((double)r.base * (80.0 + 20.0 * (double)g.magnet)));
    } else if (roll < jackpotP + bonusP) {
        r.bonus = true;
        gain = addTokens(gain, toTokens((double)r.base * bonusSize(g)));
    }
    r.meteor = fx.mining > 1.0;
    r.gain = toTokens((double)gain * fx.mining * handMult(g));

    earn(g, r.gain, Source::Mining);
    if (r.gain == 777) spotSecret(g, Secret::LuckySevens);
    if (r.jackpot && activeEvent(g, t) == EV_GOLDRUSH) spotSecret(g, Secret::GoldenHour);
    if (localHour(t) == 3) spotSecret(g, Secret::WitchingHour);
    if (g.streak + 1 >= 250 && r.meteor) spotStar(g, Constellation::Miner);   // (the streak goes up just below)
    g.contractStreak = std::min(g.contractStreak, (long long)g.streak);   // follows the streak down if it broke
    g.streak += 1;
    g.contractStreak += 1;
    g.lastMine = t;
    g.manualMines += 1;
    if (r.jackpot) {
        g.jackpots += 1;
        g.biggestJackpot = std::max(g.biggestJackpot, r.gain);
    }
    g.bestStreak = std::max<long long>(g.bestStreak, g.streak);
    g.contractPeakStreak = std::max(g.contractPeakStreak, g.contractStreak);
    return r;
}

// ----- Shop -----
Notes buyUpgrade(GameState& g, size_t idx) { return buyUpgrades(g, idx, 1); }

long long upgradesCost(const GameState& g, size_t idx, int n) {
    GameState probe;   // a blank state with just the levels copied in, to ask upgrades() the prices
    probe.pickaxe = g.pickaxe; probe.magnet = g.magnet; probe.drone = g.drone; probe.bot = g.bot;
    probe.strain = g.strain; probe.cured = g.cured;   // Rust doubles prices
    if (idx >= upgrades(probe).size() || n <= 0) return 0;
    int& lvl = *upgrades(probe)[idx].lvl;
    long long total = 0;
    for (int i = 0; i < n && lvl < MAX_LEVEL; ++i, ++lvl) total = addTokens(total, upgrades(probe)[idx].cost);
    return total;
}

Notes buyUpgrades(GameState& g, size_t idx, int n) {
    if (idx >= upgrades(g).size()) return {{Sev::Warn, "No such upgrade."}};
    if (n <= 0) return {{Sev::Warn, "Buy how many?"}};

    int bought = 0;
    long long spent = 0;
    while (bought < n) {
        UpgradeRef u = upgrades(g)[idx];   // the cost changes with every level
        if (*u.lvl >= MAX_LEVEL || g.tokens < u.cost) break;
        g.tokens -= u.cost;
        spent += u.cost;
        *u.lvl += 1;
        ++bought;
    }

    UpgradeRef u = upgrades(g)[idx];
    if (bought == 0) {
        if (*u.lvl >= MAX_LEVEL) return {{Sev::Warn, "That's maxed out."}};
        return {{Sev::Warn, "Not enough tokens. The next " + std::string(u.name) + " level costs " + num(u.cost) + "."}};
    }
    std::string msg = n == 1 ? std::string("Bought ") + u.name + " -> lvl " + std::to_string(*u.lvl)
                             : "Bought " + std::to_string(bought) + " " + u.name + " levels -> lvl " +
                                   std::to_string(*u.lvl) + " for " + num(spent) + " tokens";
    if (bought < n) msg += *u.lvl >= MAX_LEVEL ? " (maxed out)" : " (that's all you could afford)";
    return {{Sev::Good, msg + "."}};
}

Notes buyLicense(GameState& g) {
    if (g.autoUnlocked) return {{Sev::Info, "You already own the Auto-Buyer."}};
    if (g.tokens < AUTOBUY_LICENSE_COST) return {{Sev::Warn, "Not enough tokens."}};
    g.tokens -= AUTOBUY_LICENSE_COST;
    g.autoUnlocked = true;
    g.autoOn = true;
    return {{Sev::Good, "Bought the Auto-Buyer license. It's ON (toggle with 'auto off')."}};
}

Notes setAuto(GameState& g, bool on) {
    if (!g.autoUnlocked)
        return {{Sev::Warn, "The Auto-Buyer is locked. Buy its license in the shop (" + num(AUTOBUY_LICENSE_COST) + " tokens)."}};
    g.autoOn = on;
    return {{Sev::Good, on ? "Auto-Buyer ON." : "Auto-Buyer OFF."}};
}

// ----- Collectibles and cosmetics -----
Notes buyCurio(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Curios)) return {{Sev::Warn, lockedText(g, Feature::Curios)}};
    if (idx < 0 || idx >= CURIO_COUNT) return {{Sev::Warn, "No such curio."}};
    const CurioInfo& c = CURIOS[idx];
    if (hasBit(g.curiosOwned, idx)) return {{Sev::Info, "Already on your shelf."}};
    if (!inShop(idx)) return {{Sev::Info, "That one isn't sold in any shop. Maybe someone will offer it to you one day."}};
    if (g.tokens < c.cost) return {{Sev::Warn, "Not enough tokens."}};

    if (g.curiosOwned == 0 && idx == 0) spotSecret(g, Secret::RubberDuck);
    g.tokens -= c.cost;
    g.curiosOwned |= (1u << idx);
    Notes out{{Sev::Good, std::string("You bought the ") + c.name + ". \"" + c.flavor + "\""}};

    bool setDone = true;
    for (int i = 0; i < CURIO_COUNT; ++i)
        if (CURIOS[i].set == c.set && !hasBit(g.curiosOwned, i)) setDone = false;
    if (setDone) out.push_back({Sev::Event, std::string("*** Set complete: ") + CURIO_SETS[c.set] + " ***"});
    return out;
}

long long customPromptCost() { return 30 * PROMPTS[PROMPT_COUNT - 1].cost; }

bool validCustomPrompt(const std::string& text, std::string& why) {
    if (text.empty()) { why = "Type the prompt you want, e.g. 'prompt custom >_<'."; return false; }
    if (text.size() > CUSTOM_PROMPT_MAX) { why = "Keep it to " + std::to_string(CUSTOM_PROMPT_MAX) + " characters."; return false; }
    if (text.front() == ' ' || text.back() == ' ') { why = "No spaces at the start or end (one is added after it)."; return false; }
    for (char c : text) {
        unsigned char u = (unsigned char)c;
        if (u < 0x20 || u > 0x7E) { why = "Plain keyboard characters only (letters, digits, symbols)."; return false; }
    }
    return true;
}

Notes setCustomPrompt(GameState& g, const std::string& text) {
    std::string why;
    if (!validCustomPrompt(text, why)) return {{Sev::Warn, why}};
    Notes out;
    if (!g.customPromptOwned) {
        long long cost = customPromptCost();
        if (g.tokens < cost) return {{Sev::Warn, "A custom prompt costs " + num(cost) + " tokens."}};
        g.tokens -= cost;
        g.customPromptOwned = true;
        out.push_back({Sev::Good, "Bought the custom prompt for " + num(cost) + " tokens. Changing it is free from now on."});
    }
    g.customPrompt = text;
    g.promptSel = PROMPT_COUNT;
    out.push_back({Sev::Info, "Prompt set to \"" + text + "\"."});
    return out;
}

std::string promptOf(const GameState& g) {
    if (g.promptSel == PROMPT_COUNT && g.customPromptOwned && !g.customPrompt.empty()) return g.customPrompt + " ";
    return PROMPTS[std::clamp(g.promptSel, 0, PROMPT_COUNT - 1)].text;
}

Notes choosePrompt(GameState& g, int idx) {
    if (idx == PROMPT_COUNT) {
        if (!g.customPromptOwned) return {{Sev::Warn, "Make one first: 'prompt custom <text>' (" + num(customPromptCost()) + " tokens)."}};
        g.promptSel = PROMPT_COUNT;
        return {{Sev::Info, "Prompt style set."}};
    }
    if (idx < 0 || idx >= PROMPT_COUNT) return {{Sev::Warn, "No such prompt style."}};
    Notes out;
    if (!hasBit(g.promptsOwned, idx)) {
        if (g.tokens < PROMPTS[idx].cost) return {{Sev::Warn, "Not enough tokens."}};
        g.tokens -= PROMPTS[idx].cost;
        g.promptsOwned |= (1u << idx);
        out.push_back({Sev::Good, std::string("Bought prompt style \"") + PROMPTS[idx].text + "\"."});
    }
    g.promptSel = idx;
    out.push_back({Sev::Info, "Prompt style set."});
    return out;
}

// ----- Themes -----
bool themeOwned(const GameState& g, int idx) {
    return idx >= 0 && idx < THEME_COUNT && (THEMES[idx].cost == 0 || hasBit(g.themesOwned, idx));
}

Notes chooseTheme(GameState& g, int idx) {
    if (idx < 0 || idx >= THEME_COUNT) return {{Sev::Warn, "No such theme."}};
    const ThemeInfo& t = THEMES[idx];
    if (idx == g.themeSel) return {{Sev::Info, std::string("You're already using ") + t.name + "."}};
    Notes out;
    if (!themeOwned(g, idx)) {
        if (g.shards < t.cost) {
            return {{Sev::Warn, std::string(t.name) + " costs " + count(t.cost, "shard") + ". You have " + num(g.shards) + "."}};
        }
        g.shards -= t.cost;
        g.themesOwned |= 1u << idx;
        out.push_back({Sev::Good, std::string("Bought the ") + t.name + " theme for " + count(t.cost, "shard") + "."});
    }
    g.themeSel = idx;
    out.push_back({Sev::Info, std::string("Theme: ") + t.name + ". (It belongs to this profile.)"});
    return out;
}

// ----- The Workshop -----
int freeGadgetSlot(const GameState& g) {
    for (int i = 0; i < GADGET_SLOTS; ++i)
        if (g.gadgetSlots[(size_t)i] < 0) return i;
    return -1;
}

static bool equipped(const GameState& g, int idx) {
    return std::find(g.gadgetSlots.begin(), g.gadgetSlots.end(), idx) != g.gadgetSlots.end();
}

Notes craftGadget(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Workshop)) return {{Sev::Warn, lockedText(g, Feature::Workshop)}};
    if (idx < 0 || idx >= GADGET_COUNT) return {{Sev::Warn, "No such gadget."}};
    const GadgetInfo& x = GADGETS[idx];
    if (hasBit(g.gadgetsOwned, idx)) return {{Sev::Info, std::string("You already have the ") + x.name + "."}};
    if (!hasBit(g.curiosOwned, x.curio))
        return {{Sev::Warn, std::string("The ") + x.name + " is built from " + theCurio(x.curio) + ". Get that curio first."}};
    if (g.shards < x.cost) return {{Sev::Warn, std::string("The ") + x.name + " takes " + count(x.cost, "shard") + ". You have " + num(g.shards) + "."}};

    g.shards -= x.cost;
    g.gadgetsOwned |= 1u << idx;
    Notes out = {{Sev::Good, std::string("Built the ") + x.name + " (" + theCurio(x.curio) + " stays on your shelf). " +
                             "Better: " + x.plus + ". Worse: " + x.minus + "."}};
    if (int slot = freeGadgetSlot(g); slot >= 0) {
        g.gadgetSlots[(size_t)slot] = idx;
        out.push_back({Sev::Info, "It's equipped (slot " + std::to_string(slot + 1) + ")."});
    } else {
        out.push_back({Sev::Info, "All slots are taken, so it's on the bench. 'unequip' one to make room."});
    }
    return out;
}

Notes equipGadget(GameState& g, int idx) {
    if (!isUnlocked(g, Feature::Workshop)) return {{Sev::Warn, lockedText(g, Feature::Workshop)}};
    if (idx < 0 || idx >= GADGET_COUNT) return {{Sev::Warn, "No such gadget."}};
    const GadgetInfo& x = GADGETS[idx];
    if (!hasBit(g.gadgetsOwned, idx)) return {{Sev::Warn, std::string("You haven't built the ") + x.name + " yet ('craft')."}};
    if (equipped(g, idx)) return {{Sev::Info, std::string("The ") + x.name + " is already equipped."}};
    int slot = freeGadgetSlot(g);
    if (slot < 0) return {{Sev::Warn, "All " + std::to_string(GADGET_SLOTS) + " slots are taken. Unequip one first."}};
    g.gadgetSlots[(size_t)slot] = idx;
    return {{Sev::Good, std::string("Equipped the ") + x.name + ": " + x.plus + ", but " + x.minus + "."}};
}

Notes unequipGadget(GameState& g, int idx) {
    if (idx < 0 || idx >= GADGET_COUNT) return {{Sev::Warn, "No such gadget."}};
    for (int& slot : g.gadgetSlots) {
        if (slot != idx) continue;
        slot = -1;
        return {{Sev::Info, std::string("Took off the ") + GADGETS[idx].name + "."}};
    }
    return {{Sev::Info, std::string("The ") + GADGETS[idx].name + " isn't equipped."}};
}

// ----- The Orrery -----
long long orreryRemaining(const GameState& g) {
    return g.orreryStage < ORRERY_STAGE_COUNT ? ORRERY[g.orreryStage].cost - g.orreryProgress : 0;
}

long long orrerySpareShards(const GameState& g) {
    return std::max(0LL, g.shards - totalWages(g));   // one payday's worth stays
}

BuildResult buildOrrery(GameState& g, long long amount) {
    BuildResult r;
    if (!isUnlocked(g, Feature::Orrery)) { r.notes.push_back({Sev::Warn, lockedText(g, Feature::Orrery)}); return r; }
    if (g.orreryStage >= ORRERY_STAGE_COUNT) { r.notes.push_back({Sev::Info, "The Orrery is already complete."}); return r; }
    if (amount <= 0) { r.notes.push_back({Sev::Warn, "Nothing to build with. (The Orrery takes shards.)"}); return r; }
    if (amount > g.shards) { r.notes.push_back({Sev::Warn, "You only have " + count(g.shards, "shard") + "."}); return r; }

    long long left = amount;
    while (left > 0 && g.orreryStage < ORRERY_STAGE_COUNT) {
        const OrreryStage& st = ORRERY[g.orreryStage];
        long long put = std::min(left, st.cost - g.orreryProgress);
        g.orreryProgress += put;
        g.shards -= put;
        left -= put;

        if (g.orreryProgress >= st.cost) {
            g.orreryStage += 1;
            g.orreryProgress = 0;
            r.notes.push_back({Sev::Event, std::string("*** Orrery stage complete: ") + st.name + " ***"});
            r.notes.push_back({Sev::Event, std::string("Unlocked: ") + st.perk + "."});
            r.finished = (g.orreryStage == ORRERY_STAGE_COUNT);
        }
    }
    r.poured = amount - left;
    r.notes.push_back({Sev::Good, "Set " + count(r.poured, "shard") + " into the Orrery."});
    return r;
}

// ----- The Monolith -----
BuildResult buildMonolith(GameState& g, long long amount) {
    BuildResult r;
    if (!isUnlocked(g, Feature::Monolith)) { r.notes.push_back({Sev::Warn, lockedText(g, Feature::Monolith)}); return r; }
    if (g.monolithStage >= MONO_STAGE_COUNT) { r.notes.push_back({Sev::Info, "The Monolith is already complete."}); return r; }
    if (amount <= 0) { r.notes.push_back({Sev::Warn, "Nothing to build with."}); return r; }
    if (amount > g.tokens) { r.notes.push_back({Sev::Warn, "You only have " + num(g.tokens) + "."}); return r; }

    long long left = amount;
    while (left > 0 && g.monolithStage < MONO_STAGE_COUNT) {
        const MonoStage& m = MONO[g.monolithStage];
        long long put = std::min(left, m.cost - g.monolithProgress);
        g.monolithProgress += put;
        g.tokens -= put;
        left -= put;

        if (g.monolithProgress >= m.cost) {
            r.notes.push_back({Sev::Event, std::string("*** Stage complete: ") + m.name + " ***"});
            g.monolithStage += 1;
            g.monolithProgress = 0;
            r.finished = (g.monolithStage == MONO_STAGE_COUNT);
        }
    }
    r.poured = amount - left;
    r.notes.push_back({Sev::Good, "Poured " + num(r.poured) + " tokens into the Monolith."});
    return r;
}

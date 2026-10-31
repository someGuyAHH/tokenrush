// One function per tab. Each returns a component whose renderer draws from ui.snap
// (never locking), with buttons that change the real game through ui.run().

#include <algorithm>
#include <array>
#include <memory>
#include <bit>
#include <cmath>

#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/disasters.hpp"
#include "core/stars.hpp"
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/projection.hpp"
#include "core/util.hpp"
#include "shell/commands.hpp"
#include "console.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

namespace {

Element heading(const std::string& s) { return text(s) | bold | color(hue(Hue::Accent)); }
Element note(const std::string& s) { return paragraph(s) | muted(); }

bool armed(int idx, int armedIdx, Clock::time_point until) { return idx == armedIdx && Clock::now() < until; }

}  // namespace

// ----- Mine -----
Component MinePage(Ui& ui) {
    Component mineBtn = MineButton(ui);
    // Repair buttons for whatever a disaster broke (one per item, in repairs() order).
    constexpr int REPAIR_ROWS = 6;
    Components fixes;
    for (int i = 0; i < REPAIR_ROWS; ++i) {
        Component b = Btn("Repair", [&ui, i] { ui.run([i](GameState& g) { return repair(g, i); }); },
                          [&ui, i] {
                              std::vector<Repair> list = repairs(ui.snap);
                              return i < (int)list.size() && ui.snap.tokens >= list[(size_t)i].tokens && ui.snap.shards >= list[(size_t)i].shards;
                          });
        fixes.push_back(Maybe(b, [&ui, i] { return i < (int)repairs(ui.snap).size(); }));
    }
    Component box = Container::Vertical({mineBtn, Container::Vertical(fixes)});
    return Renderer(box, [&ui, mineBtn, fixes] {
        const GameState& g = ui.snap;
        std::time_t t = ui.snapTime;

        Element last = text("Click MINE (or press m) to start.") | muted();
        if (ui.hasMined) {
            const MineResult& r = ui.lastMine;
            Elements parts = {text("Last mine "), text("+" + num(r.gain)) | color(hue(Hue::Tokens)) | bold,
                              text("  (base " + std::to_string(r.base) + ", x" + fixed(r.mult, 2) + ")") | muted()};
            if (r.jackpot)    parts.push_back(text("  JACKPOT!") | color(hue(Hue::Event)) | bold);
            else if (r.bonus) parts.push_back(text("  bonus") | color(hue(Hue::Good)));
            if (r.meteor)     parts.push_back(text("  Meteor x2") | color(hue(Hue::Event)));
            last = hbox(parts);
        }

        int streak = g.streak;
        long long timeout = streakTimeout(g);
        long long since = g.lastMine ? (long long)std::difftime(t, g.lastMine) : timeout;
        if (since >= timeout) streak = 0;
        Element streakLine = text("Streak 0  (mine to start one)") | muted();
        if (streak > 0) {
            long long left = std::max(0LL, timeout - since);
            streakLine = hbox({text("Streak " + std::to_string(streak) + "  x" + fixed(1.0 + 0.05 * std::min(streak, 20), 2) + "  "),
                               gauge((float)left / (float)timeout) | size(WIDTH, EQUAL, 15) |
                                   color(pick(left <= 5, hue(Hue::Warn), hue(Hue::Good))) | track(),
                               text(" " + std::to_string(left) + "s") | muted()});
        }

        Elements info;
        int ev = activeEvent(g, t);
        if (ev != EV_NONE)
            info.push_back(hbox({text(std::string(EVENTS[ev].name) + ": ") | bold | color(hue(Hue::Event)),
                                 text(std::string(EVENTS[ev].desc) + " (" + std::to_string((long long)std::difftime(g.eventEnds, t)) + "s)")}));
        for (const Contract& c : g.contracts) {
            info.push_back(hbox({text("Job: " + contractText(c) + "  "), progress(contractProgress(g, c), contractGoal(c), 12),
                                 text("  " + duration(c.secondsLeft)) | color(pick(c.secondsLeft <= CONTRACT_WARN_AT, hue(Hue::Warn), hue(Hue::Text)))}));
        }

        Element rate = hbox({text("Earning "), text(perSecond(recentEarnRate(g))) | color(hue(Hue::Tokens)) | bold,
                             text(" tokens/s"), text("  (last " + std::to_string(EARN_WINDOW_SECONDS) + "s)") | muted()});
        std::string split = incomeSplit(g);

        Element stats = hbox({
            text("Base " + std::to_string(baseYield(g)) + "   Bonus " + percent(bonusChance(g), 0) + "   Jackpot " +
                 percent(jackpotChance(g)) + "   Passive " + perSecond(passiveRatePerSec(g)) + "/s" +
                 (g.bot > 0 ? "   Bot every " + std::to_string(botIntervalOf(g)) + "s" : "")) | muted(),
        });

        Elements broken;
        std::vector<Repair> list = repairs(g);
        if (!list.empty()) {
            broken.push_back(hbox({text("Broken by disasters") | bold | color(hue(Hue::Bad)),
                                   text(fixerSoften(g) < 1.0 ? "   (your Fixers got you a discount)" : "") | muted()}));
            for (size_t i = 0; i < list.size() && i < fixes.size(); ++i) {
                const Repair& r = list[i];
                Element cost = r.tokens > 0 ? hbox({tokens(r.tokens), text(" tokens")}) : hbox({shards(r.shards), text(" shards")});
                broken.push_back(hbox({cell(r.what, 28), text(r.effect) | muted() | size(WIDTH, EQUAL, 34), cost | size(WIDTH, EQUAL, 20),
                                       fixes[i]->Render()}));
            }
        }

        return vbox({
            text(""),
            mineBtn->Render() | hcenter,
            text(""),
            last | hcenter,
            streakLine | hcenter,
            text(""),
            rate | hcenter,
            split.empty() ? text("") : text(split) | muted() | hcenter,
            vbox(info) | hcenter,
            broken.empty() ? text("") : vbox(broken) | hcenter,
            filler(),
            stats | hcenter,
        });
    });
}

// ----- Shop -----
// A line of sample text in theme `t`'s own colors and background: the preview in the theme list.
static Element themeSwatch(int t) {
    auto part = [t](const std::string& s, Hue h) { return text(s) | color(hueOf(t, h)); };
    Element e = hbox({text(" "), part("Heading", Hue::Accent) | bold, text("  "), part("1,234", Hue::Tokens), text(" "),
                      part("56 sh", Hue::Shards), text("  "), part("good", Hue::Good), text(" "), part("bad", Hue::Bad), text(" "),
                      part("warn", Hue::Warn), text(" "), part("EVENT", Hue::Event) | bold, text(" "), part("heat", Hue::Heat),
                      text(" "), part("quiet", Hue::Muted), text(" "),
                      text(" Btn ") | bgcolor(hueOf(t, Hue::Button)) | color(hueOf(t, Hue::ButtonText)), text(" ")});
    if (themeClassic(t)) return e;
    return e | color(hueOf(t, Hue::Text)) | bgcolor(hueOf(t, Hue::Background));
}

// "x64, x128 at 10K": a Drone's or the Bot's milestone multiplier now, and the next one.
static std::string milestoneText(int level) {
    std::string next = "x" + compact((long long)milestoneMult(nextMilestone(level))) + " at " + compact(nextMilestone(level));
    return milestonesAt(level) == 0 ? next : "x" + compact((long long)milestoneMult(level)) + ", " + next;
}

Component ShopPage(Ui& ui) {
    // One row of buttons per upgrade: Buy, Buy 10, Buy 20. The bigger ones turn blue once you
    // can afford all of it; clicked earlier, they buy as many levels as you can afford.
    std::vector<Components> buys(4);
    Components rowsOfButtons;
    for (size_t i = 0; i < 4; ++i) {
        for (int n : {1, 10, 20}) {
            buys[i].push_back(Btn(n == 1 ? "Buy" : "Buy " + std::to_string(n),
                                  [&ui, i, n] { ui.run([i, n](GameState& g) { return buyUpgrades(g, i, n); }); },
                                  [&ui, i, n] { return ui.snap.tokens >= upgradesCost(ui.snap, i, n); }));
        }
        rowsOfButtons.push_back(Container::Horizontal(buys[i]));
    }
    Component license = Btn(
        [&ui] { return ui.snap.autoUnlocked ? (ui.snap.autoOn ? "Auto-Buyer: ON " : "Auto-Buyer: OFF") : "Buy license"; },
        [&ui] {
            ui.run([](GameState& g) { return g.autoUnlocked ? setAuto(g, !g.autoOn) : buyLicense(g); });
        },
        [&ui] { return ui.snap.autoUnlocked || ui.snap.tokens >= AUTOBUY_LICENSE_COST; });

    Components all = rowsOfButtons;
    all.push_back(license);
    Component box = Grid(all);

    return Renderer(box, [&ui, buys, license] {
        GameState g = ui.snap;   // upgrades() wants a non-const state
        auto ups = upgrades(g);
        // What each one does at your level, and what one more level adds to your income.
        bool oddsMaxed = bonusChance(g) >= 0.70 && jackpotChance(g) >= 0.20;
        std::string effects[] = {
            "+1 base yield (now " + num(baseYield(g)) + ")",
            oddsMaxed ? "bonus x" + compact((long long)bonusSize(g)) + ", jackpot x" + compact((long long)jackpotSize(g)) + ": +4x/+20x a level"
                      : "odds " + percent(bonusChance(g), 0) + "/" + percent(jackpotChance(g)) + ", and bigger payouts",
            "+1 Drone, " + perSecond(passiveRatePerSec(g)) + "/s now; " + milestoneText(g.drone),
            g.bot == 0 ? "mines on its own; x2 at 100" : "swings every " + std::to_string(botIntervalOf(g)) + "s; " + milestoneText(g.bot),
        };
        Elements rows = {hbox({heading("Upgrades"), text("   +/s is what one more level adds to your income, as you play now") | muted()}), text("")};
        for (size_t i = 0; i < ups.size(); ++i) {
            std::string gain = "+" + perSecond(std::max(0.0, upgradeGain(g, i))) + "/s";
            // The buttons sit right next to the numbers; what the level does goes last, where it can be long.
            rows.push_back(hbox({cell(ups[i].name, 12), cell("lvl " + std::to_string(*ups[i].lvl), 11),
                                 text(gain) | color(hue(Hue::Good)) | size(WIDTH, EQUAL, 11),
                                 tokens(ups[i].cost) | size(WIDTH, EQUAL, 16),
                                 buys[i][0]->Render(), text(" "), buys[i][1]->Render(), text(" "), buys[i][2]->Render(),
                                 text("   "), text(effects[i]) | muted() | flex}));
        }
        rows.push_back(text(""));
        rows.push_back(heading("Automation"));
        rows.push_back(hbox({cell("Auto-Buyer", 12),
                             cell(g.autoUnlocked ? "owned" : "cost " + num(AUTOBUY_LICENSE_COST), 38) ,
                             license->Render()}));
        rows.push_back(note("The Auto-Buyer buys your cheapest upgrade whenever you have " +
                            std::to_string(AUTOBUY_RESERVE) + "x its cost."));
        if (g.bot > 0) rows.push_back(note("Mining Bot mines every " + std::to_string(botIntervalOf(g)) + "s."));

        return vbox(rows) | yframe;
    });
}

// ----- Themes -----
Component ThemesPage(Ui& ui) {
    // Themes: Buy, Use, or In use. The row itself is a preview in that theme's colors.
    // The last one is the custom theme: bought once (it starts as a copy of the one in use), then edited.
    Components themeBtns;
    auto costOf = [](int i) { return i == CUSTOM_THEME ? CUSTOM_THEME_COST : THEMES[i].cost; };
    for (int i = 0; i < THEME_CHOICES; ++i) {
        themeBtns.push_back(Btn(
            [&ui, i, costOf] {
                if (i == ui.snap.themeSel) return std::string("In use");
                if (themeOwned(ui.snap, i)) return std::string("  Use ");
                return "Buy " + num(costOf(i)) + " sh";
            },
            [&ui, i] { ui.run([i](GameState& g) { return chooseTheme(g, i); }); },
            [&ui, i, costOf] { return i != ui.snap.themeSel && (themeOwned(ui.snap, i) || ui.snap.shards >= costOf(i)); }));
    }
    Component editBtn = Maybe(FlatBtn([] { return std::string("edit its colors"); }, [&ui] { ui.themeMode = 1; }),
                              [&ui] { return ui.snap.customThemeOwned; });

    Components all;
    for (int i = 0; i < THEME_COUNT; ++i) all.push_back(themeBtns[(size_t)i]);
    all.push_back(Container::Horizontal({themeBtns[CUSTOM_THEME], editBtn}));
    Component box = Grid(all);

    // Previews: the mouse over a theme's row (anywhere on it), or the keyboard on its button,
    // shows the whole screen in that theme until it moves away. Nothing is bought or switched.
    // (The custom theme only once you have it: before that it has no colors of its own.)
    auto rowBoxes = std::make_shared<std::array<Box, THEME_CHOICES>>();
    auto previewOf = [&ui, themeBtns, editBtn] {
        if (ui.themeMode == 1) return CUSTOM_THEME;   // the editor shows its colors everywhere
        int at = ui.hoverTheme;
        for (int i = 0; i < THEME_CHOICES && at < 0; ++i)
            if (themeBtns[(size_t)i]->Focused() || (i == CUSTOM_THEME && editBtn->Focused())) at = i;
        return at == CUSTOM_THEME && !ui.snap.customThemeOwned ? -1 : at;
    };
    box = CatchEvent(box, [&ui, rowBoxes, previewOf](Event e) {
        if (e.is_mouse()) {
            ui.hoverTheme = -1;
            for (int i = 0; i < THEME_CHOICES; ++i)
                if ((*rowBoxes)[(size_t)i].Contain(e.mouse().x, e.mouse().y)) ui.hoverTheme = i;
        }
        ui.previewTheme = previewOf();   // right away, so the next frame already shows it
        return false;                    // the buttons still get the event
    });

    Component list = Renderer(box, [&ui, themeBtns, editBtn, rowBoxes, previewOf] {
        ui.previewTheme = previewOf();   // the keyboard may have moved without an event reaching us
        const GameState& g = ui.snap;
        Elements rows;
        int shown = ui.previewTheme;
        Element hint = shown >= 0 && shown != g.themeSel
            ? text("   previewing " + std::string(themeName(shown)) + ": move away to go back" +
                   (themeOwned(g, shown) ? "" : ", or buy it to keep it")) | color(hue(Hue::Accent))
            : text("   bought with shards, kept by this profile; hover one to preview it") | muted();
        rows.push_back(hbox({heading("Themes"), hint}));
        rows.push_back(text(""));
        for (int i = 0; i < THEME_CHOICES; ++i) {
            bool custom = i == CUSTOM_THEME;
            Element look = custom && !g.customThemeOwned ? text(" every color picked by hand, starting from a theme you own ") | muted()
                                                         : themeSwatch(i);
            Element blurb = custom ? (g.customThemeOwned ? hbox({text("  "), editBtn->Render()}) : hbox({text("  "), paragraph("your own colors: an editor with a preview") | muted()}))
                                   : hbox({text("  "), paragraph(THEMES[i].blurb) | muted()});   // wraps, rather than cut off at 120 columns
            // The button gets a fixed width, so the blurbs start in one column whatever it says ("Use", "Buy 1,500 sh").
            Element row = hbox({text(i == shown ? "› " : "  ") | color(hue(Hue::Accent)), cell(themeName(i), 12) | bold, look,
                                text("  "), hbox({themeBtns[(size_t)i]->Render(), filler()}) | size(WIDTH, EQUAL, 15), blurb | flex});
            rows.push_back(row | reflect((*rowBoxes)[(size_t)i]));
        }
        return vbox(rows) | yframe;
    });
    return Container::Tab({list, ThemeEditor(ui)}, &ui.themeMode);
}

// ----- Market -----
// Your trading record: bought, sold, the net and what the spread took.
static Element ledger(const GameState& g) {
    if (g.shardsBought == 0 && g.shardsSold == 0) return text("");
    long long net = g.tokensFromSells - g.tokensOnBuys;
    auto avg = [](long long tokens, long long n) { return n > 0 ? "  avg " + fixed((double)tokens / (double)n, 1) : std::string(); };
    return vbox({
        heading("Your trading"),
        hbox({cell("Bought", 8), shards(g.shardsBought), text(" shards for "), tokens(g.tokensOnBuys), text(avg(g.tokensOnBuys, g.shardsBought)) | muted()}),
        hbox({cell("Sold", 8), shards(g.shardsSold), text(" shards for "), tokens(g.tokensFromSells), text(avg(g.tokensFromSells, g.shardsSold)) | muted()}),
        hbox({cell("Net", 8), text((net >= 0 ? "+" : "-") + num(net >= 0 ? net : -net)) | color(pick(net >= 0, hue(Hue::Good), hue(Hue::Bad))) | bold,
              text(" tokens   (" + num(g.tradeCost) + " went to the spread and price impact)") | muted()}),
    });
}

Component MarketPage(Ui& ui) {
    auto buyN = [&ui](long long n) { return [&ui, n] { ui.run([n](GameState& g) { return buyShards(g, n); }); }; };
    auto sellN = [&ui](long long n) { return [&ui, n] { ui.run([n](GameState& g) { return sellShards(g, n); }); }; };
    auto canBuy = [&ui](long long n) { return [&ui, n] { return n <= ui.snap.marketStock && buyCost(ui.snap, n) <= ui.snap.tokens; }; };
    auto canSell = [&ui](long long n) { return [&ui, n] { return ui.snap.shards >= n; }; };

    Components buyRow = {
        Btn("Buy 1", buyN(1), canBuy(1)), Btn("Buy 10", buyN(10), canBuy(10)), Btn("Buy 100", buyN(100), canBuy(100)),
        // A huge balance can buy shards at many times the price; then this asks for a second click.
        Btn([&ui] { return Clock::now() < ui.buyMaxArmedUntil ? "Sure? (pricey)" : "Buy max"; },
            [&ui] {
                bool pricy = pricyOrder(ui.snap, maxAffordable(ui.snap));
                if (pricy && Clock::now() >= ui.buyMaxArmedUntil) { ui.buyMaxArmedUntil = Clock::now() + CONFIRM_WINDOW; return; }
                ui.buyMaxArmedUntil = {};
                ui.run([](GameState& g) {
                    return maxAffordable(g) > 0 ? buyShards(g, maxAffordable(g)) : Notes{{Sev::Warn, "You can't afford a single shard."}};
                });
            },
            [&ui] { return maxAffordable(ui.snap) > 0; }),
    };
    Components sellRow = {
        Btn("Sell 1", sellN(1), canSell(1)), Btn("Sell 10", sellN(10), canSell(10)), Btn("Sell 100", sellN(100), canSell(100)),
        Btn("Sell all", [&ui] { ui.run([](GameState& g) {
                return g.shards > 0 ? sellShards(g, std::min(g.shards, MARKET_MAX_ORDER)) : Notes{{Sev::Warn, "You have no shards."}};
            }); }, [&ui] { return ui.snap.shards > 0; }),
    };
    // A box for any amount ("2000", "2k"), with its own Buy and Sell.
    Component amountBox = TextBox(&ui.marketAmount, &ui.marketCursor, "amount", nullptr, 12);
    ui.textBoxes.push_back(amountBox);
    auto amount = [&ui] { long long n = 0; return parseAmount(ui.marketAmount, n) ? n : 0LL; };
    Component customBuy = Btn(
        [&ui] { return Clock::now() < ui.customBuyArmedUntil ? "Sure? (pricey)" : "Buy"; },
        [&ui, amount] {
            long long n = amount();
            if (n <= 0) { ui.s.log.push(Sev::Warn, "Type an amount first, like 2000 or 2k."); return; }
            if (pricyOrder(ui.snap, n) && Clock::now() >= ui.customBuyArmedUntil) { ui.customBuyArmedUntil = Clock::now() + CONFIRM_WINDOW; return; }
            ui.customBuyArmedUntil = {};
            ui.run([n](GameState& g) { return buyShards(g, n); });
        },
        [&ui, amount] { long long n = amount(); return n > 0 && n <= MARKET_MAX_ORDER && n <= ui.snap.marketStock && buyCost(ui.snap, n) <= ui.snap.tokens; });
    Component customSell = Btn("Sell",
        [&ui, amount] {
            long long n = amount();
            if (n <= 0) { ui.s.log.push(Sev::Warn, "Type an amount first, like 2000 or 2k."); return; }
            ui.run([n](GameState& g) { return sellShards(g, n); });
        },
        [&ui, amount] { long long n = amount(); return n > 0 && n <= MARKET_MAX_ORDER && n <= ui.snap.shards; });
    Components customRow = {amountBox, customBuy, customSell};

    Component box = Grid({Container::Horizontal(buyRow), Container::Horizontal(sellRow), Container::Horizontal(customRow)});

    return Renderer(box, [&ui, buyRow, sellRow, amountBox, customBuy, customSell, amount] {
        const GameState& g = ui.snap;
        auto rowOf = [](const Components& cs) {
            Elements e;
            for (const Component& c : cs) { e.push_back(c->Render()); e.push_back(text(" ")); }
            return hbox(e);
        };

        Element chart = text("(the chart fills in as the market moves)") | muted();
        if (g.priceHistory.size() >= 2) {
            auto [lo, hi] = std::minmax_element(g.priceHistory.begin(), g.priceHistory.end());
            chart = hbox({sparkline(g.priceHistory), text("  low " + fixed(*lo, 1) + "  high " + fixed(*hi, 1)) | muted()});
        }

        return vbox({
            hbox({text("Price "), text(fixed(g.shardPrice, 1)) | bold, text("   buy at " + fixed(buyPrice(g), 1) +
                  "   sell at " + fixed(sellPrice(g), 1) + "   spread " + percent(marketSpread(g)))}),
            g.priceHistory.size() >= 2 ? hbox({text("Last " + duration((long long)g.priceHistory.size() * 5) + "  "), chart}) : chart,
            text(""),
            hbox({text("You have "), shards(g.shards), text(" shards and "), tokens(g.tokens), text(" tokens.")}),
            hbox({text("For sale "), text(num(g.marketStock)) | color(pick(g.marketStock > 0, hue(Hue::Shards), hue(Hue::Bad))) | bold,
                  text(" of " + num(marketStockMax(g)) + " shards   " + restockText(g)) | muted()}),
            text(""),
            rowOf(buyRow),
            text("        " + num(buyCost(g, 1)) + "     " + num(buyCost(g, 10)) + "      " + num(buyCost(g, 100)) +
                 "    (" + num(maxAffordable(g)) + " max)") | muted(),
            rowOf(sellRow),
            text("        " + num(sellValue(g, 1)) + "     " + num(sellValue(g, 10)) + "      " + num(sellValue(g, 100))) | muted(),
            text(""),
            hbox({text("Any amount "), amountBox->Render() | bgcolor(pick(amountBox->Focused(), hue(Hue::ButtonOff), hue(Hue::Panel))), text(" "), customBuy->Render(), text(" "), customSell->Render(),
                  [&] {
                      long long n = amount();
                      if (ui.marketAmount.empty()) return text("   e.g. 2000 or 2k") | muted();
                      if (n <= 0) return text("   that's not an amount") | color(hue(Hue::Warn));
                      if (n > MARKET_MAX_ORDER) return text("   the market takes up to " + num(MARKET_MAX_ORDER) + " at a time") | color(hue(Hue::Warn));
                      return text("   buy for " + num(buyCost(g, n)) + ", sell for " + num(sellValue(g, std::min(n, std::max(g.shards, 1LL))))) | muted();
                  }()}),
            text(""),
            g.heat > 0 ? text("Heat " + std::to_string(g.heat) + " adds " + percent(0.02 * g.heat) + " to the spread.") | color(hue(Hue::Heat))
                       : text(""),
            ledger(g),
            note("Drifts back toward " + fixed(SHARD_FAIR_PRICE, 0) + ". Each shard you trade pushes the price a little."),
        });
    });
}

// ----- Jobs (contracts) -----
static Color tierColor(Tier t) {
    switch (t) {
        case Tier::Burned:   return hue(Hue::Bad);
        case Tier::Wary:     return hue(Hue::Warn);
        case Tier::Friendly: return hue(Hue::Good);
        case Tier::Trusted:  return hue(Hue::Event);
        default:             return hue(Hue::Muted);
    }
}

// A contract's tip in its level's color: "Tight: needs a mine every 1.4s (lately one every 2.0s)."
static Element tipLine(const Contract& c, const std::string& indent) {
    if (c.tipLevel < 0) return text("");
    static const Hue hues[] = {Hue::Good, Hue::Accent, Hue::Warn, Hue::Heat, Hue::Bad};
    Hue h = hues[std::clamp(c.tipLevel, 0, (int)TipLevel::Count - 1)];
    return hbox({text(indent), text(std::string(TIP_NAMES[c.tipLevel]) + ": ") | bold | color(hue(h)), text(c.tip) | color(hue(h))});
}

// "The Syndicate (Friendly)", the tier in its color; without the tier when `withTier` is false.
static Element clientName(const GameState& g, int client, bool withTier = true) {
    Tier t = clientTier(g, client);
    if (!withTier) return text(CLIENTS[client].name);
    return hbox({text(CLIENTS[client].name), text(std::string(" (") + TIERS[(int)t].name + ")") | color(tierColor(t))});
}

Component JobsPage(Ui& ui) {
    constexpr int MAX_JOB_SLOTS = 2;
    Components abandons;
    for (int j = 0; j < MAX_JOB_SLOTS; ++j) {
        Component b = Btn(
            [&ui, j] { return armed(j, ui.abandonArmed, ui.abandonArmedUntil) ? "Really abandon?" : "Abandon"; },
            [&ui, j] {
                if (armed(j, ui.abandonArmed, ui.abandonArmedUntil)) {
                    ui.abandonArmed = -1;
                    ui.run([j](GameState& g) { return abandonContract(g, j); });
                } else {
                    ui.abandonArmed = j;
                    ui.abandonArmedUntil = Clock::now() + CONFIRM_WINDOW;
                }
            });
        abandons.push_back(Maybe(b, [&ui, j] { return j < (int)ui.snap.contracts.size(); }));
    }

    Components accepts;
    for (int i = 0; i < OFFER_COUNT; ++i) {
        Component b = Btn("Accept", [&ui, i] { ui.run([i](GameState& g) { return acceptContract(g, i); }); },
                          [&ui, i] { return (int)ui.snap.contracts.size() < contractSlots(ui.snap) && i < (int)ui.snap.offers.size() &&
                                            ui.snap.shards >= ui.snap.offers[(size_t)i].stake; });
        accepts.push_back(Maybe(b, [&ui, i] { return i < (int)ui.snap.offers.size(); }));
    }
    // A Bribe button for every client, shown only while they're Burned. Two clicks: it's not cheap.
    Components bribes;
    for (int k = 0; k < CLIENT_COUNT; ++k) {
        Component b = Btn(
            [&ui, k] { return armed(k, ui.bribeArmed, ui.bribeArmedUntil) ? "Really pay " + compact(bribeCost(ui.snap, k)) + "?"
                                                                           : "Bribe " + compact(bribeCost(ui.snap, k)) + " sh"; },
            [&ui, k] {
                if (armed(k, ui.bribeArmed, ui.bribeArmedUntil)) {
                    ui.bribeArmed = -1;
                    ui.run([k](GameState& g) { return bribeClient(g, k); });
                } else {
                    ui.bribeArmed = k;
                    ui.bribeArmedUntil = Clock::now() + CONFIRM_WINDOW;
                }
            },
            [&ui, k] { return ui.snap.shards >= bribeCost(ui.snap, k); });
        bribes.push_back(Maybe(b, [&ui, k] { return clientTier(ui.snap, k) == Tier::Burned; }));
    }

    Components all = abandons;
    for (auto& a : accepts) all.push_back(a);
    all.push_back(Container::Vertical(bribes));   // drawn two to a row; the arrows go through them in reading order
    Component box = Container::Vertical(all);

    return Renderer(box, [&ui, abandons, accepts, bribes] {
        const GameState& g = ui.snap;
        int slots = contractSlots(g);
        Elements rows;
        rows.push_back(hbox({heading("Contracts"), text("  " + std::to_string(g.contracts.size()) + "/" + std::to_string(slots)) | muted(),
                             text("   Heat "), heatBar(g.heat), text(" " + std::to_string(g.heat) + "/" + std::to_string(MAX_HEAT)),
                             text("   done " + num(g.contractsDone) + ", failed " + num(g.contractsFailed)) | muted()}));

        for (size_t j = 0; j < g.contracts.size() && j < abandons.size(); ++j) {
            const Contract& c = g.contracts[j];
            Elements pays = {text("pays "), text(compact(c.rewardTokens)) | color(hue(Hue::Tokens)), text(" + "), shards(c.rewardShards), text(" sh")};
            if (c.amends) pays = {text("no pay: wins them back") | color(hue(Hue::Good))};
            if (c.stake > 0) pays.push_back(text(" + stake") | muted());
            rows.push_back(hbox({text(slots > 1 ? "Job " + std::to_string(j + 1) + ": " : "Job: ") | bold, text(contractText(c)) | bold,
                                 c.signature ? text("  SIG") | color(hue(Hue::Event)) | bold
                                 : c.amends  ? text("  AMENDS") | color(hue(Hue::Good)) | bold : text(""),
                                 text("  for "), clientName(g, c.client), filler()}));
            if (isEndurance(c))
                rows.push_back(text(std::string("    Won by lasting until the end; fails on ") + CONTRACT_KINDS[(int)c.kind].rule + ".") |
                               color(hue(Hue::Warn)));
            rows.push_back(tipLine(c, "    "));
            rows.push_back(hbox({text("    "), progress(contractProgress(g, c), contractGoal(c), 14), text("  "),
                                 text(duration(c.secondsLeft)) | color(pick(c.secondsLeft <= CONTRACT_WARN_AT, hue(Hue::Warn), hue(Hue::Text))),
                                 text("   "), hbox(pays), filler(), abandons[j]->Render()}));
        }
        if (g.contracts.empty()) rows.push_back(text("No active contract. Pick one below.") | muted());
        else if ((int)g.contracts.size() < slots) rows.push_back(text("Room for one more job.") | muted());
        rows.push_back(heading("Offers"));

        for (int i = 0; i < OFFER_COUNT; ++i) {
            if (i >= (int)g.offers.size()) continue;
            const Contract& o = g.offers[(size_t)i];
            // The job's name at the margin, its details indented under it.
            Element tag = o.amends    ? text("  AMENDS") | color(hue(Hue::Good)) | bold
                        : o.signature ? text("  SIG") | color(hue(Hue::Event)) | bold
                        : o.big       ? text("  BIG") | color(hue(Hue::Warn)) | bold : text("");
            Element terms = hbox({o.amends ? text("no pay") | color(hue(Hue::Good))
                                           : hbox({text(compact(o.rewardTokens)) | color(hue(Hue::Tokens)), text(" + "), shards(o.rewardShards), text(" sh")}),
                                  o.stake > 0 ? text("  stake " + num(o.stake)) | color(hue(Hue::Shards)) : text("")});
            rows.push_back(hbox({hbox({text(contractText(o)) | bold, tag}) | flex, text("  "), cell((isEndurance(o) ? "for " : "in ") + duration(o.duration), 11),
                                 terms | size(WIDTH, EQUAL, 24), accepts[(size_t)i]->Render()}));
            rows.push_back(hbox({text("    from ") | muted(), clientName(g, o.client),
                                 text(std::string(", ") + CLIENT_STYLES[(int)CLIENTS[o.client].style].name + ", leaves in " + duration(o.expiresIn) +
                                      (isEndurance(o) ? std::string("   endurance: no ") + CONTRACT_KINDS[(int)o.kind].rule : "")) | muted()}));
            if (o.amends) rows.push_back(text("    no pay: deliver it and they're Wary of you instead of Burned") | color(hue(Hue::Good)));
            rows.push_back(tipLine(o, "    "));
        }
        if (g.offers.empty()) rows.push_back(text("  (the board is empty; check back soon)") | muted());

        // Standing with every client, three to a row.
        rows.push_back(text(""));
        rows.push_back(hbox({heading("Clients"), text("   standing -10..10: Burned, Wary, Neutral, Friendly, Trusted (pay 30% more, signature jobs)") | muted()}));
        for (int r = 0; r < CLIENT_COUNT; r += 3) {
            Elements line;
            for (int k = r; k < r + 3 && k < CLIENT_COUNT; ++k) {
                int v = g.standing[(size_t)k];
                line.push_back(hbox({clientName(g, k, false) | size(WIDTH, EQUAL, 22),
                                     text((v > 0 ? "+" : "") + std::to_string(v)) | size(WIDTH, EQUAL, 4) | color(tierColor(clientTier(g, k))),
                                     text(TIERS[(int)clientTier(g, k)].name) | size(WIDTH, EQUAL, 10) | color(tierColor(clientTier(g, k)))}));
            }
            rows.push_back(hbox(line));
        }
        // Burned clients: a bribe brings them back to Wary; so does a make-amends job, when they post one.
        std::vector<int> burned;
        for (int k = 0; k < CLIENT_COUNT; ++k)
            if (clientTier(g, k) == Tier::Burned) burned.push_back(k);
        if (!burned.empty()) {
            rows.push_back(hbox({text("Burned clients come back with a bribe, or by delivering a make-amends job when they post one.") | muted()}));
            for (size_t r = 0; r < burned.size(); r += 2) {
                Elements line;
                for (size_t q = r; q < r + 2 && q < burned.size(); ++q) {
                    int k = burned[q];
                    line.push_back(hbox({text("  "), clientName(g, k, false) | size(WIDTH, EQUAL, 22), bribes[(size_t)k]->Render(), text("   ")}));
                }
                rows.push_back(hbox(line));
            }
        }
        rows.push_back(filler());
        rows.push_back(note("Failing costs the stake and adds heat (big jobs +2). Heat " + std::to_string(auditMinHeat(g)) +
                            "+ invites audits."));
        return vbox(rows);
    });
}

// ----- Crew -----
Component CrewPage(Ui& ui) {
    Components trains, fires, hires, sends;
    for (int i = 0; i < MAX_CREW_SLOTS; ++i) {
        // Train until the level cap, then the same button promotes.
        auto atCap = [&ui, i] { const CrewMember& c = ui.snap.crew[(size_t)i]; return c.level >= levelCap(ui.snap, c); };
        Component tr = Btn(
            [&ui, i, atCap] { return i < (int)ui.snap.crew.size() && atCap() ? "Promote" : " Train "; },
            [&ui, i] { ui.run([i](GameState& g) { const CrewMember& c = g.crew[(size_t)i];
                                                   return c.level >= levelCap(g, c) ? promote(g, i) : train(g, i); }); },
            [&ui, i, atCap] {
                if (i >= (int)ui.snap.crew.size()) return false;
                const CrewMember& c = ui.snap.crew[(size_t)i];
                return ui.snap.shards >= (atCap() ? promoteCost(c) : trainCost(c));
            });
        Component fi = Btn(
            [&ui, i] { return armed(i, ui.fireArmed, ui.fireArmedUntil) ? "Sure?" : "Fire"; },
            [&ui, i] {
                if (armed(i, ui.fireArmed, ui.fireArmedUntil)) {
                    ui.fireArmed = -1;
                    ui.run([i](GameState& g) { return fire(g, i); });
                } else {
                    ui.fireArmed = i;
                    ui.fireArmedUntil = Clock::now() + CONFIRM_WINDOW;
                }
            });
        auto present = [&ui, i] { return i < (int)ui.snap.crew.size(); };
        Component se = Btn("Send", [&ui, i] { ui.run([&ui, i](GameState& g) { return sendExpedition(g, i, ui.expeditionTrip); }); },
                           [&ui, i] { return i < (int)ui.snap.crew.size() && !isAway(ui.snap.crew[(size_t)i]); });
        trains.push_back(Maybe(tr, present));
        fires.push_back(Maybe(fi, present));
        sends.push_back(Maybe(se, present));
    }
    Components trips;
    for (int t = 0; t < EXPEDITION_COUNT; ++t) {
        trips.push_back(Btn([&ui, t] { return std::string(t == ui.expeditionTrip ? "> " : "  ") + EXPEDITIONS[t].id + " " + duration(EXPEDITIONS[t].seconds); },
                            [&ui, t] { ui.expeditionTrip = t; }, [&ui, t] { return t == ui.expeditionTrip; }));
    }
    for (int i = 0; i < CANDIDATE_COUNT; ++i) {
        Component h = Btn("Hire", [&ui, i] { ui.run([i](GameState& g) { return hire(g, i); }); },
                          [&ui, i] { return i < (int)ui.snap.candidates.size() && (int)ui.snap.crew.size() < crewCap(ui.snap) &&
                                            ui.snap.shards >= hireCost(ui.snap, ui.snap.candidates[(size_t)i]); });
        hires.push_back(Maybe(h, [&ui, i] { return i < (int)ui.snap.candidates.size(); }));
    }
    Components rowsC;
    for (int i = 0; i < MAX_CREW_SLOTS; ++i) rowsC.push_back(Container::Horizontal({trains[(size_t)i], sends[(size_t)i], fires[(size_t)i]}));
    rowsC.push_back(Container::Horizontal(trips));
    for (auto& h : hires) rowsC.push_back(h);
    Component box = Grid(rowsC);

    return Renderer(box, [&ui, trains, fires, hires, sends, trips] {
        const GameState& g = ui.snap;
        Elements rows;
        Elements top = {heading("Crew " + std::to_string(g.crew.size()) + "/" + std::to_string(crewCap(g))),
                        text("   Shards "), shards(g.shards)};
        if (!g.crew.empty())
            top.push_back(text("   Payday in " + duration(g.paydayClock) + ": " + count(totalWages(g), "shard")) | muted());
        rows.push_back(hbox(top));

        if (g.crew.empty()) rows.push_back(text("  Nobody yet. Hire someone below.") | muted());
        for (size_t i = 0; i < g.crew.size() && i < (size_t)MAX_CREW_SLOTS; ++i) {
            const CrewMember& c = g.crew[i];
            bool capped = c.level >= levelCap(g, c);
            Element cost = hbox({shards(capped ? promoteCost(c) : trainCost(c)), text(" sh")}) | size(WIDTH, EQUAL, 11);
            rows.push_back(hbox({cell(c.name, 9) | bold, cell(ROLES[(int)c.role].name, 8),
                                 cell("L" + std::to_string(c.level) + "/" + std::to_string(levelCap(g, c)), 7),
                                 cell(starsText(c), 6) | color(hue(Hue::Tokens)) | bold, cell(traitName(c), 14) | color(hue(Hue::Accent)),
                                 text(crewEffectText(g, c)) | (isAway(c) ? muted() : nothing) | flex, text("  "), cost, trains[i]->Render(), text(" "),
                                 sends[i]->Render(), text(" "), fires[i]->Render()}));
        }
        // Expeditions: pick a length, then Send someone.
        {
            const ExpeditionInfo& e = EXPEDITIONS[ui.expeditionTrip];
            Elements tripRow = {text("Expedition ") | bold};
            for (const Component& t : trips) { tripRow.push_back(t->Render()); tripRow.push_back(text(" ")); }
            tripRow.push_back(text("  curio " + percent(e.curioChance, 0) + ", trait " + percent(e.traitChance, 0) + ", level " +
                                   percent(e.levelChance, 0) + ", ") | muted());
            tripRow.push_back(text("injury " + percent(injuryChance(g, ui.expeditionTrip), 0)) | color(hue(Hue::Warn)));
            rows.push_back(text(""));
            rows.push_back(hbox(tripRow));
            Elements loot = {text("  typical haul: ") | muted()};
            for (const CrewMember& c : g.crew)
                if (!isAway(c)) { loot.push_back(text(c.name + " ")); loot.push_back(shards(expeditionLoot(g, c, ui.expeditionTrip))); loot.push_back(text(" sh   ")); }
            rows.push_back(hbox(loot));
        }
        rows.push_back(text(""));
        rows.push_back(hbox({heading("Looking for work"), text("   new faces in " + duration(g.candidateClock)) | muted()}));
        for (size_t i = 0; i < g.candidates.size() && i < (size_t)CANDIDATE_COUNT; ++i) {
            const CrewMember& c = g.candidates[i];
            // What the role does first, then what the trait changes about them.
            std::string about = std::string(ROLES[(int)c.role].perk) + (c.trait != Trait::None ? std::string(" · ") + TRAITS[(int)c.trait].desc : "");
            rows.push_back(hbox({cell(c.name, 9), cell(ROLES[(int)c.role].name, 8), cell("L" + std::to_string(c.level), 7),
                                 cell("", 6), cell(traitName(c), 14) | color(hue(Hue::Accent)), text(about) | muted() | flex, text("  "),
                                 hbox({shards(hireCost(g, c)), text(" sh")}) | size(WIDTH, EQUAL, 8), hires[i]->Render()}));
        }
        rows.push_back(filler());
        rows.push_back(note("Payday: each member takes about their level in shards; unpaid crew quit. At the level cap, "
                            "Promote raises the cap by " + std::to_string(PROMO_LEVELS) + " and makes every level count " +
                            std::to_string((int)(PROMO_EFFECT * 100)) + "% more. Each promotion costs " +
                            fixed(PROMO_GROWTH, 0) + "x the last."));
        return vbox(rows);
    });
}

// ----- Curios -----
Component CuriosPage(Ui& ui) {
    Components buys;
    for (int i = 0; i < CURIO_COUNT; ++i) {
        Component b = Btn("Buy", [&ui, i] { ui.run([i](GameState& g) { return buyCurio(g, i); }); },
                          [&ui, i] { return ui.snap.tokens >= CURIOS[i].cost; });
        buys.push_back(Maybe(b, [&ui, i] { return inShop(i) && !hasBit(ui.snap.curiosOwned, i); }));
    }
    Component box = Container::Vertical(buys);

    return Renderer(box, [&ui, buys] {
        const GameState& g = ui.snap;
        Elements rows = {hbox({heading("Curios"), text("   shelf " + std::to_string(std::popcount(g.curiosOwned)) + "/" +
                                                       std::to_string(CURIO_COUNT)) | muted()})};
        for (int i = 0; i < CURIO_COUNT; ++i) {
            const CurioInfo& c = CURIOS[i];
            bool owned = hasBit(g.curiosOwned, i);
            Element right = owned       ? text("\"" + std::string(c.flavor) + "\"") | muted() | color(hue(Hue::Good)) | flex
                          : inShop(i)   ? hbox({tokens(c.cost) | size(WIDTH, EQUAL, 13), buys[(size_t)i]->Render(), filler()}) | flex
                                        : text("not in any shop: the Curio Dealer, or a lucky expedition") | muted() | flex;
            rows.push_back(hbox({cell(owned ? "[x]" : "[ ]", 4) | color(pick(owned, hue(Hue::Good), hue(Hue::Text))),
                                 cell(owned || inShop(i) ? c.name : "???", 28) | (owned ? bold : nothing), cell(CURIO_SETS[c.set], 19) | muted(), right}));
        }
        if (!g.keepsakes.empty()) {
            rows.push_back(text(""));
            rows.push_back(hbox({heading("Keepsakes"), text("   left by your crew") | muted()}));
            for (const Keepsake& k : g.keepsakes)
                rows.push_back(hbox({text("    "), cell(k.item, 44) | color(hue(Hue::Tokens)), text(k.why) | muted()}));
        }
        return vbox(rows) | yframe;
    });
}

// ----- Monolith -----
Component MonolithPage(Ui& ui) {
    auto build = [&ui](std::function<long long(const GameState&)> amount) {
        return [&ui, amount] { ui.run([amount](GameState& g) { return buildMonolith(g, amount(g)).notes; }); };
    };
    auto remaining = [](const GameState& g) {
        return g.monolithStage < MONO_STAGE_COUNT ? MONO[g.monolithStage].cost - g.monolithProgress : 0;
    };
    auto notDone = [&ui] { return ui.snap.monolithStage < MONO_STAGE_COUNT && ui.snap.tokens > 0; };
    Components btns = {
        Btn("Build 10%", build([](const GameState& g) { return std::max(1LL, g.tokens / 10); }), notDone),
        Btn("Build 50%", build([](const GameState& g) { return std::max(1LL, g.tokens / 2); }), notDone),
        Btn("Build all", build([](const GameState& g) { return g.tokens; }), notDone),
        Btn("Finish stage", build([remaining](const GameState& g) { return std::min(g.tokens, remaining(g)); }),
            [&ui, remaining] { return ui.snap.monolithStage < MONO_STAGE_COUNT && ui.snap.tokens >= remaining(ui.snap); }),
    };
    Component box = Container::Horizontal(btns);

    return Renderer(box, [&ui, btns] {
        const GameState& g = ui.snap;
        Elements art;
        for (int st = MONO_STAGE_COUNT - 1; st >= 0; --st) {
            bool built = st < g.monolithStage;
            for (int li = 0; li < 2 && MONO[st].art[li] != nullptr; ++li) {
                std::string line = MONO[st].art[li];
                if (!built) for (char& ch : line) if (ch != ' ') ch = '.';
                Element label = li == 0 ? text("  " + std::string(MONO[st].name) + (st == g.monolithStage ? "  (building)" : "")) : text("");
                Decorator look = built ? color(hue(Hue::Tokens)) : muted();
                art.push_back(hbox({cell(line, 26) | look, label | muted() | flex}));
            }
        }
        Elements btnRow;
        for (const Component& b : btns) { btnRow.push_back(b->Render()); btnRow.push_back(text(" ")); }

        Element status = text("Complete. It hums quietly.") | color(hue(Hue::Tokens)) | bold;
        if (g.monolithStage < MONO_STAGE_COUNT) {
            const MonoStage& m = MONO[g.monolithStage];
            status = hbox({text("Stage " + std::to_string(g.monolithStage + 1) + "/" + std::to_string(MONO_STAGE_COUNT) + ": " + m.name + "  "),
                           progress(g.monolithProgress, m.cost, 24)});
        }
        return vbox({vbox(art), text(""), status, hbox(btnRow)});
    });
}

// ----- Orrery -----
static constexpr int ORRERY_ART_WIDTH = 46;

static Color orreryColor(int stage) {
    switch (stage) {
        case 0:  return Color::Tan;            // Base Plate
        case 1:  return hue(Hue::Shards);   // First Ring
        case 2:  return Color::SteelBlue1;     // Second Ring
        case 3:  return Color::Grey70;         // Gearwork
        default: return hue(Hue::Tokens);          // The Sun
    }
}

static Element orreryRun(const ArtRun& run) {
    Element e = text(run.text);
    if (run.kind == ArtKind::Blank) return e;
    if (!run.built) return e | muted();
    switch (run.kind) {
        case ArtKind::Planet: return e | color(Color::LightSalmon1) | bold;
        case ArtKind::Star:   return e | color(Color::LightGoldenrod1);
        case ArtKind::Core:   return e | color(hue(Hue::Tokens)) | bold;
        default:              return e | color(orreryColor(run.stage));
    }
}

Component OrreryPage(Ui& ui) {
    auto build = [&ui](std::function<long long(const GameState&)> amount) {
        return [&ui, amount] { ui.run([amount](GameState& g) { return buildOrrery(g, amount(g)).notes; }); };
    };
    auto open = [&ui] { return ui.snap.orreryStage < ORRERY_STAGE_COUNT && ui.snap.orreryAssembly == 0 && ui.snap.shards > 0; };
    Components btns = {
        Btn("Add 100", build([](const GameState& g) { return std::min(100LL, g.shards); }), open),
        Btn("Add 1,000", build([](const GameState& g) { return std::min(1000LL, g.shards); }), open),
        Btn("Add all", build([](const GameState& g) { return orrerySpareShards(g); }), open),   // keeps payday wages
        Btn("Finish stage", build([](const GameState& g) { return std::min(g.shards, orreryRemaining(g)); }),
            [&ui] { return ui.snap.orreryStage < ORRERY_STAGE_COUNT && ui.snap.orreryAssembly == 0 && ui.snap.shards >= orreryRemaining(ui.snap); }),
    };
    Component box = Container::Horizontal(btns);

    return Renderer(box, [&ui, btns] {
        const GameState& g = ui.snap;
        int labelOf[ORRERY_ART_ROWS];
        std::fill(std::begin(labelOf), std::end(labelOf), -1);
        for (int st = 0; st < ORRERY_STAGE_COUNT; ++st) labelOf[ORRERY[st].labelRow] = st;

        Elements art;
        int r = 0;
        for (const std::vector<ArtRun>& runs : orreryPicture(g.orreryStage)) {
            Elements line;
            for (const ArtRun& run : runs) line.push_back(orreryRun(run));
            Element label = text("");
            if (int st = labelOf[r]; st >= 0) {
                const OrreryStage& o = ORRERY[st];
                label = st < g.orreryStage
                    ? hbox({cell(o.name, 13) | bold, text(o.perk) | color(hue(Hue::Good)) | flex})
                    : hbox({cell(o.name, 13) | muted(), text(num(o.cost) + " sh: " + o.perk) | muted() | flex});
            }
            art.push_back(hbox({hbox(line) | size(WIDTH, EQUAL, ORRERY_ART_WIDTH), label | flex}));
            ++r;
        }
        Elements btnRow;
        for (const Component& b : btns) { btnRow.push_back(b->Render()); btnRow.push_back(text(" ")); }

        Element status = text("Complete. The little sun turns, and so does everything else.") | color(hue(Hue::Shards)) | bold;
        if (g.orreryStage < ORRERY_STAGE_COUNT) {
            const OrreryStage& st = ORRERY[g.orreryStage];
            status = hbox({text("Stage " + std::to_string(g.orreryStage + 1) + "/" + std::to_string(ORRERY_STAGE_COUNT) + ": " + st.name + "  "),
                           progress(g.orreryProgress, st.cost, 24), text("   you have "), shards(g.shards), text(" shards")});
            if (g.orreryAssembly > 0)
                status = vbox({status, hbox({text("Assembling: " + duration(g.orreryAssembly) + " left") | color(hue(Hue::Shards)) | bold,
                                             text("  (while the game is open; then its perk works and the next stage takes shards)") | muted()})});
        }
        if (Terminal::Size().dimy < 30) {   // short terminal: the picture comes first
            return vbox({vbox(art), status, hbox(btnRow)});
        }
        return vbox({vbox(art), text(""), status, hbox(btnRow), filler(),
                     note("Built with shards: buy them on the Market (big orders push the price up, and it only has so many), or let Scouts "
                          "and contracts bring them in. A funded stage takes " + duration(orreryBuildSeconds(0)) + " to assemble, " +
                          std::to_string(ORRERY_BUILD_GROWTH) + "x more each stage after.")});
    });
}

// ----- Workshop -----
Component WorkshopPage(Ui& ui) {
    Components btns;
    for (int i = 0; i < GADGET_COUNT; ++i) {
        auto equippedNow = [&ui, i] { return gadgetEquipped(ui.snap, (Gadget)i); };
        btns.push_back(Btn(
            [&ui, i, equippedNow] {
                if (!hasBit(ui.snap.gadgetsOwned, i)) return "Build " + num(GADGETS[i].cost) + " sh";
                if (hasBit(ui.snap.gadgetsBroken, i)) return "Repair " + compact(gadgetRepairShards(ui.snap, i)) + " sh";
                return std::string(equippedNow() ? "Unequip" : " Equip ");
            },
            [&ui, i] {
                ui.run([i](GameState& g) {
                    if (!hasBit(g.gadgetsOwned, i)) return craftGadget(g, i);
                    if (hasBit(g.gadgetsBroken, i)) return repair(g, repairIndexOf(g, Repair::Gadget, i));
                    return gadgetEquipped(g, (Gadget)i) ? unequipGadget(g, i) : equipGadget(g, i);
                });
            },
            [&ui, i, equippedNow] {
                const GameState& g = ui.snap;
                if (!hasBit(g.gadgetsOwned, i)) return hasBit(g.curiosOwned, GADGETS[i].curio) && g.shards >= GADGETS[i].cost;
                if (hasBit(g.gadgetsBroken, i)) return g.shards >= gadgetRepairShards(g, i);
                return equippedNow() || freeGadgetSlot(g) >= 0;
            }));
    }
    Component box = Container::Vertical(btns);

    return Renderer(box, [&ui, btns] {
        const GameState& g = ui.snap;
        Elements slots = {heading("Workshop"), text("   slots ")};
        for (int i = 0; i < GADGET_SLOTS; ++i) {
            int x = g.gadgetSlots[(size_t)i];
            slots.push_back(text("[") | muted());
            bool dead = x >= 0 && hasBit(g.gadgetsBroken, x), off = x >= 0 && g.gadgetsUnplugged > 0;
            slots.push_back(x < 0 ? text("empty") | muted()
                            : dead ? text(GADGETS[x].name + std::string(" (broken)")) | color(hue(Hue::Bad)) | bold
                            : off  ? text(GADGETS[x].name + std::string(" (off)")) | color(hue(Hue::Warn))
                                   : text(GADGETS[x].name) | color(hue(Hue::Good)) | bold);
            slots.push_back(text("] ") | muted());
        }
        slots.push_back(text("   shards ") | muted());
        slots.push_back(shards(g.shards));
        Elements rows = {hbox(slots)};
        for (const Note& n : gadgetTrouble(g))   // a Short Circuit coming, unplugged, burnt out
            rows.push_back(paragraph(n.text) | color(hue(n.sev == Sev::Bad ? Hue::Bad : Hue::Warn)) | (n.sev == Sev::Bad ? bold : nothing));
        rows.push_back(text(""));
        for (int i = 0; i < GADGET_COUNT; ++i) {
            const GadgetInfo& x = GADGETS[i];
            bool built = hasBit(g.gadgetsOwned, i), on = gadgetEquipped(g, (Gadget)i), curio = hasBit(g.curiosOwned, x.curio);
            bool dead = hasBit(g.gadgetsBroken, i);
            Element name = text(x.name) | bold |   // red when burnt out: the line above says so, and its button repairs it
                           color(dead ? hue(Hue::Bad) : on ? hue(Hue::Good) : built ? hue(Hue::Text) : hue(Hue::Muted));
            Element right = curio || built ? btns[(size_t)i]->Render() : text("needs its curio") | muted();
            rows.push_back(hbox({name | size(WIDTH, EQUAL, 16), text("+ ") | color(hue(Hue::Good)), text(x.plus) | flex, text(" "), right}));
            rows.push_back(hbox({text("from " + theCurio(x.curio)) | muted() | size(WIDTH, EQUAL, 34), text("- ") | color(hue(Hue::Bad)),
                                 text(x.minus) | flex}));
        }
        Element hint = note("Each gadget is built once from a curio you own (it stays on the shelf) and some shards. Equip up to " +
                            std::to_string(GADGET_SLOTS) + "; swap them whenever you like, for free.");
        return vbox({vbox(rows) | yframe | flex, hint});
    });
}

// ----- The Constellations -----
Component StarsPage(Ui& ui) {
    return Renderer([&ui] {
        const GameState& g = ui.snap;
        Elements sky;
        for (const std::vector<SkyGlyph>& row : skyMap(g)) {
            Elements line;
            for (const SkyGlyph& c : row) {
                Element e = text(c.text);
                if (c.kind == SkyCell::Dust) e = e | muted();
                else if (c.kind != SkyCell::Empty) {
                    e = c.lit ? e | color(hue(CONSTELLATIONS[c.constellation].hue)) : e | muted();
                    if (c.lit && c.kind == SkyCell::Star) e = e | bold;
                }
                line.push_back(e);
            }
            sky.push_back(hbox(line));
        }
        Elements list;
        for (int i = 0; i < CONSTELLATION_COUNT; ++i) {
            const ConstellationInfo& c = CONSTELLATIONS[i];
            bool lit = hasBit(g.starsLit, i);
            std::string progress = lit ? "" : starProgress(g, i);
            list.push_back(hbox({text(lit ? "[*] " : "[ ] ") | (lit ? color(hue(c.hue)) : muted()),
                                 cell(c.name, 15) | bold | (lit ? color(hue(c.hue)) : nothing), text(c.feat) | (lit ? muted() : nothing),
                                 text(progress.empty() ? "" : "   " + progress) | muted()}));
        }
        return vbox({
            hbox({heading("The Constellations"), text("   " + std::to_string(std::popcount(g.starsLit)) + "/" + std::to_string(CONSTELLATION_COUNT) + " lit") | muted()}),
            text(""),
            vbox(sky) | hcenter,
            text(""),
            vbox(list) | yframe | flex,   // the sky keeps its size; the list scrolls on a short terminal
            note("They give nothing but the sky. Each one is a feat you pulled off."),
        });
    });
}

// ----- Goals -----
// Every title and what it opens: reached ones in green, the current one marked.
static Element titleLadder(int renown) {
    int ti = titleIndex(renown);
    Elements rows = {heading("Titles")};
    for (int t = 0; t < TITLE_COUNT; ++t) {
        std::string opens;
        for (int f = 0; f < FEATURE_COUNT; ++f)
            if (FEATURES[f].title == t) opens += (opens.empty() ? "" : ", ") + std::string(FEATURES[f].name);
        if (t == TITLE_COUNT - 1) opens = "every goal done";
        bool reached = t <= ti;
        Element row = hbox({text(t == ti ? "> " : "  "), cell(TITLES[t].name, 16) | bold,
                            cell(std::to_string(TITLES[t].renown), 4), text(opens.empty() ? "" : "  " + opens)});
        rows.push_back(reached ? row | color(hue(Hue::Good)) : row | muted());
    }
    return vbox(rows) | size(WIDTH, EQUAL, 42);
}

Component GoalsPage(Ui& ui) {
    Component list = Renderer([&ui](bool focused) {
        const GameState& g = ui.snap;
        int renown = renownOf(g);
        int ti = titleIndex(renown);
        Elements rows;
        for (int i = 0; i < GOAL_COUNT; ++i) {
            const Goal& goal = GOALS[i];
            bool done = hasBit(g.goalsDone, i);
            int needs = featureOfStat(goal.stat);
            bool locked = !done && needs >= 0 && !isUnlocked(g, (Feature)needs);
            Element right = done   ? text("+" + std::to_string(goal.renown) + " renown")
                          : locked ? text("opens at " + std::string(TITLES[FEATURES[needs].title].name)) | muted()
                                   : progress(std::min(statValue(g, goal.stat), goal.target), goal.target, 12);
            Element row = hbox({cell(done ? "[x]" : "[ ]", 4), cell(goal.name, 16) | bold, cell(goal.desc, 34), right});
            if (done) row = row | color(hue(Hue::Good));
            else if (locked) row = row | muted();
            if (i == ui.goalSelected) row = row | focus | (focused ? inverted : nothing);
            rows.push_back(row);
        }
        // Secret goals, under the others: a hint until found.
        rows.push_back(text(""));
        rows.push_back(hbox({heading("Secret goals"), text("   " + std::to_string(std::popcount(g.secretsDone)) + "/" + std::to_string(SECRET_COUNT) +
                                                            " found; they pay shards, not renown") | muted()}));
        for (int i = 0; i < SECRET_COUNT; ++i) {
            const SecretGoal& sg = SECRETS[i];
            bool done = hasBit(g.secretsDone, i);
            Element row = done ? hbox({cell("[x]", 4), cell(sg.name, 22) | bold, cell(sg.desc, 58), text("+" + std::to_string(sg.shards) + " sh")}) | color(hue(Hue::Good))
                               : hbox({cell("[?]", 4), cell("???", 22), text(sg.hint)}) | muted();
            int at = GOAL_COUNT + i;   // the selection runs on through the secrets
            if (at == ui.goalSelected) row = row | focus | (focused ? inverted : nothing);
            rows.push_back(row);
        }
        std::string next = ti + 1 < TITLE_COUNT ? "   next: " + std::string(TITLES[ti + 1].name) + " at " + std::to_string(TITLES[ti + 1].renown) : "";
        Element goals = vbox(rows) | vscroll_indicator | yframe | flex;
        if (Terminal::Size().dimx >= 125) goals = hbox({goals, text("   "), titleLadder(renown)});
        return vbox({
            hbox({heading("Goals " + std::to_string(std::popcount(g.goalsDone)) + "/" + std::to_string(GOAL_COUNT)),
                  text("   Renown " + std::to_string(renown) + "   Title "), text(TITLES[ti].name) | bold, text(next) | muted()}),
            goals,
        });
    });
    return CatchEvent(list, [&ui](Event e) {
        int before = ui.goalSelected;
        if (e == Event::ArrowDown || (e.is_mouse() && e.mouse().button == Mouse::WheelDown)) ++ui.goalSelected;
        else if (e == Event::ArrowUp || (e.is_mouse() && e.mouse().button == Mouse::WheelUp)) --ui.goalSelected;
        else return false;
        ui.goalSelected = std::clamp(ui.goalSelected, 0, GOAL_COUNT + SECRET_COUNT - 1);
        return ui.goalSelected != before;   // at the ends, let the arrow move focus out of the list
    });
}

// ----- Stats -----
static std::string playTime(long long secs) {
    if (secs < 0) return "before records";
    long long h = secs / 3600, m = (secs / 60) % 60;
    return h > 0 ? std::to_string(h) + "h " + std::to_string(m) + "m" : std::to_string(m) + "m";
}

Component StatsPage(Ui& ui) {
    return Renderer([&ui] {
        const GameState& g = ui.snap;
        auto row = [](const std::string& label, Element value) { return hbox({cell(label, 17) | muted(), value}); };
        auto plain = [](const std::string& v) { return text(v); };

        Elements wealth = {heading("Wealth"),
                           row("Tokens", tokens(g.tokens)),
                           row("Earned in total", tokens(g.lifetimeEarned)),
                           row("Earning now", plain(perSecond(recentEarnRate(g)) + "/s")),
                           row("Est. Income", plain(perSecond(estimatedIncome(g, std::time(nullptr))) + "/s")),
                           row("Best ever", plain(perSecond((double)g.bestEarnRate) + "/s"))};
        if (isUnlocked(g, Feature::Market)) wealth.push_back(row("Shards", shards(g.shards)));

        Elements mining = {heading("Mining"),
                           row("By hand", plain(num(g.manualMines) + " times")),
                           row("Jackpots", plain(num(g.jackpots))),
                           row("Biggest jackpot", g.biggestJackpot > 0 ? tokens(g.biggestJackpot) : plain("none yet")),
                           row("Best streak", plain(num(g.bestStreak))),
                           row("Mining Bot", tokens(g.botTotal))};
        Elements business;
        if (isUnlocked(g, Feature::Market)) {
            business.push_back(row("Shards traded", plain(num(g.shardsTraded))));
            long long net = g.tokensFromSells - g.tokensOnBuys;
            business.push_back(row("Trading net", text((net >= 0 ? "+" : "-") + compact(net >= 0 ? net : -net)) |
                                                      color(pick(net >= 0, hue(Hue::Good), hue(Hue::Bad)))));
        }
        if (isUnlocked(g, Feature::Contracts)) {
            business.push_back(row("Contracts", plain(num(g.contractsDone) + " done, " + num(g.contractsFailed) + " failed")));
            business.push_back(row("Audits", plain(num(g.audits))));
        }
        if (isUnlocked(g, Feature::Crew)) {
            business.push_back(row("Crew mined", tokens(g.crewMined)));
            business.push_back(row("Expeditions", plain(num(g.expeditionsDone))));
            business.push_back(row("Disasters", plain(num(g.disastersStopped) + " stopped, " + num(g.disastersHit) + " hit")));
        }
        if (!business.empty()) {
            mining.push_back(text(""));
            mining.push_back(heading("Business"));
            mining.insert(mining.end(), business.begin(), business.end());
        }

        // Where the money comes from: all time, with a share bar, and right now.
        Elements income = {hbox({heading("Income by source"), text("   all time, and now") | muted()})};
        {
            auto rates = recentSourceRates(g);
            long long sorted = 0;
            for (long long v : g.earnedBy) sorted = addTokens(sorted, v);
            double total = std::max(1.0, (double)g.lifetimeEarned);
            for (int i = 0; i < SOURCE_COUNT; ++i) {
                long long v = g.earnedBy[(size_t)i];
                if (v <= 0 && rates[(size_t)i] <= 0.0) continue;
                float share = (float)((double)v / total);
                income.push_back(hbox({cell(sourceName((Source)i), 17) | muted(), text(compact(v)) | color(hue(Hue::Tokens)) | size(WIDTH, EQUAL, 8),
                                       gauge(share) | size(WIDTH, EQUAL, 14) | color(hue(Hue::Tokens)) | track(), text(" " + percent(share, 0)) | size(WIDTH, EQUAL, 6),
                                       text("now " + perSecond(rates[(size_t)i]) + "/s") | muted()}));
            }
            if (g.lifetimeEarned > sorted)
                income.push_back(hbox({cell("Before records", 17) | muted(), text(compact(g.lifetimeEarned - sorted)) | muted()}));
        }

        Elements time = {heading("Time"), row("Played", plain(playTime(g.playSeconds)))};
        int ti = titleIndex(renownOf(g));
        for (int t = 1; t <= ti; ++t) time.push_back(row(TITLES[t].name, plain("at " + playTime(g.titleReachedAt[t]))));
        if (!g.formerCrew.empty()) {
            time.push_back(text(""));
            time.push_back(hbox({heading("Former crew"), text("  " + std::to_string(g.formerCrew.size())) | muted()}));
            size_t from = g.formerCrew.size() > 6 ? g.formerCrew.size() - 6 : 0;
            for (size_t i = g.formerCrew.size(); i-- > from;) {
                const FormerCrew& c = g.formerCrew[i];
                time.push_back(hbox({cell(c.name, 8) | bold, cell(std::string(ROLES[(int)c.role].name) + " L" + std::to_string(c.level), 11),
                                     text(c.how) | muted()}));
            }
        }

        int width = std::max(20, Terminal::Size().dimx - 12);
        int height = std::clamp(Terminal::Size().dimy - 32, 3, 14);
        Element chart = text("The chart fills in as you play: a point every 5 minutes.") | muted();
        std::vector<long long> cols = historyColumns(g, width);
        if (g.history.size() >= 2 && cols.size() >= 2) {
            Elements lines;
            std::vector<std::string> rows = logChart(cols, height);
            for (size_t r = 0; r < rows.size(); ++r) {
                std::string label = r == 0 ? compact(cols.back()) : r + 1 == rows.size() ? compact(std::max(1LL, cols.front())) : "";
                lines.push_back(hbox({text(label) | size(WIDTH, EQUAL, 8) | muted(), text(" "), text(rows[r]) | color(hue(Hue::Tokens))}));
            }
            lines.push_back(hbox({text("         " + playTime(g.history.front().playSeconds)) | muted(), filler(),
                                  text(playTime(g.playSeconds)) | muted()}));
            chart = vbox({hbox({text("Earned in total, over your play time") | bold, text("  (log scale)") | muted()}), vbox(lines)});
        }

        return vbox({
            hbox({vbox(wealth) | flex, text("  "), vbox(mining) | flex, text("  "), vbox(time) | flex}),
            text(""),
            vbox(income),
            text(""),
            chart,
        });
    });
}

// ----- Console -----
// The typed commands in a tab: shell/dispatch.cpp runs each line on the Console's own thread (console.cpp).
// The output scrolls: PgUp/PgDn or the mouse wheel go back through it, typing comes back down.
Component ConsolePage(Ui& ui) {
    // What the line would complete to (shell/complete.cpp), from the snapshot: greyed out, Tab takes it.
    auto suggestion = [&ui] { return ui.console ? completion(ui.snap, ui.console->history(), ui.consoleLine) : std::string(); };
    Component input = TextBox(&ui.consoleLine, &ui.consoleCursor, "", [&ui] {
        if (ui.console) ui.console->submit(ui.consoleLine);
        ui.consoleLine.clear();
        ui.consoleCursor = 0;
        ui.historyAt = -1;
        ui.consoleScroll = 0;   // a new command: back to the bottom to see what it says
    }, 0, suggestion);
    ui.textBoxes.push_back(input);

    auto page = [] { return std::max(3, Terminal::Size().dimy - 22); };   // about what the tab shows at once

    Component withKeys = CatchEvent(input, [&ui, page, suggestion](Event e) {
        if (!ui.console) return false;
        // Tab (or Right at the end of the line) takes the suggestion. Tab never leaves the line.
        bool atEnd = ui.consoleCursor >= (int)ui.consoleLine.size();
        if (e == Event::Tab || (e == Event::ArrowRight && atEnd)) {
            std::string more = suggestion();
            if (more.empty()) return e == Event::Tab;
            ui.consoleLine += more;
            ui.consoleCursor = (int)ui.consoleLine.size();
            ui.historyAt = -1;
            return true;
        }
        // Scrolling back through the output.
        int before = ui.consoleScroll;
        if (e == Event::PageUp) ui.consoleScroll += page();
        else if (e == Event::PageDown) ui.consoleScroll -= page();
        else if (e.is_mouse() && e.mouse().button == Mouse::WheelUp) ui.consoleScroll += 3;
        else if (e.is_mouse() && e.mouse().button == Mouse::WheelDown) ui.consoleScroll -= 3;
        else if (e == Event::End && ui.consoleScroll > 0) ui.consoleScroll = 0;
        if (ui.consoleScroll != before || e == Event::PageUp || e == Event::PageDown) {
            ui.consoleScroll = std::max(0, ui.consoleScroll);   // the top is clamped when drawing
            return true;
        }
        if (e.is_character()) ui.consoleScroll = 0;   // typing: back to the prompt

        // Up and Down walk through the lines typed before, skipping any that are the same as the one showing.
        if (e != Event::ArrowUp && e != Event::ArrowDown) return false;
        std::vector<std::string> h = ui.console->history();
        if (h.empty()) return true;
        int step = e == Event::ArrowUp ? -1 : 1;
        int at = ui.historyAt < 0 ? (int)h.size() : ui.historyAt;
        do at += step;
        while (at >= 0 && at < (int)h.size() && h[(size_t)at] == ui.consoleLine);
        if (at >= (int)h.size()) { ui.historyAt = -1; ui.consoleLine.clear(); }
        else if (at < 0) { /* already at the oldest different line */ }
        else { ui.historyAt = at; ui.consoleLine = h[(size_t)at]; }
        ui.consoleCursor = (int)ui.consoleLine.size();
        return true;
    });

    return Renderer(withKeys, [&ui, input] {
        if (!ui.console) return text("");
        std::vector<std::string> lines = ui.console->lines(1000);
        size_t total = ui.console->total();
        // While scrolled back, new output must not drag the view along: move the mark up with it.
        if (ui.consoleScroll > 0 && total > ui.consoleSeen) ui.consoleScroll += (int)(total - ui.consoleSeen);
        ui.consoleSeen = total;
        ui.consoleScroll = std::clamp(ui.consoleScroll, 0, std::max(0, (int)lines.size() - 1));

        Elements rows;
        int focusAt = ui.consoleScroll > 0 ? (int)lines.size() - 1 - ui.consoleScroll : -1;
        int width = Terminal::Size().dimx - 1;   // the scroll bar takes one
        for (int i = 0; i < (int)lines.size(); ++i) {
            Elements pieces;
            for (const std::string& piece : wrapAnsi(lines[(size_t)i], width)) pieces.push_back(ansiLine(piece));
            Element row = pieces.size() == 1 ? pieces[0] : vbox(pieces);
            rows.push_back(i == focusAt ? row | focus : row);
        }
        Element prompt = hbox({ansiLine(ui.console->partial()), input->Render() | flex});
        rows.push_back(ui.consoleScroll > 0 ? prompt : prompt | focus);
        Element view = vbox(rows) | vscroll_indicator | yframe | flex;
        if (ui.consoleScroll == 0) return view;
        Element mark = text(" scrolled back " + std::to_string(ui.consoleScroll) + (ui.consoleScroll == 1 ? " line" : " lines") +
                            "   PgDn, End or just type to come back down ") | color(hue(Hue::Background)) | bgcolor(hue(Hue::Accent));
        return vbox({view, mark | hcenter});
    });
}

}  // namespace tui

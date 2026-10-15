// One function per tab. Each returns a component whose renderer draws from ui.snap
// (never locking), with buttons that change the real game through ui.run().

#include <algorithm>
#include <bit>
#include <cmath>

#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/util.hpp"
#include "console.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

namespace {

Element heading(const std::string& s) { return text(s) | bold | color(Color::Cyan); }
Element note(const std::string& s) { return paragraph(s) | dim; }

bool armed(int idx, int armedIdx, Clock::time_point until) { return idx == armedIdx && Clock::now() < until; }

}  // namespace

// ----- Mine -----
Component MinePage(Ui& ui) {
    Component mineBtn = MineButton(ui);
    return Renderer(mineBtn, [&ui, mineBtn] {
        const GameState& g = ui.snap;
        std::time_t t = ui.snapTime;

        Element last = text("Click MINE (or press m) to start.") | dim;
        if (ui.hasMined) {
            const MineResult& r = ui.lastMine;
            Elements parts = {text("Last mine "), text("+" + num(r.gain)) | color(Color::Gold1) | bold,
                              text("  (base " + std::to_string(r.base) + ", x" + fixed(r.mult, 2) + ")") | dim};
            if (r.jackpot)    parts.push_back(text("  JACKPOT!") | color(Color::Magenta) | bold);
            else if (r.bonus) parts.push_back(text("  bonus") | color(Color::Green));
            if (r.meteor)     parts.push_back(text("  Meteor x2") | color(Color::Magenta));
            last = hbox(parts);
        }

        int streak = g.streak;
        long long timeout = streakTimeout(g);
        long long since = g.lastMine ? (long long)std::difftime(t, g.lastMine) : timeout;
        if (since >= timeout) streak = 0;
        Element streakLine = text("Streak 0  (mine to start one)") | dim;
        if (streak > 0) {
            long long left = std::max(0LL, timeout - since);
            streakLine = hbox({text("Streak " + std::to_string(streak) + "  x" + fixed(1.0 + 0.05 * std::min(streak, 20), 2) + "  "),
                               gauge((float)left / (float)timeout) | size(WIDTH, EQUAL, 15) |
                                   color(pick(left <= 5, Color::Yellow, Color::Green)),
                               text(" " + std::to_string(left) + "s") | dim});
        }

        Elements info;
        int ev = activeEvent(g, t);
        if (ev != EV_NONE)
            info.push_back(hbox({text(std::string(EVENTS[ev].name) + ": ") | bold | color(Color::Magenta),
                                 text(std::string(EVENTS[ev].desc) + " (" + std::to_string((long long)std::difftime(g.eventEnds, t)) + "s)")}));
        for (const Contract& c : g.contracts) {
            info.push_back(hbox({text("Job: " + contractText(c) + "  "), progress(contractProgress(g, c), c.target, 12),
                                 text("  " + duration(c.secondsLeft)) | color(pick(c.secondsLeft <= CONTRACT_WARN_AT, Color::Yellow, Color::Default))}));
        }

        Element rate = hbox({text("Earning "), text(perSecond(recentEarnRate(g))) | color(Color::Gold1) | bold,
                             text(" tokens/s"), text("  (last " + std::to_string(EARN_WINDOW_SECONDS) + "s)") | dim});
        std::string split = incomeSplit(g);

        double bonusChance = std::min(0.70, 0.06 + 0.04 * g.magnet);
        double jackpotChance = std::min(0.20, 0.01 + 0.005 * g.magnet);
        Element stats = hbox({
            text("Base " + std::to_string(baseYield(g)) + "   Bonus " + percent(bonusChance, 0) + "   Jackpot " +
                 percent(jackpotChance) + "   Passive " + perSecond(passiveRatePerSec(g)) + "/s" +
                 (g.bot > 0 ? "   Bot every " + std::to_string(botInterval(g.bot)) + "s" : "")) | dim,
        });

        return vbox({
            text(""),
            mineBtn->Render() | hcenter,
            text(""),
            last | hcenter,
            streakLine | hcenter,
            text(""),
            rate | hcenter,
            split.empty() ? text("") : text(split) | dim | hcenter,
            vbox(info) | hcenter,
            filler(),
            stats | hcenter,
        });
    });
}

// ----- Shop -----
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
    Component box = Container::Vertical(all);

    return Renderer(box, [&ui, buys, license] {
        GameState g = ui.snap;   // upgrades() wants a non-const state
        auto ups = upgrades(g);
        const char* effects[] = {"+1 base yield", "better bonus/jackpot odds", "+0.5 passive tokens/s", "auto-mines"};

        Elements rows = {heading("Upgrades"), text("")};
        for (size_t i = 0; i < ups.size(); ++i) {
            rows.push_back(hbox({cell(ups[i].name, 12), cell("lvl " + std::to_string(*ups[i].lvl), 11),
                                 cell(effects[i], 28), cell("cost ", 5), tokens(ups[i].cost) | size(WIDTH, EQUAL, 14),
                                 buys[i][0]->Render(), text(" "), buys[i][1]->Render(), text(" "), buys[i][2]->Render()}));
        }
        rows.push_back(text(""));
        rows.push_back(heading("Automation"));
        rows.push_back(hbox({cell("Auto-Buyer", 12),
                             cell(g.autoUnlocked ? "owned" : "cost " + num(AUTOBUY_LICENSE_COST), 38) ,
                             license->Render()}));
        rows.push_back(note("The Auto-Buyer buys your cheapest upgrade whenever you have " +
                            std::to_string(AUTOBUY_RESERVE) + "x its cost."));
        if (g.bot > 0) rows.push_back(note("Mining Bot mines every " + std::to_string(botInterval(g.bot)) + "s."));
        return vbox(rows);
    });
}

// ----- Market -----
// Your trading record: bought, sold, the net and what the spread took.
static Element ledger(const GameState& g) {
    if (g.shardsBought == 0 && g.shardsSold == 0) return text("");
    long long net = g.tokensFromSells - g.tokensOnBuys;
    auto avg = [](long long tokens, long long n) { return n > 0 ? "  avg " + fixed((double)tokens / (double)n, 1) : std::string(); };
    return vbox({
        heading("Your trading"),
        hbox({cell("Bought", 8), shards(g.shardsBought), text(" shards for "), tokens(g.tokensOnBuys), text(avg(g.tokensOnBuys, g.shardsBought)) | dim}),
        hbox({cell("Sold", 8), shards(g.shardsSold), text(" shards for "), tokens(g.tokensFromSells), text(avg(g.tokensFromSells, g.shardsSold)) | dim}),
        hbox({cell("Net", 8), text((net >= 0 ? "+" : "-") + num(net >= 0 ? net : -net)) | color(pick(net >= 0, Color::Green, Color::Red)) | bold,
              text(" tokens   (" + num(g.tradeCost) + " went to the spread and price impact)") | dim}),
    });
}

Component MarketPage(Ui& ui) {
    auto buyN = [&ui](long long n) { return [&ui, n] { ui.run([n](GameState& g) { return buyShards(g, n); }); }; };
    auto sellN = [&ui](long long n) { return [&ui, n] { ui.run([n](GameState& g) { return sellShards(g, n); }); }; };
    auto canBuy = [&ui](long long n) { return [&ui, n] { return buyCost(ui.snap, n) <= ui.snap.tokens; }; };
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
        [&ui, amount] { long long n = amount(); return n > 0 && n <= MARKET_MAX_ORDER && buyCost(ui.snap, n) <= ui.snap.tokens; });
    Component customSell = Btn("Sell",
        [&ui, amount] {
            long long n = amount();
            if (n <= 0) { ui.s.log.push(Sev::Warn, "Type an amount first, like 2000 or 2k."); return; }
            ui.run([n](GameState& g) { return sellShards(g, n); });
        },
        [&ui, amount] { long long n = amount(); return n > 0 && n <= MARKET_MAX_ORDER && n <= ui.snap.shards; });
    Components customRow = {amountBox, customBuy, customSell};

    Component box = Container::Vertical({Container::Horizontal(buyRow), Container::Horizontal(sellRow), Container::Horizontal(customRow)});

    return Renderer(box, [&ui, buyRow, sellRow, amountBox, customBuy, customSell, amount] {
        const GameState& g = ui.snap;
        auto rowOf = [](const Components& cs) {
            Elements e;
            for (const Component& c : cs) { e.push_back(c->Render()); e.push_back(text(" ")); }
            return hbox(e);
        };

        Element chart = text("(the chart fills in as the market moves)") | dim;
        if (g.priceHistory.size() >= 2) {
            auto [lo, hi] = std::minmax_element(g.priceHistory.begin(), g.priceHistory.end());
            chart = hbox({sparkline(g.priceHistory), text("  low " + fixed(*lo, 1) + "  high " + fixed(*hi, 1)) | dim});
        }

        return vbox({
            hbox({text("Price "), text(fixed(g.shardPrice, 1)) | bold, text("   buy at " + fixed(buyPrice(g), 1) +
                  "   sell at " + fixed(sellPrice(g), 1) + "   spread " + percent(marketSpread(g)))}),
            g.priceHistory.size() >= 2 ? hbox({text("Last " + duration((long long)g.priceHistory.size() * 5) + "  "), chart}) : chart,
            text(""),
            hbox({text("You have "), shards(g.shards), text(" shards and "), tokens(g.tokens), text(" tokens.")}),
            text(""),
            rowOf(buyRow),
            text("        " + num(buyCost(g, 1)) + "     " + num(buyCost(g, 10)) + "      " + num(buyCost(g, 100)) +
                 "    (" + num(maxAffordable(g)) + " max)") | dim,
            rowOf(sellRow),
            text("        " + num(sellValue(g, 1)) + "     " + num(sellValue(g, 10)) + "      " + num(sellValue(g, 100))) | dim,
            text(""),
            hbox({text("Any amount "), amountBox->Render() | bgcolor(pick(amountBox->Focused(), Color::GrayDark, Color::Grey19)), text(" "), customBuy->Render(), text(" "), customSell->Render(),
                  [&] {
                      long long n = amount();
                      if (ui.marketAmount.empty()) return text("   e.g. 2000 or 2k") | dim;
                      if (n <= 0) return text("   that's not an amount") | color(Color::Yellow);
                      if (n > MARKET_MAX_ORDER) return text("   the market takes up to " + num(MARKET_MAX_ORDER) + " at a time") | color(Color::Yellow);
                      return text("   buy for " + num(buyCost(g, n)) + ", sell for " + num(sellValue(g, std::min(n, std::max(g.shards, 1LL))))) | dim;
                  }()}),
            text(""),
            g.heat > 0 ? text("Heat " + std::to_string(g.heat) + " adds " + percent(0.02 * g.heat) + " to the spread.") | color(Color::DarkOrange)
                       : text(""),
            ledger(g),
            note("Drifts back toward " + fixed(SHARD_FAIR_PRICE, 0) + ". Each shard you trade pushes the price a little."),
        });
    });
}

// ----- Jobs (contracts) -----
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
    Components all = abandons;
    for (auto& a : accepts) all.push_back(a);
    Component box = Container::Vertical(all);

    return Renderer(box, [&ui, abandons, accepts] {
        const GameState& g = ui.snap;
        int slots = contractSlots(g);
        Elements rows;
        rows.push_back(hbox({heading("Contracts"), text("  " + std::to_string(g.contracts.size()) + "/" + std::to_string(slots)) | dim,
                             text("   Heat "), heatBar(g.heat), text(" " + std::to_string(g.heat) + "/" + std::to_string(MAX_HEAT)),
                             text("   done " + num(g.contractsDone) + ", failed " + num(g.contractsFailed)) | dim}));

        for (size_t j = 0; j < g.contracts.size() && j < abandons.size(); ++j) {
            const Contract& c = g.contracts[j];
            Elements pays = {text("pays "), text(compact(c.rewardTokens)) | color(Color::Gold1), text(" + "), shards(c.rewardShards), text(" sh")};
            if (c.stake > 0) pays.push_back(text(" + stake") | dim);
            rows.push_back(hbox({text(slots > 1 ? "Job " + std::to_string(j + 1) + ": " : "Job: ") | bold, text(contractText(c)) | bold,
                                 text("  for " + std::string(CLIENTS[c.client])) | dim | flex}));
            rows.push_back(hbox({text("  "), progress(contractProgress(g, c), c.target, 14), text("  "),
                                 text(duration(c.secondsLeft)) | color(pick(c.secondsLeft <= CONTRACT_WARN_AT, Color::Yellow, Color::Default)),
                                 text("   "), hbox(pays), filler(), abandons[j]->Render()}));
        }
        if (g.contracts.empty()) rows.push_back(text("No active contract. Pick one below.") | dim);
        else if ((int)g.contracts.size() < slots) rows.push_back(text("Room for one more job.") | dim);
        rows.push_back(heading("Offers"));

        for (int i = 0; i < OFFER_COUNT; ++i) {
            if (i >= (int)g.offers.size()) continue;
            const Contract& o = g.offers[(size_t)i];
            Element tag = o.big ? text("BIG ") | color(Color::Yellow) | bold : text("    ");
            Element terms = hbox({text(compact(o.rewardTokens)) | color(Color::Gold1), text(" + "), shards(o.rewardShards), text(" sh"),
                                  o.stake > 0 ? text("  stake " + num(o.stake)) | color(Color::DeepSkyBlue1) : text("")});
            rows.push_back(hbox({tag, text(contractText(o)) | flex, text("  "), cell("in " + duration(o.duration), 11),
                                 terms | size(WIDTH, EQUAL, 24), accepts[(size_t)i]->Render()}));
            rows.push_back(text("    from " + std::string(CLIENTS[o.client]) + ", leaves in " + duration(o.expiresIn)) | dim);
        }
        if (g.offers.empty()) rows.push_back(text("  (the board is empty; check back soon)") | dim);
        rows.push_back(filler());
        rows.push_back(note("Failing costs the stake and adds heat (big jobs +2). Heat " + std::to_string(AUDIT_MIN_HEAT) +
                            "+ invites audits."));
        return vbox(rows);
    });
}

// ----- Crew -----
Component CrewPage(Ui& ui) {
    Components trains, fires, hires;
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
        trains.push_back(Maybe(tr, present));
        fires.push_back(Maybe(fi, present));
    }
    for (int i = 0; i < CANDIDATE_COUNT; ++i) {
        Component h = Btn("Hire", [&ui, i] { ui.run([i](GameState& g) { return hire(g, i); }); },
                          [&ui, i] { return i < (int)ui.snap.candidates.size() && (int)ui.snap.crew.size() < crewCap(ui.snap) &&
                                            ui.snap.shards >= hireCost(ui.snap, ui.snap.candidates[(size_t)i]); });
        hires.push_back(Maybe(h, [&ui, i] { return i < (int)ui.snap.candidates.size(); }));
    }
    Components rowsC;
    for (int i = 0; i < MAX_CREW_SLOTS; ++i) rowsC.push_back(Container::Horizontal({trains[(size_t)i], fires[(size_t)i]}));
    for (auto& h : hires) rowsC.push_back(h);
    Component box = Container::Vertical(rowsC);

    return Renderer(box, [&ui, trains, fires, hires] {
        const GameState& g = ui.snap;
        Elements rows;
        Elements top = {heading("Crew " + std::to_string(g.crew.size()) + "/" + std::to_string(crewCap(g))),
                        text("   Shards "), shards(g.shards)};
        if (!g.crew.empty())
            top.push_back(text("   Payday in " + duration(g.paydayClock) + ": " + count(totalWages(g), "shard")) | dim);
        rows.push_back(hbox(top));

        if (g.crew.empty()) rows.push_back(text("  Nobody yet. Hire someone below.") | dim);
        for (size_t i = 0; i < g.crew.size() && i < (size_t)MAX_CREW_SLOTS; ++i) {
            const CrewMember& c = g.crew[i];
            bool capped = c.level >= levelCap(g, c);
            Element cost = hbox({shards(capped ? promoteCost(c) : trainCost(c)), text(" sh")}) | size(WIDTH, EQUAL, 11);
            rows.push_back(hbox({cell(c.name, 9) | bold, cell(ROLES[(int)c.role].name, 8),
                                 cell("L" + std::to_string(c.level) + "/" + std::to_string(levelCap(g, c)), 7),
                                 cell(starsText(c), 6) | color(Color::Gold1) | bold, cell(traitName(c), 14) | color(Color::Cyan),
                                 text(crewEffectText(g, c)) | flex, text("  "), cost, trains[i]->Render(), text(" "),
                                 fires[i]->Render()}));
        }
        rows.push_back(text(""));
        rows.push_back(hbox({heading("Looking for work"), text("   new faces in " + duration(g.candidateClock)) | dim}));
        for (size_t i = 0; i < g.candidates.size() && i < (size_t)CANDIDATE_COUNT; ++i) {
            const CrewMember& c = g.candidates[i];
            std::string about = c.trait != Trait::None ? std::string(TRAITS[(int)c.trait].desc) : ROLES[(int)c.role].perk;
            rows.push_back(hbox({cell(c.name, 9), cell(ROLES[(int)c.role].name, 8), cell("L" + std::to_string(c.level), 7),
                                 cell("", 6), cell(traitName(c), 14) | color(Color::Cyan), text(about) | dim | flex, text("  "),
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
        buys.push_back(Maybe(b, [&ui, i] { return !hasBit(ui.snap.curiosOwned, i); }));
    }
    Component box = Container::Vertical(buys);

    return Renderer(box, [&ui, buys] {
        const GameState& g = ui.snap;
        Elements rows = {hbox({heading("Curios"), text("   shelf " + std::to_string(std::popcount(g.curiosOwned)) + "/" +
                                                       std::to_string(CURIO_COUNT)) | dim})};
        for (int i = 0; i < CURIO_COUNT; ++i) {
            const CurioInfo& c = CURIOS[i];
            bool owned = hasBit(g.curiosOwned, i);
            Element right = owned ? text("\"" + std::string(c.flavor) + "\"") | dim | color(Color::Green) | flex
                                  : hbox({tokens(c.cost) | size(WIDTH, EQUAL, 13), buys[(size_t)i]->Render(), filler()}) | flex;
            rows.push_back(hbox({cell(owned ? "[x]" : "[ ]", 4) | color(pick(owned, Color::Green, Color::Default)),
                                 cell(c.name, 26) | (owned ? bold : nothing), cell(CURIO_SETS[c.set], 19) | dim, right}));
        }
        return vbox(rows);
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
                Decorator look = built ? color(Color::Gold1) : Decorator(dim);
                art.push_back(hbox({cell(line, 26) | look, label | dim | flex}));
            }
        }
        Elements btnRow;
        for (const Component& b : btns) { btnRow.push_back(b->Render()); btnRow.push_back(text(" ")); }

        Element status = text("Complete. It hums quietly.") | color(Color::Gold1) | bold;
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
        case 1:  return Color::DeepSkyBlue1;   // First Ring
        case 2:  return Color::SteelBlue1;     // Second Ring
        case 3:  return Color::Grey70;         // Gearwork
        default: return Color::Gold1;          // The Sun
    }
}

static Element orreryRun(const ArtRun& run) {
    Element e = text(run.text);
    if (run.kind == ArtKind::Blank) return e;
    if (!run.built) return e | dim;
    switch (run.kind) {
        case ArtKind::Planet: return e | color(Color::LightSalmon1) | bold;
        case ArtKind::Star:   return e | color(Color::LightGoldenrod1);
        case ArtKind::Core:   return e | color(Color::Gold1) | bold;
        default:              return e | color(orreryColor(run.stage));
    }
}

Component OrreryPage(Ui& ui) {
    auto build = [&ui](std::function<long long(const GameState&)> amount) {
        return [&ui, amount] { ui.run([amount](GameState& g) { return buildOrrery(g, amount(g)).notes; }); };
    };
    auto open = [&ui] { return ui.snap.orreryStage < ORRERY_STAGE_COUNT && ui.snap.shards > 0; };
    Components btns = {
        Btn("Add 100", build([](const GameState& g) { return std::min(100LL, g.shards); }), open),
        Btn("Add 1,000", build([](const GameState& g) { return std::min(1000LL, g.shards); }), open),
        Btn("Add all", build([](const GameState& g) { return orrerySpareShards(g); }), open),   // keeps payday wages
        Btn("Finish stage", build([](const GameState& g) { return std::min(g.shards, orreryRemaining(g)); }),
            [&ui] { return ui.snap.orreryStage < ORRERY_STAGE_COUNT && ui.snap.shards >= orreryRemaining(ui.snap); }),
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
                    ? hbox({cell(o.name, 13) | bold, text(o.perk) | color(Color::Green) | flex})
                    : hbox({cell(o.name, 13) | dim, text(num(o.cost) + " sh: " + o.perk) | dim | flex});
            }
            art.push_back(hbox({hbox(line) | size(WIDTH, EQUAL, ORRERY_ART_WIDTH), label | flex}));
            ++r;
        }
        Elements btnRow;
        for (const Component& b : btns) { btnRow.push_back(b->Render()); btnRow.push_back(text(" ")); }

        Element status = text("Complete. The little sun turns, and so does everything else.") | color(Color::DeepSkyBlue1) | bold;
        if (g.orreryStage < ORRERY_STAGE_COUNT) {
            const OrreryStage& st = ORRERY[g.orreryStage];
            status = hbox({text("Stage " + std::to_string(g.orreryStage + 1) + "/" + std::to_string(ORRERY_STAGE_COUNT) + ": " + st.name + "  "),
                           progress(g.orreryProgress, st.cost, 24), text("   you have "), shards(g.shards), text(" shards")});
        }
        if (Terminal::Size().dimy < 30) {   // short terminal: the picture comes first
            return vbox({vbox(art), status, hbox(btnRow)});
        }
        return vbox({vbox(art), text(""), status, hbox(btnRow), filler(),
                     note("Built with shards: buy them on the Market (big orders push the price up), or let Scouts and contracts bring them in.")});
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
        rows.push_back(reached ? row | color(Color::Green) : row | dim);
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
                          : locked ? text("opens at " + std::string(TITLES[FEATURES[needs].title].name)) | dim
                                   : progress(std::min(statValue(g, goal.stat), goal.target), goal.target, 12);
            Element row = hbox({cell(done ? "[x]" : "[ ]", 4), cell(goal.name, 16) | bold, cell(goal.desc, 34), right});
            if (done) row = row | color(Color::Green);
            else if (locked) row = row | dim;
            if (i == ui.goalSelected) row = row | focus | (focused ? inverted : nothing);
            rows.push_back(row);
        }
        std::string next = ti + 1 < TITLE_COUNT ? "   next: " + std::string(TITLES[ti + 1].name) + " at " + std::to_string(TITLES[ti + 1].renown) : "";
        Element goals = vbox(rows) | vscroll_indicator | yframe | flex;
        if (Terminal::Size().dimx >= 125) goals = hbox({goals, text("   "), titleLadder(renown)});
        return vbox({
            hbox({heading("Goals " + std::to_string(std::popcount(g.goalsDone)) + "/" + std::to_string(GOAL_COUNT)),
                  text("   Renown " + std::to_string(renown) + "   Title "), text(TITLES[ti].name) | bold, text(next) | dim}),
            goals,
        });
    });
    return CatchEvent(list, [&ui](Event e) {
        int before = ui.goalSelected;
        if (e == Event::ArrowDown || (e.is_mouse() && e.mouse().button == Mouse::WheelDown)) ++ui.goalSelected;
        else if (e == Event::ArrowUp || (e.is_mouse() && e.mouse().button == Mouse::WheelUp)) --ui.goalSelected;
        else return false;
        ui.goalSelected = std::clamp(ui.goalSelected, 0, GOAL_COUNT - 1);
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
        auto row = [](const std::string& label, Element value) { return hbox({cell(label, 17) | dim, value}); };
        auto plain = [](const std::string& v) { return text(v); };

        Elements wealth = {heading("Wealth"),
                           row("Tokens", tokens(g.tokens)),
                           row("Earned in total", tokens(g.lifetimeEarned)),
                           row("Earning now", plain(perSecond(recentEarnRate(g)) + "/s")),
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
                                                      color(pick(net >= 0, Color::Green, Color::Red))));
        }
        if (isUnlocked(g, Feature::Contracts)) {
            business.push_back(row("Contracts", plain(num(g.contractsDone) + " done, " + num(g.contractsFailed) + " failed")));
            business.push_back(row("Audits", plain(num(g.audits))));
        }
        if (isUnlocked(g, Feature::Crew)) business.push_back(row("Crew mined", tokens(g.crewMined)));
        if (!business.empty()) {
            mining.push_back(text(""));
            mining.push_back(heading("Business"));
            mining.insert(mining.end(), business.begin(), business.end());
        }

        // Where the money comes from: all time, with a share bar, and right now.
        Elements income = {hbox({heading("Income by source"), text("   all time, and now") | dim})};
        {
            auto rates = recentSourceRates(g);
            long long sorted = 0;
            for (long long v : g.earnedBy) sorted = addTokens(sorted, v);
            double total = std::max(1.0, (double)g.lifetimeEarned);
            for (int i = 0; i < SOURCE_COUNT; ++i) {
                long long v = g.earnedBy[(size_t)i];
                if (v <= 0 && rates[(size_t)i] <= 0.0) continue;
                float share = (float)((double)v / total);
                income.push_back(hbox({cell(sourceName((Source)i), 17) | dim, text(compact(v)) | color(Color::Gold1) | size(WIDTH, EQUAL, 8),
                                       gauge(share) | size(WIDTH, EQUAL, 14) | color(Color::Gold1), text(" " + percent(share, 0)) | size(WIDTH, EQUAL, 6),
                                       text("now " + perSecond(rates[(size_t)i]) + "/s") | dim}));
            }
            if (g.lifetimeEarned > sorted)
                income.push_back(hbox({cell("Before records", 17) | dim, text(compact(g.lifetimeEarned - sorted)) | dim}));
        }

        Elements time = {heading("Time"), row("Played", plain(playTime(g.playSeconds)))};
        int ti = titleIndex(renownOf(g));
        for (int t = 1; t <= ti; ++t) time.push_back(row(TITLES[t].name, plain("at " + playTime(g.titleReachedAt[t]))));

        int width = std::max(20, Terminal::Size().dimx - 12);
        int height = std::clamp(Terminal::Size().dimy - 32, 3, 14);
        Element chart = text("The chart fills in as you play: a point every 5 minutes.") | dim;
        std::vector<long long> cols = historyColumns(g, width);
        if (g.history.size() >= 2 && cols.size() >= 2) {
            Elements lines;
            std::vector<std::string> rows = logChart(cols, height);
            for (size_t r = 0; r < rows.size(); ++r) {
                std::string label = r == 0 ? compact(cols.back()) : r + 1 == rows.size() ? compact(std::max(1LL, cols.front())) : "";
                lines.push_back(hbox({text(label) | size(WIDTH, EQUAL, 8) | dim, text(" "), text(rows[r]) | color(Color::Gold1)}));
            }
            lines.push_back(hbox({text("         " + playTime(g.history.front().playSeconds)) | dim, filler(),
                                  text(playTime(g.playSeconds)) | dim}));
            chart = vbox({hbox({text("Earned in total, over your play time") | bold, text("  (log scale)") | dim}), vbox(lines)});
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
// The CLI in a tab: shell/dispatch.cpp runs each line on the Console's own thread (console.cpp).
Component ConsolePage(Ui& ui) {
    Component input = TextBox(&ui.consoleLine, &ui.consoleCursor, "", [&ui] {
        if (ui.console) ui.console->submit(ui.consoleLine);
        ui.consoleLine.clear();
        ui.consoleCursor = 0;
        ui.historyAt = -1;
    });
    ui.textBoxes.push_back(input);

    // Up and Down walk through the lines typed before.
    Component withHistory = CatchEvent(input, [&ui](Event e) {
        if (!ui.console || (e != Event::ArrowUp && e != Event::ArrowDown)) return false;
        std::vector<std::string> h = ui.console->history();
        if (h.empty()) return true;
        int at = ui.historyAt < 0 ? (int)h.size() : ui.historyAt;
        at += e == Event::ArrowUp ? -1 : 1;
        if (at >= (int)h.size()) { ui.historyAt = -1; ui.consoleLine.clear(); }
        else { ui.historyAt = std::max(0, at); ui.consoleLine = h[(size_t)ui.historyAt]; }
        ui.consoleCursor = (int)ui.consoleLine.size();
        return true;
    });

    return Renderer(withHistory, [&ui, input] {
        Elements rows;
        if (ui.console) {
            for (const std::string& l : ui.console->lines(400)) rows.push_back(ansiLine(l));
            rows.push_back(hbox({ansiLine(ui.console->partial()), input->Render() | flex}) | focus);
        }
        return vbox(rows) | yframe | flex;
    });
}

}  // namespace tui

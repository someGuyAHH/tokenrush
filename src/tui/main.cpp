// TokenRush (FTXUI). The game's only front end: tabs and buttons, and the Console tab for typing
// commands (src/shell).
//
// Threads:
//   main       - FTXUI's event loop: draws frames and runs button clicks
//   refresher  - wakes the UI ten times a second so the log, numbers and gauges stay live
//   world, market, automation, crew, autosave - the game's own workers (src/core/workers.cpp)
//
// Rules on top of the core's:
//   - Drawing never locks the game. Each frame starts with Ui::snapshot(), and every
//     renderer reads that copy. Only clicks (Ui::run, Ui::mineNow) lock the real state.
//   - App::PostEvent is the only FTXUI call made from another thread.
//   - Shutdown: leave the UI loop, stop the refresher, stop the workers, then save.
//
// Keys: 1-9 and 0 switch tabs, m mines, s saves, y/n answer an offer, : opens the Console, p switches
// profile, q or Ctrl+C quits. While a text box has focus (the Console, the Market's amount box) keys
// type instead, and Esc steps out. Tab/arrows/Enter work too, and everything is clickable.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "console.hpp"
#include "core/choices.hpp"
#include "core/contracts.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/save.hpp"
#include "core/profiles.hpp"
#include "core/session.hpp"
#include "core/util.hpp"
#include "shell/term.hpp"
#include "ui.hpp"

using namespace ftxui;
using namespace std::chrono_literals;

namespace {

// The tabs, in the order they unlock. A locked tab isn't shown at all, so new ones appear
// at the end and the number keys of the ones you already have never change.
struct TabDef {
    const char* name;
    int feature;   // a Feature, or -1 for always there
};
const TabDef TABS[] = {
    {"Mine", -1}, {"Shop", -1}, {"Goals", -1}, {"Stats", -1},
    {"Curios", (int)Feature::Curios}, {"Monolith", (int)Feature::Monolith}, {"Market", (int)Feature::Market},
    {"Jobs", (int)Feature::Contracts}, {"Crew", (int)Feature::Crew}, {"Orrery", (int)Feature::Orrery},
    {"Workshop", (int)Feature::Workshop}, {"Stars", (int)Feature::Stars},
    {"Console", -1},   // always last; ':' opens it from anywhere
};
constexpr int CONSOLE_PAGE = (int)std::size(TABS) - 1;
constexpr int SHOP_PAGE = 1;   // where theme previews live

Element statusBar(const tui::Ui& ui) {
    const GameState& g = ui.snap;
    int renown = renownOf(g);
    Elements top = {text(" Tokens ") | tui::muted(), tui::tokens(g.tokens) | bold};
    if (isUnlocked(g, Feature::Market)) {
        top.push_back(text("   Shards ") | tui::muted());
        top.push_back(tui::shards(g.shards) | bold);
        top.push_back(text(" @" + fixed(g.shardPrice, 1)) | tui::muted());
    }
    if (isUnlocked(g, Feature::Contracts)) {
        top.push_back(text("   Heat ") | tui::muted());
        top.push_back(tui::heatBar(g.heat));
    }
    top.push_back(filler());
    if (g.strain != Strain::None) {
        if (g.cured) top.push_back(text(std::string("cured of ") + STRAINS[(int)g.strain].name + " · ") | tui::muted());
        else         top.push_back(text(std::string("INFECTED: ") + STRAINS[(int)g.strain].name + " · ") | color(tui::hue(Hue::Infected)) | bold);
    }
    top.insert(top.end(), {
        ui.profile.empty() ? text("") : text(ui.profile + " · ") | tui::muted(),
        text(TITLES[titleIndex(renown)].name) | bold, text(" "),
    });

    Elements second = {text(" ")};
    int ev = activeEvent(g, ui.snapTime);
    if (ev != EV_NONE) {
        second.push_back(text(std::string(EVENTS[ev].name) + " " + std::to_string((long long)std::difftime(g.eventEnds, ui.snapTime)) + "s") |
                         color(tui::hue(Hue::Event)) | bold);
        second.push_back(text("   "));
    }
    if (g.race.stake > 0) {
        second.push_back(text("RACE " + std::to_string(g.manualMines - g.race.startMines) + "/" + std::to_string(g.race.target) +
                              " mines, " + std::to_string(g.race.secondsLeft) + "s") | color(tui::hue(Hue::Event)) | bold);
        second.push_back(text("   "));
    }
    if (g.investment.amount > 0) {
        second.push_back(text("Loan back in " + duration(g.investment.secondsLeft)) | tui::muted());
        second.push_back(text("   "));
    }
    for (size_t j = 0; j < g.contracts.size(); ++j) {
        const Contract& c = g.contracts[j];
        // One job gets its description; two share the line, so each shows only its gauge.
        if (g.contracts.size() == 1) second.push_back(text("Job: " + contractText(c) + " ") | tui::muted());
        else second.push_back(text((j > 0 ? "   Job " : "Job ") + std::to_string(j + 1) + " ") | tui::muted());
        second.push_back(tui::progress(contractProgress(g, c), contractGoal(c), 8));
        second.push_back(text("  " + duration(c.secondsLeft)) | color(tui::pick(c.secondsLeft <= CONTRACT_WARN_AT, tui::hue(Hue::Warn), tui::hue(Hue::Muted))));
    }
    if (anythingBroken(g) || g.dronesDown > 0) {
        std::string list;
        for (const Repair& r : repairs(g)) list += (list.empty() ? "" : ", ") + r.what;
        if (g.dronesDown > 0) list += (list.empty() ? "" : ", ") + num(g.dronesDown) + " Drones (back in " + duration(g.dronesDownSecs) + ")";
        if (second.size() > 1) second.push_back(text("   "));
        second.push_back(text("Broken: " + list) | color(tui::hue(Hue::Bad)));
        second.push_back(text("   "));
    }
    if (int next = nextUnlock(g); next >= 0 && g.contracts.empty()) {
        const Title& t = TITLES[FEATURES[next].title];
        second.push_back(text("Next unlock: " + std::string(FEATURES[next].name) + " at the title " + t.name + " (" +
                              std::to_string(renown) + "/" + std::to_string(t.renown) + " renown)") | tui::muted());
    } else if (g.contracts.empty()) {
        second.push_back(text("No contract. See the Jobs tab.") | tui::muted());
    }
    return vbox({hbox(top), hbox(second)});
}

Element logPane(Shared& s) {
    // 6 lines on a 30-row terminal, fewer on smaller ones so the tabs keep their room.
    int n = std::clamp(Terminal::Size().dimy - 24, tui::LOG_ROWS_MIN, tui::LOG_ROWS_MAX);
    Elements rows;
    for (const LogEntry& e : s.log.recent((size_t)n)) rows.push_back(tui::logLine(e));
    while ((int)rows.size() < n) rows.push_back(text(""));   // fixed height, so nothing jumps
    if (THEMES[tui::currentTheme()].classic) return window(text(" LOG ") | bold, vbox(rows));
    // Themed: a quiet frame with an accent title on the slightly lighter panel color, so the
    // messages stand out from the border and the log from the page below.
    return window(text(" LOG ") | bold | color(tui::hue(Hue::Accent)), vbox(rows)) | color(tui::hue(Hue::Muted)) |
           bgcolor(tui::hue(Hue::Panel));
}

}  // namespace

// One game, from loading the save to the final save. `profile` is empty with --save-dir.
static SessionEnd runGame(const std::filesystem::path& dir, const std::string& profile) {
    std::string message;
    if (!prepareSaveDir(dir, message)) {
        std::cout << message << "\n";
        return SessionEnd::Quit;
    }

    Shared s;
    tui::Ui ui(s);
    ui.profile = profile;
    StartReport start = startGame(s, ui.rng);
    for (const Note& n : start.notes) s.log.push(n);
    s.log.push(Sev::Event, start.fresh ? "Welcome to TokenRush. Click MINE to get going." : "Welcome back.");

    Workers workers;
    workers.start(s);

    auto screen = App::Fullscreen();
    ui.app = &screen;
    screen.ForceHandleCtrlC(false);   // FTXUI would re-raise SIGINT on exit and kill us before the final save

    // How the session ends. The Console can end it too ('quit' or 'logout' typed there), from its
    // own thread, so it asks the UI thread to do it.
    SessionEnd end = SessionEnd::Quit;
    tui::Console console(s, !profile.empty(), [&](Outcome o) {
        screen.Post([&, o] {
            end = o == Outcome::Logout ? SessionEnd::Logout : SessionEnd::Quit;
            screen.Exit();
        });
    });
    ui.console = &console;

    // ----- Tabs -----
    // ui.page is the page on screen; ui.tab is its position among the visible tabs. Both are
    // refreshed every frame from the snapshot, so a tab appears the moment its feature unlocks
    // (in front of the Console, which is always last).
    int tabFocus = 0;   // the menu's highlighted entry; put back on the open tab every frame
    std::vector<std::string> tabNames;
    std::vector<int> tabPage;
    auto refreshTabs = [&] {
        tabNames.clear();
        tabPage.clear();
        for (int i = 0; i < (int)std::size(TABS); ++i) {
            if (TABS[i].feature >= 0 && !isUnlocked(ui.snap, (Feature)TABS[i].feature)) continue;
            tabNames.push_back(TABS[i].name);
            tabPage.push_back(i);
        }
        auto at = std::find(tabPage.begin(), tabPage.end(), ui.page);
        if (at == tabPage.end()) { ui.page = 0; at = tabPage.begin(); }
        ui.tab = (int)(at - tabPage.begin());
        tabFocus = ui.tab;
    };
    auto showPage = [&](int page) {
        if (page != ui.page && ui.page != CONSOLE_PAGE) ui.previousPage = ui.page;
        ui.page = page;
    };
    MenuOption tabOpt = MenuOption::Horizontal();
    tabOpt.focused_entry = &tabFocus;
    auto classicTab = tabOpt.entries_option.transform;   // FTXUI's own look, kept for the Classic theme
    tabOpt.entries_option.transform = [classicTab](const EntryState& st) {
        if (THEMES[tui::currentTheme()].classic) return classicTab(st);
        // Themed: the open tab is a chip in the accent color, the rest are quiet.
        if (st.active) return text(st.label) | bold | color(tui::hue(Hue::Background)) | bgcolor(tui::hue(Hue::Accent));
        Element e = text(st.label) | color(tui::hue(st.focused ? Hue::Text : Hue::Muted));
        return st.focused ? e | underlined : e;
    };
    tabOpt.on_change = [&] { if (ui.tab >= 0 && ui.tab < (int)tabPage.size()) showPage(tabPage[(size_t)ui.tab]); };
    Component tabBar = Menu(&tabNames, &ui.tab, tabOpt);
    Components pageList = {tui::MinePage(ui), tui::ShopPage(ui), tui::GoalsPage(ui), tui::StatsPage(ui), tui::CuriosPage(ui),
                           tui::MonolithPage(ui), tui::MarketPage(ui), tui::JobsPage(ui), tui::CrewPage(ui),
                           tui::OrreryPage(ui), tui::WorkshopPage(ui), tui::StarsPage(ui), tui::ConsolePage(ui)};
    Component pages = Container::Tab(pageList, &ui.page);
    ui.snapshot();
    refreshTabs();

    // ----- Offers (choice events) -----
    auto answer = [&](bool yes) { ui.run([&ui, yes](GameState& g) { return answerChoice(g, yes, ui.rng); }); };
    Component yesBtn = tui::Btn("Yes (y)", [answer] { answer(true); });
    Component noBtn = tui::Btn("No (n)", [answer] { answer(false); });
    Component offerBar = Maybe(Container::Horizontal({yesBtn, noBtn}), [&ui] { return hasChoice(ui.snap); });

    // ----- Disasters -----
    auto respond = [&ui] { ui.run([](GameState& g) { return respondDisaster(g); }); };
    Component respondBtn = tui::Btn([&ui] {
        if (!disasterPending(ui.snap)) return std::string("");
        std::string verb = DISASTERS[ui.snap.disaster.kind].verb;
        verb[0] = (char)std::toupper((unsigned char)verb[0]);
        return verb + " (x)";
    }, respond);
    Component dangerBar = Maybe(respondBtn, [&ui] { return disasterPending(ui.snap); });

    // The offer and danger bars come last in the focus order, so one showing up never moves the
    // keyboard onto its button (a stray Enter would accept). Click it, or press y / n / x.
    Component layout = Renderer(Container::Vertical({tabBar, pages, offerBar, dangerBar}), [&] {
        if (ui.page != SHOP_PAGE) ui.previewTheme = ui.hoverTheme = -1;   // theme previews only live on the Shop tab
        ui.snapshot();   // must come first: everything below draws from it
        refreshTabs();
        // On the Console, the typing line always has the keyboard.
        if (ui.page == CONSOLE_PAGE && !ui.typing()) pageList[CONSOLE_PAGE]->TakeFocus();

        Element offer = text("");
        if (hasChoice(ui.snap)) {
            offer = hbox({text(" OFFER ") | bgcolor(tui::hue(Hue::Event)) | color(tui::hue(Hue::ButtonText)) | bold, text(" "),
                          paragraph(choiceText(ui.snap)) | flex, text("  " + std::to_string(ui.snap.choice.secondsLeft) + "s  ") | tui::muted(),
                          yesBtn->Render(), text(" "), noBtn->Render()});
        }
        Element danger = text("") | size(HEIGHT, EQUAL, 0);
        if (disasterPending(ui.snap)) {
            danger = hbox({text(" DANGER ") | bgcolor(tui::hue(Hue::Bad)) | color(tui::hue(Hue::ButtonText)) | bold, text(" "),
                           text(std::string(DISASTERS[ui.snap.disaster.kind].name) + ": ") | bold | color(tui::hue(Hue::Bad)),
                           paragraph(disasterText(ui.snap)) | flex,
                           text("  " + std::to_string(ui.snap.disaster.secondsLeft) + "s  ") | color(tui::hue(Hue::Warn)) | bold,
                           respondBtn->Render()});
        }
        return tui::themed(vbox({
            logPane(s),
            statusBar(ui),
            danger,
            hasChoice(ui.snap) ? offer : text("") | size(HEIGHT, EQUAL, 0),
            tabBar->Render(),
            THEMES[tui::currentTheme()].classic ? separatorLight() : separatorLight() | color(tui::hue(Hue::Muted)),
            pages->Render() | flex,
            text(ui.page == CONSOLE_PAGE
                     ? std::string(" Esc back to the game   Up/Down earlier lines   PgUp/PgDn or the mouse wheel scroll back   (typing goes to the console)")
                     : std::string(" 1-9 0 tabs   m mine   s save   y/n offers   ") +
                           (isUnlocked(ui.snap, Feature::Crew) ? "x stop a disaster   " : "") + ": console   " + (profile.empty() ? "" : "p profile   ") +
                           "q quit") | tui::muted(),
        }));
    });

    Component root = CatchEvent(layout, [&](Event e) {
        if (e == Event::CtrlC) { screen.Exit(); return true; }
        if (ui.typing()) {   // a text box has the keys
            if (e == Event::Escape) {
                if (ui.page == CONSOLE_PAGE) showPage(ui.previousPage == CONSOLE_PAGE ? 0 : ui.previousPage);
                tabBar->TakeFocus();
                return true;
            }
            return false;
        }
        if (e == Event::Character('q')) { screen.Exit(); return true; }
        if (e == Event::Character('m')) { ui.mineNow(); return true; }
        if (e == Event::Character('s')) { ui.saveNow(); return true; }
        if (e == Event::Character('y') && hasChoice(ui.snap)) { answer(true); return true; }
        if (e == Event::Character('x') && disasterPending(ui.snap)) { respond(); return true; }
        if (e == Event::Character('n') && hasChoice(ui.snap)) { answer(false); return true; }
        if (e == Event::Character(':')) { showPage(CONSOLE_PAGE); return true; }
        if (e == Event::Character('p') && !profile.empty()) { end = SessionEnd::Logout; screen.Exit(); return true; }
        if (e.is_character() && e.character().size() == 1) {
            char c = e.character()[0];
            int idx = c == '0' ? 9 : c - '1';
            if (c >= '0' && c <= '9' && idx < (int)tabPage.size()) { showPage(tabPage[(size_t)idx]); refreshTabs(); return true; }
        }
        return false;
    });

    {
        // Declared after `screen`, so it is stopped and joined before the screen goes away.
        std::jthread refresher([&screen](std::stop_token st) {
            while (sleepFor(st, 100ms)) screen.PostEvent(Event::Custom);
        });
        screen.Loop(root);
    }   // refresher joined here

    // Single exit path: stop the console and the workers first, so nothing changes after the final save.
    console.stop();
    workers.stop();
    bool saved = saveNow(s);
    tui::useTheme(0);   // the profile picker is nobody's: back to Classic
    setTheme(0);
    if (end == SessionEnd::Quit) std::cout << (saved ? "Saved. Bye.\n" : "Save failed. Bye.\n");
    else if (!saved)             std::cout << "Save failed!\n";
    return end;
}

int main(int argc, char** argv) {
    LaunchOptions opt;
    std::string message;
    switch (parseLaunchArgs(argc, argv, "tokenrush-tui", opt, message)) {
        case ArgsResult::Ok:    break;
        case ArgsResult::Help:  std::cout << message << "\n"; return 0;
        case ArgsResult::Error: std::cout << message << "\n"; return 2;
    }
    if (opt.noColor) {   // FTXUI honors the NO_COLOR convention, so --no-color just sets it
#ifdef _WIN32
        _putenv_s("NO_COLOR", "1");
#else
        setenv("NO_COLOR", "1", 1);
#endif
    }

    if (!opt.saveDir.empty()) {   // --save-dir: no profiles at all
        runGame(opt.saveDir, "");
        return 0;
    }

    Profiles profiles(opt.dataRoot);
    std::string name;
    if (!opt.profile.empty()) {
        name = profiles.find(opt.profile);
        if (name.empty() && !profiles.create(opt.profile, message)) {
            std::cout << message << "\n";
            return 2;
        }
        name = profiles.find(opt.profile);
    }

    // Pick a profile, play it, and come back to the picker after 'p'.
    while (true) {
        if (name.empty()) name = tui::pickProfile(profiles);
        if (name.empty()) break;
        if (runGame(profiles.dirOf(name), name) == SessionEnd::Quit) break;
        name.clear();
    }
    return 0;
}

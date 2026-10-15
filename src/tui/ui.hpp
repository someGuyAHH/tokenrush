#pragma once
// The full-screen UI, built on FTXUI. It only talks to the game through
// src/core: buttons call core actions under the game lock, and everything on screen is
// drawn from `Ui::snap`, a copy of the game taken once at the start of every frame.
// No drawing code holds the game lock, so a button can never deadlock against a frame.
//
//   ui.hpp / main.cpp   the Ui struct, startup, the frame layout, global keys
//   widgets.cpp         buttons, the Mine button, colors, small reusable elements
//   pages.cpp           one function per tab
//   console.cpp         the Console tab's engine: the typed commands' loop on its own thread
//   picker.cpp          the profile picker shown before the game

#include <chrono>
#include <deque>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "ftxui.hpp"   // FTXUI 7.0.3, amalgamated (dependencies/)

#include "core/actions.hpp"
#include "core/state.hpp"

namespace tui {

using Clock = std::chrono::steady_clock;
inline constexpr auto MINE_COOLDOWN  = std::chrono::milliseconds(250);
inline constexpr auto CONFIRM_WINDOW = std::chrono::seconds(3);   // click a risky button twice within this
inline constexpr int  LOG_ROWS_MAX   = 8;   // the log shrinks on short terminals, down to...
inline constexpr int  LOG_ROWS_MIN   = 3;

// Everything here belongs to the UI thread. Only `s` is shared with the workers.
struct Ui {
    explicit Ui(Shared& shared);

    Shared& s;
    std::mt19937 rng;
    GameState snap;            // this frame's copy of the game
    std::time_t snapTime = 0;
    ftxui::App* app = nullptr;
    std::string profile;       // shown in the status bar; empty with --save-dir
    int tab = 0;               // which visible tab is selected (the tab bar's index)
    int page = 0;              // which page that is (see TABS in main.cpp); the one that counts
    int previousPage = 0;      // where Esc goes back to from the Console
    class Console* console = nullptr;
    std::vector<ftxui::Component> textBoxes;   // while one of these has focus, letter keys type instead of acting

    // Mine tab
    Clock::time_point mineReadyAt{};
    MineResult lastMine;
    bool hasMined = false;

    // Two-click confirmations
    int abandonArmed = -1;
    Clock::time_point abandonArmedUntil{};
    Clock::time_point buyMaxArmedUntil{};
    int fireArmed = -1;
    Clock::time_point fireArmedUntil{};

    // Crew tab: which expedition the Send buttons start
    int expeditionTrip = 0;

    // Shop tab: the theme being previewed (the mouse over its row, or the keyboard on its button),
    // drawn over the whole screen instead of the profile's own; -1 = none
    int previewTheme = -1;
    int hoverTheme = -1;
    // ...and its custom theme editor (shopMode 1)
    int shopMode = 0;
    int themeRole = 0;         // the color being edited (a Hue)
    std::string themeHex;      // the hex box
    int themeHexCursor = 0;
    int themeHexFor = -1;      // the role and color the box was last filled from
    int themeBaseArmed = -1;   // "start over from" asks twice
    Clock::time_point themeBaseUntil{};

    // Goals tab
    int goalSelected = 0;

    // Market tab: the custom amount box
    std::string marketAmount;
    int marketCursor = 0;
    Clock::time_point customBuyArmedUntil{};

    // Tasks tab: the box (a new task, or task `taskEditing` being rewritten)
    std::string taskDraft;
    int taskCursor = 0;
    int taskEditing = -1;
    int taskDeleteArmed = -1;
    Clock::time_point taskDeleteUntil{};
    bool taskHelp = true;      // the list of what tasks can read and do
    ftxui::Component taskBox;

    // Console tab
    std::string consoleLine;
    int consoleCursor = 0;
    int historyAt = -1;        // -1: typing a new line; otherwise an index into the console's history
    int consoleScroll = 0;     // lines scrolled back from the bottom (0: following the newest output)
    size_t consoleSeen = 0;    // Console::total() at the last frame, to hold the view still while scrolled back

    bool typing() const;       // a text box has focus

    void snapshot();                                              // refresh `snap` and the theme (start of each frame)
    void run(const std::function<Notes(GameState&)>& action);     // lock, act, check goals/contract, log the notes
    void mineNow();                                               // respects the cooldown
    void saveNow();
    float mineCooldownLeft() const;                               // 1 = just mined, 0 = ready
};

// ----- Themes (widgets.cpp) -----
// Every color on screen comes from the profile's theme. Classic maps to the original named
// terminal colors, so it looks exactly as before; the others are 24-bit RGB, background included.
void useTheme(int theme);                  // done by Ui::snapshot() every frame
int currentTheme();
ftxui::Color hue(Hue h);                   // the current theme's color for a role
ftxui::Color hueOf(int theme, Hue h);      // another theme's (for previews)
ftxui::Decorator muted();                  // quiet text: faint in Classic, the Muted color elsewhere
ftxui::Decorator track();                  // a gauge's empty part: the Panel color when themed (Classic: nothing)
ftxui::Element themed(ftxui::Element e);   // the theme's background and text color under a whole screen

// ----- widgets.cpp -----
// `cond ? Color::Green : Color::Default` doesn't compile: the names come from different
// enums (16-color vs 256-color palettes). Passing both through Color parameters fixes that.
inline ftxui::Color pick(bool cond, ftxui::Color a, ftxui::Color b) { return cond ? a : b; }

ftxui::Color sevColor(Sev sev);
// A log message as screen lines `width` wide: wrapped at words, the time on the first line only.
// With `maxLines` it stops there, ending in "...".
ftxui::Elements logLines(const LogEntry& e, int width, int maxLines = 0);
ftxui::Element cell(const std::string& s, int width);             // fixed-width text
ftxui::Element tokens(long long n);                               // gold number
ftxui::Element shards(long long n);                               // blue number
ftxui::Element heatBar(int heat);
ftxui::Element sparkline(const std::deque<double>& values);
ftxui::Element progress(long long cur, long long target, int width);   // gauge + "cur/target"
ftxui::Element ansiLine(const std::string& line);                     // a line with terminal color codes, drawn in color

ftxui::Component Btn(std::function<std::string()> label, std::function<void()> onClick,
                     std::function<bool()> enabled = nullptr);
ftxui::Component Btn(const std::string& label, std::function<void()> onClick,
                     std::function<bool()> enabled = nullptr);
// A quiet, borderless button: muted text that brightens and underlines when it has the keyboard
// or the mouse. `warn` (if given) turns it the Bad color, e.g. while a delete is armed.
ftxui::Component FlatBtn(std::function<std::string()> label, std::function<void()> onClick,
                         std::function<bool()> warn = nullptr);
ftxui::Component MineButton(Ui& ui);
// A one-line text box with a drawn caret. (FTXUI's own Input moves the terminal's real cursor
// on every frame, which flickers on Windows Terminal; this keeps that cursor hidden.)
ftxui::Component TextBox(std::string* content, int* cursor, std::string placeholder,
                         std::function<void()> onEnter = nullptr, int width = 0);
// A column of rows (like Container::Vertical) where the rows of buttons share one column:
// moving up or down lands on the same column in the next row, not wherever that row was last.
// Any child with two or more children of its own counts as a row; the rest are left alone.
ftxui::Component Grid(ftxui::Components rows);

// ----- picker.cpp -----
}  // namespace tui
class Profiles;
namespace tui {
std::string pickProfile(Profiles& profiles);   // the chosen profile's name, or "" to quit

// ----- pages.cpp -----
ftxui::Component MinePage(Ui& ui);
ftxui::Component ShopPage(Ui& ui);
ftxui::Component MarketPage(Ui& ui);
ftxui::Component JobsPage(Ui& ui);
ftxui::Component CrewPage(Ui& ui);
ftxui::Component CuriosPage(Ui& ui);
ftxui::Component MonolithPage(Ui& ui);
ftxui::Component OrreryPage(Ui& ui);
ftxui::Component WorkshopPage(Ui& ui);
ftxui::Component StarsPage(Ui& ui);
ftxui::Component TasksPage(Ui& ui);   // tasks_page.cpp
ftxui::Component ThemeEditor(Ui& ui); // theme_editor.cpp: the custom theme, opened from the Shop
ftxui::Element themeScene(int theme); // a fixed scene using every color of a theme
ftxui::Component GoalsPage(Ui& ui);
ftxui::Component StatsPage(Ui& ui);
ftxui::Component ConsolePage(Ui& ui);

}  // namespace tui

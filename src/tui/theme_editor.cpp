// The custom theme's editor, opened from the Themes tab: every color by role (pick one on the left),
// nudged channel by channel or typed as a hex code, with a fixed scene on the right that uses
// every color. While it's open the whole screen is drawn in the custom theme, so every change
// shows everywhere at once. Changes are saved as they're made.

#include <algorithm>

#include "core/actions.hpp"
#include "core/util.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

namespace {

Element swatchOf(Rgb c) { return text("  ") | bgcolor(Color::RGB(c.r, c.g, c.b)); }

// A contrast ratio, colored by how readable it is (4.5 and up is comfortable for text).
Element contrast(const std::string& what, Rgb fg, Rgb bg) {
    double r = contrastRatio(fg, bg);
    Hue h = r >= 4.5 ? Hue::Good : r >= 3.0 ? Hue::Warn : Hue::Bad;
    std::string verdict = r >= 4.5 ? "easy to read" : r >= 3.0 ? "a bit faint" : "hard to read";
    return hbox({text(what) | muted() | size(WIDTH, EQUAL, 22), text(fixed(r, 1) + ":1 ") | color(hue(h)) | bold, text(verdict) | color(hue(h))});
}

}  // namespace

// A small, fixed scene that uses every color of `theme`, with sample numbers.
Element themeScene(int theme) {
    auto c = [theme](Hue h) { return color(hueOf(theme, h)); };
    auto line = [&](const std::string& t, const std::string& msg, Hue h, bool strong = false) {
        Element m = text(msg) | c(h);
        if (strong) m = m | bold;
        return hbox({text(t + "  ") | c(Hue::Muted), m});
    };
    Element log = window(text(" LOG ") | bold | c(Hue::Accent),
                         vbox({line("12:00:01", "Bought Pickaxe -> lvl 130.", Hue::Good),
                               line("12:00:04", "Market: shard price is 141.", Hue::Text),
                               line("12:00:09", "Not enough tokens.", Hue::Warn),
                               line("12:00:12", "Thieves took 2.1M tokens!", Hue::Bad),
                               line("12:00:15", "EVENT: Meteor Shower!", Hue::Event, true)})) |
                  c(Hue::Muted) | bgcolor(hueOf(theme, Hue::Panel));
    Elements heatBar;
    for (int i = 0; i < 10; ++i) heatBar.push_back(text(i < 4 ? "■" : "□") | c(i < 4 ? Hue::Heat : Hue::Muted));
    Element status = hbox({text(" Tokens ") | c(Hue::Muted), text("48.2M") | c(Hue::Tokens) | bold, text("  Shards ") | c(Hue::Muted),
                           text("6,420") | c(Hue::Shards) | bold, text("  Heat ") | c(Hue::Muted), hbox(heatBar)});
    Element tabs = hbox({text(" Mine  Shop ") | c(Hue::Muted), text(" Jobs ") | bold | color(hueOf(theme, Hue::Background)) | bgcolor(hueOf(theme, Hue::Accent)),
                         text(" Crew  Tasks") | c(Hue::Muted)});
    auto button = [theme](const std::string& t, bool on) {
        return text(" " + t + " ") | bgcolor(hueOf(theme, on ? Hue::Button : Hue::ButtonOff)) | color(hueOf(theme, on ? Hue::ButtonText : Hue::ButtonOffText));
    };
    Element gauge = hbox({text("██████") | c(Hue::Tokens), text("    ") | bgcolor(hueOf(theme, Hue::Panel)), text("  50/180  3m 28s") | c(Hue::Text)});
    Element scene = vbox({
        log,
        status,
        tabs,
        text(" Jobs  1/2") | c(Hue::Accent) | bold,
        hbox({text(" Mine 180 times") | c(Hue::Text) | bold, text("  for Bo ") | c(Hue::Text), text("(Friendly)") | c(Hue::Good)}),
        hbox({text("   Easy: ") | c(Hue::Good) | bold, text("keep mining every 2s") | c(Hue::Good)}),
        hbox({text("   Tight: ") | c(Hue::Warn) | bold, text("heat cools every 6m") | c(Hue::Warn)}),
        hbox({text("   "), gauge}),
        hbox({text(" "), button("Accept", true), text("  "), button("Too pricey", false)}),
        hbox({text(" › ") | c(Hue::Accent), text("the row you're on") | c(Hue::Text) | bold, filler()}) | bgcolor(hueOf(theme, Hue::Panel)),
        hbox({text(" DANGER ") | c(Hue::Bad) | bold, text(" Fire! douse it (x)") | c(Hue::Bad)}),
        hbox({text(" infected: ") | c(Hue::Muted), text("Leech") | c(Hue::Infected) | bold}),
        text(" a quiet hint, like this one") | c(Hue::Muted),
    });
    if (themeClassic(theme)) return scene;
    return scene | color(hueOf(theme, Hue::Text)) | bgcolor(hueOf(theme, Hue::Background));
}

Component ThemeEditor(Ui& ui) {
    auto current = [&ui] { return ui.snap.customTheme[(size_t)std::clamp(ui.themeRole, 0, HUE_COUNT - 1)]; };
    auto setColor = [&ui](Rgb c) {
        Hue h = (Hue)std::clamp(ui.themeRole, 0, HUE_COUNT - 1);
        ui.run([h, c](GameState& g) { setCustomColor(g, h, c); return Notes{}; });   // quiet: the screen shows it
    };

    Component done = Btn("Done", [&ui] { ui.themeMode = 0; });

    // Start over from a theme you own: two clicks, since it replaces every color.
    Components bases;
    for (int t = 0; t < THEME_COUNT; ++t) {
        auto armed = [&ui, t] { return ui.themeBaseArmed == t && Clock::now() < ui.themeBaseUntil; };
        Component b = FlatBtn([armed, t] { return armed() ? std::string(THEMES[t].name) + "? click again" : std::string(THEMES[t].name); },
                              [&ui, t, armed] {
                                  if (!armed()) { ui.themeBaseArmed = t; ui.themeBaseUntil = Clock::now() + CONFIRM_WINDOW; return; }
                                  ui.themeBaseArmed = -1;
                                  ui.run([t](GameState& g) { return copyIntoCustom(g, t); });
                              },
                              armed);
        bases.push_back(Maybe(b, [&ui, t] { return themeOwned(ui.snap, t); }));
    }

    // The roles, one row each: pick one to edit it.
    Components roles;
    for (int h = 0; h < HUE_COUNT; ++h) {
        ButtonOption opt;
        opt.on_click = [&ui, h] { ui.themeRole = h; };
        opt.transform = [&ui, h](const EntryState& st) {
            Rgb c = ui.snap.customTheme[(size_t)h];
            bool sel = ui.themeRole == h;
            Element e = hbox({text(sel ? " › " : "   ") | color(hue(Hue::Accent)), swatchOf(c), text(" "),
                              text(HUES[h].name) | (sel ? bold : nothing) | size(WIDTH, EQUAL, 18), text(hexColor(c)) | muted()});
            return st.focused || sel ? e | bgcolor(hue(Hue::Panel)) : e;
        };
        roles.push_back(Button(opt));
    }
    Component roleList = Container::Vertical(roles, &ui.themeRole);   // the keyboard and the selection move together

    // R, G and B, nudged by 1 or 16. One grid, so Up/Down keeps the step you're on.
    static const int STEPS[] = {-16, -1, 1, 16};
    Components nudgeRows;
    std::vector<Components> nudges(3);
    for (int ch = 0; ch < 3; ++ch) {
        for (int step : STEPS) {
            nudges[(size_t)ch].push_back(Btn((step > 0 ? "+" : "") + std::to_string(step), [current, setColor, ch, step] {
                Rgb c = current();
                unsigned char* v = ch == 0 ? &c.r : ch == 1 ? &c.g : &c.b;
                *v = (unsigned char)std::clamp((int)*v + step, 0, 255);
                setColor(c);
            }));
        }
        nudgeRows.push_back(Container::Horizontal(nudges[(size_t)ch]));
    }
    auto applyHex = [&ui, setColor] {
        Rgb c;
        if (parseHexColor(ui.themeHex, c)) setColor(c);
        else ui.s.log.push(Sev::Warn, "A color is written like #ff8800 (or #f80).");
    };
    Component hexBox = TextBox(&ui.themeHex, &ui.themeHexCursor, "#rrggbb", applyHex, 9);
    ui.textBoxes.push_back(hexBox);
    Component hexSet = Btn("Set", applyHex);

    Component controls = Container::Vertical({Grid(nudgeRows), Container::Horizontal({hexBox, hexSet})});
    Component all = Container::Vertical({Container::Horizontal({done, Container::Horizontal(bases)}),
                                         Container::Horizontal({roleList, controls})});

    return Renderer(all, [=, &ui] {
        ui.previewTheme = CUSTOM_THEME;   // the whole screen, while you're here (the theme list's own renderer isn't running)
        const GameState& g = ui.snap;
        int h = std::clamp(ui.themeRole, 0, HUE_COUNT - 1);
        Rgb c = g.customTheme[(size_t)h];
        if (!hexBox->Focused() && ui.themeHexFor != (h << 24 | c.r << 16 | c.g << 8 | c.b)) {   // follow the color unless you're typing
            ui.themeHex = hexColor(c);
            ui.themeHexCursor = (int)ui.themeHex.size();
            ui.themeHexFor = h << 24 | c.r << 16 | c.g << 8 | c.b;
        }

        Elements baseRow = {text("   start over from: ") | muted()};
        for (int t = 0; t < THEME_COUNT; ++t) {
            if (!themeOwned(g, t)) continue;
            baseRow.push_back(bases[(size_t)t]->Render());
            baseRow.push_back(text("  "));
        }
        Element top = hbox({text("Custom theme") | bold | color(hue(Hue::Accent)),
                            text("   saved as you go; the whole screen shows it while you're here") | muted(), filler(), done->Render()});

        Elements roleRows;
        for (const Component& r : roles) roleRows.push_back(r->Render());

        auto channel = [&](int ch, const char* name, unsigned char v) {
            Elements row = {text(std::string(name) + " ") | bold, text(fixed(v, 0)) | size(WIDTH, EQUAL, 4)};
            for (const Component& b : nudges[(size_t)ch]) { row.push_back(b->Render()); row.push_back(text(" ")); }
            return hbox(row);
        };
        Element edit = vbox({
            hbox({swatchOf(c), swatchOf(c), text(" "), text(HUES[h].name) | bold}),
            paragraph(std::string("Used for ") + HUES[h].use + ".") | muted(),
            text(""),
            channel(0, "R", c.r),
            channel(1, "G", c.g),
            channel(2, "B", c.b),
            text(""),
            hbox({text("hex  "), hexBox->Render() | bgcolor(hue(Hue::Panel)), text(" "), hexSet->Render()}),
            text(""),
            text("How readable it is") | muted(),
            contrast("Text on Background", g.customTheme[(int)Hue::Text], g.customTheme[(int)Hue::Background]),
            contrast("Quiet text on Backgr.", g.customTheme[(int)Hue::Muted], g.customTheme[(int)Hue::Background]),
            contrast("Button text on Button", g.customTheme[(int)Hue::ButtonText], g.customTheme[(int)Hue::Button]),
            contrast("Text on Panel", g.customTheme[(int)Hue::Text], g.customTheme[(int)Hue::Panel]),
        });

        return vbox({
            top,
            hbox(baseRow),
            text(""),
            hbox({vbox(roleRows) | size(WIDTH, EQUAL, 32), text(" "), edit | size(WIDTH, EQUAL, 42), text(" "), themeScene(CUSTOM_THEME) | flex}),
        }) | yframe;
    });
}

}  // namespace tui

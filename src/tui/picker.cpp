// The full-screen profile picker, shown before the game and after switching profiles ('p').
// It runs its own short FTXUI loop. Deleting a profile rebuilds the list, so the loop
// simply restarts with fresh buttons.

#include <algorithm>

#include "core/profiles.hpp"
#include "core/util.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

std::string pickProfile(Profiles& profiles) {
    std::string message;              // last error or confirmation, shown under the list
    Color messageColor = hue(Hue::Warn);

    while (true) {
        std::vector<ProfileInfo> list = profiles.list();
        bool legacy = !Profiles::legacySave().empty();
        std::time_t now = nowSec();

        auto screen = App::Fullscreen();
        screen.ForceHandleCtrlC(false);
        std::string chosen;
        bool restart = false;
        std::string newName;
        int deleteArmed = -1;
        Clock::time_point deleteUntil{};
        int renaming = -1;   // the profile whose new name is being typed in the box, or -1
        std::string placeholder = "name for a new profile";
        Component input;     // made below; the Rename buttons send the keyboard to it

        auto finish = [&](const std::string& name) { chosen = name; screen.Exit(); };
        auto create = [&] {
            std::string why;
            if (renaming >= 0) {   // the box holds a new name for an existing profile
                const std::string& old = list[(size_t)renaming].name;
                if (profiles.rename(old, newName, why)) { message = "Renamed " + old + " to " + trim(newName) + "."; messageColor = hue(Hue::Good); }
                else { message = why; messageColor = hue(Hue::Warn); }
                restart = true;
                screen.Exit();
                return;
            }
            if (profiles.create(newName, why)) finish(profiles.find(newName));
            else { message = why; messageColor = hue(Hue::Warn); }
        };

        // One row per profile: the row itself is the "play" button, plus a two-click Delete.
        Components rows;
        for (size_t i = 0; i < list.size(); ++i) {
            const ProfileInfo p = list[i];
            ButtonOption opt;
            opt.on_click = [&finish, p] { finish(p.name); };
            opt.transform = [p, now](const EntryState& st) {
                Element info = text("new profile") | muted();
                if (p.hasSave && !p.readable) info = text("save can't be read (it'll be set aside)") | color(hue(Hue::Warn));
                else if (p.readable)
                    info = hbox({hbox({filler(), text(num(p.tokens)) | color(hue(Hue::Tokens))}) | size(WIDTH, EQUAL, 15), text(" tokens  "),
                                 cell(p.title, 16), cell(timeAgo(p.lastPlayed, now), 10) | muted(),
                                 p.strain == Strain::None ? text("")
                                 : p.cured ? text(std::string("cured of ") + STRAINS[(int)p.strain].name) | muted()
                                           : text(std::string("INFECTED: ") + STRAINS[(int)p.strain].name) | color(hue(Hue::Infected)) | bold});
                Element e = hbox({text(st.focused ? " > " : "   "), cell(p.name, 18) | bold, info, filler()});
                return st.focused ? e | inverted : e;
            };
            Component play = Button(opt);
            Component del = Btn(
                [&deleteArmed, &deleteUntil, i] { return (int)i == deleteArmed && Clock::now() < deleteUntil ? "Really delete?" : "Delete"; },
                [&, i, p] {
                    if ((int)i == deleteArmed && Clock::now() < deleteUntil) {
                        std::string why;
                        if (profiles.remove(p.name, why)) { message = "Deleted " + p.name + "."; messageColor = hue(Hue::Good); }
                        else { message = why; messageColor = hue(Hue::Bad); }
                        restart = true;
                        screen.Exit();
                    } else {
                        deleteArmed = (int)i;
                        deleteUntil = Clock::now() + CONFIRM_WINDOW;
                    }
                });
            Component ren = Btn("Rename", [&, i, p] {
                renaming = (int)i;
                newName.clear();
                placeholder = "new name for " + p.name;
                deleteArmed = -1;
                if (input) input->TakeFocus();   // the next keys type the name
                message = "Type a new name for " + p.name + " in the box and press Enter. Esc cancels.";
                messageColor = hue(Hue::Accent);
            });
            Component spacedRen = Renderer(ren, [ren] { return hbox({ren->Render(), text(" ")}); });
            rows.push_back(Container::Horizontal({play | flex, spacedRen, del}));
        }

        InputOption inOpt;
        inOpt.placeholder = &placeholder;
        inOpt.multiline = false;
        inOpt.on_enter = create;
        input = Input(&newName, inOpt);
        Component createBtn = Btn([&renaming] { return renaming >= 0 ? std::string("Rename") : std::string("Create"); }, create);
        Component importBtn = Btn("Import my old save", [&] {
            std::string name = trim(newName).empty() ? "Default" : newName, why;
            if (profiles.importLegacy(name, why)) finish(profiles.find(name));
            else { message = why; messageColor = hue(Hue::Warn); }
        });
        Component quitBtn = Btn("Quit", [&] { screen.Exit(); });

        Components bottom = {input, createBtn};
        if (legacy) bottom.push_back(importBtn);
        Component all = Container::Vertical({Container::Vertical(rows), Container::Horizontal(bottom), quitBtn});

        Component ui = Renderer(all, [&] {
            Elements lines;
            for (const Component& r : rows) lines.push_back(r->Render());
            if (rows.empty()) lines.push_back(text("   No profiles yet. Make one below.") | muted());

            Elements newRow = {text(renaming >= 0 ? " Rename " + list[(size_t)renaming].name + " to: " : " New: "),
                               input->Render() | size(WIDTH, EQUAL, 26) | borderLight, text(" "),
                               createBtn->Render() | vcenter};
            if (legacy) { newRow.push_back(text("  ")); newRow.push_back(importBtn->Render() | vcenter); }

            Elements body = {
                text(" Choose a profile") | bold | color(hue(Hue::Accent)),
                text(""),
                vbox(lines),
                text(""),
                hbox(newRow),
            };
            if (legacy)
                body.push_back(text(" Found a save from before profiles: " + Profiles::legacySave().string() +
                                    ". Type a name (or leave it empty for \"Default\") and import it.") | muted());
            body.push_back(text(""));
            body.push_back(text(" " + message) | color(messageColor));
            body.push_back(filler());
            body.push_back(hbox({quitBtn->Render(), text("   Click a profile to play. Saves live in " +
                                                          profiles.folder().string()) | muted()}));
            return window(text(" TokenRush ") | bold | color(hue(Hue::Tokens)), vbox(body));
        });

        if (rows.empty()) input->TakeFocus();   // nothing to pick yet: start in the name box

        Component root = CatchEvent(ui, [&](Event e) {
            if (e == Event::Escape && renaming >= 0) {   // cancel the rename, not the picker
                renaming = -1;
                newName.clear();
                placeholder = "name for a new profile";
                message.clear();
                return true;
            }
            if (e == Event::CtrlC || e == Event::Escape) { screen.Exit(); return true; }
            return false;
        });

        screen.Loop(root);
        if (!restart) return chosen;   // a profile, or "" to quit
    }
}

}  // namespace tui

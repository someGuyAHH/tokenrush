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
    Color messageColor = Color::Yellow;

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

        auto finish = [&](const std::string& name) { chosen = name; screen.Exit(); };
        auto create = [&] {
            std::string why;
            if (profiles.create(newName, why)) finish(profiles.find(newName));
            else { message = why; messageColor = Color::Yellow; }
        };

        // One row per profile: the row itself is the "play" button, plus a two-click Delete.
        Components rows;
        for (size_t i = 0; i < list.size(); ++i) {
            const ProfileInfo p = list[i];
            ButtonOption opt;
            opt.on_click = [&finish, p] { finish(p.name); };
            opt.transform = [p, now](const EntryState& st) {
                Element info = text("new profile") | dim;
                if (p.hasSave && !p.readable) info = text("save can't be read (it'll be set aside)") | color(Color::Yellow);
                else if (p.readable)
                    info = hbox({hbox({filler(), text(num(p.tokens)) | color(Color::Gold1)}) | size(WIDTH, EQUAL, 15), text(" tokens  "),
                                 cell(p.title, 16), cell(timeAgo(p.lastPlayed, now), 10) | dim,
                                 p.strain == Strain::None ? text("")
                                 : p.cured ? text(std::string("cured of ") + STRAINS[(int)p.strain].name) | dim
                                           : text(std::string("INFECTED: ") + STRAINS[(int)p.strain].name) | color(Color::GreenYellow) | bold});
                Element e = hbox({text(st.focused ? " > " : "   "), cell(p.name, 18) | bold, info, filler()});
                return st.focused ? e | inverted : e;
            };
            Component play = Button(opt);
            Component del = Btn(
                [&deleteArmed, &deleteUntil, i] { return (int)i == deleteArmed && Clock::now() < deleteUntil ? "Really delete?" : "Delete"; },
                [&, i, p] {
                    if ((int)i == deleteArmed && Clock::now() < deleteUntil) {
                        std::string why;
                        if (profiles.remove(p.name, why)) { message = "Deleted " + p.name + "."; messageColor = Color::Green; }
                        else { message = why; messageColor = Color::Red; }
                        restart = true;
                        screen.Exit();
                    } else {
                        deleteArmed = (int)i;
                        deleteUntil = Clock::now() + CONFIRM_WINDOW;
                    }
                });
            rows.push_back(Container::Horizontal({play | flex, del}));
        }

        InputOption inOpt;
        inOpt.placeholder = "name for a new profile";
        inOpt.multiline = false;
        inOpt.on_enter = create;
        Component input = Input(&newName, inOpt);
        Component createBtn = Btn("Create", create);
        Component importBtn = Btn("Import my old save", [&] {
            std::string name = trim(newName).empty() ? "Default" : newName, why;
            if (profiles.importLegacy(name, why)) finish(profiles.find(name));
            else { message = why; messageColor = Color::Yellow; }
        });
        Component quitBtn = Btn("Quit", [&] { screen.Exit(); });

        Components bottom = {input, createBtn};
        if (legacy) bottom.push_back(importBtn);
        Component all = Container::Vertical({Container::Vertical(rows), Container::Horizontal(bottom), quitBtn});

        Component ui = Renderer(all, [&] {
            Elements lines;
            for (const Component& r : rows) lines.push_back(r->Render());
            if (rows.empty()) lines.push_back(text("   No profiles yet. Make one below.") | dim);

            Elements newRow = {text(" New: "), input->Render() | size(WIDTH, EQUAL, 26) | borderLight, text(" "),
                               createBtn->Render() | vcenter};
            if (legacy) { newRow.push_back(text("  ")); newRow.push_back(importBtn->Render() | vcenter); }

            Elements body = {
                text(" Choose a profile") | bold | color(Color::Cyan),
                text(""),
                vbox(lines),
                text(""),
                hbox(newRow),
            };
            if (legacy)
                body.push_back(text(" Found a save from before profiles: " + Profiles::legacySave().string() +
                                    ". Type a name (or leave it empty for \"Default\") and import it.") | dim);
            body.push_back(text(""));
            body.push_back(text(" " + message) | color(messageColor));
            body.push_back(filler());
            body.push_back(hbox({quitBtn->Render(), text("   Click a profile to play. Saves live in " +
                                                          profiles.folder().string()) | dim}));
            return window(text(" TokenRush ") | bold | color(Color::Gold1), vbox(body));
        });

        if (rows.empty()) input->TakeFocus();   // nothing to pick yet: start in the name box

        Component root = CatchEvent(ui, [&](Event e) {
            if (e == Event::CtrlC || e == Event::Escape) { screen.Exit(); return true; }
            return false;
        });

        screen.Loop(root);
        if (!restart) return chosen;   // a profile, or "" to quit
    }
}

}  // namespace tui

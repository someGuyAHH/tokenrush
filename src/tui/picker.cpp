// The full-screen profile picker, shown before the game and after switching profiles ('p').
// It runs its own short FTXUI loop. Deleting a profile rebuilds the list, so the loop
// simply restarts with fresh buttons.

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/profiles.hpp"
#include "core/util.hpp"
#include "ui.hpp"
#include "update.hpp"

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
            // Quiet on purpose: the row with the keyboard (or the mouse) gets a soft panel color and an
            // accent marker, not an inverted bar; numbers are compact so they never get cut off.
            opt.transform = [p, now](const EntryState& st) {
                bool hot = st.focused;   // the keyboard, or the mouse hovering
                Element info = text("new profile") | muted();
                if (p.hasSave && !p.readable) info = text("save can't be read (it'll be set aside)") | color(hue(Hue::Warn));
                else if (p.readable)
                    info = hbox({hbox({filler(), text(compact(p.tokens)) | color(hue(Hue::Tokens))}) | size(WIDTH, EQUAL, 9),
                                 text(" tokens   ") | muted(), cell(p.title, 16), cell(timeAgo(p.lastPlayed, now), 11) | muted(),
                                 p.strain == Strain::None ? text("")
                                 : p.cured ? text(std::string("cured of ") + STRAINS[(int)p.strain].name) | muted()
                                           : text(std::string("infected: ") + STRAINS[(int)p.strain].name) | color(hue(Hue::Infected))});
                Element e = hbox({text(hot ? " › " : "   ") | color(hue(Hue::Accent)), cell(p.name, 22) | (hot ? bold : nothing), info, filler()});
                return hot ? e | bgcolor(hue(Hue::Panel)) : e;
            };
            Component play = Button(opt);
            auto armedNow = [&deleteArmed, &deleteUntil, i] { return (int)i == deleteArmed && Clock::now() < deleteUntil; };
            Component del = FlatBtn(
                [armedNow] { return armedNow() ? std::string("really delete?") : std::string("delete"); },
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
                }, armedNow);
            Component ren = FlatBtn([] { return std::string("rename"); }, [&, i, p] {
                renaming = (int)i;
                newName.clear();
                placeholder = "new name for " + p.name;
                deleteArmed = -1;
                if (input) input->TakeFocus();   // the next keys type the name
                message = "Type a new name for " + p.name + " in the box and press Enter. Esc cancels.";
                messageColor = hue(Hue::Accent);
            });
            Component spacedRen = Renderer(ren, [ren] { return hbox({ren->Render(), text("   ")}); });
            Component spacedDel = Renderer(del, [del] { return hbox({del->Render() | size(WIDTH, EQUAL, 14), text(" ")}); });   // "really delete?" doesn't shove "rename"
            rows.push_back(Container::Horizontal({play | flex, spacedRen, spacedDel}));
        }

        InputOption inOpt;
        inOpt.placeholder = &placeholder;
        inOpt.multiline = false;
        inOpt.on_enter = create;
        input = Input(&newName, inOpt);
        Component createBtn = FlatBtn([&renaming] { return renaming >= 0 ? std::string("rename") : std::string("create"); }, create);
        Component importBtn = FlatBtn([] { return std::string("import my old save"); }, [&] {
            std::string name = trim(newName).empty() ? "Default" : newName, why;
            if (profiles.importLegacy(name, why)) finish(profiles.find(name));
            else { message = why; messageColor = hue(Hue::Warn); }
        });
        Component quitBtn = FlatBtn([] { return std::string("quit"); }, [&] { screen.Exit(); });

        // An update found by play.py's check: update now (quit, and play.py pulls, builds and
        // starts the game again), or not now (not asked again this run).
        auto updateShown = [] { return tui::updateReady() && !tui::updateDismissed(); };
        Component updateBtn = FlatBtn([] { return std::string("update now"); }, [&] { requestUpdate(); screen.Exit(); });
        Component laterBtn = FlatBtn([] { return std::string("not now"); }, [] { dismissUpdate(); });
        Component updateRow = Maybe(Container::Horizontal({updateBtn, laterBtn}), updateShown);

        Components bottom = {input, createBtn};
        if (legacy) bottom.push_back(importBtn);
        Component all = Container::Vertical({Grid(rows), Container::Horizontal(bottom), quitBtn, updateRow});

        Component ui = Renderer(all, [&] {
            Elements lines;
            for (const Component& r : rows) lines.push_back(r->Render());
            if (rows.empty()) lines.push_back(text("   No profiles yet. Make one below.") | muted());

            // The name box: a soft panel color, a little lighter while it has the keyboard.
            Element box = input->Render() | size(WIDTH, EQUAL, 26) |
                          bgcolor(input->Focused() ? hue(Hue::ButtonOff) : hue(Hue::Panel));
            Elements newRow = {text(renaming >= 0 ? "   rename " + list[(size_t)renaming].name + " to  " : "   new profile  ") | muted(),
                               box, text("   "), createBtn->Render()};
            if (legacy) { newRow.push_back(text("   ")); newRow.push_back(importBtn->Render()); }

            Elements body = {text("")};
            if (updateShown()) {
                UpdateInfo u = *updateAvailable();
                body.push_back(hbox({text("   Update: ") | color(hue(Hue::Accent)), paragraph(updateSummary(u)) | color(hue(Hue::Text)) | flex}));
                if (u.edited) body.push_back(text("   You've changed some of the code; if the update touches the same files it'll stop and say so.") | muted());
                body.push_back(hbox({text("   "), updateBtn->Render(), text("   "), laterBtn->Render(),
                                     text("     or press u") | muted()}));
                body.push_back(text(""));
            }
            for (Element e : {text("   Choose a profile") | color(hue(Hue::Accent)), text("")}) body.push_back(e);
            Elements rest = {
                vbox(lines),
                text(""),
                hbox(newRow),
            };
            for (Element& e : rest) body.push_back(e);
            if (legacy)
                body.push_back(text(" Found a save from before profiles: " + Profiles::legacySave().string() +
                                    ". Type a name (or leave it empty for \"Default\") and import it.") | muted());
            body.push_back(text(""));
            body.push_back(text("   " + message) | color(messageColor));
            body.push_back(filler());
            body.push_back(hbox({text("   "), quitBtn->Render(), text("     click a profile, or pick one with the arrows and Enter") | muted()}));
            body.push_back(text("   saves live in " + profiles.folder().string()) | muted());
            body.push_back(text(""));
            return window(text(" TokenRush ") | color(hue(Hue::Tokens)), vbox(body)) | color(hue(Hue::Muted));
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
            if (e == Event::Character('u') && updateShown() && !input->Focused()) { requestUpdate(); screen.Exit(); return true; }
            return false;
        });

        {
            // Redraw once the update check finishes, so its prompt shows up without a key press.
            std::jthread waker([&screen](std::stop_token st) {
                while (!st.stop_requested() && !updateCheckDone()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!st.stop_requested()) screen.PostEvent(Event::Custom);
            });
            screen.Loop(root);
        }
        if (updateRequested()) return "";   // main() hands over to play.py
        if (!restart) return chosen;        // a profile, or "" to quit
    }
}

}  // namespace tui

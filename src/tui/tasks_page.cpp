// The Tasks tab: your tasks with their switches, the box to write one in (checked as you type,
// with a ^ under any mistake), and what tasks can read and do.

#include <algorithm>

#include "core/game.hpp"
#include "core/tasks.hpp"
#include "core/util.hpp"
#include "ui.hpp"

using namespace ftxui;

namespace tui {

namespace {

Element heading(const std::string& s) { return text(s) | bold | color(hue(Hue::Accent)); }

constexpr int BOX_INDENT = 11;   // where the text box starts on its line ("  New task ")

}  // namespace

Component TasksPage(Ui& ui) {
    auto present = [&ui](int i) { return [&ui, i] { return i < (int)ui.snap.tasks.size(); }; };
    auto deleteArmed = [&ui](int i) { return i == ui.taskDeleteArmed && Clock::now() < ui.taskDeleteUntil; };

    auto startEdit = [&ui](int i) {
        if (i >= (int)ui.snap.tasks.size()) return;
        ui.taskEditing = i;
        ui.taskDraft = ui.snap.tasks[(size_t)i].text;
        ui.taskCursor = (int)ui.taskDraft.size();
        if (ui.taskBox) ui.taskBox->TakeFocus();
    };
    auto stopEdit = [&ui] {
        ui.taskEditing = -1;
        ui.taskDraft.clear();
        ui.taskCursor = 0;
    };
    // Add (or save the edit). The box is only cleared when it worked.
    auto submit = [&ui, stopEdit] {
        if (trim(ui.taskDraft).empty()) return;
        bool worked = false;
        int editing = ui.taskEditing;
        std::string text = ui.taskDraft;
        ui.run([&worked, editing, text](GameState& g) {
            Notes n = editing >= 0 ? editTask(g, editing, text) : addTask(g, text);
            worked = !n.empty() && n[0].sev == Sev::Good;
            return n;
        });
        if (worked) stopEdit();
    };

    Components rows;
    std::vector<Component> onBtns, editBtns, quietBtns, delBtns;
    for (int i = 0; i < TASK_SLOTS_MAX; ++i) {
        // Running: Suspend (free). Suspended: Resume, for its startup cost.
        Component on = Btn([&ui, i] {
                               if (i >= (int)ui.snap.tasks.size()) return std::string("");
                               const ::Task& t = ui.snap.tasks[(size_t)i];
                               return t.on ? std::string("Suspend") : "Resume " + compact(taskStartupCost(ui.snap, t));
                           },
                           [&ui, i] { ui.run([i](GameState& g) { return i < (int)g.tasks.size() ? setTaskOn(g, i, !g.tasks[(size_t)i].on) : Notes{}; }); },
                           [&ui, i] { return i < (int)ui.snap.tasks.size() && ui.snap.tasks[(size_t)i].on; });
        Component edit = FlatBtn([&ui, i] { return std::string(ui.taskEditing == i ? "editing" : "edit"); }, [startEdit, i] { startEdit(i); });
        Component quiet = FlatBtn([&ui, i] { return std::string(i < (int)ui.snap.tasks.size() && ui.snap.tasks[(size_t)i].quiet ? "loud" : "quiet"); },
                                  [&ui, i] { ui.run([i](GameState& g) { return i < (int)g.tasks.size() ? setTaskQuiet(g, i, !g.tasks[(size_t)i].quiet) : Notes{}; }); });
        Component del = FlatBtn([deleteArmed, i] { return std::string(deleteArmed(i) ? "really delete?" : "delete"); },
                                [&ui, i, deleteArmed, stopEdit] {
                                    if (!deleteArmed(i)) { ui.taskDeleteArmed = i; ui.taskDeleteUntil = Clock::now() + CONFIRM_WINDOW; return; }
                                    ui.taskDeleteArmed = -1;
                                    if (ui.taskEditing == i) stopEdit();
                                    else if (ui.taskEditing > i) ui.taskEditing -= 1;
                                    ui.run([i](GameState& g) { return removeTask(g, i); });
                                },
                                [deleteArmed, i] { return deleteArmed(i); });
        onBtns.push_back(on);
        editBtns.push_back(edit);
        quietBtns.push_back(quiet);
        delBtns.push_back(del);
        rows.push_back(Container::Horizontal({Maybe(on, present(i)), Maybe(edit, present(i)), Maybe(quiet, present(i)), Maybe(del, present(i))}));
    }

    Component box = TextBox(&ui.taskDraft, &ui.taskCursor, "when broken do repair", submit, 0);
    ui.taskBox = box;
    ui.textBoxes.push_back(box);
    Component addBtn = Btn([&ui] { return std::string(ui.taskEditing >= 0 ? "Save" : "Add"); }, submit, [&ui] {
        TaskCheck c = checkTask(ui.taskDraft, &ui.snap);
        return c.ok && (ui.taskEditing >= 0 || (int)ui.snap.tasks.size() < ui.snap.taskSlots);
    });
    Component cancelBtn = Maybe(FlatBtn([] { return std::string("cancel"); }, stopEdit), [&ui] { return ui.taskEditing >= 0; });
    Component licenseBtn = Maybe(Btn("Buy the Automation License (" + num(TASK_LICENSE_COST) + " shards)",
                                     [&ui] { ui.run([](GameState& g) { return buyTaskLicense(g); }); },
                                     [&ui] { return ui.snap.shards >= TASK_LICENSE_COST; }),
                                 [&ui] { return ui.snap.taskSlots == 0; });
    Component slotBtn = Maybe(Btn([&ui] { return "Another slot: " + num(taskSlotCost(ui.snap.taskSlots)) + " sh"; },
                                  [&ui] { ui.run([](GameState& g) { return buyTaskSlot(g); }); },
                                  [&ui] { return ui.snap.shards >= taskSlotCost(ui.snap.taskSlots); }),
                              [&ui] { return ui.snap.taskSlots > 0 && taskSlotCost(ui.snap.taskSlots) > 0; });
    Component helpBtn = FlatBtn([&ui] { return std::string(ui.taskHelp ? "hide what tasks can read and do" : "what can tasks read and do?"); },
                                [&ui] { ui.taskHelp = !ui.taskHelp; });

    Component editor = Maybe(Container::Horizontal({box, addBtn, cancelBtn}), [&ui] { return ui.snap.taskSlots > 0; });
    Component all = Container::Vertical({licenseBtn, slotBtn, Grid(rows), editor, helpBtn});

    return Renderer(all, [=, &ui] {
        const GameState& g = ui.snap;
        Elements out;
        Elements top = {heading("Tasks")};
        if (g.taskSlots > 0) {
            top.push_back(text("  " + std::to_string(g.tasks.size()) + "/" + std::to_string(g.taskSlots) + " slots") | muted());
            long long fees = taskFeesLastMinute(g);
            top.push_back(text("   fees in the last minute ") | muted());
            top.push_back(text(compact(fees)) | color(hue(Hue::Tokens)));
            top.push_back(text(" (they stop at " + compact(taskFeeBudget(g)) + ")") | muted());
        }
        top.push_back(filler());
        top.push_back(slotBtn->Render());
        out.push_back(hbox(top));

        if (g.taskSlots == 0) {
            out.push_back(paragraph("A task is one line that runs an errand for you: when something is true, it does one thing, "
                                    "for a small fee each time. Like 'while broken do repair', or 'every 10m if tokens >= "
                                    "5 * cost.pickaxe do upgrade pickaxe 10'."));
            out.push_back(text(""));
            out.push_back(hbox({licenseBtn->Render(), text("  once, with " + std::to_string(TASK_SLOTS_START) + " slots (up to " +
                                                           std::to_string(TASK_SLOTS_MAX) + ")") | muted()}));
        } else {
            for (int i = 0; i < (int)g.tasks.size() && i < TASK_SLOTS_MAX; ++i) {
                const ::Task& t = g.tasks[(size_t)i];
                Color st = !t.error.empty() ? hue(Hue::Bad) : !t.on ? hue(Hue::Muted) : !t.waiting.empty() ? hue(Hue::Warn) : hue(Hue::Good);
                out.push_back(hbox({text(std::to_string(i + 1) + " ") | bold, onBtns[(size_t)i]->Render(), text(" "),
                                    paragraph(t.text) | (t.on ? nothing : muted()) | flex, text("  "), editBtns[(size_t)i]->Render(),
                                    text("  "), quietBtns[(size_t)i]->Render(), text("  "),
                                    delBtns[(size_t)i]->Render() | size(WIDTH, EQUAL, 14)}));
                out.push_back(hbox({text("       "), text(taskStatus(g, t)) | color(st)}));
            }
            if (g.tasks.empty()) out.push_back(text("  No tasks yet. Write one below and press Enter.") | muted());

            // The box, checked as you type.
            out.push_back(text(""));
            std::string label = ui.taskEditing >= 0 ? "  Task " + std::to_string(ui.taskEditing + 1) + "   " : "  New task ";
            out.push_back(hbox({text(label) | bold | size(WIDTH, EQUAL, BOX_INDENT), box->Render() | flex | bgcolor(hue(Hue::Panel)),
                                text("  "), addBtn->Render(), text("  "), cancelBtn->Render()}));
            if (!trim(ui.taskDraft).empty()) {
                TaskCheck c = checkTask(ui.taskDraft, &g);
                if (!c.ok) {
                    if (c.pos >= 0)
                        out.push_back(hbox({text(std::string((size_t)(BOX_INDENT + c.pos), ' ')),
                                            text(std::string((size_t)std::max(1, c.len), '^')) | bold | color(hue(Hue::Bad))}));
                    out.push_back(hbox({text(std::string(BOX_INDENT, ' ')), paragraph(c.error) | color(hue(Hue::Warn)) | flex}));
                } else {
                    long long fee = taskFee(g, c.instructions);
                    bool starts = ui.taskEditing < 0 || (ui.taskEditing < (int)g.tasks.size() && g.tasks[(size_t)ui.taskEditing].on);
                    long long startup = starts ? taskStartupCost(g, c.instructions) : 0;
                    std::string often = c.repeats ? ", up to " + compact(std::min(fee * 60, taskFeeBudget(g))) + " a minute while it's true"
                                      : c.every > 0 ? ", once every " + duration(c.every) + " at most" : ", once each time it turns true";
                    out.push_back(hbox({text(std::string(BOX_INDENT, ' ')), text("Looks right. ") | color(hue(Hue::Good)),
                                        text(std::to_string(c.instructions) + " instructions: about ") | muted(),
                                        text(compact(fee)) | color(hue(Hue::Tokens)), text(" per run" + often + ".") | muted(),
                                        startup > 0 ? hbox({text(" Starting it: ") | muted(), text(compact(startup)) | color(hue(Hue::Tokens))})
                                                    : text("")}));
                }
            } else if ((int)g.tasks.size() >= g.taskSlots && ui.taskEditing < 0) {
                out.push_back(hbox({text(std::string(BOX_INDENT, ' ')), text("Every slot is in use: delete a task, or buy another slot.") | muted()}));
            } else {
                out.push_back(hbox({text(std::string(BOX_INDENT, ' ')),
                                    text("when <condition> do ... (once each time it turns true)   while <condition> do ... (every second)   "
                                         "every 5m [if <condition>] do ...") | muted()}));
            }
        }

        out.push_back(text(""));
        out.push_back(helpBtn->Render());
        if (ui.taskHelp) {
            Elements reads, does;
            for (int i = 0; i < TASK_VALUE_COUNT; ++i)
                reads.push_back(hbox({text(TASK_VALUES[i].name) | bold | size(WIDTH, EQUAL, 18), text(TASK_VALUES[i].what) | muted()}));
            for (int i = 0; i < TASK_ACTION_COUNT; ++i) does.push_back(text(TASK_ACTIONS[i].usage) | bold);
            does.push_back(text("<who>: a name, a slot number, or anyone") | muted());
            out.push_back(hbox({vbox(reads) | flex, text("  "), vbox(does)}));
            out.push_back(paragraph("Numbers: 500, 5,000, 2.5k, 3M, 1.2B. Times: 30s, 5m, 1h. '5m' is 5 minutes next to payday and 5 million "
                                    "next to tokens. Fees: 0.3s of income per instruction, per run; they stop at a fifth of a minute's "
                                    "income. Starting a task (adding it, saving an edit, resuming it) costs 1s of income per instruction; "
                                    "suspending is free. Tasks never mine, stop disasters, answer offers, fire, hire or take jobs. More: 'help task' "
                                    "in the Console.") | muted());
        }
        return vbox(out) | yframe;
    });
}

}  // namespace tui

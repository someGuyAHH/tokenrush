// Tasks from the Console: list, add, edit, switch on and off, and the license.
// Same rule as every command: lock, call the core, unlock, then print.

#include <iostream>
#include <sstream>

#include "commands.hpp"
#include "core/game.hpp"
#include "core/tasks.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

std::string header(const std::string& title) { return paint(Style::Header, "=== " + title + " ==="); }

int parseIndex(const std::string& arg) {
    long long n;
    if (!parseLongLong(arg, n) || n < 1 || n > 1000) return -1;
    return (int)n - 1;
}

// The text, a caret under the mistake, and what's wrong.
void printMistake(const std::string& text, const TaskCheck& c) {
    out() << "  " << text << "\n";
    if (c.pos >= 0) out() << "  " << std::string((size_t)c.pos, ' ') << paint(Style::Bad, std::string((size_t)std::max(1, c.len), '^')) << "\n";
    out() << paint(Style::Warn, "  " + c.error) << "\n";
}

void printList(const GameState& g) {
    out() << header("TASKS");
    if (g.taskSlots == 0) {
        out() << "\n  Tasks need the Automation License: " << paint(Style::Shards, num(TASK_LICENSE_COST) + " shards") << ", once ("
              << TASK_SLOTS_START << " slots). Type 'task license'.\n";
        out() << paint(Style::Muted, "  'help task' explains how tasks are written.") << "\n";
        return;
    }
    out() << paint(Style::Muted, "   " + std::to_string(g.tasks.size()) + "/" + std::to_string(g.taskSlots) + " slots, fees in the last minute " +
                                     compact(taskFeesLastMinute(g)) + " tokens")
          << "\n";
    for (size_t i = 0; i < g.tasks.size(); ++i) {
        const Task& t = g.tasks[i];
        std::string tag = !t.error.empty() ? "broken" : t.on ? (t.quiet ? "running, quiet" : "running")
                        : "suspended: resume for " + compact(taskStartupCost(g, t));
        Style st = !t.error.empty() ? Style::Bad : t.on ? Style::Good : Style::Muted;
        out() << "  " << (i + 1) << ") " << paint(st, "[" + tag + "]") << " " << t.text << "\n";
        out() << paint(Style::Muted, "       " + taskStatus(g, t)) << "\n";
    }
    if (g.tasks.empty()) out() << paint(Style::Muted, "  No tasks yet. 'task add when broken do repair', for example.") << "\n";
    long long next = taskSlotCost(g.taskSlots);
    if (next > 0) out() << paint(Style::Muted, "  Another slot: " + num(next) + " shards ('task slot').") << "\n";
}

void printValues() {
    out() << paint(Style::Bold, "What tasks can read") << "\n";
    for (int i = 0; i < TASK_VALUE_COUNT; ++i)
        out() << "  " << paint(Style::Bold, padRight(TASK_VALUES[i].name, 18)) << TASK_VALUES[i].what << "\n";
}

void printActions() {
    out() << paint(Style::Bold, "What tasks can do") << "\n";
    for (int i = 0; i < TASK_ACTION_COUNT; ++i)
        out() << "  " << paint(Style::Bold, padRight(TASK_ACTIONS[i].usage, 44)) << TASK_ACTIONS[i].what << "\n";
    out() << paint(Style::Muted, "  <who> is a name (Odette), a slot number (2) or anyone (the first one it works for).") << "\n";
}

// "edit 2 while broken do repair" -> 1 and the text (case kept).
bool splitIndex(const std::string& rest, int& idx, std::string& text) {
    std::stringstream ss(rest);
    std::string n;
    ss >> n;
    idx = parseIndex(n);
    std::getline(ss, text);
    text = trim(text);
    return idx >= 0;
}

}  // namespace

void doTask(Shared& s, const std::string& args) {
    std::stringstream ss(args);
    std::string sub;
    ss >> sub;
    std::string rest;
    std::getline(ss, rest);
    rest = trim(rest);
    sub = lower(sub);

    if (sub.empty() || sub == "list") { std::lock_guard lk(s.m); printList(s.g); return; }
    if (sub == "values") { printValues(); return; }
    if (sub == "actions") { printActions(); return; }

    Notes notes;
    if (sub == "license") {
        std::lock_guard lk(s.m);
        notes = buyTaskLicense(s.g);
    } else if (sub == "slot") {
        std::lock_guard lk(s.m);
        notes = buyTaskSlot(s.g);
    } else if (sub == "add" || sub == "edit") {
        int idx = -1;
        std::string text = rest;
        if (sub == "edit" && !splitIndex(rest, idx, text)) { out() << "Usage: task edit <n> <the new task>\n"; return; }
        if (text.empty()) { out() << "Usage: task " << sub << (sub == "edit" ? " <n>" : "") << " <the task>, like: when broken do repair\n"; return; }
        std::lock_guard lk(s.m);
        TaskCheck c = checkTask(text, &s.g);
        if (!c.ok) { printMistake(text, c); return; }
        notes = sub == "add" ? addTask(s.g, text) : editTask(s.g, idx, text);
    } else if (sub == "on" || sub == "off" || sub == "resume" || sub == "suspend" || sub == "quiet" || sub == "loud" || sub == "remove" ||
               sub == "delete" || sub == "rm") {
        int idx = parseIndex(rest);
        if (idx < 0) { out() << "Usage: task " << sub << " <n>\n"; return; }
        std::lock_guard lk(s.m);
        if (sub == "on" || sub == "off" || sub == "resume" || sub == "suspend") notes = setTaskOn(s.g, idx, sub == "on" || sub == "resume");
        else if (sub == "quiet" || sub == "loud") notes = setTaskQuiet(s.g, idx, sub == "quiet");
        else notes = removeTask(s.g, idx);
    } else if (sub == "move") {
        std::stringstream rs(rest);
        std::string a, b;
        rs >> a >> b;
        int from = parseIndex(a), to = parseIndex(b);
        if (from < 0 || to < 0) { out() << "Usage: task move <n> <to>  (tasks run top to bottom)\n"; return; }
        std::lock_guard lk(s.m);
        notes = moveTask(s.g, from, to);
    } else if (sub == "check") {
        if (rest.empty()) { out() << "Usage: task check <a task>: is it right, and what would a run cost?\n"; return; }
        std::lock_guard lk(s.m);
        TaskCheck c = checkTask(rest, &s.g);
        if (!c.ok) { printMistake(rest, c); return; }
        out() << paint(Style::Good, "  That works.") << " " << c.instructions << " instructions: about "
              << paint(Style::Tokens, compact(taskFee(s.g, c.instructions))) << " tokens per run at today's income.\n";
        return;
    } else {
        out() << "Unknown: task " << sub << ". Try 'task', 'task add <task>', or 'help task'.\n";
        return;
    }
    printNotes(notes);
}

// alias and rmalias: the player's own short names for commands, saved with the profile.
// 'alias predict tokens 1h pt': the last word is the new name, everything before it is what it runs.
// Same rule as every command: lock, change the state, unlock, then print.

#include <algorithm>

#include "commands.hpp"
#include "core/game.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

std::string firstWord(const std::string& s) { return lower(s.substr(0, s.find_first_of(" \t"))); }

const std::string* aliasFor(const GameState& g, const std::string& name) {
    for (const auto& [n, cmd] : g.aliases)
        if (n == name) return &cmd;
    return nullptr;
}

void listAliases(const GameState& g) {
    out() << paint(Style::Header, "=== ALIASES ===")
          << paint(Style::Muted, "   " + std::to_string(g.aliases.size()) + " of " + std::to_string(ALIAS_MAX)) << "\n";
    if (g.aliases.empty()) out() << "  None yet.\n";
    for (const auto& [name, cmd] : g.aliases) out() << "  " << paint(Style::Bold, padRight(name, 16)) << " " << cmd << "\n";
    out() << paint(Style::Muted, "'alias <command...> <name>' makes one ('alias predict tokens 1h pt'); 'rmalias <name>' removes one.") << "\n";
}

// Why `name` can't run `cmd`, or "" if it can.
std::string aliasProblem(const GameState& g, const std::string& name, const std::string& cmd) {
    if (!aliasNameOk(name))
        return "An alias name is one word of up to " + std::to_string(ALIAS_NAME_MAX) + " letters, digits, - or _.";
    if (parseCmd(name) != Cmd::Unknown) return "'" + name + "' is already a command; pick another name.";
    std::string first = firstWord(cmd);
    if (parseCmd(first) == Cmd::Unknown) {
        if (const std::string* other = aliasFor(g, first))
            return "Aliases can't run other aliases: use what '" + first + "' runs instead ('" + *other + "').";
        return "'" + first + "' isn't a command. Type 'help' for the list.";
    }
    if (cmd.find(';') != std::string::npos) return "No ';' in an alias: it runs one command.";
    if (cmd.size() > ALIAS_TEXT_MAX) return "That's too long for an alias (" + std::to_string(ALIAS_TEXT_MAX) + " characters at most).";
    return "";
}

}  // namespace

bool expandAlias(const GameState& g, std::string& line) {
    std::string t = trim(line);
    std::string word = firstWord(t);
    if (word.empty() || parseCmd(word) != Cmd::Unknown) return false;   // a real command always wins
    const std::string* cmd = aliasFor(g, word);
    if (!cmd) return false;
    std::string extra = trim(t.substr(word.size()));
    line = extra.empty() ? *cmd : *cmd + " " + extra;   // with pt = 'predict tokens', 'pt 2h' runs 'predict tokens 2h'
    return true;
}

void doAlias(Shared& s, const std::string& args) {
    std::string t = trim(args);
    std::lock_guard lk(s.m);   // it prints with the lock held, like 'pay': nothing here waits
    GameState& g = s.g;
    if (t.empty()) { listAliases(g); return; }
    size_t cut = t.find_last_of(" \t");
    if (cut == std::string::npos) {
        if (const std::string* cmd = aliasFor(g, lower(t))) { out() << "'" << lower(t) << "' runs '" << *cmd << "'.\n"; return; }
        out() << "The last word is the new name: 'alias predict tokens 1h pt' makes 'pt' run 'predict tokens 1h'.\n";
        return;
    }
    std::string name = lower(t.substr(cut + 1)), cmd = trim(t.substr(0, cut));
    if (std::string why = aliasProblem(g, name, cmd); !why.empty()) { out() << paint(Style::Warn, why) << "\n"; return; }

    for (auto& [n, old] : g.aliases) {
        if (n != name) continue;
        out() << paint(Style::Good, "'" + name + "' now runs '" + cmd + "'") << paint(Style::Muted, " (it ran '" + old + "').") << "\n";
        old = cmd;
        return;
    }
    if ((int)g.aliases.size() >= ALIAS_MAX) {
        out() << paint(Style::Warn, "That's " + std::to_string(ALIAS_MAX) + " aliases already; 'rmalias' one first.") << "\n";
        return;
    }
    g.aliases.push_back({name, cmd});
    out() << paint(Style::Good, "'" + name + "' now runs '" + cmd + "'.")
          << paint(Style::Muted, " Anything typed after it goes on the end.") << "\n";
}

void doRmAlias(Shared& s, const std::string& args) {
    std::string name = lower(trim(args));
    std::lock_guard lk(s.m);
    GameState& g = s.g;
    if (name.empty()) { listAliases(g); return; }
    auto it = std::find_if(g.aliases.begin(), g.aliases.end(), [&](const auto& a) { return a.first == name; });
    if (it == g.aliases.end()) { out() << "No alias called '" << name << "'. Type 'alias' for yours.\n"; return; }
    out() << "Removed '" << name << "'" << paint(Style::Muted, " (it ran '" + it->second + "').") << "\n";
    g.aliases.erase(it);
}

std::string aliasHelp(Shared& s, const std::string& word) {
    std::lock_guard lk(s.m);
    const std::string* cmd = aliasFor(s.g, word);
    return cmd ? *cmd : "";
}

// The Console's completion: what the line being typed would most likely become, shown greyed out
// after the cursor and added on Tab. Read from the TUI's snapshot of the state, so it never locks.

#include <algorithm>
#include <sstream>

#include "commands.hpp"
#include "core/content.hpp"
#include "core/contracts.hpp"
#include "core/game.hpp"
#include "core/tasks.hpp"
#include "core/util.hpp"

namespace {

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string w; ss >> w;) out.push_back(w);
    return out;
}

// The first of `options` that `typed` starts (and isn't all of), as the part still to type, with a
// space after it (`spaced`) so the next word can follow straight away.
std::string firstExtending(const std::vector<std::string>& options, const std::string& typed, bool spaced = true) {
    std::string t = lower(typed);
    if (t.empty()) return "";
    for (const std::string& o : options)
        if (o.size() > t.size() && lower(o).compare(0, t.size(), t) == 0) return o.substr(t.size()) + (spaced ? " " : "");
    return "";
}

// What can come as word number `at` (1 = the first after the command) of command `c`.
std::vector<std::string> argumentsOf(const GameState& g, Cmd c, const std::vector<std::string>& w, size_t at) {
    std::string first = at >= 2 && w.size() > 1 ? lower(w[1]) : "";
    switch (c) {
        case Cmd::Task:
            if (at == 1) return {"add", "edit", "check", "suspend", "resume", "on", "off", "quiet", "loud", "remove", "move", "values",
                                 "actions", "license", "slot"};
            if (first == "add" || first == "edit" || first == "check") return taskWords(false);
            return {};
        case Cmd::Check:
            if (at == 1) return {"list", "count", "titles", "dates", "show", "files", "current", "history", "find"};
            return {};
        case Cmd::Predict:
            if (at == 1) return {"tokens", "shards", "when", "heat", "paydays", "events", "disasters", "mines", "jackpots", "bonuses",
                                 "pickaxe", "magnet", "drone", "bot"};
            if (first == "when") return taskWords(true);
            return {};
        case Cmd::Eval:
            return taskWords(true);
        case Cmd::Theme:
            if (at == 1) {
                std::vector<std::string> out;
                for (int i = 0; i < THEME_COUNT; ++i) out.push_back(THEMES[i].id);
                out.push_back("custom");
                return out;
            }
            if (first == "custom" && at == 2) return {"from"};
            if (first == "custom" && at == 3 && w.size() > 2 && lower(w[2]) == "from") {
                std::vector<std::string> out;
                for (int i = 0; i < THEME_COUNT; ++i) out.push_back(THEMES[i].id);
                return out;
            }
            return {};
        case Cmd::Prompt:
            if (at == 1) return {"custom", "color"};
            if (first == "color" && at == 2) return {"off", "on"};
            return {};
        case Cmd::Auto:      return at == 1 ? std::vector<std::string>{"on", "off"} : std::vector<std::string>{};
        case Cmd::Buy:       return at == 1 ? std::vector<std::string>{"max"} : std::vector<std::string>{};
        case Cmd::Sell:      return at == 1 ? std::vector<std::string>{"all"} : std::vector<std::string>{};
        case Cmd::Repair:    return at == 1 ? std::vector<std::string>{"all"} : std::vector<std::string>{};
        case Cmd::Log:       return {"good", "bad", "warn", "event", "info"};
        case Cmd::Expedition: return at == 2 ? std::vector<std::string>{"short", "long", "deep"} : std::vector<std::string>{};
        case Cmd::Help:      return at == 1 ? commandWords(g) : std::vector<std::string>{};
        case Cmd::Alias:     return at == 1 ? commandWords(g) : std::vector<std::string>{};
        case Cmd::RmAlias: {
            std::vector<std::string> out;
            for (const auto& a : g.aliases) out.push_back(a.first);
            return at == 1 ? out : std::vector<std::string>{};
        }
        case Cmd::Bribe: {
            std::vector<std::string> out;
            for (int i = 0; i < CLIENT_COUNT; ++i)
                if (clientTier(g, i) == Tier::Burned) out.push_back(CLIENTS[i].id);   // the Burned ones first
            for (int i = 0; i < CLIENT_COUNT; ++i) out.push_back(CLIENTS[i].id);
            return at == 1 ? out : std::vector<std::string>{};
        }
        default:
            return {};
    }
}

}  // namespace

std::string completion(const GameState& g, const std::vector<std::string>& history, const std::string& line) {
    if (trim(line).empty()) return "";

    // An earlier line that starts the same way, the newest first (two characters at least).
    if (line.size() >= 2)
        for (auto it = history.rbegin(); it != history.rend(); ++it)
            if (it->size() > line.size() && it->compare(0, line.size(), line) == 0) return it->substr(line.size());

    if (line.back() == ' ') return "";   // nothing being typed: nothing to guess
    std::vector<std::string> w = words(line);
    if (w.size() == 1) {
        std::vector<std::string> options = commandWords(g);
        for (const auto& a : g.aliases) options.push_back(a.first);
        return firstExtending(options, w[0]);
    }

    Cmd c = parseCmd(lower(w[0]));
    if (c == Cmd::Unknown) {   // one of your aliases: complete what it runs
        for (const auto& a : g.aliases)
            if (a.first == lower(w[0])) {
                std::vector<std::string> expanded = words(a.second);
                c = expanded.empty() ? Cmd::Unknown : parseCmd(lower(expanded[0]));
                w.erase(w.begin());
                w.insert(w.begin(), expanded.begin(), expanded.end());
                break;
            }
    }
    // Gadget names can have spaces: complete everything after the command.
    if (c == Cmd::Equip || c == Cmd::Unequip || c == Cmd::Craft) {
        std::string rest = trim(line.substr(line.find(' ') + 1));
        std::vector<std::string> names;
        for (int i = 0; i < GADGET_COUNT; ++i) names.push_back(lower(GADGETS[i].name));
        return firstExtending(names, rest, false);
    }
    return firstExtending(argumentsOf(g, c, w, w.size() - 1), w.back());
}

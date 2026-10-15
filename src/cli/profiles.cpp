// The profile picker shown before the game starts (and after 'logout').

#include <iostream>

#include "shell/commands.hpp"
#include "core/profiles.hpp"
#include "core/util.hpp"
#include "shell/term.hpp"

namespace {

bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, out)) return false;
    out = trim(out);
    return true;
}

void listProfiles(const std::vector<ProfileInfo>& list) {
    std::time_t now = nowSec();
    for (size_t i = 0; i < list.size(); ++i) {
        const ProfileInfo& p = list[i];
        std::cout << "  " << (i + 1) << ") " << paint(Style::Bold, padRight(p.name, 16));
        if (!p.hasSave)       std::cout << paint(Style::Muted, "new");
        else if (!p.readable) std::cout << paint(Style::Warn, "(save can't be read; it'll be set aside when you play)");
        else std::cout << paint(Style::Tokens, padLeft(num(p.tokens), 15)) << " tokens   " << padRight(p.title, 16)
                       << paint(Style::Muted, padRight(timeAgo(p.lastPlayed, now), 10))
                       << (p.strain == Strain::None ? std::string()
                           : p.cured ? paint(Style::Muted, std::string("cured of ") + STRAINS[(int)p.strain].name)
                                     : paint(Style::Bad, std::string("INFECTED: ") + STRAINS[(int)p.strain].name));
        std::cout << "\n";
    }
}

}  // namespace

std::string pickProfile(Profiles& profiles) {
    std::string line, why;
    while (true) {
        std::vector<ProfileInfo> list = profiles.list();
        bool legacy = !Profiles::legacySave().empty();

        // First run ever: skip the menu and just ask for a name.
        if (list.empty() && !legacy) {
            std::cout << paint(Style::Header, "Welcome to TokenRush.") << " Profiles keep separate games apart.\n";
            if (!readLine("Name your profile: ", line)) return "";
            if (profiles.create(line, why)) return profiles.find(line);
            std::cout << paint(Style::Warn, why) << "\n";
            continue;
        }

        std::cout << "\n" << paint(Style::Header, "=== PROFILES ===") << paint(Style::Muted, "  " + profiles.folder().string()) << "\n";
        listProfiles(list);
        std::cout << "  n) New profile\n";
        if (legacy) std::cout << "  i) Import your save from before profiles " << paint(Style::Muted, "(" + Profiles::legacySave().string() + ")") << "\n";
        if (!list.empty()) std::cout << "  d <n>) Delete a profile\n";
        std::cout << "  q) Quit\n";
        if (!readLine("> ", line)) return "";

        std::string word = lower(line.substr(0, line.find(' ')));
        std::string arg = line.find(' ') == std::string::npos ? "" : trim(line.substr(line.find(' ') + 1));
        long long n = 0;

        if (word == "q" || word == "quit" || word == "exit") return "";
        if (parseLongLong(word, n) && n >= 1 && n <= (long long)list.size()) return list[(size_t)n - 1].name;

        if (word == "n" || word == "new") {
            if (!readLine("Name: ", line)) return "";
            if (profiles.create(line, why)) return profiles.find(line);
            std::cout << paint(Style::Warn, why) << "\n";
        } else if ((word == "i" || word == "import") && legacy) {
            if (!readLine("Name for the imported profile (Enter for \"Default\"): ", line)) return "";
            if (line.empty()) line = "Default";
            if (profiles.importLegacy(line, why)) {
                std::cout << paint(Style::Good, "Imported. The old file was kept as tokenrush.save.imported.") << "\n";
                return profiles.find(line);
            }
            std::cout << paint(Style::Warn, why) << "\n";
        } else if ((word == "d" || word == "delete") && parseLongLong(arg, n) && n >= 1 && n <= (long long)list.size()) {
            const std::string& name = list[(size_t)n - 1].name;
            std::cout << paint(Style::Warn, "This deletes " + name + "'s save for good. Type the name to confirm: ") << std::flush;
            if (!std::getline(std::cin, line)) return "";
            if (trim(line) != name) { std::cout << "Cancelled.\n"; continue; }
            if (profiles.remove(name, why)) std::cout << paint(Style::Good, "Deleted " + name + ".") << "\n";
            else std::cout << paint(Style::Bad, why) << "\n";
        } else {
            std::cout << paint(Style::Warn, "Pick a number, or n, " + std::string(legacy ? "i, " : "") + "d <n> or q.") << "\n";
        }
    }
}

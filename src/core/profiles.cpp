#include "profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <random>
#include <system_error>

#include "game.hpp"
#include "platform.hpp"
#include "save.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

fs::path defaultDataRoot() {
#ifdef _WIN32
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && *local) return fs::path(local) / "TokenRush";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::path(xdg) / "tokenrush";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".local" / "share" / "tokenrush";
#endif
    return exeDir() / "TokenRushData";   // nowhere better: keep it next to the game
}

Profiles::Profiles(fs::path dataRoot) : root_(std::move(dataRoot)) {}

fs::path Profiles::folder() const { return root_ / "profiles"; }
fs::path Profiles::dirOf(const std::string& name) const { return folder() / name; }

std::vector<ProfileInfo> Profiles::list() const {
    std::vector<ProfileInfo> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(folder(), ec)) {
        if (!e.is_directory(ec)) continue;
        ProfileInfo p;
        p.name = e.path().filename().string();
        p.dir = e.path();
        fs::path save = e.path() / "tokenrush.save";
        p.hasSave = fs::exists(save, ec);
        if (p.hasSave) {
            GameState g;
            if (loadGameFile(save, g).status == LoadStatus::Ok) {
                p.readable = true;
                p.tokens = g.tokens;
                p.title = TITLES[titleIndex(renownOf(g))].name;
                p.lastPlayed = g.lastUpdate;
                p.strain = g.strain;
                p.cured = g.cured;
            }
        }
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const ProfileInfo& a, const ProfileInfo& b) {
        if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

std::string Profiles::find(const std::string& name) const {
    std::string want = lower(trim(name));
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(folder(), ec))
        if (e.is_directory(ec) && lower(e.path().filename().string()) == want) return e.path().filename().string();
    return "";
}

bool Profiles::exists(const std::string& name) const { return !find(name).empty(); }

bool Profiles::validName(const std::string& raw, std::string& why) const {
    std::string name = trim(raw);
    if (name.empty())      { why = "Give the profile a name."; return false; }
    if (name.size() > 24)  { why = "Keep it to 24 characters or fewer."; return false; }
    for (char c : name) {
        unsigned char u = (unsigned char)c;
        if (!(std::isalnum(u) || c == ' ' || c == '-' || c == '_')) {
            why = "Use letters, digits, spaces, - and _ only.";   // keeps it a safe folder name everywhere
            return false;
        }
    }
    static const char* const reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5",
                                           "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4",
                                           "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* r : reserved)
        if (lower(name) == r) { why = "Windows doesn't allow that name for a folder."; return false; }
    if (exists(name)) { why = "There's already a profile called " + name + "."; return false; }
    return true;
}

namespace {

bool makeFolder(const fs::path& dir, std::string& why) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { why = "Couldn't create the profile folder: " + ec.message(); return false; }
    return true;
}

}  // namespace

bool Profiles::create(const std::string& raw, std::string& why, Strain* strain) {
    if (!validName(raw, why) || !makeFolder(dirOf(trim(raw)), why)) return false;

    std::mt19937 rng(makeSeed());
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    Strain s = Strain::None;
    if (uni(rng) < INFECTION_CHANCE) {
        std::uniform_int_distribution<int> pick(1, STRAIN_COUNT - 1);
        s = (Strain)pick(rng);
        GameState g;   // the first save only carries the infection; the game starts fresh
        g.strain = s;
        if (!writeSaveFileAt(dirOf(trim(raw)) / "tokenrush.save", g)) {
            why = "Couldn't write the new profile's save.";
            return false;
        }
    }
    if (strain) *strain = s;
    return true;
}

bool Profiles::remove(const std::string& name, std::string& why) {
    std::string real = find(name);
    if (real.empty()) { why = "No profile called " + name + "."; return false; }
    std::error_code ec;
    fs::remove_all(dirOf(real), ec);
    if (ec) { why = "Couldn't delete it: " + ec.message(); return false; }
    return true;
}

fs::path Profiles::legacySave() {
    fs::path p = exeDir() / ".save" / "tokenrush.save";
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path();
}

bool Profiles::importLegacy(const std::string& raw, std::string& why) {
    fs::path from = legacySave();
    if (from.empty()) { why = "There's no old save to import."; return false; }
    if (!validName(raw, why) || !makeFolder(dirOf(trim(raw)), why)) return false;   // imports are never infected

    std::error_code ec;
    fs::copy_file(from, dirOf(trim(raw)) / "tokenrush.save", fs::copy_options::overwrite_existing, ec);
    if (ec) { why = "Couldn't copy the old save: " + ec.message(); return false; }
    fs::path done = from;
    done += ".imported";
    fs::rename(from, done, ec);   // keep the original, just stop offering it
    return true;
}

std::string timeAgo(std::time_t then, std::time_t now) {
    if (then <= 0) return "never";
    long long s = (long long)std::difftime(now, then);
    if (s < 60)        return "just now";
    if (s < 3600)      return std::to_string(s / 60) + "m ago";
    if (s < 86400)     return std::to_string(s / 3600) + "h ago";
    return std::to_string(s / 86400) + "d ago";
}

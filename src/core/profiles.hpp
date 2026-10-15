#pragma once
// Profiles: one folder per player, picked at startup (no passwords, for now).
//
//   Windows:      %LOCALAPPDATA%\TokenRush\profiles\<name>\tokenrush.save
//   Linux/macOS:  $XDG_DATA_HOME/tokenrush/profiles/<name>/   (default ~/.local/share/...)
//
// Saves from before profiles existed lived in a .save folder next to the executable; those
// can be imported into a new profile (the original is kept, renamed to *.imported).

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "content.hpp"

std::filesystem::path defaultDataRoot();

struct ProfileInfo {
    std::string name;
    std::filesystem::path dir;
    bool hasSave = false;
    bool readable = false;      // the save loaded fine (otherwise the summary is empty)
    long long tokens = 0;
    std::string title;
    std::time_t lastPlayed = 0;
    Strain strain = Strain::None;   // Infected profiles (see content.hpp)
    bool cured = false;
};

class Profiles {
public:
    explicit Profiles(std::filesystem::path dataRoot);

    std::filesystem::path folder() const;                         // .../profiles
    std::filesystem::path dirOf(const std::string& name) const;
    std::vector<ProfileInfo> list() const;                        // most recently played first
    bool exists(const std::string& name) const;                   // case-insensitive, like Windows
    std::string find(const std::string& name) const;              // the folder's real spelling, or ""

    bool validName(const std::string& name, std::string& why) const;
    // A new profile is Infected with a random strain INFECTION_CHANCE of the time; that's
    // written into its first save. `strain` (if given) says which, or Strain::None.
    bool create(const std::string& name, std::string& why, Strain* strain = nullptr);
    bool remove(const std::string& name, std::string& why);       // deletes the folder and its save

    // The save from before profiles, if there still is one.
    static std::filesystem::path legacySave();
    bool importLegacy(const std::string& name, std::string& why);

private:
    std::filesystem::path root_;
};

std::string timeAgo(std::time_t then, std::time_t now);   // "just now", "5m ago", "3h ago", "2d ago"

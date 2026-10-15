#pragma once
// Saving and loading. The format is an INI file; see save.cpp for the field table.

#include <filesystem>
#include <string>

#include "state.hpp"

inline constexpr int SAVE_VERSION = 9;

// Where the save lives: the current profile's folder. Call setSaveDir before any worker
// thread exists (at startup, or between sessions when switching profiles).
void setSaveDir(const std::filesystem::path& dir);
bool ensureSaveDir(std::string& why);   // creates the folder if needed
std::filesystem::path savePath();       // .../tokenrush.save
std::filesystem::path badSavePath();    // .../tokenrush.save.bad

enum class LoadStatus { Ok, NoFile, Corrupt };
struct LoadResult {
    LoadStatus status;
    std::string why;      // set when status == Corrupt
    std::string notice;   // something the player should know about an Ok load (may be empty)
};

// Loads into `out`. `out` is left untouched unless the result is Ok.
LoadResult loadGame(GameState& out);                                           // from savePath()
LoadResult loadGameFile(const std::filesystem::path& file, GameState& out);    // from any file

// Copies the state under the game lock, then writes the copy. Safe to call from any thread.
bool saveNow(Shared& s);
bool writeSaveFileAt(const std::filesystem::path& file, const GameState& g);   // any file (a new profile's first save)

// Moves an unreadable save aside to the .bad file so the next save can't silently destroy it.
bool quarantineSave();

void deleteSaveFiles();

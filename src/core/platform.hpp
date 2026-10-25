#pragma once
// The few things that differ between Windows and everything else.

#include <filesystem>

std::filesystem::path exeDir();   // folder containing the running executable (current dir if unknown)

// Closing the terminal window (SIGHUP) or a plain `kill` (SIGTERM): FTXUI leaves its loop and then
// re-raises the signal, which used to end the process before the final save. With this handler
// installed first, the re-raised signal only sets a flag, the game saves and quits as on 'q'.
void catchHangup();
bool hangupReceived();

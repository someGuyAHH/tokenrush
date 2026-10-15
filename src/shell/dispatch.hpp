#pragma once
// Runs one typed command line. The CLI's input loop and the TUI's console tab both use this,
// so a command behaves the same in both (menus and "are you sure?" questions included: they
// read their answers from in(), see term.hpp).

#include <random>
#include <string>

#include "core/state.hpp"

enum class Outcome { Done, Empty, Clear, Quit, Logout };

// `profiles` is false when the game runs from --save-dir (then 'logout' has nowhere to go).
Outcome runCommand(Shared& s, std::mt19937& rng, const std::string& line, bool profiles);

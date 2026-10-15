#pragma once
// Runs one typed command line, for the TUI's Console tab (menus and "are you sure?" questions
// included: they read their answers from in(), see term.hpp).

#include <random>
#include <string>

#include "core/state.hpp"

enum class Outcome { Done, Empty, Clear, Quit, Logout };

// `profiles` is false when the game runs from --save-dir (then 'logout' has nowhere to go).
Outcome runCommand(Shared& s, std::mt19937& rng, const std::string& line, bool profiles);

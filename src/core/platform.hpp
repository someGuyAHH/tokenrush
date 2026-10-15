#pragma once
// The few things that differ between Windows and everything else.

#include <filesystem>

std::filesystem::path exeDir();   // folder containing the running executable (current dir if unknown)

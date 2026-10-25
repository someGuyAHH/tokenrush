# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

TokenRush: an idle mining/clicker game, C++20, full-screen terminal UI built on FTXUI. No test suite; the balance simulator (`tools/sim.cpp`) is the closest thing to one.

## Build and run

Works on Windows (MSYS2 MinGW), Linux and macOS. Output goes to `target/debug/` (`.exe` suffix on Windows only).

    python tools/fetch_ftxui.py                          # once: FTXUI 7.0.3 into dependencies/ (gitignored)
    python build.py [--tui] [--sim] [--rebuild] [-j N]   # incremental, hash-based; CXX=clang++ to switch compiler
    python play.py [--profile <name>]                    # builds if needed, runs, handles self-update
    make / make sim / make clean                         # Linux/macOS alternative; output in build/, binaries at the repo root

- Simulator: `target/debug/sim <hours> <seeds> <strategy> [row_minutes]`, e.g. `sim 4 3 normal 10`. Strategies: normal, trader, idle, noecon, tasks-light, tasks-heavy, tasks-greedy, speedrun. Run it after changing any game numbers.
- `build.py` uses `-O1`, the Makefile `-O2`; both use `-Wall -Wextra`. Windows adds `-DUNICODE -D_UNICODE -DNOMINMAX`, others `-pthread`.
- Platform-specific code is confined to `#ifdef _WIN32` / `__APPLE__` blocks (`core/platform.cpp`, `core/profiles.cpp` for the data folder, `core/update.cpp` for `popen`); the macOS branch of `exeDir()` is untested.

## Architecture

Three layers with a strict dependency direction: `tui` -> `shell` -> `core`.

- `src/core/` is the game with no I/O: it never prints or reads input. `GameState` (`state.hpp`) holds everything; `actions` is everything the player can do; `game` holds rules (costs, passive income, titles); `content` holds the data tables; `ticks` advances the world one second at a time. `workers` runs five background threads that call the ticks, and `session` owns options, loading and the shutdown order. The simulator links only this layer.
- `src/shell/` implements the typed commands (the in-game Console tab). `help.cpp` holds the command table (names, aliases, help text); `dispatch` maps a typed line to a command. Output goes through `out()`/`in()` from `term.hpp`, which the Console redirects, so commands must not use `std::cout` directly.
- `src/tui/` is the FTXUI front end: `main.cpp` (layout, global keys, shutdown), `pages.cpp` (one function per tab), `console.cpp` (runs the shell loop on its own thread).

Every game action is reachable both from a tab button and as a Console command; keep the two in sync when adding features.

## Save files

`tokenrush.save` is INI, driven by a single field table: to add a save field, add one line to `saveFields()` in `src/core/save.cpp`, which handles read, write and validation. Unknown keys are ignored, missing keys keep defaults, and an unreadable file is moved to `.save.bad`. Old save versions must keep loading. Profiles (`core/profiles`) and the cross-profile `exchange` folder are documented in `FEATURES.md`.

## Updates and commit messages

`play.py` makes the game poll GitHub for new commits. Commit messages are parsed: a message marked `Update: gameplay` forces connected players to save, quit and update immediately; anything else is a quiet "minor" update. Only use that marker for changes that alter how the game plays.

## Docs

`README.md` has the full command and feature walkthrough and a file-by-file layout table; `FEATURES.md` (~60 KB) is the detailed feature reference. Check both before changing game mechanics.

## Intended mechanics (not bugs)

Things that look like exploits but are deliberate. Don't "fix" them in audits or refactors; raise them with the owner first if you think one needs changing.

- **Swapping gadgets between trades.** Equipping and unequipping are free and take effect at once, so a player can, for example, buy with the Moon Anchor off and sell with it on, or sell with the Golden Ledger on and buy back with it off, and come out ahead. Tasks can automate the loop. This is a technique the player works out: they do the math and write logic that is basically code. Don't add warm-ups, cooldowns or swap costs to gadgets: a 10-minute warm-up was proposed in PR #4 (audit 2026-10-25) and rejected because it makes gadgets boring to use.

# TokenRush

A token-mining idle game with background automation, in two editions that share one game
and one save:

- **tokenrush-tui**: full screen. A pinned log, tabs, and a clickable MINE button.
- **tokenrush-cli**: the classic typed-command version.

C++20. The full-screen edition uses [FTXUI](https://github.com/ArthurSonzogni/FTXUI) 7.0.3.

## Build

Windows (Python 3.10+ and MSYS2 MinGW g++ on the PATH), into `target\debug\`:

    python build.py              # both editions
    python build.py --cli        # just tokenrush-cli.exe
    python build.py --tui        # just tokenrush-tui.exe
    python build.py --sim        # the balance simulator
    python build.py --rebuild    # ignore what's already built and compile everything

Builds are incremental: each `.cpp` is compiled to its own `.o` in `target\debug\obj\`, with a
hash of the `.cpp`, every header it includes, and the compiler flags. Only files whose hash
changed are compiled again (edit one `.cpp`, one file compiles; edit a header, only the files
that include it do), and an exe is only relinked when one of its objects changed. Times go to
`target\debug\build-times.log`. (`build.ps1` is the older PowerShell version of the same thing.)

The TUI needs FTXUI's amalgamated header and a compiled object in `dependencies\`:
`ftxui.hpp` and `ftxui.o`, built once with

    g++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DNOMINMAX -c ftxui.cpp -o ftxui.o

Linux / macOS: `make` (put `ftxui.hpp` and `ftxui.cpp` in `dependencies/`).

## Run

    tokenrush-tui [--profile <name>] [--data-dir <folder>] [--save-dir <folder>] [--no-color]
    tokenrush-cli   (same options)

Both start with a **profile picker**: choose a profile, make a new one, or delete one (no
passwords, for now). Each profile is a folder with its own save:

    Windows:      %LOCALAPPDATA%\TokenRush\profiles\<name>\tokenrush.save
    Linux/macOS:  ~/.local/share/tokenrush/profiles/<name>/tokenrush.save

- `--profile <name>` skips the picker (and creates the profile if it's new).
- `--data-dir <folder>` keeps profiles somewhere else.
- `--save-dir <folder>` ignores profiles and plays straight from that folder.
- `--no-color` (or the `NO_COLOR` environment variable) turns colors off.

A save from before profiles (the `.save` folder next to the exe) can be imported from the
picker. The original is kept, renamed to `tokenrush.save.imported`.

Both editions read the same profiles, so you can switch between them, just not play the same
profile in both at once.

**TUI keys:** click anything, or `1`-`9` and `0` to switch tabs (Mine, Shop, Goals, Stats, then
Curios, Monolith, Market, Jobs, Crew, Orrery in unlock order; locked ones aren't shown), `m` to
mine, `s` to save, `y`/`n` to answer an offer, `:` for the Console, `p` to switch profile, `q`
or Ctrl+C to quit. Tab, the arrows and Enter move around and press buttons. While a text box
has the keyboard (the Console, the Market's amount box), keys type into it and Esc steps out.
Risky buttons (Fire, Abandon, Delete, and Buy when it would buy at a steep price) need a
second click within 3 seconds.

**The Console tab** is the CLI inside the TUI: the same commands, menus and questions, in
color, with Up/Down for earlier lines. That's also where the CLI-only bits (prompt styles,
`wipe`) live in the TUI.

**CLI:** type `help`. `logout` goes back to the profile list, `cls`/`clear` clears the screen,
and Enter on an empty line does nothing. `shop 1 10` buys 10 Pickaxe levels without the menu,
`curios 3` buys curio 3, and `buy`/`sell` take amounts like `2k`.

## What's in the game

A new game starts with just mining, the shop and the goals. Goals earn renown, renown earns
titles, and titles open the rest of the game, one piece at a time:

| Title | Renown | Opens |
|---|---|---|
| Prospector | 4 | Curios |
| Hustler | 10 | the Monolith |
| Baron | 18 | the Market |
| Magnate | 28 | Contracts |
| Legend | 40 | the Crew |
| Monolith-Maker | 55 | the Orrery |

Once open, a feature stays open. Saves from before this kept everything they were already using.
The table lives in `FEATURES` in `src/core/content.cpp`.

- **Mining, upgrades, curios, prompt styles, the Monolith.** The original game; the Monolith
  is the mid-game project. Upgrades can be bought 10 or 20 levels at a time (the Buy 10 and
  Buy 20 buttons, or `1 10` in the CLI shop). The Mine tab (and `status`) shows what you've
  earned per second over the last 10 seconds, from every source. (Heists are gone. Saves that still mention them load fine; an old
  heist contract is cancelled and its stake refunded.)
- **The Orrery.** The late-game project, built with shards (`orrery <n|all>` in the CLI, the
  Orrery tab in the TUI). Every stage unlocks something:

  | Stage | Shards | Unlocks |
  |---|---|---|
  | Base Plate | 2,000 | a 5th crew slot |
  | First Ring | 6,000 | Resonance: Drones and the Mining Bot scale with your mining |
  | Second Ring | 15,000 | a second contract slot |
  | Gearwork | 35,000 | crew can train up to level 8 |
  | The Sun | 80,000 | the last goal, and the Celestial title |

  Token wealth can't rush it: every shard you buy pushes the price up, so a fortune buys only
  a few thousand shards before the price needs time to settle. `orrery all` keeps back your
  crew's next payday.
- **Shards and the market.** A second currency, bought and sold with tokens. The price wanders,
  drifts back toward 150, and every shard you trade pushes it a little. Buying costs a bit
  above the price and selling pays a bit below it (the spread).
- **Contracts.** Timed jobs on a board of three. Targets are sized from how fast you've
  actually been playing. Big jobs need a shard stake and pay more. Fail or abandon one and you
  lose the stake and gain **Heat**. One job at a time (two with the Orrery's Second Ring, as
  long as they're different kinds).
- **Heat.** Widens the market spread. From 3 up, audits can seize part of your tokens.
  It cools slowly on its own; big jobs you finish cool it by 1.
- **Crew.** Up to four named hires (five with the Orrery), paid in shards every 5 minutes
  (their level in shards each).
  Miners mine, Brokers improve the market, Scouts find shards, Fixers handle heat and audits.
  Anyone you can't pay quits.

- **Crew traits and promotions.** About two in three candidates have a trait, always a
  trade-off: Diligent (works 25% harder), Greedy (+50% work, double wages), Frugal (half the
  wages, 15% less work), Quick Learner (half-price training), Ambitious (cheaper promotions),
  Loyal (stays through one missed payday), Night Owl (works twice as hard while you're not
  mining). A member at their level cap can be **promoted** for shards: +3 levels of cap and
  every level counts 25% more. Each promotion costs 3x the last (300, 900, 2,700...), so
  there's no limit, just a point where it stops paying.
- **Offers.** Every 10-15 minutes someone makes you an offer; answer yes or no within 45
  seconds (ignoring it is a no). A shady broker selling cheap shards for heat, an investor
  who usually pays back 1.3x (and sometimes vanishes), a rival betting you can't mine 40
  times in a minute, a crew member asking for a raise, an auditor you can bribe when heat is
  high. Each only shows up once the features it involves are open.
- **Infected profiles.** A quarter of new profiles are Infected with one strain that makes
  that game harder: Fever (heat never cools on its own), Rust (upgrades cost double), Static
  (a wilder market with twice the spread), Mutiny (double crew wages), Leech (1% of your
  tokens drains every 5 minutes) or Amnesia (your streak fades after 6 seconds). Completing
  the Orrery cures it. Nothing carries over between profiles, so a fresh profile you don't
  like can simply be deleted.
- **Stats.** The Stats tab (`stats` in the CLI): your records, when you reached each title,
  and a chart of everything you've earned over your play time (log scale, a point every 5
  minutes, kept in the save).

The bot, crew, market and contract timers only run while the game is open. Idle earnings
from Drones are capped at 12 hours.

## Layout

| Folder / file | What it holds |
|---|---|
| `src/core/` | The game itself. Never prints, never reads input. |
| `core/state.hpp/.cpp` | `GameState`, the message log (with severities), `Shared` |
| `core/actions.hpp/.cpp` | Everything the player can do: mine, buy, build... |
| `core/market`, `core/contracts`, `core/crew` | The shard market, contracts and heat, the crew |
| `core/choices.hpp/.cpp` | Offers (choice events), the Investor's loan, the rival's race |
| `core/ticks.hpp/.cpp` | One second of the world, with time passed in |
| `core/workers.hpp/.cpp` | The five background threads that call the ticks |
| `core/game.hpp/.cpp` | Rules: costs, passive income, events, goals, titles |
| `core/content.hpp/.cpp` | Data tables: curios, goals, titles, roles, crew names, contract kinds |
| `core/save.hpp/.cpp` | INI save file: one field table drives read, write and validation |
| `core/platform`, `core/util` | Executable path; formatting and parsing helpers |
| `core/session.hpp/.cpp` | Options, loading, the worker threads: the startup both editions share |
| `core/profiles.hpp/.cpp` | Where profiles live; list, create, delete, import an old save |
| `src/shell/` | The typed commands, shared by the CLI and the TUI's Console tab |
| `shell/dispatch.hpp/.cpp` | One typed line -> the right command |
| `shell/commands.cpp`, `spending.cpp`, `economy.cpp` | The screens |
| `shell/term.hpp/.cpp` | Colors (`NO_COLOR` aware), charts, padding; `out()`/`in()`, which the Console redirects |
| `src/cli/` | The command-line edition |
| `cli/main.cpp` | Startup, options, the input loop, Ctrl+C, shutdown order |
| `cli/profiles.cpp` | The profile picker |
| `src/tui/` | The full-screen UI (FTXUI) |
| `tui/main.cpp` | Startup, the frame layout (log, status bar, tabs), global keys, shutdown |
| `tui/pages.cpp` | One function per tab |
| `tui/console.hpp/.cpp` | The Console tab's engine: the shell's command loop on its own thread |
| `tui/widgets.cpp` | Buttons, the MINE button, colors, gauges, the chart |
| `tui/picker.cpp` | The profile picker |
| `dependencies/` | FTXUI (`ftxui.hpp` + `ftxui.o`), not part of the source |
| `tools/sim.cpp` | Headless balance simulator (see below) |

## Save file

`tokenrush.save` is a plain INI file you can edit while the game is closed. Unknown keys are
ignored, missing keys keep their defaults, and a file that can't be read is moved to
`tokenrush.save.bad` instead of being overwritten. Saves from every earlier version still load.

Crew members are numbered sections (`[crew.1]`, `[crew.2]`...), active contracts are
`[contract.1]` and `[contract.2]`, `[progress] unlocked` lists the features that are open,
`[infection]` holds an Infected profile's strain, and `[history] samples` is the Stats chart. To add a field to the save, add one line to `saveFields()` in
`src/core/save.cpp`.

## Balance simulator

`tools/sim.cpp` links only `src/core` and plays the game with a scripted player, one simulated
second per loop, so hours of play take a moment. It prints a progress table per seed.

    python build.py --sim                 # Windows; or `make sim` elsewhere
    .\target\debug\sim.exe 4 3 normal     # 4 hours, 3 seeds; strategies: normal, trader, idle, noecon

Use it after changing numbers: `noecon` is the old game for comparison, and `trader` checks
that playing the market alone can't beat actually playing.

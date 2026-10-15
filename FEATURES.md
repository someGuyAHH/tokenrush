# TokenRush: every feature, in one place

A reference for picking the project back up later. The README covers building and running.
This file covers what the game does, the numbers behind it, where each piece lives, and the
design decisions made along the way. When a number here and the code disagree, the code wins:
the tunables live in the headers named next to each section.

Current save version: **8**. C++20; the full-screen edition uses FTXUI 7.0.3.

---

## The two editions

- **tokenrush-cli**: typed commands (`help` lists them). This is the edition Script plays most.
- **tokenrush-tui**: full screen. A pinned log at the top, a status bar, tabs, clickable everything.
- Both play the **same game and the same profiles/saves**, just not the same profile at once.
- The typed commands live in `src/shell/` and are shared: the CLI's input loop and the TUI's
  **Console** tab both run them through `runCommand()` (`shell/dispatch.cpp`).

### TUI layout and keys
- Tabs, in order: **Mine, Shop, Goals, Stats**, then **Curios, Monolith, Market, Jobs, Crew,
  Orrery** as they unlock (locked tabs are hidden), then **Console** (always last).
- Keys:
  - `1`–`9` and `0`: the first ten visible tabs.
  - `m`: mine.
  - `s`: save.
  - `y`/`n`: answer an offer.
  - `:`: open the Console.
  - `p`: switch profile.
  - `q` or Ctrl+C: quit.
- **Typing:** while a text box has the keyboard (the Console, the Market's amount box), keys
  type instead of acting, and Esc steps out.
- **Status bar:**
  - tokens, then shards and heat once they're unlocked
  - the infection badge
  - the title
  - the active jobs, or the next unlock
  - a race, a loan
- **OFFER bar:** appears under the status bar when an offer is open. It's last in the focus
  order, so an offer never lands the keyboard on "Yes".
- **Risky buttons** need a second click within 3 s: Fire, Abandon, Delete profile, and Buy
  (max or a custom amount) when it would buy at a steep price.
- **Text boxes** draw their own caret (`TextBox` in `tui/widgets.cpp`). FTXUI's Input moves the
  real cursor every frame, which flickered on Windows Terminal.

### CLI specifics
- **Empty Enter** does nothing: the cursor goes back after the prompt.
- **Clearing the screen:** `cls`/`clear`.
- **Logging out:** `logout` goes back to the profile picker.
- **Background news** prints before each prompt.
- **One-line shortcuts:**
  - `shop 1 10`: 10 Pickaxe levels.
  - `curios 3`: buy curio 3.
  - `buy 2k` / `sell 2k`: amounts accept k/m/b and commas.

---

## Profiles (`core/profiles.*`, pickers in `cli/profiles.cpp`, `tui/picker.cpp`)
- One folder per profile, no passwords:
  - Windows: `%LOCALAPPDATA%\TokenRush\profiles\<name>\tokenrush.save`
  - Linux/macOS: `~/.local/share/tokenrush/...`
- The picker can create, play and delete profiles, and can import a pre-profiles save (the
  `.save` folder next to the exe).
- Options:
  - `--profile <name>`
  - `--data-dir <folder>`
  - `--save-dir <folder>`: no profiles at all.
  - `--no-color` (or `NO_COLOR`)

### Infected profiles (`Strain` in `core/content.*`)
- A new profile has a **25%** chance to be Infected with one strain. The strain is written into
  its first save, so the game counts it as a new game.
- The strains:

  | Strain | Effect |
  |---|---|
  | Fever | heat never cools on its own |
  | Rust | upgrades cost double |
  | Static | the market's spread and volatility are doubled |
  | Mutiny | crew wages are doubled |
  | Leech | 1% of your tokens drains every 5 min of play |
  | Amnesia | your streak expires after 6 s instead of 15 |

- **Completing the Orrery cures it**: the effects stop and the profile shows "cured of X".
- `wipe` keeps the strain (it belongs to the profile) but un-cures it.
- Nothing ever carries between profiles. Farming a strain by creating and deleting profiles is fine.
- Imports are never infected. Both pickers and the status bar show the strain.

---

## Mining (`core/actions.cpp: mine`)
- **Yield:** base = 1 + Pickaxe; a mine yields base + rand(0..base).
- **Streak multiplier:** ×(1 + 0.05 × streak), capped at ×2.0 from streak 20. The streak breaks
  after 15 s without mining (6 s with Amnesia).
- **Bonus:** chance min(70%, 6% + 4%/Magnet); adds base × (8 + 4 × Magnet).
- **Jackpot:** chance min(20%, 1% + 0.5%/Magnet); adds base × (80 + 20 × Magnet).
- **Cooldowns:** the TUI MINE button has a 250 ms cooldown; the CLI has none (typing is the limit).

## Upgrades (`core/game.cpp`), Shop tab / `shop`
- **Costs:**

  | Upgrade | Cost | Effect |
  |---|---|---|
  | Pickaxe | 25·(lvl+1)² | +1 base yield |
  | Magnet | 60·(lvl+1)² | better bonus/jackpot odds |
  | Drone | 120·(lvl+1)² | +0.5 tokens/s passive |
  | Mining Bot | 400·(lvl+1)² | auto-mines every max(2, 10 − lvl) s |

  Rust doubles all of these. Levels are capped at 1,000,000.
- **Buy, Buy 10, Buy 20:** the bigger buttons buy as many as you can afford up to n
  (`buyUpgrades`, `upgradesCost`).
- **Auto-Buyer license (2,000):** buys your cheapest upgrade whenever you have 3× its cost (checks every 5 s).
- **Idle:** Drones earn while the game is closed, capped at 12 h. The bot, crew, market and timers only run while it's open.
- **Bot reports:** every 30 s in the log.

## Random events (`core/game.cpp`, `ticks.cpp`)
- About one every 4 minutes:
  - Meteor Shower: ×2 mining, you and the bot, 30 s.
  - Drone Overclock: ×3 passive, 45 s.
  - Gold Rush: ×2 bonus/jackpot odds, 20 s.

## Goals, renown, titles and unlocks (`core/content.cpp`: GOALS, TITLES, FEATURES)
- **26 goals**, worth 87 renown in total.
- **Titles:** Rookie 0, Prospector 4, Hustler 10, Baron 18, Magnate 28, Legend 40,
  Monolith-Maker 55, Kingpin 68, Celestial 87 (every goal).
- **Titles unlock features.** A new game has only Mine, Shop and Goals (plus Stats and the Console):

  | Title | Opens |
  |---|---|
  | Prospector | Curios |
  | Hustler | the Monolith |
  | Baron | the Market |
  | Magnate | Contracts (and heat) |
  | Legend | the Crew |
  | Monolith-Maker | the Orrery |

- **Once open, a feature stays open.** This is the saved `[progress] unlocked` list.
  - Saves from before v7 keep everything they were already using, with no gaps.
  - The core refuses locked actions, explaining which title opens them.
  - Locked features stay quiet: no market news, no offer pings.
- **Goals for locked features** show "opens at <title>".
- **Simulator pacing:** a steady player gets Curios after about 5 min, the Monolith ~30, the
  Market ~34, Contracts ~45, the Crew ~70 and the Orrery ~95.

## Curios and prompt styles (`core/content.cpp`)
- **12 curios** in 3 sets (Desk Toys, Relics, Impossible Things), costing 300 to 80,000 tokens.
  They're purely collectible; completing a set or all of them feeds goals.
- **Prompt styles (CLI prompt, shown in the TUI Console too):**
  - The styles: `> ` free, `$ ` 500, `>>> ` 1.5K, `~> ` 3K, `[#] ` 7.5K, `<3 ` 15K.
  - The **custom prompt** (`prompt custom <text>`): 1–5 printable characters for 30× the
    priciest style (450,000). Changing the text afterwards is free.

## The Monolith (mid-game, tokens) (`core/actions.cpp: buildMonolith`)
- **Stages:** Foundation 5K, Pillars 20K, Spire 60K, Crown 150K, Beacon 400K tokens.
- No perks: it's the mid-game project and the Architect goal (+10 renown). Script wants it kept as is.

## Shards and the Market (`core/market.*`)
- A second currency.
- **Price:** one price that wanders (log-space mean reversion toward **150**, half-life ~3 min),
  with rare ±15–35% shocks, clamped to 10–100,000.
- **Spread:** 5% + 2% per heat − 0.6% per effective Broker level (min 1%), ×2 with Static.
- **Impact:** 0.25% of the price per shard you trade (geometric), cut by up to 50% by Brokers.
- **Orders:** max 5,000 shards per order. Buy 1/10/100/max, Sell 1/10/100/all, and the
  **amount box** (any amount, `2k` style).
- **Price warning:** an order that would average more than 3× the price asks for confirmation.
- **Known balance gap:** in the late game, income is so high that buying at the 100,000 ceiling
  is cheap, so the last Orrery stage can be bought outright. Not fixed yet (Script hasn't asked).

## Contracts and heat (`core/contracts.*`), Jobs tab / `contracts`
- **The board:** 3 offers, refilled every 60 s.
- **Job kinds:** Earn tokens, Mine N times, Hit N jackpots, Build a fresh streak, Let the Bot earn N.
  Only one job of each kind at a time.
- **Sizing:** targets come from how fast you've actually been playing (an EMA of income and mine
  rate), so jobs stay a stretch at every stage.
- **Big jobs (35%):**
  - Longer, and targets of 1.2–1.7×.
  - A shard stake of 3–60.
  - They pay 0.5 × value + 4–8 shards.
  - Small jobs pay 0.25 × value + 1–3 shards.
  - Brokers add up to +200% shards.
- **Slots:** 1, or 2 after the Orrery's Second Ring.
- **Failing or abandoning:** you lose the stake and gain heat (+1, big +2). Finishing a big job cools heat by 1.
- **Heat (0–10):**
  - Widens the spread.
  - Cools 1 point per 15 min, faster with Fixers, never with Fever.
  - From heat 3, **audits** can seize up to 20% of your tokens. Fixers block up to 75% of audits.

## Crew (`core/crew.*`), Crew tab / `crew`
- **Slots:** up to 4 hires (5 after the Orrery's Base Plate).
- **Candidates:** 3 at a time, refreshed every 5 min.
- **Hire cost:** shards, rising with each member you already have.
- **Payday:** every 5 min, each member takes their wage in shards. Unpaid members quit.
- **Roles** (all use the member's *effective level*):
  - **Miner:** a swing every 5 s worth expectedMine × (0.15 + 0.10 × eff).
  - **Broker:** a tighter spread, less price impact, more contract shards.
  - **Scout:** min(95%, 25% + 10% × eff) chance a minute to find 1..eff shards.
  - **Fixer:** heat cools faster; blocks min(75%, 15% × eff) of audits.
- **Effective level** = level × (1 + 0.25 × stars) × (1 + 0.25 × raises) × trait, ×0.5 while
  sulking, ×2 for an idle Night Owl.
- **Traits:** about two in three candidates have one, and each is a trade-off:

  | Trait | Effect |
  |---|---|
  | Diligent | ×1.25 work |
  | Greedy | ×1.5 work, double wage |
  | Frugal | half wage, ×0.85 work |
  | Quick Learner | half-price training |
  | Ambitious | promotions 30% off |
  | Loyal | survives one missed payday |
  | Night Owl | ×2 work while you haven't mined for 30 s |

- **Training:** 6 × level² shards, up to the level cap: 5, or 8 after the Gearwork, plus 3 per star.
- **Promotions:** available at the cap. Each costs 300 × 3^stars shards (300, 900, 2.7K, 8.1K…;
  30% less if Ambitious) and gives **+3 levels of cap** and **+25% per level**. There's no hard
  limit; it just stops being worth it. The TUI Train button turns into Promote; the CLI has
  `promote <n>`.
- **Wage** = level (×2 Greedy, half Frugal, ×2 Mutiny) + raises.

## Offers: choice events (`core/choices.*`)
- **When:** one every 10–15 min (the first one 5–15 min in), never two at once. Answer within
  **45 s**; ignoring one counts as no, with no penalty.
- **Sizing:** amounts scale with income.
- **Gating:** each offer appears only when the features it involves are open.
- **The offers:**
  1. **Shady Broker** (Market and Contracts open): shards at 60% of the buy price, heat +1.
  2. **The Investor** (needs 1,000 tokens): lend 10% of your tokens; 80% of the time you get 1.3×
     back in 10 min, 20% of the time they vanish. The loan is saved in `[investment]`.
  3. **Rival Miner** (needs 2,000 tokens): bet 5% of your tokens; mine 40 times by hand within
     60 s to double it. The race isn't saved, so quitting mid-race loses the bet.
  4. **Raise Request** (Crew open): yes gives +1 wage and +25% work (at most 3 raises per
     member); no means they sulk at half effort for 10 min.
  5. **Bribe the Auditor** (heat 3+): pay about 3 min of income to drop heat by 2.
- **Answering:** `yes`/`no` (`y`/`n`) in the CLI or Console; `y`/`n` keys or the OFFER bar in the TUI.
- **Test builds:** `-DTOKENRUSH_TEST_OFFERS` makes the first offer arrive after 3 s. Never use
  it in `build.py`.

## The Orrery (late game, shards) (`core/content.cpp: ORRERY, ORRERY_ART`)
- **Stages and perks:**

  | Stage | Shards | Perk |
  |---|---|---|
  | Base Plate | 2,000 | a 5th crew slot |
  | First Ring | 6,000 | **Resonance** |
  | Second Ring | 15,000 | a second contract slot |
  | Gearwork | 35,000 | crew level cap 8 |
  | The Sun | 80,000 | the last goal (Clockwork Sky) and the Celestial title |

- **Resonance:**
  - Each Drone level also earns 0.3% of an expected manual mine per second.
  - The Mining Bot rolls bonuses and jackpots like a manual mine, at a power of 0.10 + 0.005 × Bot level.
- **Building it:** `orrery all` / Add all keep back the crew's next payday.
- **Completing it** cures an Infection.
- **The art:** one 12-row composite picture; each stage lights up its own part, in its own color.

## Stats and history (`core/game.cpp: recordStats`), Stats tab / `stats`
- **Earn rate:** "Earning X tokens/s" over the last **10 s**, measured from lifetime earnings
  (every source counts, spending doesn't lower it). Shown on the Mine tab and in `status`.
- **Records:**
  - tokens and lifetime earned
  - best earn rate
  - manual mines and jackpots
  - biggest jackpot and best streak
  - Mining Bot total
  - shards traded, contracts done/failed, audits
  - crew mined
  - play time, and when each title was reached ("before records" for titles reached before v8)
- **Chart:** lifetime earned over play time on a **log scale**. A sample every 5 min of play is
  saved in `[history] samples`, at most 240; past that, the old part is thinned (every other
  sample dropped).

---

## Under the hood
- **`src/core/`:** the game. It never prints or reads input; actions return `Notes`.
- **Threads:**
  - The workers: world, market, automation, crew, autosave every 30 s. All lock `Shared::m`.
  - Lock order: `saveMtx` → `m`.
- **The TUI:**
  - It snapshots `GameState` every frame and draws from the copy.
  - Clicks lock and act through `Ui::run`.
  - A refresher thread posts an event every 100 ms.
- **The Console tab (`tui/console.*`):**
  - It runs the shell's command loop on its own thread.
  - `out()`/`in()` (`shell/term.hpp`) are redirected per thread to the console's buffers,
    colors included.
  - The TUI turns the ANSI colors back into styled text (`ansiLine`).
- **Save (`core/save.cpp`):**
  - An INI file driven by one field table (`saveFields()`).
  - Unknown keys are ignored; missing keys keep their defaults.
  - A bad file is moved to `.save.bad`.
  - Every older version loads (v1–v2 positional, v3 sectionless, v4–v8 INI).
  - Doubles are saved floored to 4 decimals.
  - **Token cap:** token totals saturate at 10^18 (`TOKEN_CAP`, `addTokens`).
- **Builds:**
  - `python build.py [--cli] [--tui] [--sim] [--rebuild]`: incremental, with per-file hashes
    covering the source, the headers it includes and the flags.
  - `build.ps1` is the older PowerShell version of the same thing.
  - `make` on Linux/macOS.
  - Build times go to `target/debug/build-times.log`.
- **Simulator:** `tools/sim.cpp` (`sim <hours> <seeds> normal|trader|idle|noecon`) plays the
  core headlessly to check balance after changing numbers.
- **`.trash/`** in the project root is for old or temporary files instead of deleting them.

## Design decisions to respect
- **No prestige or reset loops.** They feel repetitive, and permanent carry-over power makes the
  game feel unfair. Things that add new things to do are welcome; things that add bigger numbers
  aren't. Crew promotions are fine because they're per member and within one save.
- **The Monolith stays as it is**, as the mid-game project.
- **The CLI is the main edition.** Features should work in the CLI first. (A "Golden Meteor"
  click event was rejected because it doesn't fit the CLI.)
- **Ideas offered but never asked for:**
  - a profile lock against running the same profile twice
  - parallel compiles in `build.py`
  - fixing the Orrery price ceiling
  - Monolith art like the Orrery's
  - market limit orders
  - deciding the fate of `build.ps1`

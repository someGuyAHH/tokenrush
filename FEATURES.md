# TokenRush: every feature, in one place

A reference for picking the project back up later. The README covers building and running.
This file covers what the game does, the numbers behind it, where each piece lives, and the
design decisions made along the way. When a number here and the code disagree, the code wins:
the tunables live in the headers named next to each section.

Current save version: **9**. C++20, with FTXUI 7.0.3.

---

## The game: one edition, tokenrush-tui

- Full screen: a pinned log at the top, a status bar, tabs, clickable everything, and the
  **Console** tab, where every command can be typed.
- **The command-line edition (tokenrush-cli) is discontinued.** Its commands, menus and questions
  all live on in the Console: `src/shell/` runs each typed line through `runCommand()`
  (`shell/dispatch.cpp`) on the Console's own thread. `src/cli/` and the `--cli` build options are
  gone; old saves and profiles play exactly as before.

### TUI layout and keys
- Tabs, in order: **Mine, Shop, Goals, Stats**, then **Curios, Monolith, Market, Themes, Jobs,
  Crew, Orrery, Workshop, Stars, Tasks** as they unlock (locked tabs are hidden), then **Console**
  (always last). Themes opens with the Market, next to it: themes cost shards, so there's
  nothing to buy there before.
- Keys:
  - `1`–`9` and `0`: the first ten visible tabs.
  - `m`: mine.
  - `s`: save.
  - `y`/`n`: answer an offer.
  - `x`: stop the disaster that's coming (the DANGER bar's button).
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
  - what disasters broke ("Broken: the Mining Bot, 27 Drones (back in 6m)")
- **OFFER bar:** appears under the status bar when an offer is open. It's last in the focus
  order, so an offer never lands the keyboard on "Yes".
- **DANGER bar:** the same for a disaster's warning: what's coming, the countdown, and the
  button that stops it (key `x`).
- **Themes:** every color goes through `hue(Hue::...)` (`tui/widgets.cpp`). Classic maps the roles
  to the original named colors; the others are RGB with the theme's own background (`themed()`),
  a lighter Panel color under the log, and the open tab as an accent-colored chip.
- **Console scrollback:** PgUp/PgDn (a page), the mouse wheel (3 lines) and End; typing or
  Enter comes back down. While scrolled back, new output doesn't move the view, and a marker
  says how far back you are. 1,000 lines are kept.
- **Risky buttons** need a second click within 3 s: Fire, Abandon, Delete profile, and Buy
  (max or a custom amount) when it would buy at a steep price.
- **Text boxes** draw their own caret (`TextBox` in `tui/widgets.cpp`). FTXUI's Input moves the
  real cursor every frame, which flickered on Windows Terminal.

### The Console's commands
- **Help:** `help` lists what's open, by section; `help <command>` (any alias: `help ct`) is a
  full page: usage, aliases, what it does and how it works, examples, related commands, and
  which title opens it if it's still locked. One table in `shell/help.cpp` holds every command:
  `parseCmd()`, the locks (`commandFeature()`) and both kinds of help all read it.
- **Log:** `log [n] [good|bad|warn|event|info]` (either order; plurals work): the last 20 by
  default. The log keeps the last 400 messages of the session.
- **Empty Enter** does nothing.
- **Clearing:** `cls`/`clear` empties the Console, scrollback included.
- **Logging out:** `logout` goes back to the profile picker; `quit` closes the game.
- **Background news** goes to the pinned log pane (and `log`), not into the Console.
- **Long lines wrap** (`wrapAnsi` in `tui/widgets.cpp`): at spaces, with the color carried into the
  next row (indented by two), so nothing gets squeezed off the edge.
- **Predictions** (`shell/predict.cpp`): `predict <what> <time|count>`.
  - Over a time (`1h`, `30min`, `2h30m`, `90s`): `tokens` (the last minute's steady income,
    contracts aside, or the 5-minute average before there's a minute, × the time, and by source),
    `shards` (Scouts at home at their odds, the Lucky Penny's jackpot shards, minus the paydays'
    wages), `heat` (cooling, and when it reaches 0; Fever: it doesn't), `paydays`, `events` (and
    disasters, once the Crew is open; the Lightning Rod counts).
  - Over mines (`1000`, `5k`; a time turns into mines at your recent pace): `mines` (by hand, a
    full streak), `jackpots`, `bonuses` (n × p, give or take √(n·p·(1−p)), and the pay of each).
  - Upgrades: `predict <pickaxe|magnet|drone|bot> <n>`: the cost of n levels, the income they
    add (`incomeEstimate` before and after, applied to your income) and the payback time.
  - **`predict when <condition>`:** the time until a condition holds, and the wall-clock time.
    The condition is the Tasks language (`compileCondition` in `core/tasks.cpp`: same values,
    units, `and/or/not`, arithmetic and `^` errors), plus three values only predict reads:
    `earned`, `mines`, `jackpots`. It scans the projection forward (1 s steps for 2 min, then 10 s
    to an hour, 1 min to a day, 5 min to 30 days) and narrows the step it turned true in.
    "Already true"; "never by itself" when nothing it reads moves; the price is refused (random);
    "not within 30 days", with why (no income, shards falling by N a payday).
  - **One projection** (`core/projection.*`), read both ways so they agree: tokens and earned at the
    last minute's steady income (contracts aside; the 5-minute average before there's a minute),
    shards (Scouts at their odds once home, expedition loot on return, the Lucky Penny, minus each
    payday's wages), heat's cooling schedule, the payday clock, crew coming home, mines and
    jackpots at your pace. Nothing gets bought; no events, contracts or disasters.
  - **With tasks or the Auto-Buyer on** (`automationOn`), `predict tokens`, `predict shards` and
    `predict when` use a **Forecast** instead (`core/projection.cpp`): the same rates, applied a
    second at a time, with the real `tasksTick` and the Auto-Buyer's purchase (`autoBuyOnce`, every
    5 s) running on the future state: fees, budgets, `when`/`while`/`every` and all. What they buy
    rescales the income source by source (mining by hand by `expectedMine`, Drones by
    `passiveRatePerSec`, the Bot by `botRatePerSec`, crew Miners by their share), so Pickaxe levels
    do nothing for someone who doesn't mine by hand; tasks read `income` scaled the same way.
    Scouts, the Penny, expedition loot (crew a task sends come back with it) and paydays as above.
    It answers with what's left, plus each task's runs and fees, the Auto-Buyer's buys and the
    levels by then. `predict when` checks the condition every simulated second, up to 30 days
    (about 15 ms of work per simulated day). Nothing on: the closed form, which it matches exactly.
  - `predict` works on a copy of the state, so the game doesn't wait while it thinks.
- **Eval** (`eval`/`calc <expression>`, `shell/predict.cpp`; `compileExpression` in
  `core/tasks.cpp`): any expression in the Tasks language with predict's extra values, worked out
  now. A comparison or and/or/not prints true/false, a time prints as a time (`8m 17s (497s)`),
  anything else as a number with separators (and short form from 10,000). Mistakes get the caret.
- **Aliases** (`shell/aliases.cpp`): `alias <command...> <name>`: the last word is the name,
  everything before it is what it runs (case kept); `runCommand` swaps a typed alias for its
  command and puts whatever followed on the end, and shows the line it ran. `alias` lists them,
  `alias <name>` says what one runs, `rmalias`/`unalias <name>` removes one, `help <alias>` names it
  and shows its command's page, and `help` lists your aliases at the end. Rules: names are 1-16 of
  a-z 0-9 - _ (lowercased), never a command's name (commands always win, even one added later), an
  alias starts with a real command (no alias of an alias), one command (no `;`), 120 characters,
  50 per profile. Reusing a name replaces it. Saved as `[console] aliases = "pt: predict tokens 1h ;
  ..."` (a broken entry is dropped, not the save) and kept by `wipe`.
- **Looking without installing:** `check` (`shell/commands.cpp: doCheck`, `core/update.cpp:
  repoStatus`): one fetch, then your version and the new commits (short hash, commit time, author,
  title, the rest of the message) read from `git log` with \x1f/\x1e separators. Alone: up to date,
  or how many and the newest one's title and age. `count` (with your own commits too), `list [n]`
  (default 10, up to 100: age and local time, title), `titles [n]`, `dates [n]`, `show <n|hash>`
  (the whole message and author), `files` (`git diff --name-status HEAD...@{u}`: new, changed,
  gone, renamed), `current` (your hash, branch, date, title, own commits, uncommitted edits). It
  needs `play.py`'s `TOKENRUSH_REPO` (it works with `--no-update-check` too), runs on the Console's
  thread, and refreshes what the log and the `u` key know.
- **Updates in the Console:** `update`/`u` checks GitHub right away (on the Console's thread, a
  second or two of git) and says "Up to date", "pull it yourself" (your copy has commits of its
  own) or what's new, then saves and quits with exit code 42 for `play.py` to pull, build and
  restart. Not under `play.py` (or with `--no-update-check`) it says how to get updates.
- **One-line shortcuts:**
  - `shop 1 10`: 10 Pickaxe levels.
  - `curios 3`: buy curio 3.
  - `buy 2k` / `sell 2k`: amounts accept k/m/b and commas.

---

## Profiles (`core/profiles.*`, the picker in `tui/picker.cpp`)
- One folder per profile, no passwords:
  - Windows: `%LOCALAPPDATA%\TokenRush\profiles\<name>\tokenrush.save`
  - Linux/macOS: `~/.local/share/tokenrush/...`
- The picker can create, play, rename and delete profiles, and can import a pre-profiles save
  (the `.save` folder next to the exe).
- **Renaming** (`Profiles::rename`): the rename button, which sends the keyboard to the name box
  (Esc cancels).
- **The picker's look** is deliberately quiet: no inverted bar or button blocks. The row with
  the keyboard or the mouse gets the Panel color and an accent `›`; rename, delete, create and
  quit are flat text buttons (`FlatBtn` in `tui/widgets.cpp`) that brighten on focus; an armed
  delete turns red. Token counts are compact (2.24T). A case-only change ("bob" to "Bob")
  goes through a temporary folder name, since Windows and macOS see those as the same folder.
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
  after 15 s without mining (6 s with Amnesia, 8 s with the Tuning Fork).
- **Bonus:** chance min(70%, 6% + 4%/Magnet); adds base × (8 + 4 × Magnet), ×0.9 with the Lucky
  Penny (`bonusChance`, `bonusSize` in `game.cpp`).
- **Jackpot:** chance min(20%, 1% + 0.5%/Magnet); adds base × (80 + 20 × Magnet) (`jackpotSize`).
- **The Magnet never stops paying:** the odds cap at Magnet 16 and 38, but every level keeps
  adding +4 base to a bonus and +20 to a jackpot, and after the Orrery's First Ring the Drones and
  the Bot scale with the average mine too. The Shop says what each upgrade does at your level and
  what one more level adds to your income (`incomeEstimate` before and after); late in the game
  a Magnet level is often worth more than a Pickaxe level.
- **Night Shift:** mining by hand pays ×0.75 (`handMult`).
- **Cooldowns:** the MINE button (and `m`) has a 250 ms cooldown; typing `mine` in the Console has
  none (typing is the limit).

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
- **Idle:** Drones earn while the game is closed, capped at 12 h (4 h with the Lava Core). The bot, crew, market and timers only run while it's open.
- **Bot reports:** every 30 s in the log.

## Random events (`core/game.cpp`, `ticks.cpp`)
- About one every 4 minutes (twice as often with the Lightning Rod, same length):
  - Meteor Shower: ×2 mining, you and the bot, 30 s.
  - Drone Overclock: ×3 passive, 45 s.
  - Gold Rush: ×2 bonuses and jackpots, 20 s: double the odds, and whatever the 70%/20% caps
    swallow comes back as pay (`bonusBoost`, `jackpotBoost`), so it still doubles them late.
  - **Payday Rush** (only with a crew): the crew works twice as hard (`EventFx::crew`, applied in
    `effectiveLevel`) for 60 s, and payday moves to when it ends, so have the wages ready.

## Goals, renown, titles and unlocks (`core/content.cpp`: GOALS, TITLES, FEATURES)
- **29 goals**, worth 96 renown in total. New in v9: Regular (be Trusted by a client, 3),
  Gadgeteer (build 3 gadgets, 3), Explorer (finish 5 expeditions, 3). Curator now means every
  curio *in the shop*.
- **Titles:** Rookie 0, Prospector 4, Hustler 10, Baron 18, Magnate 28, Legend 40,
  Monolith-Maker 55, Kingpin 62, Overseer 72 (v10), Celestial 96 (every goal). Celestial's renown is checked
  against the sum of the goals at compile time (`allRenown()` in `content.cpp`).
- **Titles unlock features.** A new game has only Mine, Shop and Goals (plus Stats and the Console):

  | Title | Opens |
  |---|---|
  | Prospector | Curios |
  | Hustler | the Monolith |
  | Baron | the Market |
  | Magnate | Contracts (and heat) |
  | Legend | the Crew (and disasters start) |
  | Monolith-Maker | the Orrery |
  | Kingpin | the Workshop |
  | Overseer | Tasks (and then the Automation License) |
  | (completing the Orrery) | the Constellations |

- **Once open, a feature stays open.** This is the saved `[progress] unlocked` list.
  - Saves from before v7 keep everything they were already using, with no gaps.
  - The core refuses locked actions, explaining which title opens them.
  - Locked features stay quiet: no market news, no offer pings.
- **Goals for locked features** show "opens at <title>".
- **The Constellations** have no title (`FeatureInfo::title` is -1): `specialFeatureReady()` in
  `game.cpp` opens them when the Orrery is complete. `nextUnlock()` skips them.
- **Title times:** `checkGoals` records the play time for every reached title that has none yet,
  so a save that loads past a threshold that moved (Kingpin went from 68 to 62) gets one.
- **Simulator pacing:** a steady player gets Curios after about 5 min, the Monolith ~30, the
  Market ~34, Contracts ~45, the Crew ~60-80, the Orrery ~80-120, the Workshop ~90-140 and
  Tasks ~3-4 hours. v9 saves had no Overseer: their title times get a "not yet" slot for it.

### Secret goals (`SECRETS` in `core/content.cpp`)
- 15 of them, shown as `???` and a hint until found. They pay **shards, not renown**, so they
  never stand between you and a title (and they're paid even before the Market opens).
- Two kinds: things that just happened (an action calls `spotSecret`) and how things stand
  (`spotStandingSecrets` in `game.cpp`). `checkGoals` pays both.

  | Secret | How | Shards |
  |---|---|---|
  | Rubber Duck Debugging | the Rubber Duck as your first curio | 25 |
  | Cold Feet | say no to 10 offers in a row (`refusedInRow`) | 40 |
  | Witching Hour | mine by hand between 3 and 4 AM, your clock (`localHour`) | 66 |
  | Lucky Sevens | a single mine of exactly 777 | 77 |
  | Well Rested | come back after 8+ hours away | 30 |
  | Glass Cannon | finish a big contract at heat 8+ | 120 |
  | Golden Hour | a jackpot during a Gold Rush | 50 |
  | Dragon's Hoard | hold 10,000 shards | 150 |
  | Overkill | a streak of 500 | 100 |
  | Clean Hands | 10 contracts done, none ever failed | 80 |
  | Speedrunner | Baron within 25 minutes of play | 60 |
  | Down to Zero | spend down to exactly 0 tokens (after earning 10K) | 20 |
  | Loyal to a Fault | a Loyal member stays through a missed payday | 40 |
  | Under the Counter | own every Dealer curio | 200 |
  | Firefighter | stop 5 disasters | 100 |

## Curios, keepsakes, prompt styles and themes (`core/content.cpp`)
- **16 curios** in 4 sets. They're purely collectible (and the Workshop builds gadgets from them;
  the curio stays on the shelf). Completing a set or all of them feeds goals.

  | Set | Where | Costs |
  |---|---|---|
  | Desk Toys | the shop | 1.5K, 4K, 10K, 25K |
  | Relics | the shop | 150K, 600K, 2M, 6M |
  | Impossible Things | the shop | 25M, 80M, 250M, 750M |
  | Under the Counter | the Curio Dealer (at 1.5× this), or an expedition | 2M, 20M, 200M, 1.5B |

  The shop prices went up in v9 (they were 300 to 80K): the simulator's careful player now
  finishes Desk Toys around 45-55 min, Relics around 2 h and the last Impossible Thing after 3 h.
  `theCurio(i)` gives the name with the right article for the middle of a sentence.
- **Keepsakes:** a crew member who quits unpaid, or earns their third star, leaves one on the
  shelf ("Mara's Lucky Wrench"; six items per role, picked by a hash of who they are). The last
  30 are kept.
- **Prompt styles (the Console's prompt):**
  - The styles: `> ` free, `$ ` 500, `>>> ` 1.5K, `~> ` 3K, `[#] ` 7.5K, `<3 ` 15K.
  - The **custom prompt** (`prompt custom <text>`): 1–5 printable characters for 30× the
    priciest style (450,000). Changing the text afterwards is free.
  - The **prompt color** (`prompt color <#rrggbb|#rgb|off|on>`): 12,000 shards once (4× the custom
    prompt's 450,000 tokens, in shards at the fair price of 150), then free to change. Whatever
    prompt you use is drawn in that 24-bit color and reset right after it, so the space and what
    you type keep the normal color. `off` keeps the color for later. Saved as `[cosmetics]
    promptColor = #ff8800` (or `off #ff8800`). Shards, so it needs the Market.
- **Themes (`THEMES`; `theme` in the Console, or the Themes tab; both open with the Market):** kept per profile
  (`[cosmetics] theme`, `themesOwned`). Each is a palette of 17 roles (`Hue`: Text, Muted, Accent,
  Good, Bad, Warn, Event, Tokens, Shards, Heat, Background, Panel, Button, ButtonText, ButtonOff,
  ButtonOffText, Infected).

  | Theme | Cost | Look |
  |---|---|---|
  | Classic | free | the original: the terminal's own 16/256 colors, no background |
  | Dusk | free | lavender and apricot on deep violet |
  | Glacier | 300 shards | frost blue and snow |
  | Ember | 750 shards | amber and coal, with a cold blue spark |
  | Verdigris | 1,500 shards | patina green, brass, sea glass |
  | Neon | 3,000 shards | hot pink and cyan on purple |

  - The Console's commands write `ESC[38;2;r;g;bm` for every theme but Classic (`codeFor` in `shell/term.cpp`);
    the theme list shows each one as a swatch line on its own background (`ESC[48;2;...m`).
  - The TUI draws from `hue()`; the Console tab parses the 24-bit codes back (`ansiLine`).
  - **Previews:** on the Themes tab, the mouse over a theme's row (or the keyboard on its button)
    redraws the whole screen in that theme (`Ui::previewTheme`) until it moves away or you leave
    the tab. Nothing is bought or switched; the Console keeps the real theme.
  - An unknown theme id in a save falls back to Classic instead of rejecting the save.
  - **The custom theme (v10):** one per profile, 15,000 shards once (`CUSTOM_THEME_COST`; it's
    `themeSel` `CUSTOM_THEME`, saved as `theme = custom`). It starts as a copy of the theme in use
    (or, in the Console, any theme you own), then all 17 colors are free to change.
    - Its colors are the profile's (`[cosmetics] customTheme`, 17 hex codes in `Hue` order). A
      process-wide atomic copy (`setCustomPalette`, refreshed from the snapshot every frame) is
      what the TUI, the terminal backdrop and the Console's painter read, through `themeRgb()`.
    - **The editor** (Themes → Custom → "edit its colors", `tui/theme_editor.cpp`): the 17 roles on
      the left (the keyboard and the selection move together), R/G/B nudges of ±1 and ±16 (a
      Grid, so Up/Down keeps the step) and a hex box (`#ff8800` or `#f80`), how readable the main
      pairs are (contrast ratios: 4.5:1 and up is easy, under 3 is hard), and a fixed scene on
      the right that uses every color (a log, the status bar, tab chips, a job with tips and a
      gauge, buttons on and off, the row you're on, a disaster, an infection, a hint). While
      it's open the whole screen is drawn in the custom theme. "Start over from" copies another
      owned theme's colors (two clicks). Changes save as they're made.
    - **Console:** `theme custom` (switch to it, buying it the first time, and list its colors),
      `theme custom <color> <#hex>` (color names: text, quiettext, accent, good, bad, warning,
      event, tokens, shards, heat, background, panel, button, buttontext, buttonoff,
      buttontextoff, infected), `theme custom from <theme>`.

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
- **Trading record** (`[ledger]`): shards bought and sold, tokens spent and received (the Shady
  Broker counts as a buy), and `tradeCost`, what went to the spread and impact against the mid
  price. The market screen and Stats show it; a negative net is normal while you buy shards to use.
- **Gadgets:** the Golden Ledger makes both quotes 15% higher (selling pays more, buying costs
  more: for net sellers); the Moon Anchor cuts your own price impact to a quarter but doubles
  volatility and shocks.
- **Known balance gap:** in the late game, income is so high that buying at the 100,000 ceiling
  is cheap, so the last Orrery stage can be bought outright. Not fixed yet (Script hasn't asked).

## Contracts and heat (`core/contracts.*`), Jobs tab / `contracts`
- **The board:** 3 offers, refilled every 60 s.
- **Job kinds:** Earn tokens, Mine N times, Hit N jackpots, Build a fresh streak, Let the Bot earn N,
  and (v9) Trade N shards (40-120, big 150-400: a few round trips), Cool your heat down by 1 (big 2;
  offered at heat 2+, 7-15 min), and two **endurance** jobs, won by lasting until the timer runs out
  and failed the moment the rule breaks: **Hold N shards** (55-80% of what you had, big 85-100%;
  the stake comes out first) and **Hands off** (no mining by hand; offered once something else earns
  for you). Only one job of each kind at a time, and Hands off can't run beside a mining job.
- **Kind pay:** Trade and Cool off ×0.8, Hold ×0.6, Hands off ×1.2 of the usual reward.
- **Tips** (`judgeContract` in `contracts.cpp`): every offer and running job carries one, Easy /
  Doable / Tight / Unlikely / Out of reach and why, shown on the Jobs tab and in `contracts`.
  They're judged again every 5 s (`TIP_EVERY`) from your recent pace (income EMA, mine rate),
  odds, wallet, shards and paydays, heat cooling, and the stake. Mining jobs are judged by the
  mines per second they need (the MINE button takes 4 at most); time-based ones by how long they'd
  take at your pace against the time left.
- **Sizing:** targets come from how fast you've actually been playing (an EMA of income and mine
  rate), so jobs stay a stretch at every stage.
- **Big jobs (35%):**
  - Longer, and targets of 1.2–1.7×.
  - A shard stake of 3–60.
  - They pay 0.7 × value + 4–8 shards.
  - Small jobs pay 0.35 × value + 1–3 shards.
  - The shards grow with each doubling of income: × (1 + log2(income / 10K)), at least ×1
    (about ×8 at 1M tokens/s, ×17 at 500M/s). A shard costs about the same at every stage, so
    they can't follow income one for one the way tokens do.
  - Brokers add up to +200% shards.
  - The stake is capped at 60, or half the shard pay if that's more.
- **Slots:** 1, or 2 after the Orrery's Second Ring.
- **Failing or abandoning:** you lose the stake and gain heat (+1, big +2). Finishing a big job cools heat by 1.
- **Gadgets:** Forged Papers give new races against the clock 25% more time (the job and its pay
  are sized from the normal time first; endurance jobs are left alone), but failing or dropping a
  job costs twice the heat and standing. The Fog Machine pays 10% more per point of heat when a
  job is done (tokens and shards), and heat cools half as fast.

### Clients and standing (`CLIENTS`, `TIERS`; `clients` in the Console, the grid on the Jobs tab)
- **Standing** with each of the 12 clients, -10..10 (`[jobs] standing`). Finishing a job: +1 (big or
  signature +2), and **their rival -1**. Failing or abandoning: -2 (big -3; small fry -1).

  | Tier | Standing | Effect |
  |---|---|---|
  | Burned | -5 and down | posts no jobs for you (but see "Coming back" below) |
  | Wary | -4 to -2 | pays 20% less |
  | Neutral | -1 to 2 | normal |
  | Friendly | 3 to 5 | pays 15% more |
  | Trusted | 6 and up | pays 30% more; 25% of their offers are **signature** jobs: 1.5× as long, no stake, ×2 pay |

- **Styles:** each client has one, which decides what they post and how they pay:
  - **Shady** (the Syndicate, a burner account, the Night Exchange, Corvid Holdings): ×1.4 tokens,
    but every job you finish for them adds 1 heat (instead of a big job's -1). No Hold or Cool off.
  - **Miners** (the Foundry, Old Man Halvard, the Guild of Pickaxes): only Mine, Jackpots and
    Streak jobs; ×1.25 shards.
  - **Merchants** (Saffron & Sons, Mother Kestrel, Dr. Pellucid): Earn, Bot, Trade, Hold and Cool
    off; ×0.8 tokens, ×1.6 shards.
  - **Small fry** (a nervous intern, your landlord): never big jobs; forgive easily.
- **Rivals** come in pairs: Syndicate/Guild, Corvid/Saffron, Night Exchange/Foundry, burner/Kestrel,
  Halvard/Pellucid, intern/landlord.
- Offers pick a kind first, then a client who posts it and isn't Burned. (If nobody posts it, the
  nervous intern does; that fallback never fired before, since an unpicked offer looked like an
  Earn job from the Foundry, Burned or not.)
- **Coming back from Burned** (a Burned client posts nothing, so delivering can't fix it). Either way
  they land at -4 (Wary):
  - **Bribes** (`bribe <client>`, by part of a name or its number in `clients`; a two-click Bribe
    button per Burned client on the Jobs tab): 150 shards × the job-pay shard scale
    (`contractShardScale`: ×1 at 10K tokens/s, ×17 at 500M/s) × depth (×1 at -5, ×2 at -10),
    doubled for every earlier bribe to the same client (`[jobs] bribes`, up to ×64). It also clears
    their make-amends offer off the board.
  - **Make-amends jobs:** a new offer has a 25% chance to come from a Burned client who has none out
    (one at a time each), as a job of a kind they post, sized like a big one, with its stake and
    **no pay** (AMENDS on the Jobs tab, `[MEND]` in `contracts`, an event in the log). Deliver it:
    stake back, Wary. Fail or abandon it while they're still Burned: the usual heat and stake, -10,
    and no new one from them for 30 minutes of play (`[jobs] amendsAfter`). Bribed meanwhile, it
    fails like any job.
- **Heat (0–10):**
  - Widens the spread.
  - Cools 1 point per 15 min, faster with Fixers, never with Fever.
  - From heat 3, **audits** can seize up to 20% of your tokens. Fixers block up to 75% of audits.

## Crew (`core/crew.*`), Crew tab / `crew`
- **Slots:** up to 4 hires (5 after the Orrery's Base Plate).
- **Candidates:** 3 at a time, refreshed every 5 min.
- **Hire cost:** shards, rising with each member you already have.
- **Payday:** every 5 min, each member takes their wage in shards. Unpaid members quit.
- **Wages (v10)** follow the member's lasting strength, *rank* = level × (1 + 0.25 × stars) ×
  (1 + 0.25 × raises): wage = rank × (1 + rank / 16), rounded (×2 Greedy, ÷2 Frugal, ×2 Mutiny).
  Rookies cost about their level as before (L1 1, L2 2, L5 7); an L8 costs 12, a two-star L8
  21, a two-star, two-raise L10 54. Before, a wage was just level + raises, so a starred,
  raised veteran cost almost nothing next to what they brought in.
- **Roles** (all use the member's *effective level*):
  - **Miner:** a swing every 5 s worth expectedMine × (0.15 + 0.10 × eff).
  - **Broker:** a tighter spread, less price impact, more contract shards.
  - **Scout:** min(95%, 25% + 10% × eff) chance a minute to find shards: eff at best, and no less
    than 40% (10 at most) below that (L8: 5–8, L22: 14–22).
  - **Fixer:** heat cools faster; blocks min(75%, 15% × eff) of audits.
- **Effective level** = level × (1 + 0.25 × stars) × (1 + 0.25 × raises) × trait, ×0.5 while
  sulking or hurt, ×2 for an idle Night Owl, ×2 during a Payday Rush, and 0 while away.
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
  limit; it just stops being worth it. The Train button turns into Promote; the Console has
  `promote <n>`.
- **Wage:** see Wages above (rank-based since v10). Still paid while away.
- **Former crew:** everyone who quits or is fired is remembered (`[formercrew]`, the last 40),
  shown under Stats. A full crew of 5 through paydays with nobody leaving counts for the Hearth.

### Expeditions (`EXPEDITIONS` in `crew.cpp`; `expedition <n> <trip>`, Send on the Crew tab)
- An away member doesn't work and can't be trained, promoted or fired; wages still come out.

  | Trip | Time | Shards (×) | Curio | Trait | Level | Injury |
  |---|---|---|---|---|---|---|
  | short | 10 min | 1 | 2% | 5% | 0% | 5% |
  | long | 30 min | 3.5 | 8% | 15% | 10% | 12% |
  | deep | 60 min | 8 | 20% | 25% | 30% | 25% |

- **Shards:** × (8 + 4 × effective level), ×1.5 for a Scout, × 0.6-1.4 luck.
- **Curio:** an Under the Counter one you don't have (none left: +50% shards instead).
- **Trait:** only if they had none. **Level:** only below the cap.
- **Injury:** half effort for 15 min and half the shards. Fixers at home cut the chance by up to 60%.

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
  4. **Raise Request** (Crew open): yes gives +25% work for good, and the wage that goes with it (the offer says what it'll be) (at most 3 raises per
     member); no means they sulk at half effort for 10 min.
  5. **Bribe the Auditor** (heat 3+): pay about 3 min of income to drop heat by 2.
  6. **Curio Dealer** (Curios open): one of the Under the Counter curios you don't have and can
     afford, at 1.5× its worth (`DEALER_MARKUP`).
- **The Debug Duck** gadget: offers twice as often, but 20 s to answer (`choiceSeconds`).
- **Answering:** `y`/`n` keys or the OFFER bar, or `yes`/`no` in the Console.
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
  | The Sun | 80,000 | the Clockwork Sky goal, and the Constellations |

- **Resonance:**
  - Each Drone level also earns 0.3% of an expected manual mine per second.
  - The Mining Bot rolls bonuses and jackpots like a manual mine, at a power of 0.10 + 0.005 × Bot level.
- **Building it:** `orrery all` / Add all keep back the crew's next payday.
- **Completing it** cures an Infection and opens the Constellations.
- **Quakes** (a disaster) can crack a built stage except the Sun: its perk is off until repaired
  (`stageWorks()` in `game.cpp`; a 5th crew member already hired stays).
- **The art:** one 12-row composite picture; each stage lights up its own part, in its own color.

## Stats and history (`core/game.cpp: recordStats`), Stats tab / `stats`
- **Earn rate:** "Earning X tokens/s" over the last **10 s**, measured from lifetime earnings
  (every source counts, spending doesn't lower it). Shown on the Mine tab and in `status`, with
  the split by source ("mining 61%, drones 27%").
- **Income by source** (`[income]`): `earn(g, amount, Source)` books every token under Mining by
  hand, Drones, Mining Bot, Crew, Contracts, or Offers and other. Stats shows each all time (with
  its share) and now. Tokens earned before v9 show as "Before records".
- **Records:**
  - tokens and lifetime earned
  - best earn rate
  - manual mines and jackpots
  - biggest jackpot and best streak
  - Mining Bot total
  - shards traded and the trading net, contracts done/failed, audits
  - crew mined, expeditions, disasters stopped and hit
  - former crew (the last few who left, and how)
  - play time, and when each title was reached ("before records" for titles reached before v8)
- **Chart:** lifetime earned over play time on a **log scale**. A sample every 5 min of play is
  saved in `[history] samples`, at most 240; past that, the old part is thinned (every other
  sample dropped).

## The Workshop (`GADGETS` in `core/content.cpp`; `workshop`, `craft`, `equip`, `unequip`)
- Opens at **Kingpin**. A gadget is built once from a shop curio you own (it stays on the shelf)
  plus shards, then equipped in one of **3 slots**. Equipping and unequipping are free.
- `gadgetOn()` is "equipped and working" (not broken by a Short Circuit, not unplugged);
  `gadgetEquipped()` is just "in a slot".

  | Gadget | From | Shards | Better | Worse |
  |---|---|---|---|---|
  | Debug Duck | Rubber Duck | 150 | offers twice as often | 20 s to answer |
  | Tuning Fork | Newton's Cradle | 250 | with a streak of 20+, Drones and the Bot ×1.3 (`humBoost`) | the streak breaks after 8 s |
  | Lava Core | Lava Lamp | 400 | Drones ×1.5 | idle earnings capped at 4 h |
  | Forged Papers | Cracked Floppy Disk | 500 | races against the clock +25% time | failing costs twice the heat and standing |
  | Lucky Penny | The First Token Ever | 650 | every jackpot (yours or the resonant Bot's) +1-3 shards | bonuses pay 10% less |
  | Night Shift | Haunted Pickaxe | 900 | Mining Bot twice as fast | mining by hand ×0.75 |
  | Golden Ledger | Golden Keyboard | 1,200 | shards sell for 15% more | shards cost 15% more |
  | Lightning Rod | Bottled Lightning | 1,800 | events twice as often (full length) | disasters 25% more often |
  | Fog Machine | Jar of Fog from Day One | 2,600 | contracts pay +10% per point of heat | heat cools half as fast |
  | Moon Anchor | A Spare Moon | 4,000 | your trades move the price ×0.25 | random swings ×2 |

- **v10 rework (Script: "nobody's gonna buy them"):** every gadget gets an upside on income that
  matters late (Drones, the Bot, contracts, shards) and a downside on another axis. Measured with
  `SIM_GADGETS=<id> SIM_GADGETS_FREE=1 sim 10 20 normal` (late income, hours 6-10, against no
  gadget; the median of 20 seeds): Night Shift +17%, Forged Papers +17%, Tuning Fork +16%, Lava
  Core +13%, Moon Anchor +12% (and more shards), Lucky Penny +7% (and ~250 more shards), Debug
  Duck +2%, Fog Machine and Lightning Rod about even (swingy: they pay when you run hot or keep
  disasters stopped), Golden Ledger -3% for that buying player (it's for net sellers).

## Disasters (`core/disasters.*`; `respond`, `repair`)
- Start once the **Crew** is open: one every **20-35 min** of play (the first after 10-20), one at
  a time, timers only while the game is open. The clock is saved (quitting doesn't reset it); a
  warning in progress isn't (quitting drops it).
- Each starts with a **warning** (20-30 s) that says what's coming, how to stop it and what it
  costs. `respond` (or the verb: brace, oil, douse, chase, unplug) stops it; key `x` in the TUI.

  | Disaster | Needs | Stop it | If it hits |
  |---|---|---|---|
  | Power Surge | 2+ Drones | brace: Drones and Bot off for 60 s | half the Drones down for 10 min (temporary) |
  | Bot Breakdown | a Bot | oil: 60 s of bot income + 10 s of income | the Bot is broken until repaired |
  | Vault Fire | 100+ shards | douse: 20 s of income (min 500) | 8-15% of shards burn |
  | Thieves | 5,000+ tokens | chase: mine 15 times before the warning ends | 5-10% of tokens stolen |
  | Quake | a built stage (not the Sun) | brace: 2% of that stage in shards | the stage cracks: perk off until repaired |
  | Short Circuit | an equipped gadget | unplug: gadgets off for 2 min | one gadget broken until repaired |

- **Fixers soften it:** damage, burnt-out time and repair bills × (1 - min(50%, 5% per effective
  Fixer level)) (`fixerSoften`).
- **Repairs** (`repairs()`, `repair <n|all>`, the Mine tab): the Bot 15% of its next level in
  tokens, an Orrery stage 10% of its cost in shards, a gadget 50% of its cost in shards.
- Script's call: disasters hit machines, stockpiles, and builds and gear (never the crew);
  "permanent" damage means broken until repaired; every one warns first and can be stopped
  where that makes sense; Fixers soften them.

## The Constellations (`core/stars.*`; `stars`, the Stars tab)
- Open when the Orrery is complete. Ten constellations on a sky map (two bands of five), each
  lit by one hard feat. **No power**: the picture and the record. Feats only count once open.

  | Constellation | Feat |
  |---|---|
  | The Miner | a streak of 250 during a Meteor Shower |
  | The Twins | two contracts finished within 10 s of each other |
  | The Hearth | a full crew of 5 through 10 paydays in a row, nobody leaving |
  | The Scales | sell 100+ shards in one order at 3× the fair price or better |
  | The Lantern | stop 3 disasters in a row |
  | The Wanderer | 3 crew members away on deep expeditions at once |
  | The Crown | signature jobs for 3 different clients |
  | The Phoenix | heat from 10 down to 0 within 20 minutes |
  | The Hourglass | 4T tokens earned in an hour without mining by hand (1B until v10, which late-game income makes in a second) |
  | The Magpie | every curio, the Dealer's too |

- Unlit constellations show their shape as faint `+`; lit ones are `*` in their theme color.

## Tasks (`core/tasks.*`; the Tasks tab, `task` in the Console)
- Open at **Overseer** (72 renown). Then the **Automation License**: 7,500 shards, once, with
  3 slots; more slots cost 1.5K, 3K, 5K, 8K and 12K shards (8 at most).
- **A task is one line:** a trigger, `do`, one action. They run once a second, top to bottom,
  only while the game is open, and each sees the game as the tasks above it left it.

  | Trigger | Runs |
  |---|---|
  | `when <condition> do ...` | the second the condition turns true (false → true); if the action can't happen then, it waits while the condition stays true |
  | `while <condition> do ...` | every second the condition is true and the action can happen |
  | `every <time> [if <condition>] do ...` | once per interval (5 s to 24 h), if the condition holds and the action can happen; otherwise at the next interval |

  A new, edited or re-enabled task counts its condition as false (so a `when` that's already
  true fires at once) and an `every` checks right away. `when`'s state is saved, so restarting
  the game doesn't fire it again.
- **Conditions:** `< <= > >= = == !=`, `and or not`, brackets, `+ - * /` (dividing by 0 gives 0).
  A value alone is true when it isn't 0. Numbers: `500`, `5,000`, `2.5k`, `3M`, `1.2B`, `5T`;
  times: `30s`, `5m`, `1h`, `1h30m`. Values are amounts or times (only `payday` is a time): a
  plain number takes the other side's kind, and `5m` means 5 minutes next to a time and 5
  million next to an amount. Comparing or adding a time and an amount is an error. One
  comparison at a time (`a < b < c` is an error: join with `and`).
- **What tasks can read** (only what the screen already shows): `tokens`, `shards`, `income`
  (the 5-minute average), `price`, `heat`, `level.<pickaxe|magnet|drone|bot>`, `cost.<...>` (the
  next level), `crew`, `home`, `away`, `wages`, `payday`, `broken`, `orrery.left`, `jobs`, `true`,
  `false`. Nothing about offers, clients, candidates, events or chance.
- **What tasks can do:** `upgrade <item> [n|max]`, `buy shards <n|max>`, `sell shards <n|all>`,
  `train <who>`, `promote <who>`, `send <who> <short|long|deep>`, `repair`, `fund orrery <n|all>`
  (all keeps the next payday's wages), `equip <gadget>`, `unequip <gadget>`, `autobuyer on|off`,
  `notify "<text>"` (not with `while`). `<who>` is a name, a slot number or `anyone` (the first
  member it works for). **Never:** mining, stopping disasters, answering offers, firing,
  hiring, taking or dropping jobs, the save, themes. Typing one of those says so.
- **Never runs what can't happen, never pays for it:** every action is checked first (away,
  at the cap, can't afford, nothing broken, already on...). The task waits and its status says
  why. A crew member who left stays named in the task; it just waits.
- **Fees (tuned with the simulator):** each run costs instructions × 0.3 s of income (the
  5-minute average), at least 10 tokens. Every value, number, operator and the action count as
  one instruction. Fees never take more than a fifth of a minute's income: past that, tasks
  wait. Fees grow with income, so they stay the same share at every stage. In 12-hour sims:
  3 sensible tasks ≈ 2% of income, 8 busy tasks ≈ 7-9%, 8 `while` tasks firing every second
  ≈ 9-14% (and they never pass the 20% limit).
- **Mistakes** are caught while you type, with a `^` under the spot and a suggestion ("did you
  mean shards?", "you have Odette, Kaan..."). Crew names are checked against your crew when
  you write the task; a saved task this build can't read is kept, marked broken.
- **Console:** `task` (the list), `task add <task>`, `task edit <n> <task>`, `task on|off <n>`,
  `task quiet|loud <n>`, `task move <n> <to>`, `task remove <n>`, `task check <task>` (is it
  right, what it costs), `task values`, `task actions`, `task license`, `task slot`.
- **The Tasks tab:** one row per task (On/Off, edit, quiet, a two-click delete) with its status
  ("ran 14 times, last 2m ago; 5.6B in fees", "waiting: Odette is away", "next check in 3m"),
  the fees of the last minute against the limit, the box (checked as you type, with the cost
  of a run), and what tasks can read and do.

## Between profiles (`core/exchange.*`; `pay`, `borrow`)
- Two profiles that are **running at the same time** (two windows, one computer, one profiles
  folder) can move **tokens** between them. Never shards (Script's rule). Not with `--save-dir`.
- `pay <profile> [amount]`: the tokens leave at once. A payment first pays off what you owe them,
  then lends what they asked for, and the rest is a gift. Without an amount: what they asked
  for, or else what you owe. `pay` alone: who's running, the balances, open requests.
- `borrow <profile> <amount>`: a request in their log, standing for 5 minutes; their `pay <you>`
  lends it. Names with spaces work (`pay Big Bob 250M`); amounts take k, M, B and T.
- **No server:** the games meet in `<data>/exchange/` (next to `profiles/`):
  - `<name>/alive`: each game's heartbeat, every 2 s; older than 8 s means it isn't running.
    A fresh heartbeat with another session id at start means the same profile is open in two
    windows, which gets a warning.
  - `<name>/<id>.pay` and `<id>.ask`: files written to a temporary name, then renamed in.
  - **Exactly once:** the receiver claims a payment by renaming it (`.taken`), then credits it;
    a payer who sees no claim within 20 s takes it back by renaming it (`.back`), then refunds.
    One rename wins, so a payment is never both delivered and refunded, and never lost.
    Quitting takes back anything unclaimed before the final save.
- **Balances** (`[exchange] balances`, "+ they owe you, - you owe them") are kept by both games,
  each updating its side when the payment lands (the receiver on claiming, the payer when the
  file is gone). Received tokens aren't income (no goals, no income rate).

---

## Updates (`play.py`, `core/update.*`)
- **`play.py`** (the recommended way to start): builds if needed, starts the game with
  `TOKENRUSH_REPO` set to its git checkout (and git told never to ask for a password), and passes
  every option through. Exit code 42 means "update me": it runs `git pull --ff-only`, then
  `build.py`, then starts the game again. If either fails it shows why, waits for Enter, and starts
  the version you had with `--no-update-check`.
- **The check** runs on its own detached thread: `git fetch`, then how many commits `HEAD` is
  behind its upstream, whether it has commits of its own (diverged: play.py can't fast-forward,
  so it says "pull it yourself"), the newest three commit titles, and whether tracked files are
  edited. It runs at start and again every 5 to 10 minutes (random); offline, the last answer
  stands. The Console's `update` runs one right away (one check at a time, behind a mutex).
- **Where it shows:** the profile picker's update row (**update now** / **not now**, or `u`); in the
  game a log line for each newer update (one turned down on the picker isn't repeated), the `u`
  key and a footer hint; `update`/`u` in the Console.
- Started without `play.py`, or with `--no-update-check`, the game never runs git.

## Under the hood
- **`src/core/`:** the game. It never prints or reads input; actions return `Notes`.
- **Threads:**
  - The workers: world, market, automation, crew, autosave every 30 s, and the exchange between
    profiles. All lock `Shared::m`.
  - Lock order: `saveMtx` → `m`.
- **World tick order** (`worldTick`): passive income, play time, streak expiry, events, Leech,
  contracts and heat, offers, disasters, tasks, the Hourglass, stats, then `checkGoals` (which also pays
  secret goals and lights constellations).
- **The theme** the Console's commands paint with is a process-wide atomic (`setTheme` in `shell/term.cpp`), set
  when a profile loads, by `theme`, and by the TUI every frame (for its Console); back to Classic
  for the profile pickers.
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
  - An INI file driven by one field table (`saveFields()`). Lists (keepsakes, former crew) are
    one line per entry, "`a ; b ; c`".
  - Unknown keys are ignored; missing keys keep their defaults.
  - A bad file is moved to `.save.bad`.
  - Every older version loads (v1–v2 positional, v3 sectionless, v4–v10 INI). A real v8 save
    (made by the v8 build) loads and upgrades.
  - Doubles are saved floored to 4 decimals.
  - **Token cap:** token totals saturate at 10^18 (`TOKEN_CAP`, `addTokens`).
- **Builds:**
  - `python build.py [--tui] [--sim] [--rebuild]`: incremental, with per-file hashes
    covering the source, the headers it includes and the flags.
  - `build.ps1` is the older PowerShell version of the same thing.
  - `make` on Linux/macOS.
  - Build times go to `target/debug/build-times.log`.
- **Simulator:** `tools/sim.cpp` (`sim <hours> <seeds> normal|trader|idle|noecon|tasks-light|tasks-heavy|tasks-greedy`)
  plays the core headlessly to check balance after changing numbers. The `tasks-*` players save
  up for the license (and slots) once Tasks open, hand chores to a set of tasks, and report what
  share of their income went to fees.
- **`.trash/`** in the project root is for old or temporary files instead of deleting them.

## Design decisions to respect
- **No prestige or reset loops.** They feel repetitive, and permanent carry-over power makes the
  game feel unfair. Things that add new things to do are welcome; things that add bigger numbers
  aren't. Crew promotions are fine because they're per member and within one save.
- **The Monolith stays as it is**, as the mid-game project.
- **One edition, with a Console.** The separate CLI was discontinued (Script's call) and folded into
  the Console tab. Every feature still gets a typed command in the Console (with its `help` page),
  as well as its place in the tabs. (A "Golden Meteor" click event was once rejected because it
  didn't fit typing; that reasoning still holds for the Console.)
- **Help is detailed.** `help <command>` is a full page (what, how, examples, related), not a
  sentence. A new command gets its page in the `shell/help.cpp` table.
- **Themes are per profile**, and Classic stays the terminal's own colors. The custom theme is one
  slot, bought dearly (15,000 shards), edited by hand with fixed preview scenes.
- **Disasters** (Script's decisions): they damage machines, stockpiles, and builds and gear, never
  the crew; they can be stopped when the logic allows (put out the fire, chase the thieves); they
  always warn first; lasting damage means broken until repaired; Fixers soften them; they start
  once the Crew is open.
- **Secret goals pay shards, not renown.** Constellations pay nothing at all.
- **Ideas offered but never asked for:**
  - a profile lock against running the same profile twice
  - parallel compiles in `build.py`
  - fixing the Orrery price ceiling
  - Monolith art like the Orrery's
  - market limit orders
  - deciding the fate of `build.ps1`

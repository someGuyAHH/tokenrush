// Every typed command in one table: its names, usage, a one-line summary, and a full help page
// (what it does, how it works, examples, related commands). parseCmd(), 'help' and
// 'help <command>' all read it, so a new command is one new row.

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "commands.hpp"
#include "core/content.hpp"
#include "core/game.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

enum Section { PLAYING, BUSINESS, SPENDING, PROGRESS, SECTION_COUNT };
const char* const SECTION_NAMES[SECTION_COUNT] = {
    "Playing:", "Shards, contracts and crew:", "Spending (not about mining):", "Progress and other:",
};

struct Example {
    const char* typed;
    const char* what;
};

struct CommandInfo {
    Cmd cmd;
    std::vector<std::string> names;    // the first one is the command's real name, the rest are aliases
    const char* usage;                 // how it's typed, e.g. "shop [item [n]]"
    const char* summary;               // one line for 'help'
    std::vector<const char*> details;  // the paragraphs of 'help <command>'
    std::vector<Example> examples;
    std::vector<const char*> see;      // related commands
    int feature;                       // a Feature it needs, or -1
    Section section;
};

constexpr int MARKET = (int)Feature::Market, CONTRACTS = (int)Feature::Contracts, CREW = (int)Feature::Crew;
constexpr int CURIOS = (int)Feature::Curios, MONOLITH = (int)Feature::Monolith, ORRERY = (int)Feature::Orrery;
constexpr int WORKSHOP = (int)Feature::Workshop, STARS = (int)Feature::Stars, TASKS = (int)Feature::Tasks;

const std::vector<CommandInfo>& commands() {
    static const std::vector<CommandInfo> table = {
        // ----- Playing -----
        {Cmd::Mine, {"mine", "m"}, "mine", "Mine tokens (build a streak for bigger payouts)",
         {"Every mine pays your base yield (1 + your Pickaxe level) plus a random extra of up to the same again, "
          "times your streak multiplier.",
          "Streak: each mine adds 1 to your streak, and every point adds 5% to what you mine, up to x2.00 at a "
          "streak of 20. The streak breaks if you don't mine for 15 seconds (6 with the Amnesia strain).",
          "Luck: a mine can turn into a bonus (8x base and up) or, rarely, a jackpot (80x base and up). Magnet "
          "levels raise both the odds and the size. A Meteor Shower doubles everything you mine while it lasts."},
         {{"mine", "mine once"}, {"m", "the same, shorter"}},
         {"shop", "status"}, -1, PLAYING},
        {Cmd::Shop, {"shop"}, "shop [item [n]]", "Upgrades and the Auto-Buyer ('shop 1 10': 10 Pickaxe levels)",
         {"With no arguments it opens the shop menu, which stays open so you can buy several things in a row; "
          "type 0 (or back) to leave it. With an item it buys right away, without the menu.",
          "The items: 1 Pickaxe (+1 base yield), 2 Magnet (better bonus and jackpot odds), 3 Drone (+0.5 "
          "tokens a second, also while the game is closed, up to 12 hours), 4 Mining Bot (mines on its own, "
          "faster every level), 5 the Auto-Buyer license.",
          "Every level costs more than the last (the price grows with the square of the level). A number after "
          "the item buys that many levels, or as many as you can afford if that's fewer."},
         {{"shop", "open the shop menu"}, {"shop 1", "buy one Pickaxe level"}, {"shop 3 20", "buy up to 20 Drone levels"},
          {"shop drone 5", "items can be named too: pickaxe, magnet, drone, bot, license"}},
         {"auto", "status"}, -1, PLAYING},
        {Cmd::Status, {"status", "s"}, "status", "Rates, upgrade costs, active event, open offers",
         {"One screen with everything going on right now: your tokens and what you're earning (and from where), "
          "your title and the next unlock, your streak, the active event, an open offer, a race or a loan, heat, "
          "your jobs and crew, upgrade levels and prices, and when the game last saved."},
         {{"status", "show it"}, {"s", "the same, shorter"}},
         {"stats", "goals"}, -1, PLAYING},
        {Cmd::Yes, {"yes", "y"}, "yes", "Say yes to the offer on the table",
         {"Every 10 to 15 minutes someone makes you an offer: a shady broker, an investor, a rival miner, a crew "
          "member who wants a raise, an auditor who can be bribed, a dealer with a curio no shop sells. It shows up "
          "in the log with what it costs.",
          "You have 45 seconds to answer. If the price has moved since (say you spent the tokens), the deal is off."},
         {{"yes", "accept the open offer"}, {"y", "the same"}},
         {"no", "status"}, -1, PLAYING},
        {Cmd::No, {"no", "n"}, "no", "Say no to the offer on the table",
         {"Turns the open offer down. Letting it run out does the same. Nobody minds, except a crew member who "
          "asked for a raise: they sulk and work at half effort for 10 minutes."},
         {{"no", "turn it down"}, {"n", "the same"}},
         {"yes"}, -1, PLAYING},
        {Cmd::Respond, {"respond", "douse", "chase", "brace", "oil", "unplug"}, "respond", "Stop the disaster that's coming",
         {"Once the Crew is open, something goes wrong every 20 to 35 minutes of play. It always starts with a warning in the "
          "log (and on the status screen) that says what's coming, what stopping it takes, and how long you have. "
          "'respond' does whatever this one needs; the words in the warning (douse, chase, brace, oil, unplug) all work too.",
          "Power Surge: brace pulls the breakers (Drones and Bot stop for a minute); otherwise half your Drones burn out for "
          "about ten minutes. Bot Breakdown: oil costs a few tokens; otherwise the Bot stops until you repair it. Vault Fire: "
          "douse pays the firefighters; otherwise 8-15% of your shards burn. Thieves: chase, then mine 15 times before they "
          "get away; otherwise 5-10% of your tokens are gone. Quake: brace shores the Orrery up with a few shards; otherwise a "
          "stage cracks and its perk is off until repaired. Short Circuit: unplug turns your gadgets off for two minutes; "
          "otherwise one burns out until repaired.",
          "Fixers soften all of it: less damage and cheaper repairs, up to half with enough of them."},
         {{"respond", "stop it"}, {"douse", "the same, during a Vault Fire"}, {"chase", "start chasing thieves (then mine!)"}},
         {"repair", "status", "crew"}, CREW, PLAYING},
        {Cmd::Repair, {"repair", "fix"}, "repair [n|all]", "Fix what disasters broke",
         {"Lists everything that's broken (the Mining Bot, a cracked Orrery stage, a burnt-out gadget), what it costs to fix "
          "and what's off until then. The Bot is repaired with tokens; Orrery stages and gadgets with shards. Fixers make "
          "repairs cheaper. Burnt-out Drones don't need you: they come back on their own."},
         {{"repair", "what's broken"}, {"repair 1", "fix the first"}, {"repair all", "fix everything you can afford"}},
         {"respond"}, CREW, PLAYING},
        {Cmd::Auto, {"auto"}, "auto [on|off]", "Show or toggle the Auto-Buyer",
         {"The Auto-Buyer license costs 2,000 tokens (shop item 5). Once you own it, every 5 seconds it buys "
          "your cheapest upgrade, but only while you have 3x its price, so it never empties your wallet.",
          "It's switched on when you buy it. Turn it off when you're saving up for something."},
         {{"auto", "say whether it's on"}, {"auto off", "stop it buying"}, {"auto on", "start it again"}},
         {"shop"}, -1, PLAYING},

        // ----- Shards, contracts and crew -----
        {Cmd::Market, {"market", "mk"}, "market", "Shard price, chart, buy and sell quotes, your trading record",
         {"Shards are the second currency: crew wages, contract stakes and the Orrery are paid in them. You buy "
          "and sell them here with tokens.",
          "The price wanders on its own and drifts back toward 150, with the odd sudden rush or crash. Buying "
          "costs a little above the price and selling pays a little below it: that gap is the spread (5% to start). "
          "Heat widens it by 2% a point; Brokers narrow it. Every shard you trade also pushes the price a little "
          "(up when you buy, down when you sell), so big orders get worse as they go.",
          "The screen shows the price and a chart of the last 5 minutes, quotes for 10 shards, and your trading "
          "record: what you've bought and sold, the average prices, the net, and how much went to the spread "
          "and price impact."},
         {{"market", "show it"}, {"mk", "the same, shorter"}},
         {"buy", "sell", "crew"}, MARKET, BUSINESS},
        {Cmd::Buy, {"buy"}, "buy <n|max>", "Buy shards with tokens",
         {"Buys shards at the current buy price, plus the price impact of the order itself (each shard pushes the "
          "next one up by about 0.25%). One order can be up to 5,000 shards.",
          "An order that would fill at more than 3x the current price (a huge order, or a price spike) asks you "
          "to type yes first."},
         {{"buy 10", "buy ten shards"}, {"buy 2k", "amounts can be 2000, 2,000 or 2k (also m and b)"},
          {"buy max", "as many as your tokens cover, up to 5,000"}},
         {"sell", "market"}, MARKET, BUSINESS},
        {Cmd::Sell, {"sell"}, "sell <n|all>", "Sell shards for tokens",
         {"Sells shards at the current sell price, minus the price impact of the order (each shard pushes the next "
          "one down). One order can be up to 5,000 shards. Selling isn't income: it turns shards back into tokens, "
          "so it doesn't count toward lifetime earnings or your goals.",
          "Keep enough shards for your crew's next payday, or they quit."},
         {{"sell 10", "sell ten shards"}, {"sell 1.5k", "sell 1,500"}, {"sell all", "everything, up to 5,000"}},
         {"buy", "market", "crew"}, MARKET, BUSINESS},
        {Cmd::Contracts, {"contracts", "ct", "jobs"}, "contracts", "Timed jobs: offers on the board and your active one",
         {"Clients post timed jobs on a board of three: earn some tokens, mine so many times, hit jackpots, build a "
          "fresh streak, let the Mining Bot earn, trade shards, cool your heat down. Targets are sized from how fast "
          "you've actually been playing, so they're a stretch at every stage of the game. A new offer arrives every "
          "minute while the board isn't full, and offers leave the board after a while.",
          "Endurance jobs are won by lasting, not by reaching a number: hold so many shards until the timer runs out, "
          "or keep your hands off the mine (the bot, drones and crew keep earning). Break the rule once and the job "
          "fails on the spot.",
          "Every client remembers you. Deliver and your standing with them rises (see 'clients'): better pay, and "
          "Trusted clients sometimes post signature jobs (marked SIG: longer, no stake, double pay). Fail them and it "
          "falls, down to where they stop posting jobs for you at all. Clients have rivals, and a style: Shady ones pay "
          "40% more but their money adds heat, Miners only want mining by hand, Merchants pay in shards.",
          "Big jobs (marked BIG) take a shard stake, last longer and pay much more. Finish a job before its timer runs "
          "out and you get its tokens, its shards and your stake back; a big one also cools heat by 1.",
          "Fail or abandon a job and you lose the stake and gain heat: +1, or +2 for a big job. Heat widens the market "
          "spread and, from 3 up, invites audits that seize part of your tokens. It cools by itself, slowly.",
          "Timers only run while the game is open."},
         {{"contracts", "show the board and your jobs"}, {"ct", "the same, shorter"}},
         {"accept", "abandon", "market"}, CONTRACTS, BUSINESS},
        {Cmd::Clients, {"clients", "cl"}, "clients", "Your standing with every client, their styles and rivals",
         {"Standing runs from -10 to 10, starting at 0. Finishing a job for a client raises it by 1 (2 for a big or "
          "signature job) and lowers it by 1 with their rival. Failing or abandoning one lowers it by 2 (3 for a big "
          "job); small fry forgive more easily and only take 1.",
          "Tiers: Burned (-5 and below) won't post jobs for you, until you bribe them ('bribe') or deliver a "
          "make-amends job they post (see 'help bribe'). Wary (-4 to -2) pay 20% less. Neutral (-1 to 2) pay "
          "normally. Friendly (3 to 5) pay 15% more. Trusted (6 and up) pay 30% more and now and then post a signature "
          "job: half again as long, no stake, double pay.",
          "Styles: Shady clients pay 40% more, but every job you finish for them adds 1 heat. Miners only post "
          "mining-by-hand jobs and pay 25% more shards. Merchants post money and market jobs and pay 60% more shards "
          "and 20% fewer tokens. Small fry only post small jobs."},
         {{"clients", "the table"}, {"cl", "the same"}},
         {"contracts", "bribe"}, CONTRACTS, BUSINESS},
        {Cmd::Bribe, {"bribe"}, "bribe <client>", "Pay a Burned client to work with you again",
         {"A Burned client posts no jobs, so delivering can't win them back. Two things can. A bribe, paid in shards, "
          "puts them at -4 (Wary) right away. It costs more the deeper you've sunk with them (twice as much at -10 as at "
          "-5), grows with your income like job pay does, and every bribe to the same client doubles the next one. "
          "'clients' shows each Burned client's price.",
          "Or wait for a make-amends job: now and then a Burned client posts one (the log says so). It's sized like a "
          "big job, with a stake and no pay at all; deliver it and they're Wary again. Fail or abandon it and they're "
          "back at -10, and won't offer another for half an hour of play."},
         {{"bribe syndicate", "by part of a name"}, {"bribe 5", "by its number in 'clients'"}},
         {"clients", "contracts"}, CONTRACTS, BUSINESS},
        {Cmd::Accept, {"accept"}, "accept <n>", "Take offer n from the board",
         {"Takes an offer off the board and starts its timer. A big job takes its shard stake right away.",
          "You can run one job at a time, or two after the Orrery's Second Ring, as long as they're different kinds "
          "(two jobs watching the same thing would count the same work twice). A streak job starts a fresh streak."},
         {{"accept 2", "take the second offer"}},
         {"contracts", "abandon"}, CONTRACTS, BUSINESS},
        {Cmd::Abandon, {"abandon"}, "abandon [n]", "Walk away from a contract (counts as a failure)",
         {"Gives up a job: you lose its stake and gain heat, exactly as if the timer had run out. It asks you to type "
          "yes first. With two jobs running, say which one."},
         {{"abandon", "give up your only job"}, {"abandon 2", "give up the second of two"}},
         {"contracts"}, CONTRACTS, BUSINESS},
        {Cmd::Crew, {"crew"}, "crew", "Your hires and who's looking for work",
         {"Up to four named hires (five after the Orrery's Base Plate) work for you in the background. There are "
          "always three candidates looking for work, and new faces show up every 5 minutes.",
          "Roles: Miners mine a swing every 5 seconds. Brokers tighten the market spread, cut price impact and add "
          "shards to contract rewards. Scouts find shards now and then. Fixers make heat cool faster and can make "
          "audits go away.",
          "Traits: about two in three candidates have one, and every trait is a trade-off (Greedy works harder but "
          "wants double wages, Frugal is cheap but works less, and so on).",
          "Payday: every 5 minutes each member takes about their level in shards. Anyone you can't pay walks out "
          "(a Loyal member stays through one missed payday)."},
         {{"crew", "show your crew and the candidates"}},
         {"hire", "train", "promote", "fire"}, CREW, BUSINESS},
        {Cmd::Hire, {"hire"}, "hire <n>", "Hire candidate n",
         {"Hiring costs shards: the role's base price times the candidate's level, and 50% more for every member you "
          "already have. A fresh crew gets a full 5 minutes before the first payday."},
         {{"hire 1", "hire the first candidate"}},
         {"crew", "fire"}, CREW, BUSINESS},
        {Cmd::Fire, {"fire"}, "fire <n>", "Let crew member n go",
         {"They leave right away, with no severance and no refund. Useful to free a slot for a better candidate."},
         {{"fire 3", "let the third member go"}},
         {"crew", "hire"}, CREW, BUSINESS},
        {Cmd::Train, {"train"}, "train <n>", "Train crew member n one level",
         {"Costs 6 x level² shards (half for a Quick Learner). Every level makes them better at their job, and also "
          "raises their wage by one shard a payday.",
          "The level cap is 5, or 8 after the Orrery's Gearwork, plus 3 for every promotion."},
         {{"train 2", "train the second member"}},
         {"promote", "crew"}, CREW, BUSINESS},
        {Cmd::Promote, {"promote", "pr"}, "promote <n>", "Promote crew member n (at their level cap)",
         {"Only possible at the level cap. A promotion gives a star: the cap goes up by 3, and every level counts 25% "
          "more. The first costs 300 shards and each one after costs 3x the last (30% less if Ambitious), so there's "
          "no limit, just a point where it stops paying."},
         {{"promote 1", "promote the first member"}, {"pr 1", "the same"}},
         {"train", "crew"}, CREW, BUSINESS},

        {Cmd::Expedition, {"expedition", "exp", "send"}, "expedition <n> <short|long|deep>", "Send a crew member exploring",
         {"An expedition takes a member away for 10 minutes (short), 30 (long) or an hour (deep). While they're away they "
          "don't work, and their wages still come out on payday. The timer only runs while the game is open.",
          "When they come back they bring shards: more for longer trips and higher levels, and half as much again for a "
          "Scout. Longer trips can also turn up a curio no shop sells (from the Under the Counter set), give a member with "
          "no trait one, or teach them a level. They can also come back hurt (half effort for 15 minutes, and half the "
          "shards); Fixers who stay home make that rarer.",
          "With just a member (or nothing), it shows the odds for each trip and what that member would typically bring back."},
         {{"expedition", "the trips and the odds"}, {"expedition 2", "what member 2 would bring back"},
          {"expedition 2 deep", "send member 2 on a deep expedition"}, {"exp 1 short", "the same, shorter"}},
         {"crew"}, CREW, BUSINESS},

        // ----- Spending -----
        {Cmd::Curios, {"curios", "c"}, "curios [n]", "Buy collectibles for your shelf",
         {"Curios are collectibles in sets. They don't make you richer: owning them, and completing sets, is for "
          "goals and for the shelf. With no number this opens the shelf menu; with a number it buys that curio.",
          "The first three sets (Desk Toys, Relics, Impossible Things) are in the shop, priced from the early game to "
          "the very late one. The fourth, Under the Counter, isn't sold anywhere: now and then the Curio Dealer offers "
          "one (at half again what it's worth), and a crew expedition can turn one up.",
          "The shelf also keeps your keepsakes: things crew members leave behind when they quit, or give you when they "
          "earn their third star."},
         {{"curios", "open the shelf"}, {"curios 3", "buy curio 3"}},
         {"goals"}, CURIOS, SPENDING},
        {Cmd::Prompt, {"prompt"}, "prompt [n] | prompt custom <text> | prompt color <#rrggbb|off|on>", "Buy or switch prompt styles, make your own, or color it",
         {"Changes the prompt in front of your cursor. Styles are bought once with tokens and are yours to switch "
          "between after that.",
          "A custom prompt is any 1 to 5 plain keyboard characters (no spaces at the ends). It costs 30x the dearest "
          "style once; after that you can change its text for free.",
          "'prompt color #ff8800' draws whichever prompt you use in that color (#f80 works too); only the prompt, so "
          "what you type keeps the normal color. It's bought once with shards (4x the custom prompt's price, in shards), "
          "then changing it is free. 'prompt color off' goes back to the theme's color, 'prompt color on' brings yours back."},
         {{"prompt", "list the styles"}, {"prompt 3", "buy or switch to style 3"}, {"prompt custom >_<", "your own prompt"},
          {"prompt color #ff8800", "an orange prompt"}, {"prompt color off", "the theme's color again"}},
         {}, -1, SPENDING},
        {Cmd::Theme, {"theme", "themes"}, "theme [n|name] | theme custom [<color> <#hex> | from <theme>]", "Change how the game looks: color themes, with previews",
         {"A theme recolors everything: headings, tokens, shards, good and bad news, and in the full-screen edition the "
          "background and buttons too. The list shows each theme as a preview line in its own colors.",
          "Classic and Dusk are free. The others are bought once with shards and then stay yours to switch between. "
          "A theme belongs to the profile: another profile keeps its own.",
          "Classic uses your terminal's own color palette, so it looks like the game always did. The other themes use "
          "24-bit color, which any modern terminal (Windows Terminal, most Linux and macOS terminals) can show.",
          "The custom theme (15,000 shards, once) is yours to color by hand: it starts as a copy of the theme you're "
          "using, then every one of its 17 colors can be changed for free. The Themes tab has an editor for it, with a "
          "preview that uses every color and a check of how readable the text is. Here, 'theme custom' switches to it "
          "and lists its colors, 'theme custom <color> <#hex>' changes one (the color names are text, quiettext, accent, "
          "good, bad, warning, event, tokens, shards, heat, background, panel, button, buttontext, buttonoff, "
          "buttontextoff and infected), and 'theme custom from <theme>' starts it over from another theme you own."},
         {{"theme", "every theme, with a preview"}, {"theme 2", "switch to Dusk"}, {"theme glacier", "names work too"},
          {"theme custom", "switch to your custom theme (buying it the first time)"},
          {"theme custom accent #ff8800", "make the accent orange"}, {"theme custom from neon", "start over from Neon's colors"}},
         {"prompt"}, MARKET, SPENDING},
        {Cmd::Monolith, {"monolith"}, "monolith", "Look at the Monolith, the mid-game build",
         {"The Monolith is a five-stage build you pour tokens into: Foundation, Pillars, Spire, Crown and Beacon. "
          "Finishing it is worth a big goal. This draws it as it stands."},
         {{"monolith", "look at it"}},
         {"build"}, MONOLITH, SPENDING},
        {Cmd::Build, {"build", "b"}, "build <n|all>", "Pour tokens into the Monolith",
         {"Pours tokens into the current stage; whatever is left over carries into the next one."},
         {{"build 5000", "pour in 5,000 tokens"}, {"build all", "pour in everything you have"}},
         {"monolith"}, MONOLITH, SPENDING},
        {Cmd::Orrery, {"orrery", "o"}, "orrery [n|all]", "Look at the Orrery, or set shards into it (late game)",
         {"The Orrery is the late-game build, made of shards. Each of its five stages unlocks something: a 5th crew "
          "slot, Resonance (Drones and the Bot scale with your mining), a second contract slot, a higher crew level "
          "cap, and finally the Sun. Completing it cures an Infected profile.",
          "Token wealth can't rush it: buying lots of shards at once drives their price up, so it takes time and "
          "the market."},
         {{"orrery", "look at it"}, {"orrery 500", "set 500 shards into it"},
          {"orrery all", "everything except your crew's next payday"}},
         {"market", "buy"}, ORRERY, SPENDING},

        {Cmd::Workshop, {"workshop", "ws", "gadgets"}, "workshop", "Gadgets: what you've built, what's equipped, what you could build",
         {"The Workshop turns curios into gadgets. Each gadget is built from one curio you own (the curio stays on your "
          "shelf) plus some shards, and once built it's yours to equip in one of 3 slots or take off again, as often as "
          "you like.",
          "Every gadget is a trade-off: something gets better and something else gets worse. The Tuning Fork makes "
          "your Drones and Bot earn 30% more while your streak is 20 or more, but the streak breaks after 8 seconds; "
          "the Night Shift doubles the Mining Bot but your own mining pays less; the Golden Ledger makes shards sell "
          "for 15% more and cost 15% more. Pick the ones that fit how you're playing right now.",
          "The list shows every gadget, what it's built from, its price, and both sides of the deal."},
         {{"workshop", "the list and your slots"}, {"ws", "the same"}},
         {"craft", "equip", "unequip", "curios"}, WORKSHOP, SPENDING},
        {Cmd::Craft, {"craft"}, "craft <n|name>", "Build a gadget (needs its curio and shards)",
         {"Builds gadget n from the 'workshop' list. You need its curio on your shelf (it stays there) and the shards. "
          "A new gadget goes straight into a free slot; with all three taken, it waits on the bench."},
         {{"craft 2", "build the second gadget"}, {"craft tuning", "names (or their start) work too"}},
         {"workshop", "equip"}, WORKSHOP, SPENDING},
        {Cmd::Equip, {"equip"}, "equip <n|name>", "Put a gadget you've built into a free slot",
         {"Equipping is free and instant. With all three slots taken, unequip one first."},
         {{"equip 5", "equip the fifth gadget"}},
         {"unequip", "workshop"}, WORKSHOP, SPENDING},
        {Cmd::Unequip, {"unequip"}, "unequip <n|name>", "Take a gadget off (it stays built)",
         {"Frees its slot. The gadget stays built, so you can equip it again any time for nothing."},
         {{"unequip 5", "take off the fifth gadget"}},
         {"equip", "workshop"}, WORKSHOP, SPENDING},

        {Cmd::Task, {"task", "tasks"}, "task [add|edit|on|off|quiet|loud|move|remove|check|license|slot] ...",
         "Tasks: one-line rules that run errands for you, for a fee",
         {"A task is one line: when something is true, do one thing. For example 'while broken do repair', or "
          "'every 10m if tokens >= 5 * cost.pickaxe do upgrade pickaxe 10'. Tasks need the Automation License "
          "(7,500 shards, once, with 3 slots; more slots cost 1.5K, 3K, 5K, 8K and 12K, up to 8). They run once a second, top to "
          "bottom, only while the game is open.",
          "There are three ways to start a task. 'when <condition> do ...' acts the moment the condition turns true; "
          "if the action can't happen right then, it waits while the condition stays true. 'while <condition> do ...' "
          "acts every second the condition is true. 'every <time> [if <condition>] do ...' checks once per interval "
          "(5s at the most often, 24h at the least).",
          "Conditions compare numbers: < <= > >= = != with and, or, not, brackets and + - * /. Numbers are written "
          "like the game shows them: 500, 5,000, 2.5k, 3M, 1.2B, 5T; times like 30s, 5m, 1h or 1h30m. '5m' means 5 "
          "minutes next to a time (payday < 5m) and 5 million next to an amount (tokens > 5m). A value on its own "
          "counts as true when it isn't 0 ('while broken do repair'). 'task values' lists what tasks can read: only "
          "what the screen already shows (tokens, shards, income, price, heat, upgrade levels and costs, crew at "
          "home, wages, payday, broken things, the Orrery, jobs).",
          "'task actions' lists what tasks can do: upgrade, buy and sell shards, train, promote, send on expeditions, "
          "repair, fund the Orrery, equip and unequip gadgets, the Auto-Buyer, and notify (a message in the log). "
          "Never: mine, stop disasters, answer offers, fire, hire, take jobs, or anything about your save.",
          "A task never runs an action that can't happen, and never pays for one: it waits, and the list says why "
          "(\"waiting: Odette is away\"). Each run costs a fee of 0.3 seconds of your income for every instruction "
          "(each value, number, operator and the action count as one), and fees never take more than a fifth of "
          "a minute's income: past that, tasks wait. 'task check <task>' tells you what a task would cost, and "
          "mistakes are pointed at with a ^.",
          "Starting a task costs 1 second of your income for every instruction (at least 100 tokens): adding it, "
          "saving an edit to one that's running, and resuming one you suspended. Suspending is free."},
         {{"task", "your tasks, what they're doing, what they've cost"},
          {"task add while broken do repair", "a new task"},
          {"task add when heat >= 8 do notify \"cool it\"", "a reminder, once each time heat reaches 8"},
          {"task edit 2 every 5m do upgrade bot max", "rewrite task 2"},
          {"task suspend 2", "pause it, free ('task resume 2' starts it again, for its startup cost; off/on work too)"},
          {"task quiet 1", "no log lines from it (task loud 1 undoes it)"},
          {"task move 3 1", "make task 3 run first"},
          {"task check while shards > 6k do fund orrery all", "is it right, and what would it cost?"},
          {"task license", "buy the Automation License"}, {"task slot", "buy another slot"}},
         {"repair", "expedition", "shop"}, TASKS, PROGRESS},

        {Cmd::Pay, {"pay", "give"}, "pay [<profile> [amount]]", "Send tokens to another profile that's running too",
         {"Two profiles can trade tokens while both games are open at once (in two windows, on this computer). Only "
          "tokens: never shards. 'pay' alone shows who's running, who owes whom, and any requests.",
          "A payment first pays off what you owe them, then lends them what they asked for with 'borrow' (they owe you "
          "that), and anything more is a gift. Without an amount, 'pay <profile>' pays what they asked for, or else "
          "what you owe them.",
          "The tokens leave your game at once and arrive in theirs within a second or two. If their game doesn't pick "
          "them up within 20 seconds (it closed, say), they come back to you. A payment is never lost or doubled: "
          "exactly one of the two games gets it. Both games keep a note of the balance between you."},
         {{"pay", "who's running, and who owes what"}, {"pay Beta 5B", "send Beta 5 billion tokens"},
          {"pay Beta", "lend what Beta asked for, or pay back what you owe"}, {"pay Big Bob 250M", "names with spaces work"}},
         {"borrow"}, -1, BUSINESS},
        {Cmd::Borrow, {"borrow"}, "borrow <profile> <amount>", "Ask another running profile to lend you tokens",
         {"Sends a request to the other profile's game: its log says what you asked for, and 'pay <you>' there lends it "
          "(then you owe them). The request stands for 5 minutes. Both games have to be open, and it's only tokens.",
          "Paying them back is just 'pay <profile> <amount>' (or 'pay <profile>' for all of it): it goes to the debt first."},
         {{"borrow Alpha 10B", "ask Alpha for 10 billion tokens"}, {"pay Alpha", "pay it all back later"}},
         {"pay"}, -1, BUSINESS},

        // ----- Progress and other -----
        {Cmd::Stars, {"stars", "sky", "constellations"}, "stars", "The Constellations: the sky map and the feats that light it",
         {"Once the Orrery is complete, the sky around it opens: ten constellations, each lit by one hard feat, like a "
          "streak of 250 during a Meteor Shower, two contracts finished within ten seconds of each other, or an hour "
          "of 4T tokens without mining by hand.",
          "They give no power: just the picture, and a record of what you pulled off. Feats only count once the "
          "Constellations are open. Where a feat builds up over time, the list shows how far along you are."},
         {{"stars", "the sky and the list"}, {"sky", "the same"}},
         {"orrery", "goals"}, STARS, PROGRESS},
        {Cmd::Goals, {"goals", "g"}, "goals", "Goals, renown, titles and what they unlock",
         {"Goals are milestones: mine so many times, earn so much, finish the Monolith... Each one earns renown, "
          "renown earns titles, and titles open the rest of the game (the Market, Contracts, the Crew and more). "
          "Once a feature is open it stays open.",
          "Goals for features you haven't opened yet say which title opens them.",
          "Secret goals are listed with only a hint until you find them, usually by doing something odd. They pay "
          "shards instead of renown, so they never stand between you and a title."},
         {{"goals", "show goals and titles"}, {"g", "the same"}},
         {"stats", "status"}, -1, PROGRESS},
        {Cmd::Stats, {"stats", "st"}, "stats", "Your records, where your income comes from, and a chart",
         {"Your records: wealth and the best you've earned per second, mining, business (trading, contracts, audits, "
          "crew), play time and when you reached each title.",
          "Income by source splits everything you've earned between mining by hand, Drones, the Mining Bot, the crew, "
          "contracts and offers, both all time and over the last few seconds.",
          "The chart is everything you've earned over your play time, on a log scale, with a point every 5 minutes.",
          "Former crew lists the last people who left, and how: quit unpaid, or fired."},
         {{"stats", "show them"}, {"st", "the same"}},
         {"status", "market"}, -1, PROGRESS},
        {Cmd::Predict, {"predict", "forecast"}, "predict <what> <time|count> | predict when <condition>", "What the next stretch should bring, or when something will be true",
         {"Multiplies your current rates out. Over a time (1h, 30min, 2h30m, 90s): 'tokens' (your recent income, split "
          "by source), 'shards' (what your Scouts and the Lucky Penny find, minus the wages of the paydays in between), "
          "'heat' (where it will have cooled to, and when it reaches 0), 'paydays' and 'events' (with disasters).",
          "Over a number of mines (1000, 5k, 2.5M): 'mines' (tokens from mining by hand, with a full streak), 'jackpots' "
          "and 'bonuses' (how many to expect at your odds, give or take, and what they pay). Give these a time instead "
          "(10min, 1h) and it's turned into mines at your actual mining pace.",
          "And upgrades: 'predict pickaxe 10' (or magnet, drone, bot) gives the cost of the next 10 levels, how much "
          "they add to your income, and how long they take to pay for themselves.",
          "The other way round, 'predict when <condition>' says how long until something is true, and at what time: "
          "'predict when tokens > 20t', 'predict when shards > 100k', 'predict when tokens > 5 * cost.pickaxe' (when "
          "you can afford 5 Pickaxe levels), 'predict when heat = 0 and home = 5'. The condition is written exactly like "
          "a task's (see 'help task'), and can also read earned (lifetime tokens), mines and jackpots. What moves on "
          "its own: tokens, earned, shards, heat, payday, crew coming home, mines and jackpots; everything else only "
          "changes when you act, and the price is random, so predict won't guess it. It looks up to 30 days ahead.",
          "Both ways read the same projection, so they agree: if 'predict tokens 1h' says 20T, 'predict when tokens > "
          "20t' says about an hour. These are averages from right now: events, contracts, disasters, luck and what you "
          "buy all move the real numbers. Every answer says what it's based on."},
         {{"predict tokens 1h", "an hour of income"}, {"predict jackpots 1000", "jackpots in 1000 mines"},
          {"predict jackpots 10min", "...in ten minutes of mining at your pace"}, {"predict shards 2h", "shards after two hours"},
          {"predict heat 30min", "how cool you'll be"}, {"predict magnet 50", "is 50 Magnet levels worth it?"},
          {"predict when tokens > 20t", "how long until 20T"}, {"predict when tokens > 5 * cost.pickaxe", "when 5 Pickaxe levels are affordable"},
          {"predict when jackpots >= 10k", "at your mining pace"}},
         {"status", "stats", "shop"}, -1, PROGRESS},
        {Cmd::Eval, {"eval", "calc"}, "eval <expression>", "Work something out from your numbers right now ('eval tokens / income')",
         {"Any expression in the Tasks language (see 'help task' for the values), worked out right now: arithmetic "
          "(+ - * / and brackets), comparisons (< <= > >= = !=) and and / or / not. It can read every value a task can "
          "(tokens, shards, income, price, heat, level.pickaxe, cost.bot, crew, payday...) and predict's three extra ones "
          "(earned, mines, jackpots).",
          "Numbers can be written 5k, 2M, 1.5B, 3T, and times 30s, 5m, 1h30m. A comparison says true or false; a time "
          "comes out as a time (eval payday); anything else as a number."},
         {{"eval tokens / income", "seconds of income you're holding"}, {"eval cost.pickaxe * 10", "about what 10 more levels cost"},
          {"eval shards - wages", "shards left after the next payday"}, {"eval payday + 5m", "a time"},
          {"eval tokens >= 5 * cost.pickaxe and heat < 3", "true or false"}},
         {"predict", "task"}, -1, PROGRESS},
        {Cmd::Log, {"log"}, "log [n] [good|bad|warn|event|info]", "Earlier background messages, optionally filtered",
         {"Background news (the bot's reports, events, payday, audits, offers...) prints before each prompt. This "
          "shows it again. The game keeps the last 400 messages of this session.",
          "A number says how many; a kind keeps only that kind of message. They can come in either order."},
         {{"log", "the last 20"}, {"log 50", "the last 50"}, {"log bad", "only bad news"},
          {"log 100 events", "the last 100 events"}},
         {"status"}, -1, PROGRESS},
        {Cmd::Help, {"help", "?"}, "help [command]", "This list, or a full page about one command ('help shop')",
         {"With no command it lists everything you can do right now. With a command (or any of its aliases) it "
          "explains that command in detail, with examples."},
         {{"help", "the list"}, {"help market", "about the market"}, {"help ct", "aliases work: this is 'contracts'"}},
         {}, -1, PROGRESS},
        {Cmd::Clear, {"cls", "clear"}, "cls", "Clear the screen", {"Clears the Console's output, scrollback included."},
         {}, {}, -1, PROGRESS},
        {Cmd::Logout, {"logout", "profile", "profiles"}, "logout", "Save and go back to the profile list",
         {"Saves and goes back to the profile picker, where you can switch to another profile, make one or delete one. "
          "Not available when the game was started with --save-dir."},
         {}, {"quit"}, -1, PROGRESS},
        {Cmd::Save, {"save"}, "save", "Save now (the game also autosaves every 30s)",
         {"Writes the save file right away. The game also saves every 30 seconds and when you quit."},
         {}, {}, -1, PROGRESS},
        {Cmd::Wipe, {"wipe"}, "wipe", "Reset progress (deletes the save)",
         {"Starts this profile over from nothing. It asks you to type WIPE (in capitals) to confirm. An Infected "
          "profile stays infected: the strain belongs to the profile."},
         {}, {"logout"}, -1, PROGRESS},
        {Cmd::Alias, {"alias"}, "alias [command...] <name>", "Your own short names for commands ('alias predict tokens 1h pt')",
         {"The last word is the new name; everything before it is what it runs. 'alias predict tokens 1h pt' makes 'pt' "
          "run 'predict tokens 1h'. Anything typed after an alias goes on the end, so with 'alias predict tokens pt', "
          "'pt 2h' runs 'predict tokens 2h'. The Console shows what an alias ran.",
          "An alias starts with a real command (not another alias) and runs one command. Its name is one word of up to "
          "16 letters, digits, - or _, and can't be a command's name (those always win). Making one with a name you "
          "already use replaces it. With no arguments, 'alias' lists yours.",
          "Aliases are saved with the profile (up to 50), and a wipe keeps them."},
         {{"alias", "list your aliases"}, {"alias predict tokens 1h pt", "pt = predict tokens 1h"},
          {"alias expedition 1 deep deep1", "send crew 1 on a deep trip"}, {"alias pt", "what pt runs"}},
         {"rmalias", "help"}, -1, PROGRESS},
        {Cmd::RmAlias, {"rmalias", "unalias"}, "rmalias <name>", "Remove one of your aliases",
         {"Removes the alias with that name. 'alias' lists them."},
         {{"rmalias pt", "pt is gone"}},
         {"alias"}, -1, PROGRESS},
        {Cmd::Check, {"check"}, "check [count|list|titles|dates|show <n>|files|current|history|find]", "What's new on GitHub (and what came before), without installing it",
         {"Looks at GitHub right away (under 'python play.py') and tells you, without installing anything. Alone, it "
          "says whether you're up to date, and what the newest change is called and when it was made.",
          "'check count' says how many new versions there are (and how many commits of your own GitHub doesn't have). "
          "'check list' lists them, newest first, with when each was made and what it's called (the newest 10; 'check "
          "list 30' for more). 'check titles' and 'check dates' list just one or the other. 'check show 2' prints the "
          "whole message of the second one (or give its hash). 'check files' lists the files the new versions change. "
          "'check current' says which version you're running.",
          "Earlier versions too: 'check history' lists yours and the 10 before it ('check history 30' for more), "
          "'check show ~3' prints the whole message of the third one before yours (or give any hash), and 'check find "
          "<words>' finds every version whose message mentions them, yours and GitHub's, marking the ones you don't "
          "have yet.",
          "'update' installs what's new."},
         {{"check", "up to date?"}, {"check list", "the new versions"}, {"check show 1", "all about the newest"},
          {"check files", "what they change"}, {"check current", "your version"},
          {"check history", "the versions before yours"}, {"check show ~1", "the one before yours"}, {"check find predict", "every version about predict"}},
         {"update"}, -1, PROGRESS},
        {Cmd::Update, {"update", "u"}, "update", "Check GitHub for a new version now, and install it",
         {"When the game is started with 'python play.py', it checks GitHub for a new version when it starts and again "
          "every 5 to 10 minutes, and says so in the log. 'update' checks right away. If there's one, it saves and "
          "quits, and play.py pulls it, builds it and starts the game again (on the same profile picker).",
          "If your copy has commits of its own, it can't just move ahead: pull it yourself. Started any other way (or "
          "with --no-update-check), the game doesn't check at all."},
         {{"update", "check now, and update if there's something new"}, {"u", "the same"}},
         {"quit"}, -1, PROGRESS},
        {Cmd::Quit, {"quit", "q", "exit"}, "quit", "Save and quit (Ctrl+C works too)",
         {"Saves and closes the game. Drones keep earning while it's closed (up to 12 hours); everything else waits."},
         {}, {"logout"}, -1, PROGRESS},
    };
    return table;
}

const CommandInfo* findCommand(Cmd c) {
    for (const CommandInfo& info : commands())
        if (info.cmd == c) return &info;
    return nullptr;
}

// Word-wraps a paragraph to `width` columns, every line starting with `indent`.
std::string wrap(const std::string& text, size_t width, const std::string& indent) {
    std::stringstream words(text);
    std::string word, line, out;
    while (words >> word) {
        if (!line.empty() && indent.size() + line.size() + 1 + word.size() > width) {
            out += indent + line + "\n";
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) out += indent + line + "\n";
    return out;
}

void printPage(const CommandInfo& info, bool open, const GameState& g) {
    constexpr size_t WIDTH = 92;
    out() << paint(Style::Header, "=== " + info.names[0] + " ===") << "  " << info.summary << "\n";
    out() << "  " << paint(Style::Bold, "Usage: ") << info.usage << "\n";
    if (info.names.size() > 1) {
        std::string also;
        for (size_t i = 1; i < info.names.size(); ++i) also += (also.empty() ? "" : ", ") + info.names[i];
        out() << "  " << paint(Style::Bold, "Also:  ") << also << "\n";
    }
    if (!open) out() << paint(Style::Warn, "  Not open yet. " + lockedText(g, (Feature)info.feature)) << "\n";
    for (const char* p : info.details) out() << "\n" << wrap(p, WIDTH, "  ");
    if (!info.examples.empty()) {
        size_t w = 0;
        for (const Example& e : info.examples) w = std::max(w, std::string(e.typed).size());
        out() << "\n" << paint(Style::Bold, "Examples") << "\n";
        for (const Example& e : info.examples)
            out() << "  " << paint(Style::Tokens, padRight(e.typed, w)) << "   " << paint(Style::Muted, e.what) << "\n";
    }
    if (!info.see.empty()) {
        std::string see;
        for (const char* s : info.see) see += (see.empty() ? "" : ", ") + std::string(s);
        out() << "\n" << paint(Style::Muted, "See also: " + see) << "\n";
    }
}

}  // namespace

int commandFeature(Cmd c) {
    const CommandInfo* info = findCommand(c);
    return info ? info->feature : -1;
}

Cmd parseCmd(const std::string& word) {
    static const std::unordered_map<std::string, Cmd> byName = [] {
        std::unordered_map<std::string, Cmd> m;
        for (const CommandInfo& info : commands())
            for (const std::string& n : info.names) m.emplace(n, info.cmd);
        return m;
    }();
    if (word.empty()) return Cmd::Empty;
    auto it = byName.find(word);
    return it == byName.end() ? Cmd::Unknown : it->second;
}

void printHelp(Shared& s, const std::string& topic) {
    GameState g;
    {
        std::lock_guard lk(s.m);
        g = s.g;
    }

    if (!topic.empty()) {
        std::string word = lower(trim(topic));
        word = word.substr(0, word.find(' '));
        if (parseCmd(word) == Cmd::Unknown) {
            if (std::string cmd = aliasHelp(s, word); !cmd.empty()) {   // your alias: what it runs, then that command's page
                out() << paint(Style::Muted, "'" + word + "' is your alias for '" + cmd + "'.") << "\n";
                word = lower(cmd.substr(0, cmd.find(' ')));
            }
        }
        const CommandInfo* info = findCommand(parseCmd(word));
        if (!info) { out() << paint(Style::Warn, "No command called '" + word + "'. Type 'help' for the list.") << "\n"; return; }
        printPage(*info, info->feature < 0 || isUnlocked(g, (Feature)info->feature), g);
        return;
    }

    for (int sec = 0; sec < SECTION_COUNT; ++sec) {
        std::string body;
        for (const CommandInfo& info : commands()) {
            if (info.section != sec) continue;
            if (info.feature >= 0 && !isUnlocked(g, (Feature)info.feature)) continue;
            std::string name = info.usage;   // "status | s", "mine | m"
            if (info.names.size() > 1) name = info.names[0] + " | " + info.names[1] + name.substr(info.names[0].size());
            if (name.size() > 24) body += "  " + name + "\n  " + std::string(25, ' ') + info.summary + "\n";   // too long to share a line
            else body += "  " + padRight(name, 24) + " " + info.summary + "\n";
        }
        if (!body.empty()) out() << paint(Style::Header, SECTION_NAMES[sec]) << "\n" << body;
    }
    if (int next = nextUnlock(g); next >= 0) {
        out() << paint(Style::Muted, std::string("More opens up as you earn titles. Next: ") + FEATURES[next].name +
                                         " at " + TITLES[FEATURES[next].title].name + " (see 'goals').") << "\n";
    }
    if (!g.aliases.empty()) {
        std::string names;
        for (const auto& a : g.aliases) names += (names.empty() ? "" : ", ") + a.first;
        out() << paint(Style::Header, "Your aliases:") << " " << names << paint(Style::Muted, "  ('alias' shows what they run)") << "\n";
    }
    out() << paint(Style::Muted, "Type 'help <command>' for a full page on one (aliases work too: 'help ct').") << "\n";
}

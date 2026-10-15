#pragma once
// The command-line UI. Everything here runs on the main thread, and it is the only code
// that prints or reads input. The game rules live in src/core; these functions lock the
// state, call into the core, and turn what comes back into colored text.
//
//   dispatch.cpp  one typed line -> the right function below (shared by the CLI and the TUI console)
//   help.cpp      the command table: names, aliases, 'help' and 'help <command>'
//   commands.cpp  status, stats, goals, log, mining, offers, auto, wipe
//   spending.cpp  shop, curios, prompt styles, the Monolith
//   economy.cpp   market, contracts, crew

#include <random>
#include <string>

#include "core/state.hpp"

// C++ can't switch on strings, so a typed word is parsed into an enum first.
enum class Cmd {
    Empty, Unknown, Help, Status, Mine, Shop, Auto, Log, Save, Wipe, Quit,
    Goals, Curios, Prompt, Monolith, Build,
    Market, Buy, Sell, Contracts, Accept, Abandon, Crew, Hire, Fire, Train,
    Clear, Logout, Orrery,
    Stats, Promote, Yes, No,
    Theme, Clients, Workshop, Craft, Equip, Unequip, Expedition,
    Locked,   // a command whose feature isn't unlocked yet (see commandAllowed)
};

Cmd parseCmd(const std::string& word);   // expects lowercase (help.cpp, from the command table)
int commandFeature(Cmd c);               // the Feature a command needs, or -1 (help.cpp)

// ----- commands.cpp -----
bool commandAllowed(Shared& s, Cmd c);           // false (after saying why) for a feature that isn't unlocked yet
void printHelp(Shared& s, const std::string& topic = "");   // only lists what's unlocked; a topic explains one command (help.cpp)
void printStatus(Shared& s);
void printPending(Shared& s);                    // background messages, shown before each prompt
std::string promptText(Shared& s);               // "[Title] > "
void doMine(Shared& s, std::mt19937& rng);
void doStats(Shared& s);
void doAnswer(Shared& s, std::mt19937& rng, bool yes);   // the open offer (choice event)
void doAuto(Shared& s, const std::string& arg);
void doGoals(Shared& s);
void doLog(Shared& s, const std::string& args);   // [n] [good|bad|warn|event|info]
void doWipe(Shared& s);

// ----- spending.cpp -----
void doShop(Shared& s, const std::string& args);     // no args: the shop menu; "1 10": buy right away
void doCurios(Shared& s, const std::string& arg);    // no arg: the shelf menu; "3": buy that one
void doPrompt(Shared& s, const std::string& arg);
void doTheme(Shared& s, const std::string& arg);     // no arg: every theme with a preview; "3" or "glacier": buy or switch
void doBuild(Shared& s, const std::string& arg); // empty arg just shows the Monolith
void doOrrery(Shared& s, const std::string& arg); // empty arg just shows the Orrery

// ----- profiles.cpp -----
class Profiles;
std::string pickProfile(Profiles& profiles);   // the chosen profile's name, or "" to quit

// ----- workshop.cpp -----
void doWorkshop(Shared& s);
void doCraft(Shared& s, const std::string& arg);     // a number or a name
void doEquip(Shared& s, const std::string& arg);
void doUnequip(Shared& s, const std::string& arg);

// ----- economy.cpp -----
void doMarket(Shared& s);
void doBuy(Shared& s, const std::string& arg);   // <n|max>
void doSell(Shared& s, const std::string& arg);  // <n|all>
void doContracts(Shared& s);
void doClients(Shared& s);                       // standing with every client
void doAccept(Shared& s, const std::string& arg);
void doAbandon(Shared& s, const std::string& arg);   // empty: the only job
void doCrew(Shared& s);
void doHire(Shared& s, const std::string& arg);
void doFire(Shared& s, const std::string& arg);
void doTrain(Shared& s, const std::string& arg);
void doPromote(Shared& s, const std::string& arg);
void doExpedition(Shared& s, const std::string& args);   // <n> <short|long|deep>; anything less explains

#pragma once
// The typed commands: what the TUI's Console tab runs, on the Console's own thread. They print
// to out() and read from in() (shell/term.hpp), which the Console points at its own buffers.
// The game rules live in src/core; these functions lock the state, call into the core, and turn
// what comes back into colored text.
//
//   dispatch.cpp  one typed line -> the right function below
//   help.cpp      the command table: names, aliases, 'help' and 'help <command>'
//   commands.cpp  status, stats, goals, log, mining, offers, auto, wipe
//   spending.cpp  shop, curios, prompt styles, the Monolith
//   economy.cpp   market, contracts, clients, crew, expeditions
//   workshop.cpp  gadgets
//   aliases.cpp   alias, rmalias, and expanding a typed alias

#include <random>
#include <string>
#include <vector>

#include "core/state.hpp"

// C++ can't switch on strings, so a typed word is parsed into an enum first.
enum class Cmd {
    Empty, Unknown, Help, Status, Mine, Shop, Auto, Log, Save, Wipe, Quit,
    Goals, Curios, Prompt, Monolith, Build,
    Market, Buy, Sell, Contracts, Accept, Abandon, Crew, Hire, Fire, Train,
    Clear, Logout, Orrery,
    Stats, Promote, Yes, No,
    Theme, Clients, Workshop, Craft, Equip, Unequip, Expedition, Respond, Repair, Stars, Task, Pay, Borrow, Predict,
    Update, Alias, RmAlias, Bribe, ClientInfo, Eval, Check, Silence, Unsilence,
    Locked,   // a command whose feature isn't unlocked yet (see commandAllowed)
};

Cmd parseCmd(const std::string& word);   // expects lowercase (help.cpp, from the command table)
int commandFeature(Cmd c);               // the Feature a command needs, or -1 (help.cpp)
std::vector<std::string> commandWords(const GameState& g);   // every unlocked command's names: real names first, then aliases (help.cpp)

// ----- complete.cpp -----
// What the Console would complete `line` to: the rest of it (to show greyed out, and add on Tab),
// or "". From the newest earlier line that starts the same way, or else the word being typed.
std::string completion(const GameState& g, const std::vector<std::string>& history, const std::string& line);

// ----- commands.cpp -----
bool commandAllowed(Shared& s, Cmd c);           // false (after saying why) for a feature that isn't unlocked yet
void printHelp(Shared& s, const std::string& topic = "");   // only lists what's unlocked; a topic explains one command (help.cpp)
void printStatus(Shared& s);
std::string promptText(Shared& s);               // "[Title] > "
void doMine(Shared& s, std::mt19937& rng);
void doStats(Shared& s);
void doAnswer(Shared& s, std::mt19937& rng, bool yes);   // the open offer (choice event)
void doAuto(Shared& s, const std::string& arg);
void doRespond(Shared& s);                       // the disaster that's coming: stop it
void doRepair(Shared& s, const std::string& arg); // no arg: what's broken; "2" or "all": fix it
void doGoals(Shared& s);
void doLog(Shared& s, const std::string& args);   // [n] [good|bad|warn|event|info]
void doSilence(Shared& s, const std::string& args, bool silence);   // kinds of log message kept out of the log pane
void doWipe(Shared& s);
bool doUpdate();
void doCheck(const std::string& args);           // what's new on GitHub, without installing it                                 // checks GitHub now; true = an update is ready: save, quit, play.py installs it

// ----- aliases.cpp -----
void doAlias(Shared& s, const std::string& args);     // nothing: the list; "predict tokens 1h pt": pt runs that
void doRmAlias(Shared& s, const std::string& args);
bool expandAlias(const GameState& g, std::string& line);   // an alias's first word -> what it runs (the rest goes on the end)
std::string aliasHelp(Shared& s, const std::string& word); // what that alias runs, or ""

// ----- spending.cpp -----
void doShop(Shared& s, const std::string& args);     // no args: the shop menu; "1 10": buy right away
void doCurios(Shared& s, const std::string& arg);    // no arg: the shelf menu; "3": buy that one
void doPrompt(Shared& s, const std::string& arg);
void doTheme(Shared& s, const std::string& arg);     // no arg: every theme with a preview; "3" or "glacier": buy or switch
void doStars(Shared& s);                             // the Constellations: the sky map and the feats
void doBuild(Shared& s, const std::string& arg); // empty arg just shows the Monolith
void doOrrery(Shared& s, const std::string& arg); // empty arg just shows the Orrery

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
void doBribe(Shared& s, const std::string& args);   // a Burned client, by number or name
void doClientInfo(Shared& s, const std::string& args);   // one client in full
void doAccept(Shared& s, const std::string& arg);
void doAbandon(Shared& s, const std::string& arg);   // empty: the only job
void doCrew(Shared& s);
void doHire(Shared& s, const std::string& arg);
void doFire(Shared& s, const std::string& arg);
void doTrain(Shared& s, const std::string& arg);
void doPromote(Shared& s, const std::string& arg);
void doPay(Shared& s, const std::string& args);          // <profile> [amount]; nothing: who's running, who owes what
void doBorrow(Shared& s, const std::string& args);       // <profile> <amount>
void doPredict(Shared& s, const std::string& args);
void doEval(Shared& s, const std::string& args);         // an expression in the Tasks language, right now (shell/predict.cpp)      // <what> <time|count>: what's coming, from your rates (shell/predict.cpp)
void doTask(Shared& s, const std::string& args);         // list, add, edit, on/off, license... (shell/tasks.cpp)
void doExpedition(Shared& s, const std::string& args);   // <n> <short|long|deep>; anything less explains

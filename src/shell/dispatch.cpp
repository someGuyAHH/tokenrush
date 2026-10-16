#include "dispatch.hpp"

#include <sstream>

#include "commands.hpp"
#include "core/choices.hpp"
#include "core/contracts.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/save.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

// "Save and quit? (y/n)": only y (or yes) goes ahead.
bool confirmLeave(const std::string& question) {
    out() << paint(Style::Warn, question + " (y/n) ") << std::flush;
    std::string answer;
    if (!std::getline(in(), answer)) return false;
    answer = lower(trim(answer));
    if (answer == "y" || answer == "yes") return true;
    out() << "Staying.\n";
    return false;
}

}  // namespace

Outcome runCommand(Shared& s, std::mt19937& rng, const std::string& typed, bool profiles) {
    std::string line = typed;
    bool aliased;
    {
        std::lock_guard lk(s.m);
        aliased = expandAlias(s.g, line);
    }
    if (aliased) out() << paint(Style::Muted, "> " + line) << "\n";   // what the alias ran
    std::stringstream ss(line);
    std::string cmd, arg;
    ss >> cmd >> arg;
    std::string rest;            // everything after the command, case kept (for 'prompt custom >_<')
    std::getline(ss, rest);
    rest = trim(arg + rest);
    arg = lower(arg);

    Cmd c = parseCmd(lower(cmd));
    if (!commandAllowed(s, c)) c = Cmd::Locked;
    Outcome result = Outcome::Done;
    switch (c) {
        case Cmd::Empty:    return Outcome::Empty;
        case Cmd::Clear:    return Outcome::Clear;
        case Cmd::Unknown:  out() << paint(Style::Warn, "Unknown command. Type 'help'.") << "\n"; break;
        case Cmd::Locked:   break;   // commandAllowed already said why
        case Cmd::Help:     printHelp(s, rest); break;
        case Cmd::Status:   printStatus(s); break;
        case Cmd::Stats:    doStats(s); break;
        case Cmd::Mine:     doMine(s, rng); break;
        case Cmd::Shop:     doShop(s, rest); break;
        case Cmd::Auto:     doAuto(s, arg); break;
        case Cmd::Respond:  doRespond(s); break;
        case Cmd::Repair:   doRepair(s, arg); break;
        case Cmd::Log:      doLog(s, rest); break;
        case Cmd::Save:     out() << (saveNow(s) ? paint(Style::Good, "Saved.") : paint(Style::Bad, "Save failed.")) << "\n"; break;
        case Cmd::Wipe:     doWipe(s); setTheme(0); break;
        case Cmd::Goals:    doGoals(s); break;
        case Cmd::Curios:   doCurios(s, arg); break;
        case Cmd::Prompt:   doPrompt(s, rest); break;
        case Cmd::Theme:    doTheme(s, rest); break;
        case Cmd::Stars:    doStars(s); break;
        case Cmd::Orrery:   doOrrery(s, arg); break;
        case Cmd::Monolith: doBuild(s, ""); break;
        case Cmd::Build:    doBuild(s, arg); break;
        case Cmd::Market:    doMarket(s); break;
        case Cmd::Buy:       doBuy(s, arg); break;
        case Cmd::Sell:      doSell(s, arg); break;
        case Cmd::Contracts: doContracts(s); break;
        case Cmd::Clients:   doClients(s); break;
        case Cmd::Bribe:     doBribe(s, rest); break;
        case Cmd::Workshop:  doWorkshop(s); break;
        case Cmd::Craft:     doCraft(s, rest); break;
        case Cmd::Equip:     doEquip(s, rest); break;
        case Cmd::Unequip:   doUnequip(s, rest); break;
        case Cmd::Accept:    doAccept(s, arg); break;
        case Cmd::Abandon:   doAbandon(s, arg); break;
        case Cmd::Crew:      doCrew(s); break;
        case Cmd::Hire:      doHire(s, arg); break;
        case Cmd::Fire:      doFire(s, arg); break;
        case Cmd::Train:     doTrain(s, arg); break;
        case Cmd::Promote:   doPromote(s, arg); break;
        case Cmd::Expedition: doExpedition(s, rest); break;
        case Cmd::Task:      doTask(s, rest); break;
        case Cmd::Pay:       doPay(s, rest); break;
        case Cmd::Predict:   doPredict(s, rest); break;
        case Cmd::Eval:      doEval(s, rest); break;
        case Cmd::Borrow:    doBorrow(s, rest); break;
        case Cmd::Yes:       doAnswer(s, rng, true); break;
        case Cmd::No:        doAnswer(s, rng, false); break;
        case Cmd::Alias:     doAlias(s, rest); break;
        case Cmd::RmAlias:   doRmAlias(s, rest); break;
        case Cmd::Check:     doCheck(rest); break;
        case Cmd::Update:    if (doUpdate()) result = Outcome::Quit; break;   // main() hands over to play.py after the save
        case Cmd::Quit:      if (confirmLeave("Save and quit?")) result = Outcome::Quit; break;
        case Cmd::Logout:
            if (!profiles) { out() << "No profiles in this session (started with --save-dir).\n"; break; }
            if (confirmLeave("Save and go back to the profiles?")) result = Outcome::Logout;
            break;
    }

    { // instant feedback instead of waiting for the next world tick
        std::lock_guard lk(s.m);
        checkContract(s.g, s.log);
        checkRace(s.g, s.log);
        checkDisaster(s.g, s.log);
        checkGoals(s.g, s.log);
    }
    return result;
}

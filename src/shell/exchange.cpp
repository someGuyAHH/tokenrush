// pay and borrow: tokens between profiles that are running at the same time.
// Same rule as every command: lock, call the core, unlock, then print.

#include <iostream>
#include <sstream>
#include <vector>

#include "commands.hpp"
#include "core/exchange.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

// "Big Bob 5k" -> "Big Bob" and 5000 (names can have spaces; the amount, if any, is last).
void splitWhoAmount(const std::string& args, std::string& who, long long& amount) {
    std::vector<std::string> words;
    std::stringstream ss(args);
    for (std::string w; ss >> w;) words.push_back(w);
    amount = 0;
    if (words.size() >= 2 && parseAmount(words.back(), amount)) words.pop_back();
    else amount = 0;
    who.clear();
    for (const std::string& w : words) who += (who.empty() ? "" : " ") + w;
}

void printExchange(const GameState& g) {
    out() << paint(Style::Header, "=== BETWEEN PROFILES ===") << paint(Style::Muted, "   tokens only; both games have to be running") << "\n";
    if (!exchangeOpen()) {
        out() << "  Not with --save-dir: start the game with a profile to pay or borrow.\n";
        return;
    }
    std::vector<std::string> online = onlineProfiles();
    out() << "  Running now: " << (online.empty() ? paint(Style::Muted, "nobody else (open another profile in another window)") : "");
    for (size_t i = 0; i < online.size(); ++i) out() << (i ? ", " : "") << paint(Style::Bold, online[i]);
    out() << "\n";
    for (const auto& [name, b] : g.balances)
        out() << "  " << padRight(name, 16) << (b > 0 ? paint(Style::Good, "owes you " + num(b)) : paint(Style::Warn, "you owe " + num(-b))) << "\n";
    for (const LoanAsk& a : g.asksIn)
        out() << "  " << paint(Style::Event, a.who + " asks to borrow " + num(a.amount)) << paint(Style::Muted, "  ('pay " + a.who + "' lends it)") << "\n";
    for (const LoanAsk& a : g.asksOut) out() << "  " << paint(Style::Muted, "You asked " + a.who + " for " + num(a.amount) + ".") << "\n";
    for (const Transfer& t : g.outgoing) out() << "  " << paint(Style::Muted, num(t.amount) + " on its way to " + t.to + "...") << "\n";
    out() << "Use 'pay <profile> <amount>' or 'borrow <profile> <amount>' (amounts like 5B work).\n";
}

}  // namespace

void doPay(Shared& s, const std::string& args) {
    std::string who;
    long long amount = 0;
    splitWhoAmount(args, who, amount);
    Notes notes;
    {
        std::lock_guard lk(s.m);
        if (who.empty()) { printExchange(s.g); return; }
        notes = payProfile(s.g, who, amount);
    }
    printNotes(notes);
}

void doBorrow(Shared& s, const std::string& args) {
    std::string who;
    long long amount = 0;
    splitWhoAmount(args, who, amount);
    Notes notes;
    {
        std::lock_guard lk(s.m);
        if (who.empty()) { printExchange(s.g); return; }
        if (amount <= 0) { out() << "How much? 'borrow " << who << " <amount>', like 5B.\n"; return; }
        notes = askToBorrow(s.g, who, amount);
    }
    printNotes(notes);
}

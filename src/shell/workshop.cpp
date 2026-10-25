// The Workshop: gadgets built from curios, three slots to equip them in.
// Same rule as every command: lock, call the core, unlock, then print.

#include <iostream>

#include "commands.hpp"
#include "core/actions.hpp"
#include "core/content.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/util.hpp"
#include "term.hpp"

namespace {

// "3", "tuning fork", "tuningfork" or the start of a name -> an index into GADGETS, or -1.
int parseGadget(const std::string& arg) {
    long long n;
    if (parseLongLong(arg, n)) return n >= 1 && n <= GADGET_COUNT ? (int)n - 1 : -1;
    std::string want = lower(trim(arg));
    std::erase(want, ' ');
    if (want.empty()) return -1;
    for (int i = 0; i < GADGET_COUNT; ++i) {
        std::string name = lower(GADGETS[i].name);
        std::erase(name, ' ');
        if (want == GADGETS[i].id || name.rfind(want, 0) == 0) return i;
    }
    return -1;
}

void act(Shared& s, const std::string& arg, const char* usage, Notes (*fn)(GameState&, int)) {
    int idx = parseGadget(arg);
    if (idx < 0) { out() << paint(Style::Warn, std::string("Usage: ") + usage + "  (see 'workshop')") << "\n"; return; }
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = fn(s.g, idx);
    }
    printNotes(notes);
}

}  // namespace

void doWorkshop(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;
    out() << paint(Style::Header, "=== WORKSHOP ===") << "  Shards: " << paint(Style::Shards, num(g.shards)) << "\n";
    out() << "Slots: ";
    for (int i = 0; i < GADGET_SLOTS; ++i) {
        int x = g.gadgetSlots[(size_t)i];
        std::string name = x < 0 ? paint(Style::Muted, "empty")
                         : hasBit(g.gadgetsBroken, x) ? paint(Style::Bad, std::string(GADGETS[x].name) + " (broken)")
                         : g.gadgetsUnplugged > 0 ? paint(Style::Warn, std::string(GADGETS[x].name) + " (off)")
                         : g.gadgetWarmup[(size_t)i] > 0 ? paint(Style::Warn, std::string(GADGETS[x].name) + " (warming up)") : paint(Style::Good, GADGETS[x].name);
        out() << (i ? "  " : "") << "[" << name << "]";
    }
    out() << "\n";
    for (const Note& n : gadgetTrouble(g)) out() << paint(styleFor(n.sev), n.text) << "\n";
    out() << "\n";
    for (int i = 0; i < GADGET_COUNT; ++i) {
        const GadgetInfo& x = GADGETS[i];
        bool built = hasBit(g.gadgetsOwned, i), on = gadgetEquipped(g, (Gadget)i), curio = hasBit(g.curiosOwned, x.curio);
        std::string state = hasBit(g.gadgetsBroken, i) ? paint(Style::Bad, "BROKEN") : on && g.gadgetsUnplugged > 0 ? paint(Style::Warn, "unplugged")
                          : on && gadgetWarmupLeft(g, (Gadget)i) > 0 ? paint(Style::Warn, "warming up")
                          : on ? paint(Style::Good, "equipped") : built ? std::string("built")
                          : curio ? paint(Style::Shards, count(x.cost, "shard")) : paint(Style::Muted, "needs curio");
        out() << "  " << padLeft(std::to_string(i + 1), 2) << ") " << paint(Style::Bold, padRight(x.name, 15)) << padVisible(state, 17)
              << paint(Style::Good, "+ ") << x.plus << "\n"
              << "      " << paint(Style::Muted, padRight("from " + theCurio(x.curio), 34)) << paint(Style::Bad, "- ") << x.minus << "\n";
    }
    out() << paint(Style::Muted, "Building a gadget keeps its curio on the shelf. Swapping is free, but a gadget you put in works after " + duration(GADGET_WARMUP_SECONDS) + " of warming up.") << "\n";
    out() << "Use 'craft <n>', 'equip <n>' or 'unequip <n>'.\n";
}

void doCraft(Shared& s, const std::string& arg)   { act(s, arg, "craft <n>", craftGadget); }
void doEquip(Shared& s, const std::string& arg)   { act(s, arg, "equip <n>", equipGadget); }
void doUnequip(Shared& s, const std::string& arg) { act(s, arg, "unequip <n>", unequipGadget); }

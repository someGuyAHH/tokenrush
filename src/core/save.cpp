// Save file format (INI):
//
//   ; comment
//   [meta]
//   version = 4
//
//   [player]
//   tokens = 1610
//   ...
//   [progress]
//   goalsDone = First Steps, Pocket Change
//
// Everything about a field (its section, key, how to print it, how to parse and range-check it)
// is declared once, in saveFields(). Writing, reading and range validation all walk that table,
// so adding a field to the save is one new line there.
//
// Reading is forgiving on purpose: unknown keys are ignored, missing keys keep their defaults,
// and keys are case-insensitive. A value that is present but invalid fails the whole load, and
// main() then moves the file to tokenrush.save.bad instead of overwriting it.
//
// Lists are numbered sections: each crew member gets [crew.1], [crew.2], ... A field can say
// it is only "present" sometimes (a crew slot nobody fills, a contract you don't have), and
// then it simply isn't written.
//
// Older formats still load: v1/v2 (positional numbers after a "TOKENRUSH n" header), v3
// (flat key=value lines, bitmasks stored as numbers) and v4 (INI without the market, contracts
// and crew, which then start empty).

#include "save.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <system_error>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "actions.hpp"
#include "content.hpp"
#include "contracts.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "stars.hpp"
#include "tasks.hpp"
#include "util.hpp"

namespace {

std::filesystem::path g_saveDir = ".save";   // set once in main() before any thread starts
std::filesystem::path tmpSavePath() { return g_saveDir / "tokenrush.save.tmp"; }

// ----- The field table -----
struct Field {
    std::string section;
    std::string key;
    std::function<std::string(const GameState&)> get;
    std::function<bool(GameState&, const std::string&)> set;   // false = value rejected
    std::function<bool(const GameState&)> present = nullptr;   // null = always written
};

using Present = std::function<bool(const GameState&)>;

template <class T>
bool parseNumber(const std::string& text, T& out) {
    std::istringstream is(text);
    T v{};
    if (!(is >> v)) return false;
    is >> std::ws;
    if (!is.eof()) return false;   // reject trailing junk like "12abc"
    out = v;
    return true;
}

template <class T>
std::string formatNumber(T v) {
    std::ostringstream os;
    if constexpr (std::is_floating_point_v<T>) {
        // Four decimals is plenty for a price or a fraction of a token. Floor rather than
        // round, so a value that must stay below 1 (passiveCarry) can't round up to 1.
        os << std::fixed << std::setprecision(4) << std::floor(v * 10'000.0) / 10'000.0;
        std::string s = os.str();
        while (s.back() == '0') s.pop_back();   // "60.1993", "0.5", "150"
        if (s.back() == '.') s.pop_back();
        return s;
    }
    os << v;
    return os.str();
}

// A number with an allowed range. `at` returns a reference to the value inside a GameState,
// which lets a field point into a nested struct or a list slot, not just a plain member.
template <class At>
using ValueOf = std::remove_reference_t<std::invoke_result_t<At, GameState&>>;   // what `at` points to

template <class At>
Field numAt(std::string section, std::string key, At at,
            ValueOf<At> lo = 0, ValueOf<At> hi = std::numeric_limits<ValueOf<At>>::max(),
            Present present = nullptr) {
    using T = ValueOf<At>;
    return Field{
        std::move(section), std::move(key),
        [at](const GameState& g) { return formatNumber(at(const_cast<GameState&>(g))); },   // read-only use
        [at, lo, hi](GameState& g, const std::string& text) {
            T v{};
            if (!parseNumber(text, v) || v < lo || v > hi) return false;
            at(g) = v;
            return true;
        },
        std::move(present)};
}

// The common case: a plain GameState member.
template <class T>
Field numField(const char* section, const char* key, T GameState::* member,
               T lo = 0, T hi = std::numeric_limits<T>::max()) {
    return numAt(section, key, [member](GameState& g) -> T& { return g.*member; }, lo, hi);
}

bool parseBool(const std::string& text, bool& out) {
    std::string t = lower(trim(text));
    if (t == "true"  || t == "1" || t == "yes" || t == "on")  { out = true;  return true; }
    if (t == "false" || t == "0" || t == "no"  || t == "off") { out = false; return true; }
    return false;
}

template <class At>
Field boolAt(std::string section, std::string key, At at, Present present = nullptr) {
    return Field{
        std::move(section), std::move(key),
        [at](const GameState& g) -> std::string { return at(const_cast<GameState&>(g)) ? "true" : "false"; },
        [at](GameState& g, const std::string& text) { return parseBool(text, at(g)); },
        std::move(present)};
}

Field boolField(const char* section, const char* key, bool GameState::* member) {
    return boolAt(section, key, [member](GameState& g) -> bool& { return g.*member; });
}

// An enum stored by its id ("miner", "streak"). E is an enum class with values 0..count-1.
template <class At>
Field enumAt(std::string section, std::string key, At at, int count, std::function<const char*(int)> idOf,
             Present present = nullptr) {
    using E = ValueOf<At>;
    return Field{
        std::move(section), std::move(key),
        [at, idOf](const GameState& g) -> std::string { return idOf((int)at(const_cast<GameState&>(g))); },
        [at, idOf, count](GameState& g, const std::string& text) {
            std::string t = lower(trim(text));
            for (int i = 0; i < count; ++i) {
                if (t == idOf(i)) { at(g) = (E)i; return true; }
            }
            return false;
        },
        std::move(present)};
}

// Free text such as a crew member's name. One line, 1-24 characters.
template <class At>
Field textAt(std::string section, std::string key, At at, Present present = nullptr) {
    return Field{
        std::move(section), std::move(key),
        [at](const GameState& g) { return at(const_cast<GameState&>(g)); },
        [at](GameState& g, const std::string& text) {
            std::string t = trim(text);
            if (t.empty() || t.size() > 24) return false;
            at(g) = t;
            return true;
        },
        std::move(present)};
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// A set of owned things, stored as a readable list of names ("Rubber Duck, Lava Lamp").
// A bare number is still accepted as a raw bitmask, which is what v3 saves contain.
Field maskField(const char* section, const char* key, std::uint32_t GameState::* member, int count,
                std::function<const char*(int)> nameOf) {
    return Field{
        section, key,
        [=](const GameState& g) {
            std::string out;
            for (int i = 0; i < count; ++i) {
                if (!hasBit(g.*member, i)) continue;
                if (!out.empty()) out += ", ";
                out += nameOf(i);
            }
            return out;
        },
        [=](GameState& g, const std::string& text) {
            std::string t = trim(text);
            std::uint32_t mask = 0;
            if (allDigits(t)) {
                unsigned long long raw = 0;
                if (!parseNumber(t, raw) || (raw >> count) != 0) return false;
                mask = (std::uint32_t)raw;
            } else {
                std::stringstream ss(t);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string want = lower(trim(item));
                    if (want.empty()) continue;
                    // Unknown names are skipped on purpose: a renamed or removed entry
                    // shouldn't make the whole save unreadable.
                    for (int i = 0; i < count; ++i)
                        if (lower(nameOf(i)) == want) mask |= (1u << i);
                }
            }
            g.*member = mask;
            return true;
        }};
}

// The selected prompt style, stored by id ("dollar"). A bare number is a v3 index.
Field promptStyleField() {
    return Field{
        "cosmetics", "promptSel",
        [](const GameState& g) -> std::string {
            if (g.promptSel == PROMPT_COUNT) return "custom";
            int i = std::clamp(g.promptSel, 0, PROMPT_COUNT - 1);
            return PROMPTS[i].id;
        },
        [](GameState& g, const std::string& text) {
            std::string t = lower(trim(text));
            if (t == "custom") { g.promptSel = PROMPT_COUNT; return true; }
            if (allDigits(t)) {
                int idx = 0;
                if (!parseNumber(t, idx) || idx >= PROMPT_COUNT) return false;
                g.promptSel = idx;
                return true;
            }
            for (int i = 0; i < PROMPT_COUNT; ++i) {
                if (t == PROMPTS[i].id) { g.promptSel = i; return true; }
            }
            return false;
        }};
}

// One crew slot. Reading any of its keys creates the slot; empty slots are cleaned up afterwards.
CrewMember& crewSlot(GameState& g, size_t i) {
    if (g.crew.size() <= i) g.crew.resize(i + 1);
    return g.crew[i];
}

// Tasks: [task.1] text = ..., on = 1, ... The text is kept as written; it's compiled after loading.
Task& taskSlot(GameState& g, size_t i) {
    if (g.tasks.size() <= i) g.tasks.resize(i + 1);
    return g.tasks[i];
}

void addTaskFields(std::vector<Field>& fields) {
    for (size_t i = 0; i < (size_t)TASK_SLOTS_MAX; ++i) {
        std::string sec = "task." + std::to_string(i + 1);
        Present present = [i](const GameState& g) { return i < g.tasks.size(); };
        fields.push_back(Field{
            sec, "text",
            [i](const GameState& g) { return g.tasks[i].text; },
            [i](GameState& g, const std::string& text) {
                std::string t = trim(text);
                if (t.empty() || t.size() > TASK_TEXT_MAX) return false;
                taskSlot(g, i).text = t;
                return true;
            },
            present});
        fields.push_back(boolAt(sec, "on", [i](GameState& g) -> bool& { return taskSlot(g, i).on; }, present));
        fields.push_back(boolAt(sec, "quiet", [i](GameState& g) -> bool& { return taskSlot(g, i).quiet; }, present));
        using LL = long long;
        fields.push_back(numAt(sec, "runs", [i](GameState& g) -> LL& { return taskSlot(g, i).runs; }, 0LL, std::numeric_limits<LL>::max(), present));
        fields.push_back(numAt(sec, "fees", [i](GameState& g) -> LL& { return taskSlot(g, i).fees; }, 0LL, std::numeric_limits<LL>::max(), present));
        fields.push_back(numAt(sec, "lastRun", [i](GameState& g) -> LL& { return taskSlot(g, i).lastRun; }, -1LL, std::numeric_limits<LL>::max(), present));
        fields.push_back(boolAt(sec, "wasTrue", [i](GameState& g) -> bool& { return taskSlot(g, i).wasTrue; }, present));
        fields.push_back(boolAt(sec, "pending", [i](GameState& g) -> bool& { return taskSlot(g, i).pending; }, present));
        fields.push_back(numAt(sec, "nextAt", [i](GameState& g) -> LL& { return taskSlot(g, i).nextAt; }, 0LL, std::numeric_limits<LL>::max(), present));
    }
}

void addCrewFields(std::vector<Field>& fields) {
    for (size_t i = 0; i < (size_t)MAX_CREW_SLOTS; ++i) {
        std::string sec = "crew." + std::to_string(i + 1);
        Present present = [i](const GameState& g) { return i < g.crew.size(); };
        fields.push_back(textAt(sec, "name", [i](GameState& g) -> std::string& { return crewSlot(g, i).name; }, present));
        fields.push_back(enumAt(sec, "role", [i](GameState& g) -> Role& { return crewSlot(g, i).role; },
                                ROLE_COUNT, [](int r) { return ROLES[r].id; }, present));
        fields.push_back(numAt(sec, "level", [i](GameState& g) -> int& { return crewSlot(g, i).level; },
                               1, 10'000, present));   // promotions lift the cap well past CREW_LEVEL_LIMIT
        fields.push_back(enumAt(sec, "trait", [i](GameState& g) -> Trait& { return crewSlot(g, i).trait; },
                                TRAIT_COUNT, [](int t) { return TRAITS[t].id; }, present));
        fields.push_back(numAt(sec, "stars", [i](GameState& g) -> int& { return crewSlot(g, i).stars; }, 0, 1000, present));
        fields.push_back(numAt(sec, "raises", [i](GameState& g) -> int& { return crewSlot(g, i).raises; }, 0, MAX_RAISES, present));
        fields.push_back(numAt(sec, "sulk", [i](GameState& g) -> int& { return crewSlot(g, i).sulk; }, 0, 24 * 3600, present));
        fields.push_back(boolAt(sec, "missedPay", [i](GameState& g) -> bool& { return crewSlot(g, i).missedPay; }, present));
        fields.push_back(numAt(sec, "away", [i](GameState& g) -> int& { return crewSlot(g, i).away; }, 0, 24 * 3600, present));
        fields.push_back(numAt(sec, "trip", [i](GameState& g) -> int& { return crewSlot(g, i).trip; }, 0, EXPEDITION_COUNT - 1, present));
        fields.push_back(numAt(sec, "hurt", [i](GameState& g) -> int& { return crewSlot(g, i).hurt; }, 0, 24 * 3600, present));
    }
}

// "a ; b ; c" -> {"a", "b", "c"}
std::vector<std::string> splitFields(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';')) out.push_back(trim(item));
    return out;
}

int roleOf(const std::string& id) {
    for (int r = 0; r < ROLE_COUNT; ++r) if (lower(id) == ROLES[r].id) return r;
    return -1;
}

// The shelf's keepsakes and the Stats tab's former crew: one line each, "[keepsakes] 1 = ...".
void addPeopleFields(std::vector<Field>& fields) {
    for (size_t i = 0; i < KEEPSAKE_MAX; ++i) {
        fields.push_back(Field{
            "keepsakes", std::to_string(i + 1),
            [i](const GameState& g) {
                const Keepsake& k = g.keepsakes[i];
                return k.item + " ; " + k.from + " ; " + ROLES[(int)k.role].id + " ; " + k.why + " ; " + std::to_string(k.at);
            },
            [i](GameState& g, const std::string& text) {
                std::vector<std::string> p = splitFields(text);
                Keepsake k;
                int role = p.size() == 5 ? roleOf(p[2]) : -1;
                if (role < 0 || p[0].empty() || p[0].size() > 80 || !parseNumber(p[4], k.at)) return false;
                k.item = p[0]; k.from = p[1]; k.role = (Role)role; k.why = p[3];
                if (g.keepsakes.size() <= i) g.keepsakes.resize(i + 1);
                g.keepsakes[i] = k;
                return true;
            },
            [i](const GameState& g) { return i < g.keepsakes.size(); }});
    }
    for (size_t i = 0; i < FORMER_CREW_MAX; ++i) {
        fields.push_back(Field{
            "formercrew", std::to_string(i + 1),
            [i](const GameState& g) {
                const FormerCrew& c = g.formerCrew[i];
                return c.name + " ; " + ROLES[(int)c.role].id + " ; " + std::to_string(c.level) + " ; " + std::to_string(c.stars) + " ; " +
                       TRAITS[(int)c.trait].id + " ; " + c.how + " ; " + std::to_string(c.at);
            },
            [i](GameState& g, const std::string& text) {
                std::vector<std::string> p = splitFields(text);
                FormerCrew c;
                int role = p.size() == 7 ? roleOf(p[1]) : -1;
                int trait = -1;
                for (int t = 0; role >= 0 && t < TRAIT_COUNT; ++t) if (lower(p[4]) == TRAITS[t].id) trait = t;
                if (role < 0 || trait < 0 || p[0].empty() || !parseNumber(p[2], c.level) || !parseNumber(p[3], c.stars) ||
                    !parseNumber(p[6], c.at))
                    return false;
                c.name = p[0]; c.role = (Role)role; c.trait = (Trait)trait; c.how = p[5];
                if (g.formerCrew.size() <= i) g.formerCrew.resize(i + 1);
                g.formerCrew[i] = c;
                return true;
            },
            [i](const GameState& g) { return i < g.formerCrew.size(); }});
    }
}

// Seconds of play when each title was reached: "0, 312, 1800, -1, 0..." (-1 = reached before
// this was recorded, 0 = not yet, except for the first title).
Field titleTimesField() {
    return Field{
        "stats", "titleTimes",
        [](const GameState& g) {
            std::string out;
            for (int i = 0; i < TITLE_COUNT; ++i) out += (i ? ", " : "") + std::to_string(g.titleReachedAt[i]);
            return out;
        },
        [](GameState& g, const std::string& text) {
            std::stringstream ss(text);
            std::string item;
            std::vector<long long> times;
            while (std::getline(ss, item, ',')) {
                long long v = 0;
                if (!parseNumber(trim(item), v) || v < -1) return false;
                times.push_back(v);
            }
            // v9 and earlier had no Overseer (the title before Celestial): its slot starts at "not yet".
            if ((int)times.size() == TITLE_COUNT - 1) times.insert(times.end() - 1, 0);
            if ((int)times.size() != TITLE_COUNT) return false;
            for (int i = 0; i < TITLE_COUNT; ++i) g.titleReachedAt[i] = times[(size_t)i];
            return true;
        }};
}

// The Stats chart: "seconds:lifetime:tokens:shards, ..." oldest first.
Field historyField() {
    return Field{
        "history", "samples",
        [](const GameState& g) {
            std::string out;
            for (const HistorySample& h : g.history) {
                if (!out.empty()) out += ", ";
                out += std::to_string(h.playSeconds) + ":" + std::to_string(h.lifetime) + ":" +
                       std::to_string(h.tokens) + ":" + std::to_string(h.shards);
            }
            return out;
        },
        [](GameState& g, const std::string& text) {
            std::vector<HistorySample> out;
            std::stringstream ss(text);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::stringstream parts(trim(item));
                std::string a, b, c, d;
                HistorySample h;
                if (!std::getline(parts, a, ':') || !std::getline(parts, b, ':') || !std::getline(parts, c, ':') ||
                    !std::getline(parts, d) || !parseNumber(a, h.playSeconds) || !parseNumber(b, h.lifetime) ||
                    !parseNumber(c, h.tokens) || !parseNumber(d, h.shards))
                    return false;
                if (!out.empty() && h.playSeconds < out.back().playSeconds) return false;   // must be in order
                out.push_back(h);
            }
            if (out.size() > STATS_HISTORY_MAX) return false;
            g.history = std::move(out);
            return true;
        },
        [](const GameState& g) { return !g.history.empty(); }};
}

Contract& contractSlot(GameState& g, size_t i) {
    if (g.contracts.size() <= i) g.contracts.resize(i + 1);
    return g.contracts[i];
}

// The fields of one contract slot. `section` is "contract.1", "contract.2", or the plain
// "contract" that saves from before the second slot used (read into slot 1, never written).
void addContractSlot(std::vector<Field>& fields, const std::string& sec, size_t i, Present present) {
    auto c = [i](GameState& g) -> Contract& { return contractSlot(g, i); };
    using LL = long long;
    const LL big = std::numeric_limits<LL>::max();

    // A kind this build doesn't know (heist contracts from before heists were removed) isn't
    // corruption: it loads as ContractKind::Count and normalizeAndValidate() cancels it.
    fields.push_back(Field{
        sec, "kind",
        [i](const GameState& g) -> std::string { return CONTRACT_KINDS[(int)g.contracts[i].kind].id; },
        [c](GameState& g, const std::string& text) {
            std::string t = lower(trim(text));
            Contract& k = c(g);
            k.kind = ContractKind::Count;
            for (int n = 0; n < CONTRACT_KIND_COUNT; ++n)
                if (t == CONTRACT_KINDS[n].id) k.kind = (ContractKind)n;
            return !t.empty();
        },
        present});
    fields.push_back(numAt(sec, "client", [c](GameState& g) -> int& { return c(g).client; }, 0, CLIENT_COUNT - 1, present));
    fields.push_back(boolAt(sec, "big", [c](GameState& g) -> bool& { return c(g).big; }, present));
    fields.push_back(boolAt(sec, "signature", [c](GameState& g) -> bool& { return c(g).signature; }, present));
    fields.push_back(boolAt(sec, "amends", [c](GameState& g) -> bool& { return c(g).amends; }, present));
    fields.push_back(numAt(sec, "target", [c](GameState& g) -> LL& { return c(g).target; }, 1LL, big, present));
    fields.push_back(numAt(sec, "start", [c](GameState& g) -> LL& { return c(g).start; }, 0LL, big, present));
    fields.push_back(numAt(sec, "duration", [c](GameState& g) -> int& { return c(g).duration; }, 1, 24 * 3600, present));
    fields.push_back(numAt(sec, "secondsLeft", [c](GameState& g) -> int& { return c(g).secondsLeft; }, 0, 24 * 3600, present));
    fields.push_back(numAt(sec, "stake", [c](GameState& g) -> LL& { return c(g).stake; }, 0LL, big, present));
    fields.push_back(numAt(sec, "rewardTokens", [c](GameState& g) -> LL& { return c(g).rewardTokens; }, 0LL, big, present));
    fields.push_back(numAt(sec, "rewardShards", [c](GameState& g) -> LL& { return c(g).rewardShards; }, 0LL, big, present));
}

// One number per client, in CLIENTS order: "0, 3, -2, ...". Missing ones stay 0; written only if any isn't.
template <typename T>
Field perClientField(const std::string& key, std::array<T, CLIENT_COUNT> GameState::*member, T lo, T hi) {
    return Field{
        "jobs", key,
        [member](const GameState& g) {
            std::string out;
            for (int i = 0; i < CLIENT_COUNT; ++i) out += (i ? ", " : "") + std::to_string((g.*member)[(size_t)i]);
            return out;
        },
        [member, lo, hi](GameState& g, const std::string& text) {
            std::stringstream ss(text);
            std::string item;
            for (int i = 0; i < CLIENT_COUNT && std::getline(ss, item, ','); ++i) {
                T v = 0;
                if (!parseNumber(trim(item), v) || v < lo || v > hi) return false;
                (g.*member)[(size_t)i] = v;
            }
            return true;
        },
        [member](const GameState& g) { return std::any_of((g.*member).begin(), (g.*member).end(), [](T v) { return v != 0; }); }};
}

void addContractFields(std::vector<Field>& fields) {
    using LL = long long;
    const LL big = std::numeric_limits<LL>::max();
    Present never = [](const GameState&) { return false; };
    Present any = [](const GameState& g) { return !g.contracts.empty(); };

    // Old saves: a single [contract] section with the streak counters inside it.
    addContractSlot(fields, "contract", 0, never);
    fields.push_back(numAt("contract", "streak", [](GameState& g) -> LL& { return g.contractStreak; }, 0LL, big, never));
    fields.push_back(numAt("contract", "peakStreak", [](GameState& g) -> LL& { return g.contractPeakStreak; }, 0LL, big, never));

    // Standing with every client, in CLIENTS order: "0, 3, -2, ...". Missing ones stay 0.
    fields.push_back(Field{
        "jobs", "standing",
        [](const GameState& g) {
            std::string out;
            for (int i = 0; i < CLIENT_COUNT; ++i) out += (i ? ", " : "") + std::to_string(g.standing[(size_t)i]);
            return out;
        },
        [](GameState& g, const std::string& text) {
            std::stringstream ss(text);
            std::string item;
            for (int i = 0; i < CLIENT_COUNT && std::getline(ss, item, ','); ++i) {
                int v = 0;
                if (!parseNumber(trim(item), v) || v < STANDING_MIN || v > STANDING_MAX) return false;
                g.standing[(size_t)i] = v;
            }
            return true;
        },
        [](const GameState& g) { return std::any_of(g.standing.begin(), g.standing.end(), [](int v) { return v != 0; }); }});
    fields.push_back(perClientField("bribes", &GameState::bribes, 0, 1'000'000));
    fields.push_back(perClientField("amendsAfter", &GameState::amendsAfter, std::numeric_limits<LL>::min(), big));
    fields.push_back(numAt("jobs", "streak", [](GameState& g) -> LL& { return g.contractStreak; }, 0LL, big, any));
    fields.push_back(numAt("jobs", "peakStreak", [](GameState& g) -> LL& { return g.contractPeakStreak; }, 0LL, big, any));
    for (size_t i = 0; i < 2; ++i) {
        addContractSlot(fields, "contract." + std::to_string(i + 1), i,
                        [i](const GameState& g) { return i < g.contracts.size(); });
    }
}

const std::vector<Field>& saveFields() {
    static const std::vector<Field> fields = [] {
        std::vector<Field> f = {
        numField("player", "tokens",       &GameState::tokens),
        numField("player", "shards",       &GameState::shards),
        numField("player", "streak",       &GameState::streak),
        numField("player", "passiveCarry", &GameState::passiveCarry),
        numField("player", "playSeconds",  &GameState::playSeconds),
        numField("player", "lastUpdate",   &GameState::lastUpdate),
        numField("player", "lastMine",     &GameState::lastMine),

        numField("upgrades", "pickaxe", &GameState::pickaxe, 0, MAX_LEVEL),
        numField("upgrades", "magnet",  &GameState::magnet,  0, MAX_LEVEL),
        numField("upgrades", "drone",   &GameState::drone,   0, MAX_LEVEL),
        numField("upgrades", "bot",     &GameState::bot,     0, MAX_LEVEL),
        boolField("upgrades", "autoUnlocked", &GameState::autoUnlocked),
        boolField("upgrades", "autoOn",       &GameState::autoOn),

        numField("event", "event",     &GameState::event, 0, (int)EV_COUNT - 1),
        numField("event", "eventEnds", &GameState::eventEnds),

        numField("stats", "lifetimeEarned", &GameState::lifetimeEarned),
        numField("stats", "botTotal",       &GameState::botTotal),
        numField("stats", "manualMines",    &GameState::manualMines),
        numField("stats", "jackpots",       &GameState::jackpots),
        numField("stats", "bestStreak",     &GameState::bestStreak),
        numField("stats", "eventsSeen",     &GameState::eventsSeen),
        numField("stats", "shardsTraded",   &GameState::shardsTraded),
        numField("stats", "contractsDone",  &GameState::contractsDone),
        numField("stats", "contractsFailed",&GameState::contractsFailed),
        numField("stats", "crewHired",      &GameState::crewHired),
        numField("stats", "crewMined",      &GameState::crewMined),
        numField("stats", "expeditionsDone", &GameState::expeditionsDone),
        numField("stats", "audits",         &GameState::audits),
        numField("stats", "biggestJackpot", &GameState::biggestJackpot),
        numField("stats", "bestEarnRate",   &GameState::bestEarnRate),
        titleTimesField(),

        maskField("progress", "goalsDone",   &GameState::goalsDone,   GOAL_COUNT,
                  [](int i) { return GOALS[i].name; }),
        maskField("progress", "secretsDone", &GameState::secretsDone, SECRET_COUNT,
                  [](int i) { return SECRETS[i].id; }),
        numField("progress", "refusedInRow", &GameState::refusedInRow, 0, 1'000'000),
        maskField("progress", "unlocked",    &GameState::unlocked,    FEATURE_COUNT,
                  [](int i) { return FEATURES[i].id; }),
        maskField("progress", "curiosOwned", &GameState::curiosOwned, CURIO_COUNT,
                  [](int i) { return CURIOS[i].name; }),
        numField("progress", "monolithStage",    &GameState::monolithStage, 0, MONO_STAGE_COUNT),
        numField("progress", "monolithProgress", &GameState::monolithProgress),

        maskField("cosmetics", "promptsOwned", &GameState::promptsOwned, PROMPT_COUNT,
                  [](int i) { return PROMPTS[i].id; }),
        promptStyleField(),
        boolField("cosmetics", "customPromptOwned", &GameState::customPromptOwned),
        Field{"cosmetics", "customPrompt",
              [](const GameState& g) { return g.customPrompt; },
              [](GameState& g, const std::string& text) {
                  std::string why;
                  if (!validCustomPrompt(text, why)) return false;
                  g.customPrompt = text;
                  return true;
              },
              [](const GameState& g) { return g.customPromptOwned && !g.customPrompt.empty(); }},

        boolField("cosmetics", "promptColorOwned", &GameState::promptColorOwned),
        Field{"cosmetics", "promptColor",   // "#ff8800", or "off #ff8800" when it's turned off
              [](const GameState& g) { return (g.promptColorOn ? "" : "off ") + hexColor(g.promptColor); },
              [](GameState& g, const std::string& text) {
                  std::string t = lower(trim(text));
                  g.promptColorOn = t.rfind("off", 0) != 0;
                  if (!g.promptColorOn) t = trim(t.substr(3));
                  return parseHexColor(t, g.promptColor);
              },
              [](const GameState& g) { return g.promptColorOwned; }},
        maskField("cosmetics", "themesOwned", &GameState::themesOwned, THEME_COUNT,
                  [](int i) { return THEMES[i].id; }),
        // An unknown theme (hand-edited, or from a newer build) falls back to Classic: it's
        // only a look, not worth rejecting the whole save over.
        Field{"cosmetics", "theme",
              [](const GameState& g) -> std::string { return themeId(std::clamp(g.themeSel, 0, CUSTOM_THEME)); },
              [](GameState& g, const std::string& text) {
                  std::string t = lower(trim(text));
                  g.themeSel = t == "custom" ? CUSTOM_THEME : 0;   // checked against customThemeOwned after loading
                  for (int i = 0; i < THEME_COUNT; ++i)
                      if (t == THEMES[i].id) g.themeSel = i;
                  return true;
              }},
        boolField("cosmetics", "customThemeOwned", &GameState::customThemeOwned),
        // The custom theme's colors, in Hue order: "#cccccc, #767676, ..."
        Field{"cosmetics", "customTheme",
              [](const GameState& g) {
                  std::string out;
                  for (int i = 0; i < HUE_COUNT; ++i) out += (i ? ", " : "") + hexColor(g.customTheme[(size_t)i]);
                  return out;
              },
              [](GameState& g, const std::string& text) {
                  std::stringstream ss(text);
                  std::string item;
                  for (int i = 0; i < HUE_COUNT; ++i)
                      if (!std::getline(ss, item, ',') || !parseHexColor(item, g.customTheme[(size_t)i])) return false;
                  return true;
              },
              [](const GameState& g) { return g.customThemeOwned; }},

        maskField("workshop", "built", &GameState::gadgetsOwned, GADGET_COUNT, [](int i) { return GADGETS[i].id; }),
        // The slots: "tuningfork, -, lavacore". Unknown gadgets leave the slot empty.
        Field{"workshop", "slots",
              [](const GameState& g) {
                  std::string out;
                  for (int i = 0; i < GADGET_SLOTS; ++i) {
                      int x = g.gadgetSlots[(size_t)i];
                      out += (i ? ", " : "") + std::string(x >= 0 && x < GADGET_COUNT ? GADGETS[x].id : "-");
                  }
                  return out;
              },
              [](GameState& g, const std::string& text) {
                  std::stringstream ss(text);
                  std::string item;
                  for (int i = 0; i < GADGET_SLOTS; ++i) {
                      g.gadgetSlots[(size_t)i] = -1;
                      if (!std::getline(ss, item, ',')) continue;
                      for (int x = 0; x < GADGET_COUNT; ++x)
                          if (lower(trim(item)) == GADGETS[x].id) g.gadgetSlots[(size_t)i] = x;
                  }
                  return true;
              },
              [](const GameState& g) { return g.gadgetsOwned != 0; }},

        numField("market", "shardPrice", &GameState::shardPrice, 10.0, 100'000.0),
        numField("market", "heat",       &GameState::heat, 0, MAX_HEAT),
        numField("market", "heatClock",  &GameState::heatClock, 0, 24 * 3600),

        numField("crew", "paydayClock", &GameState::paydayClock, 0, 24 * 3600),

        numField("disasters", "clock",          &GameState::disasterClock, 0, 24 * 3600),
        numField("disasters", "dronesDown",     &GameState::dronesDown, 0, MAX_LEVEL),
        numField("disasters", "dronesDownSecs", &GameState::dronesDownSecs, 0, 24 * 3600),
        numField("disasters", "paused",         &GameState::machinesPaused, 0, 24 * 3600),
        boolField("disasters", "botBroken",     &GameState::botBroken),
        maskField("disasters", "cracked",       &GameState::orreryCracked, ORRERY_STAGE_COUNT, [](int i) { return ORRERY[i].name; }),
        maskField("disasters", "gadgetsBroken", &GameState::gadgetsBroken, GADGET_COUNT, [](int i) { return GADGETS[i].id; }),
        numField("disasters", "unplugged",      &GameState::gadgetsUnplugged, 0, 24 * 3600),
        numField("stats", "disastersHit",       &GameState::disastersHit),
        numField("stats", "disastersStopped",   &GameState::disastersStopped),

        maskField("stars", "lit",              &GameState::starsLit, CONSTELLATION_COUNT, [](int i) { return CONSTELLATIONS[i].id; }),
        numField("stars", "lastContractAt",    &GameState::lastContractAt, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()),
        numField("stars", "fullCrewPaydays",   &GameState::fullCrewPaydays, 0, 1'000'000),
        numField("stars", "stoppedInRow",      &GameState::stoppedInRow, 0, 1'000'000),
        maskField("stars", "signatureClients", &GameState::signatureClients, CLIENT_COUNT, [](int i) { return CLIENTS[i].id; }),
        numField("stars", "heat10At",          &GameState::heat10At, -1LL, std::numeric_limits<long long>::max()),
        numField("stars", "handsOffSince",     &GameState::handsOffSince),
        numField("stars", "handsOffFrom",      &GameState::handsOffFrom),
        numField("stars", "handsOffMines",     &GameState::handsOffMines),

        numField("orrery", "stage",    &GameState::orreryStage, 0, ORRERY_STAGE_COUNT),
        numField("orrery", "progress", &GameState::orreryProgress),

        enumAt("infection", "strain", [](GameState& g) -> Strain& { return g.strain; }, STRAIN_COUNT,
               [](int s) { return STRAINS[s].id; }, [](const GameState& g) { return g.strain != Strain::None; }),
        boolAt("infection", "cured", [](GameState& g) -> bool& { return g.cured; },
               [](const GameState& g) { return g.strain != Strain::None; }),

        numField("ledger", "shardsBought",    &GameState::shardsBought),
        numField("ledger", "shardsSold",      &GameState::shardsSold),
        numField("ledger", "tokensOnBuys",    &GameState::tokensOnBuys),
        numField("ledger", "tokensFromSells", &GameState::tokensFromSells),
        numField("ledger", "tradeCost",       &GameState::tradeCost),

        historyField(),
        };
        // Lifetime income by source: [income] mining = ..., drones = ...
        static const char* const sourceKeys[SOURCE_COUNT] = {"mining", "drones", "bot", "crew", "contracts", "other"};
        for (int i = 0; i < SOURCE_COUNT; ++i)
            f.push_back(numAt("income", sourceKeys[i], [i](GameState& g) -> long long& { return g.earnedBy[(size_t)i]; }));
        Present investing = [](const GameState& g) { return g.investment.amount > 0; };
        using LL = long long;
        f.push_back(numAt("investment", "amount", [](GameState& g) -> LL& { return g.investment.amount; }, 0LL,
                          std::numeric_limits<LL>::max(), investing));
        f.push_back(numAt("investment", "payback", [](GameState& g) -> LL& { return g.investment.payback; }, 0LL,
                          std::numeric_limits<LL>::max(), investing));
        f.push_back(numAt("investment", "secondsLeft", [](GameState& g) -> int& { return g.investment.secondsLeft; }, 0,
                          24 * 3600, investing));
        f.push_back(boolAt("investment", "scam", [](GameState& g) -> bool& { return g.investment.scam; }, investing));
        f.push_back(numAt("tasks", "slots", [](GameState& g) -> int& { return g.taskSlots; }, 0, TASK_SLOTS_MAX));
        // Between profiles: "Alpha: 5000000000 ; Beta: -300" (+ they owe you, - you owe them)
        f.push_back(Field{
            "exchange", "balances",
            [](const GameState& g) {
                std::string out;
                for (const auto& [name, b] : g.balances) out += (out.empty() ? "" : " ; ") + name + ": " + std::to_string(b);
                return out;
            },
            [](GameState& g, const std::string& text) {
                g.balances.clear();
                for (const std::string& part : splitFields(text)) {
                    size_t colon = part.rfind(':');
                    long long b = 0;
                    if (colon == std::string::npos || !parseNumber(trim(part.substr(colon + 1)), b)) return false;
                    std::string name = trim(part.substr(0, colon));
                    if (name.empty() || name.size() > 24) return false;
                    if (b != 0) g.balances.push_back({name, b});
                }
                return true;
            },
            [](const GameState& g) { return !g.balances.empty(); }});
        // Console aliases: "pt: predict tokens 1h ; ps: predict shards 1h"
        f.push_back(Field{
            "console", "aliases",
            [](const GameState& g) {
                std::string out;
                for (const auto& [name, cmd] : g.aliases) out += (out.empty() ? "" : " ; ") + name + ": " + cmd;
                return out;
            },
            [](GameState& g, const std::string& text) {
                g.aliases.clear();
                for (const std::string& part : splitFields(text)) {
                    size_t colon = part.find(':');
                    if (colon == std::string::npos) continue;   // a broken alias is dropped, not the save
                    std::string name = lower(trim(part.substr(0, colon))), cmd = trim(part.substr(colon + 1));
                    if (!aliasNameOk(name) || cmd.empty() || cmd.size() > ALIAS_TEXT_MAX) continue;
                    if ((int)g.aliases.size() < ALIAS_MAX) g.aliases.push_back({name, cmd});
                }
                return true;
            },
            [](const GameState& g) { return !g.aliases.empty(); }});
        addTaskFields(f);
        addCrewFields(f);
        addContractFields(f);
        addPeopleFields(f);
        return f;
    }();
    return fields;
}

// ----- INI reading -----
using KeyValues = std::unordered_map<std::string, std::string>;   // "section.key" (lowercase) -> value

bool parseIni(std::istream& in, KeyValues& kv, std::string& why) {
    std::string section;   // keys before any [section] header (v3 files) land in section ""
    std::string line;
    int lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);   // also removes the '\r' from CRLF files
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[') {
            if (line.back() != ']') { why = "line " + std::to_string(lineNo) + ": unterminated [section]"; return false; }
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t eq = line.find('=');
        std::string key = eq == std::string::npos ? "" : lower(trim(line.substr(0, eq)));
        if (key.empty()) { why = "line " + std::to_string(lineNo) + ": expected \"key = value\""; return false; }
        kv[section + "." + key] = trim(line.substr(eq + 1));
    }
    return true;
}

bool applyFields(const KeyValues& kv, GameState& g, std::string& why) {
    for (const Field& f : saveFields()) {
        std::string key = lower(f.key);
        auto it = kv.find(lower(f.section) + "." + key);
        if (it == kv.end()) it = kv.find("." + key);   // v3 had no sections
        if (it == kv.end()) continue;                  // missing key: keep the default

        if (!f.set(g, it->second)) {
            why = "bad value for [" + f.section + "] " + f.key + " = \"" + it->second + "\"";
            return false;
        }
    }
    return true;
}

// Positional v1/v2 files bypass the table when they are read, so run their values through
// the table's own range checks by round-tripping every field.
bool checkRanges(const GameState& g, std::string& why) {
    GameState probe;
    for (const Field& f : saveFields()) {
        if (f.present && !f.present(g)) continue;
        if (!f.set(probe, f.get(g))) { why = "[" + f.section + "] " + f.key + " is out of range"; return false; }
    }
    return true;
}

// Fixes harmless inconsistencies and rejects the ones that mean real corruption.
bool normalizeAndValidate(GameState& g, std::string& why, std::string& notice) {
    // Contracts this version can't run (a removed kind, or a half-written slot) are cancelled
    // and their stake refunded, instead of making the whole save unreadable.
    for (size_t i = g.contracts.size(); i-- > 0;) {
        const Contract& c = g.contracts[i];
        if (c.kind != ContractKind::Count && c.target > 0) continue;
        g.shards += c.stake;
        notice = "A saved contract no longer exists in this version, so it was cancelled" +
                 std::string(c.stake > 0 ? " and its " + num(c.stake) + "-shard stake refunded." : ".");
        g.contracts.erase(g.contracts.begin() + (std::ptrdiff_t)i);
    }

    g.promptsOwned |= 1u;                                   // the default prompt is always owned
    if (g.promptSel == PROMPT_COUNT) {
        if (!g.customPromptOwned || g.customPrompt.empty()) g.promptSel = 0;
    } else if (!hasBit(g.promptsOwned, g.promptSel)) {
        g.promptSel = 0;
    }
    g.autoOn = g.autoOn && g.autoUnlocked;
    g.orreryCracked &= (1u << g.orreryStage) - 1;            // only built stages can be cracked
    g.gadgetsBroken &= g.gadgetsOwned;
    if (g.bot == 0) g.botBroken = false;
    if (!themeOwned(g, g.themeSel)) g.themeSel = 0;          // a hand-edited theme you never bought
    for (size_t i = 0; i < g.gadgetSlots.size(); ++i) {      // equipped gadgets must be built, and in one slot only
        int x = g.gadgetSlots[i];
        bool dup = std::find(g.gadgetSlots.begin(), g.gadgetSlots.begin() + (std::ptrdiff_t)i, x) != g.gadgetSlots.begin() + (std::ptrdiff_t)i;
        if (x >= 0 && (!hasBit(g.gadgetsOwned, x) || dup)) g.gadgetSlots[i] = -1;
    }

    // Crew slots that were only half-written (say, [crew.3] with no name) are dropped, and so are
    // gaps in the keepsake and former crew lists (a hand-edited file that skips a number).
    std::erase_if(g.crew, [](const CrewMember& c) { return c.name.empty(); });
    std::erase_if(g.keepsakes, [](const Keepsake& k) { return k.item.empty(); });
    std::erase_if(g.formerCrew, [](const FormerCrew& c) { return c.name.empty(); });

    for (Contract& c : g.contracts)
        if (c.secondsLeft <= 0) c.secondsLeft = 1;   // fails on the next tick
    if (!g.crew.empty() && g.paydayClock <= 0) g.paydayClock = PAYDAY_SECONDS;         // a hand-made crew gets a full pay period
    g.lastReportedPrice = g.shardPrice;

    if (!(g.passiveCarry >= 0.0 && g.passiveCarry < 1.0)) {
        why = "[player] passiveCarry must be at least 0 and below 1";
        return false;
    }
    if (g.monolithStage < MONO_STAGE_COUNT) {
        if (g.monolithProgress >= MONO[g.monolithStage].cost) {
            why = "[progress] monolithProgress is at or past the current stage's cost";
            return false;
        }
    } else if (g.monolithProgress != 0) {
        why = "[progress] monolithProgress should be 0 once the Monolith is complete";
        return false;
    }
    if (g.orreryStage < ORRERY_STAGE_COUNT) {
        if (g.orreryProgress >= ORRERY[g.orreryStage].cost) {
            why = "[orrery] progress is at or past the current stage's cost";
            return false;
        }
    } else if (g.orreryProgress != 0) {
        why = "[orrery] progress should be 0 once the Orrery is complete";
        return false;
    }
    return true;
}

bool readLegacy(std::istream& in, int ver, GameState& tmp) {
    switch (ver) {
        case 1:
            if (!(in >> tmp.tokens
                     >> tmp.pickaxe >> tmp.magnet >> tmp.drone >> tmp.streak
                     >> tmp.passiveCarry
                     >> tmp.lastUpdate >> tmp.lastMine)) return false;
            tmp.lifetimeEarned = tmp.tokens;   // best guess for a stat that didn't exist yet
            return true;

        case 2: {
            int autoUnlocked = 0, autoOn = 0;
            if (!(in >> tmp.tokens
                     >> tmp.pickaxe >> tmp.magnet >> tmp.drone >> tmp.bot >> tmp.streak
                     >> autoUnlocked >> autoOn
                     >> tmp.passiveCarry
                     >> tmp.lastUpdate >> tmp.lastMine
                     >> tmp.event >> tmp.eventEnds
                     >> tmp.botTotal)) return false;
            tmp.autoUnlocked = autoUnlocked != 0;
            tmp.autoOn = autoOn != 0;
            tmp.lifetimeEarned = tmp.tokens + tmp.botTotal;
            return true;
        }

        default:
            return false;
    }
}


LoadResult corrupt(std::string why) { return LoadResult{LoadStatus::Corrupt, std::move(why), ""}; }

}  // namespace

bool writeSaveFileAt(const std::filesystem::path& file, const GameState& g) {
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;

        out << "; TokenRush save file. Safe to edit by hand while the game is closed.\n"
            << "[meta]\n"
            << "version = " << SAVE_VERSION << "\n";

        std::string current;
        for (const Field& f : saveFields()) {
            if (f.present && !f.present(g)) continue;
            if (f.section != current) {
                out << "\n[" << f.section << "]\n";
                current = f.section;
            }
            out << f.key << " = " << f.get(g) << "\n";
        }

        out.flush();
        if (!out) return false;
    }

    std::error_code ec;
#ifdef _WIN32
    // Best-effort replace on Windows, where rename can fail if the destination exists.
    std::filesystem::remove(file, ec);
#endif
    std::filesystem::rename(tmp, file, ec);   // atomic replace on POSIX
    return !ec;
}

bool writeSaveFile(const GameState& g) { return writeSaveFileAt(savePath(), g); }

void setSaveDir(const std::filesystem::path& dir) { g_saveDir = dir; }

bool ensureSaveDir(std::string& why) {
    std::error_code ec;
    std::filesystem::create_directories(g_saveDir, ec);
    if (ec) { why = ec.message(); return false; }
    return true;
}

std::filesystem::path savePath()    { return g_saveDir / "tokenrush.save"; }
std::filesystem::path badSavePath() { return g_saveDir / "tokenrush.save.bad"; }

LoadResult loadGame(GameState& out) { return loadGameFile(savePath(), out); }

LoadResult loadGameFile(const std::filesystem::path& path, GameState& out) {
    std::ifstream file(path);
    if (!file) return {LoadStatus::NoFile, "", ""};

    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (trim(text).empty()) return corrupt("the file is empty");

    std::istringstream in(text);
    std::string first;
    in >> first;
    in.seekg(0);

    GameState tmp;
    std::string why;
    bool hasUnlocks = false;   // saves from before v7 don't say what's unlocked
    bool hasTitleTimes = false;   // ...and from before v8, when titles were reached

    if (first == "TOKENRUSH") {
        // v1..v3: "TOKENRUSH <version>" header line
        std::string word, restOfHeader;
        int ver = 0;
        if (!(in >> word >> ver)) return corrupt("unreadable header line");
        std::getline(in, restOfHeader);

        switch (ver) {
            case 1:
            case 2:
                if (!readLegacy(in, ver, tmp)) return corrupt("truncated or unreadable v" + std::to_string(ver) + " save");
                if (!checkRanges(tmp, why)) return corrupt(why);
                break;
            case 3: {
                KeyValues kv;
                if (!parseIni(in, kv, why) || !applyFields(kv, tmp, why)) return corrupt(why);
                break;
            }
            default:
                return corrupt("unknown save version " + std::to_string(ver));
        }
    } else {
        KeyValues kv;
        if (!parseIni(in, kv, why)) return corrupt(why);

        int ver = 0;
        auto it = kv.find("meta.version");
        if (it == kv.end() || !parseNumber(it->second, ver)) return corrupt("missing or invalid [meta] version");
        if (ver > SAVE_VERSION) {
            return corrupt("this save is version " + std::to_string(ver) +
                           ", newer than this build understands (" + std::to_string(SAVE_VERSION) + ")");
        }
        if (!applyFields(kv, tmp, why)) return corrupt(why);
        hasUnlocks = kv.count("progress.unlocked") > 0;
        hasTitleTimes = kv.count("stats.titletimes") > 0;
    }

    std::string notice;
    if (!normalizeAndValidate(tmp, why, notice)) return corrupt(why);
    if (!hasUnlocks) grandfatherUnlocks(tmp);   // whatever you were already using stays open
    syncUnlocks(tmp);                           // and whatever your title allows
    if (!hasTitleTimes) {                       // titles already reached: when is unknown
        int ti = titleIndex(renownOf(tmp));
        for (int i = 1; i <= ti; ++i) tmp.titleReachedAt[i] = -1;
    }
    tmp.tasks.erase(std::remove_if(tmp.tasks.begin(), tmp.tasks.end(), [](const Task& t) { return t.text.empty(); }), tmp.tasks.end());
    if ((int)tmp.tasks.size() > tmp.taskSlots) tmp.tasks.resize((size_t)tmp.taskSlots);
    compileTasks(tmp);                          // a task this build can't read stays, marked as broken
    out = tmp;
    return {LoadStatus::Ok, "", notice};
}

bool saveNow(Shared& s) {
    std::lock_guard sl(s.saveMtx);
    GameState copy;
    {
        std::lock_guard lk(s.m);
        copy = s.g;
    }
    bool ok = writeSaveFile(copy);
    if (ok) s.lastSave = (long long)nowSec();
    return ok;
}

bool quarantineSave() {
    std::error_code ec;
    std::filesystem::remove(badSavePath(), ec);   // rename may not overwrite an existing file on Windows
    std::filesystem::rename(savePath(), badSavePath(), ec);
    return !ec;
}

void deleteSaveFiles() {
    std::error_code ec;
    std::filesystem::remove(savePath(), ec);
    std::filesystem::remove(tmpSavePath(), ec);
}

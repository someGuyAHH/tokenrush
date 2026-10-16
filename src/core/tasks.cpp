#include "tasks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

#include "actions.hpp"
#include "crew.hpp"
#include "disasters.hpp"
#include "game.hpp"
#include "market.hpp"
#include "util.hpp"

// ===== What tasks may read =====
// Only numbers the screen already shows. Nothing about offers, clients, the future or chance.
namespace {

enum class Unit { Amount, Time };

struct ValueDef {
    const char* name;
    Unit unit;
    double (*get)(GameState&);
};

double upgradeCost(GameState& g, int i) { return (double)upgrades(g)[(size_t)i].cost; }
int homeCount(const GameState& g) {
    return (int)std::count_if(g.crew.begin(), g.crew.end(), [](const CrewMember& c) { return !isAway(c); });
}

const ValueDef VALUES[] = {
    {"tokens",        Unit::Amount, [](GameState& g) { return (double)g.tokens; }},
    {"shards",        Unit::Amount, [](GameState& g) { return (double)g.shards; }},
    {"income",        Unit::Amount, [](GameState& g) { return g.incomeRate; }},
    {"price",         Unit::Amount, [](GameState& g) { return g.shardPrice; }},
    {"heat",          Unit::Amount, [](GameState& g) { return (double)g.heat; }},
    {"level.pickaxe", Unit::Amount, [](GameState& g) { return (double)g.pickaxe; }},
    {"level.magnet",  Unit::Amount, [](GameState& g) { return (double)g.magnet; }},
    {"level.drone",   Unit::Amount, [](GameState& g) { return (double)g.drone; }},
    {"level.bot",     Unit::Amount, [](GameState& g) { return (double)g.bot; }},
    {"cost.pickaxe",  Unit::Amount, [](GameState& g) { return upgradeCost(g, 0); }},
    {"cost.magnet",   Unit::Amount, [](GameState& g) { return upgradeCost(g, 1); }},
    {"cost.drone",    Unit::Amount, [](GameState& g) { return upgradeCost(g, 2); }},
    {"cost.bot",      Unit::Amount, [](GameState& g) { return upgradeCost(g, 3); }},
    {"crew",          Unit::Amount, [](GameState& g) { return (double)g.crew.size(); }},
    {"home",          Unit::Amount, [](GameState& g) { return (double)homeCount(g); }},
    {"away",          Unit::Amount, [](GameState& g) { return (double)((int)g.crew.size() - homeCount(g)); }},
    {"wages",         Unit::Amount, [](GameState& g) { return (double)totalWages(g); }},
    {"payday",        Unit::Time,   [](GameState& g) { return g.crew.empty() ? 0.0 : (double)g.paydayClock; }},
    {"broken",        Unit::Amount, [](GameState& g) { return (double)repairs(g).size(); }},
    {"orrery.left",   Unit::Amount, [](GameState& g) { return (double)orreryRemaining(g); }},
    {"jobs",          Unit::Amount, [](GameState& g) { return (double)g.contracts.size(); }},
    // Only `predict when` reads these (from here on): running totals that only grow.
    {"earned",        Unit::Amount, [](GameState& g) { return (double)g.lifetimeEarned; }},
    {"mines",         Unit::Amount, [](GameState& g) { return (double)g.manualMines; }},
    {"jackpots",      Unit::Amount, [](GameState& g) { return (double)g.jackpots; }},
};
constexpr int VALUE_DEFS = (int)std::size(VALUES);
constexpr int PREDICT_ONLY_FROM = VALUE_DEFS - 3;   // tasks stop before these

const char* const UPGRADE_IDS[] = {"pickaxe", "magnet", "drone", "bot"};
const char* const UPGRADE_NAMES[] = {"Pickaxe", "Magnet", "Drone", "Mining Bot"};

}  // namespace

const TaskValueInfo TASK_VALUES[] = {
    {"tokens, shards",        "what you have"},
    {"income",                "tokens per second (the 5-minute average)"},
    {"price",                 "the shard price on the market"},
    {"heat",                  "0 to 10"},
    {"level.<upgrade>",       "pickaxe, magnet, drone or bot: its level"},
    {"cost.<upgrade>",        "what its next level costs"},
    {"crew, home, away",      "crew members: all of them, at home, on expeditions"},
    {"wages",                 "shards due at the next payday"},
    {"payday",                "time until payday (a time: 30s, 2m...)"},
    {"broken",                "things disasters have broken"},
    {"orrery.left",           "shards the Orrery's current stage still needs"},
    {"jobs",                  "contracts you're working on"},
};
const int TASK_VALUE_COUNT = (int)std::size(TASK_VALUES);

const TaskActionInfo TASK_ACTIONS[] = {
    {"upgrade <pickaxe|magnet|drone|bot> [n|max]", "buys up to n levels (1 if you don't say)"},
    {"buy shards <n|max>",                         "a market order (max: all you can afford)"},
    {"sell shards <n|all>",                        "a market order"},
    {"train <who>",                                "one level; waits at the level cap"},
    {"promote <who>",                              "at the level cap"},
    {"send <who> <short|long|deep>",               "an expedition"},
    {"repair",                                     "everything broken you can pay for"},
    {"fund orrery <n|all>",                        "sets shards in (all: keeps the next payday's wages)"},
    {"equip <gadget>, unequip <gadget>",           "swaps gadgets (by name: fog machine, forged papers...)"},
    {"autobuyer on|off",                           "the Auto-Buyer"},
    {"notify \"<text>\"",                          "puts your message in the log"},
};
const int TASK_ACTION_COUNT = (int)std::size(TASK_ACTIONS);

// ===== The compiled form =====
enum class TaskMode { When, While, Every };

struct TaskNode {
    enum Kind { Num, Val, Neg, Not, Bin } kind = Num;
    double num = 0;      // Num: the value once its unit is settled
    double raw = 0;      // Num: as written
    char unit = 0;       // Num: 0 plain, 'a' amount (k/b/t), 't' time (s/h/compound), 'm' 5m (minutes or million)
    int val = -1;        // Val: index into VALUES
    char op = 0;         // Bin: + - * / < l(<=) > g(>=) = ! (!=) & (and) | (or)
    int a = -1, b = -1;  // children
    int pos = 0, len = 1;
};

struct TaskAction {
    enum Kind { Upgrade, BuyShards, SellShards, Train, Promote, Send, Repair, Fund, Equip, Unequip, Autobuyer, Notify } kind = Repair;
    int item = -1;            // upgrade, gadget or trip
    long long n = 1;
    bool all = false;         // max / all
    bool on = true;           // autobuyer
    std::string text;         // notify
    enum Who { Nobody, Name, Slot, Anyone } who = Nobody;
    std::string name;         // Who::Name (as written)
    int slot = -1;            // Who::Slot (0-based)
};

struct TaskProgram {
    TaskMode mode = TaskMode::When;
    int every = 0;            // seconds
    int cond = -1;            // root node; -1 = always true
    std::vector<TaskNode> nodes;
    TaskAction act;
    int instructions = 1;
};

namespace {

// ===== Reading the text =====
struct Fail {
    std::string what;
    int pos, len;
};

struct Token {
    enum Kind { Word, Number, String, Op, End } kind = End;
    std::string text;    // words lowercased; ops as written; strings without the quotes
    std::string raw;     // as written
    double num = 0;
    char unit = 0;       // see TaskNode::unit
    int pos = 0, len = 0;
};

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> row(b.size() + 1);
    std::iota(row.begin(), row.end(), 0);
    for (size_t i = 1; i <= a.size(); ++i) {
        int diag = row[0];
        row[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

// " (did you mean pickaxe?)", or nothing when nothing is close.
std::string suggest(const std::string& word, const std::vector<std::string>& options) {
    std::string best;
    int bestD = 3;
    for (const std::string& o : options) {
        int d = editDistance(word, o);
        if (d < bestD) { bestD = d; best = o; }
    }
    return best.empty() ? "" : " (did you mean " + best + "?)";
}

std::string joinOr(const std::vector<std::string>& items) {
    std::string s;
    for (size_t i = 0; i < items.size(); ++i) s += (i == 0 ? "" : i + 1 == items.size() ? " or " : ", ") + items[i];
    return s;
}

std::vector<Token> lex(const std::string& s) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (std::isspace(c)) { ++i; continue; }
        Token t;
        t.pos = (int)i;
        if (std::isalpha(c) || c == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '.')) ++j;
            while (j > i + 1 && s[j - 1] == '.') --j;   // "repair." : the dot isn't part of the word
            t.kind = Token::Word;
            t.raw = s.substr(i, j - i);
            t.text = lower(t.raw);
            i = j;
        } else if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            // 5000, 5,000, 2.5k, 3M, 1.2B, 5T; 30s, 5m, 1h, 1h30m
            size_t j = i;
            double total = 0;
            bool time = false;
            while (true) {
                size_t k = j;
                std::string digits;
                while (k < s.size() && (std::isdigit((unsigned char)s[k]) || s[k] == '.' ||
                                        (s[k] == ',' && k + 1 < s.size() && std::isdigit((unsigned char)s[k + 1])))) {
                    if (s[k] != ',') digits += s[k];
                    ++k;
                }
                size_t e = k;
                while (e < s.size() && std::isalpha((unsigned char)s[e])) ++e;
                std::string suffix = lower(s.substr(k, e - k));
                double v = 0;
                try { size_t used = 0; v = std::stod(digits, &used); if (used != digits.size()) throw 0; }
                catch (...) { throw Fail{"\"" + s.substr(i, e - i) + "\" isn't a number", (int)i, (int)(e - i)}; }
                double unitSec = suffix == "s" ? 1 : suffix == "m" ? 60 : suffix == "h" ? 3600 : 0;
                bool more = unitSec > 0 && e < s.size() && std::isdigit((unsigned char)s[e]);   // 1h30m
                if (time || more) {
                    if (unitSec == 0) throw Fail{"a time is written like 30s, 5m, 1h or 1h30m", (int)i, (int)(e - i)};
                    time = true;
                    total += v * unitSec;
                    j = e;
                    if (more) continue;
                    t.num = total;
                    t.unit = 't';
                } else if (suffix.empty()) {
                    t.num = v;
                } else if (suffix == "k" || suffix == "b" || suffix == "t") {
                    t.num = v * (suffix == "k" ? 1e3 : suffix == "b" ? 1e9 : 1e12);
                    t.unit = 'a';
                } else if (suffix == "m") {
                    t.num = v;
                    t.unit = 'm';   // 5 minutes or 5 million: whatever it's compared with says
                } else if (suffix == "s" || suffix == "h") {
                    t.num = v * unitSec;
                    t.unit = 't';
                } else {
                    throw Fail{"\"" + s.substr(i, e - i) + "\" isn't a number I know: try 5k, 2M, 1.5B, 3T, or a time like 30s, 5m, 1h",
                               (int)i, (int)(e - i)};
                }
                j = e;
                break;
            }
            t.kind = Token::Number;
            t.raw = s.substr(i, j - i);
            i = j;
        } else if (c == '"') {
            size_t j = s.find('"', i + 1);
            if (j == std::string::npos) throw Fail{"this text needs a closing \"", (int)i, (int)(s.size() - i)};
            t.kind = Token::String;
            t.text = s.substr(i + 1, j - i - 1);
            t.raw = s.substr(i, j + 1 - i);
            i = j + 1;
        } else {
            static const char* const TWO[] = {"<=", ">=", "!=", "=="};
            std::string op;
            for (const char* two : TWO) if (s.compare(i, 2, two) == 0) op = two;
            if (op.empty() && std::string("<>=+-*/()").find((char)c) != std::string::npos) op = std::string(1, (char)c);
            if (op.empty()) {
                if (c == '&' || c == '|') throw Fail{std::string("write \"") + (c == '&' ? "and" : "or") + "\" instead", (int)i, 1};
                throw Fail{std::string("tasks don't use \"") + (char)c + "\"", (int)i, 1};
            }
            t.kind = Token::Op;
            t.text = t.raw = op;
            i += op.size();
        }
        t.len = (int)(i - (size_t)t.pos);
        out.push_back(t);
    }
    Token end;
    end.pos = (int)s.size();
    out.push_back(end);
    return out;
}

const char* const FORBIDDEN[][2] = {
    {"mine",     "tasks can't mine: mining is yours"},
    {"m",        "tasks can't mine: mining is yours"},
    {"respond",  "tasks can't stop disasters: that's on you"},
    {"douse",    "tasks can't stop disasters: that's on you"},
    {"chase",    "tasks can't stop disasters: that's on you"},
    {"brace",    "tasks can't stop disasters: that's on you"},
    {"oil",      "tasks can't stop disasters: that's on you"},
    {"unplug",   "tasks can't stop disasters: that's on you"},
    {"yes",      "tasks can't answer offers"},
    {"no",       "tasks can't answer offers"},
    {"y",        "tasks can't answer offers"},
    {"n",        "tasks can't answer offers"},
    {"fire",     "tasks can't fire anyone"},
    {"hire",     "tasks can't hire (not yet, anyway)"},
    {"accept",   "tasks can't take jobs (not yet, anyway)"},
    {"abandon",  "tasks can't drop jobs"},
    {"wipe",     "tasks can't touch your save"},
    {"theme",    "tasks can't change themes"},
};

class Parser {
public:
    // `reader`: who's reading, for the messages ("predict", "eval"); they also may read predict's
    // extra values. Null for a task.
    Parser(const std::string& text, const GameState* g, const char* reader = nullptr)
        : text_(text), g_(g), predicting_(reader != nullptr), reader_(reader ? reader : "tasks"), toks_(lex(text)) {}

    // `eval`: any expression. `kind` says what came out: 'b' true/false, 't' a time, 'a' a number.
    TaskProgram parseExpression(char& kind) {
        TaskProgram p;
        if (peek().kind == Token::End) fail(peek(), "nothing to work out: try eval tokens / income");
        int root = orExpr(p);
        if (peek().kind != Token::End) fail(peek(), "expected + - * / or a comparison here, or the end");
        const TaskNode& n = p.nodes[(size_t)root];
        bool boolean = n.kind == TaskNode::Not || (n.kind == TaskNode::Bin && (isCompare(n.op) || n.op == '&' || n.op == '|'));
        char k = kindOf(p, root);
        settle(p, root, k == 't' ? 't' : 'a');
        p.cond = root;
        kind = boolean ? 'b' : k == 't' ? 't' : 'a';
        return p;
    }

    // `predict when`: a condition and nothing else.
    TaskProgram parseCondition() {
        TaskProgram p;
        p.cond = condition(p);
        if (peek().kind != Token::End) fail(peek(), peekWord("do") ? "predict when takes just the condition (no do ...)" : "expected and / or here, or the end");
        return p;
    }

    TaskProgram parse() {
        TaskProgram p;
        const Token& first = peek();
        if (first.kind != Token::Word) fail(first, "a task starts with when, while or every");
        if (first.text == "when" || first.text == "while") {
            next();
            p.mode = first.text == "when" ? TaskMode::When : TaskMode::While;
            p.cond = condition(p);
        } else if (first.text == "every") {
            next();
            p.mode = TaskMode::Every;
            const Token& t = peek();
            if (t.kind != Token::Number || (t.unit != 't' && t.unit != 'm')) fail(t, "after every comes a time, like 30s, 5m or 1h");
            next();
            double secs = t.unit == 'm' ? t.num * 60 : t.num;
            if (secs < TASK_EVERY_MIN) fail(t, "every " + std::to_string(TASK_EVERY_MIN) + "s is as often as it gets (use while for every second)");
            if (secs > TASK_EVERY_MAX) fail(t, "every 24h is as long as it gets");
            p.every = (int)std::lround(secs);
            if (peekWord("if")) { next(); p.cond = condition(p); }
        } else {
            std::string hint = suggest(first.text, {"when", "while", "every"});
            fail(first, "a task starts with when, while or every" + hint);
        }
        if (!peekWord("do")) {
            const Token& t = peek();
            fail(t, t.kind == Token::End ? "then comes do and what to do, like: do repair" : "expected do here (then what to do)");
        }
        next();
        p.act = action(p);
        if (peek().kind != Token::End) fail(peek(), "the task ends after the action: one action per task");
        p.instructions = (int)p.nodes.size() + 1;
        if (p.mode == TaskMode::While && p.act.kind == TaskAction::Notify)
            throw Fail{"notify with while would repeat every second: use when (once each time it turns true) or every", 0, 5};
        return p;
    }

private:
    const std::string& text_;
    const GameState* g_;
    bool predicting_ = false;   // predict's extra values are allowed
    const char* reader_;        // "tasks", "predict", "eval"
    std::vector<Token> toks_;
    size_t at_ = 0;

    const Token& peek() const { return toks_[std::min(at_, toks_.size() - 1)]; }
    const Token& next() { const Token& t = peek(); if (at_ < toks_.size() - 1) ++at_; return t; }
    bool peekWord(const char* w) const { return peek().kind == Token::Word && peek().text == w; }
    bool peekOp(const char* op) const { return peek().kind == Token::Op && peek().text == op; }
    [[noreturn]] void fail(const Token& t, const std::string& what) const { throw Fail{what, t.pos, std::max(1, t.len)}; }

    // ----- Conditions -----
    int condition(TaskProgram& p) {
        if (peek().kind == Token::End || peekWord("do")) fail(peek(), "the condition is missing, like: shards >= 5k");
        int root = orExpr(p);
        settle(p, root, kindOf(p, root) == 't' ? 't' : 'a');
        return root;
    }
    int add(TaskProgram& p, TaskNode n) { p.nodes.push_back(n); return (int)p.nodes.size() - 1; }
    int bin(TaskProgram& p, char op, int a, int b, const Token& at) {
        TaskNode n;
        n.kind = TaskNode::Bin; n.op = op; n.a = a; n.b = b; n.pos = at.pos; n.len = at.len;
        return add(p, n);
    }
    int orExpr(TaskProgram& p) {
        int a = andExpr(p);
        while (peekWord("or")) { const Token& t = next(); a = bin(p, '|', a, andExpr(p), t); }
        return a;
    }
    int andExpr(TaskProgram& p) {
        int a = notExpr(p);
        while (peekWord("and")) { const Token& t = next(); a = bin(p, '&', a, notExpr(p), t); }
        return a;
    }
    int notExpr(TaskProgram& p) {
        if (peekWord("not")) {
            const Token& t = next();
            TaskNode n;
            n.kind = TaskNode::Not; n.a = notExpr(p); n.pos = t.pos; n.len = t.len;
            return add(p, n);
        }
        return compare(p);
    }
    int compare(TaskProgram& p) {
        int a = sum(p);
        static const std::pair<const char*, char> OPS[] = {{"<", '<'}, {"<=", 'l'}, {">", '>'}, {">=", 'g'}, {"=", '='}, {"==", '='}, {"!=", '!'}};
        for (auto [txt, op] : OPS) {
            if (!peekOp(txt)) continue;
            const Token& t = next();
            int b = sum(p);
            for (auto [txt2, op2] : OPS) if (peekOp(txt2)) fail(peek(), "one comparison at a time: join them with and / or");
            return bin(p, op, a, b, t);
        }
        return a;
    }
    int sum(TaskProgram& p) {
        int a = product(p);
        while (peekOp("+") || peekOp("-")) { const Token& t = next(); a = bin(p, t.text[0], a, product(p), t); }
        return a;
    }
    int product(TaskProgram& p) {
        int a = atom(p);
        while (peekOp("*") || peekOp("/")) { const Token& t = next(); a = bin(p, t.text[0], a, atom(p), t); }
        return a;
    }
    int atom(TaskProgram& p) {
        const Token& t = next();
        TaskNode n;
        n.pos = t.pos; n.len = t.len;
        if (t.kind == Token::Op && t.text == "(") {
            int inner = orExpr(p);
            if (!peekOp(")")) fail(peek(), "this needs a closing )");
            next();
            return inner;
        }
        if (t.kind == Token::Op && t.text == "-") {
            n.kind = TaskNode::Neg;
            n.a = atom(p);
            return add(p, n);
        }
        if (t.kind == Token::Number) {
            n.kind = TaskNode::Num; n.raw = t.num; n.num = t.num; n.unit = t.unit;
            return add(p, n);
        }
        if (t.kind == Token::Word) {
            if (t.text == "true" || t.text == "false") { n.kind = TaskNode::Num; n.raw = n.num = t.text == "true" ? 1 : 0; return add(p, n); }
            for (int v = 0; v < VALUE_DEFS; ++v) {
                if (t.text != VALUES[v].name) continue;
                if (v >= PREDICT_ONLY_FROM && !predicting_) fail(t, "tasks can't read " + t.text + " (only predict and eval can)");
                n.kind = TaskNode::Val; n.val = v; return add(p, n);
            }
            if (t.text == "do" || t.text == "and" || t.text == "or") fail(t, "something's missing before " + t.text);
            std::vector<std::string> names;
            for (const ValueDef& v : VALUES) names.push_back(v.name);
            std::string hint = suggest(t.text, names);
            if (hint.empty() && (t.text == "level" || t.text == "cost")) hint = " (" + t.text + ".pickaxe, " + t.text + ".bot...)";
            fail(t, std::string(reader_) + " can't read \"" + t.raw + "\"" + (hint.empty() ? ": see the list of values" : hint));
        }
        if (t.kind == Token::End) fail(t, std::string(reader_ == std::string("eval") ? "the expression" : "the condition") + " stops halfway");
        fail(t, "expected a number or a value here, not \"" + t.raw + "\"");
    }

    // ----- Units: amounts and times, and "5m" in between -----
    // Returns 'a' (amount), 't' (time), 'f' (a plain number, either) or 'm' (5m: minutes or million).
    char kindOf(const TaskProgram& p, int i) const {
        const TaskNode& n = p.nodes[(size_t)i];
        switch (n.kind) {
            case TaskNode::Num: return n.unit == 0 ? 'f' : n.unit;
            case TaskNode::Val: return VALUES[n.val].unit == Unit::Time ? 't' : 'a';
            case TaskNode::Neg: return kindOf(p, n.a);
            case TaskNode::Not: return 'a';
            case TaskNode::Bin: break;
        }
        char ka = kindOf(p, n.a), kb = kindOf(p, n.b);
        auto typed = [](char k) { return k == 'a' || k == 't'; };
        auto loose = [](char ka2, char kb2) { return ka2 == 'm' || kb2 == 'm' ? 'm' : 'f'; };
        switch (n.op) {
            case '&': case '|': return 'a';
            case '+': case '-': case '<': case 'l': case '>': case 'g': case '=': case '!': {
                if (typed(ka) && typed(kb) && ka != kb) {
                    throw Fail{std::string(isCompare(n.op) ? "can't compare" : "can't add") + " a time with an amount (payday is a time: try 30s or 2m)",
                               n.pos, n.len};
                }
                char k = typed(ka) ? ka : typed(kb) ? kb : loose(ka, kb);
                return isCompare(n.op) ? 'a' : k;
            }
            case '*':
                if (ka == 't' && kb == 't') throw Fail{"can't multiply two times", n.pos, n.len};
                return ka == 't' || kb == 't' ? 't' : typed(ka) || typed(kb) ? 'a' : loose(ka, kb);
            case '/':
                if (ka == 't' && kb == 't') return 'a';
                if (kb == 't') throw Fail{"can't divide by a time", n.pos, n.len};
                return ka == 't' ? 't' : typed(ka) || typed(kb) ? 'a' : loose(ka, kb);
        }
        return 'a';
    }
    static bool isCompare(char op) { return op == '<' || op == 'l' || op == '>' || op == 'g' || op == '=' || op == '!'; }

    // Gives loose numbers their meaning: `want` is 't' (they're times) or 'a' (amounts).
    void settle(TaskProgram& p, int i, char want) {
        TaskNode& n = p.nodes[(size_t)i];
        switch (n.kind) {
            case TaskNode::Num:
                if (n.unit == 'm') n.num = want == 't' ? n.raw * 60 : n.raw * 1e6;
                return;
            case TaskNode::Val: return;
            case TaskNode::Neg: settle(p, n.a, want); return;
            case TaskNode::Not: { char k = kindOf(p, n.a); settle(p, n.a, k == 't' ? 't' : 'a'); return; }
            case TaskNode::Bin: break;
        }
        int a = n.a, b = n.b;
        char op = n.op;
        char ka = kindOf(p, a), kb = kindOf(p, b);
        if (op == '&' || op == '|') {
            settle(p, a, ka == 't' ? 't' : 'a');
            settle(p, b, kb == 't' ? 't' : 'a');
        } else if (isCompare(op) || op == '+' || op == '-') {
            char side = ka == 't' || kb == 't' ? 't' : ka == 'a' || kb == 'a' ? 'a' : isCompare(op) ? 'a' : want;
            settle(p, a, side);
            settle(p, b, side);
        } else if (op == '*') {   // a time times a plain number; or plain numbers
            bool aTime = ka == 't' || (want == 't' && kb != 't' && (ka == 'm' || kb != 'm'));
            settle(p, a, aTime ? 't' : 'a');
            settle(p, b, aTime ? 'a' : (kb == 't' || want == 't') ? 't' : 'a');
        } else {                  // '/'
            settle(p, a, ka == 't' || (want == 't' && kb != 't') ? 't' : 'a');
            settle(p, b, ka == 't' && kb != 'a' && want == 'a' ? 't' : kb == 't' ? 't' : 'a');
        }
    }

    // ----- Actions -----
    long long amount(const Token& t, const char* what) {
        if (t.kind != Token::Number || t.unit == 't') fail(t, std::string("how many ") + what + "? (a number)");
        double v = t.unit == 'm' ? t.num * 1e6 : t.num;
        if (v < 1 || v != std::floor(v) || v > 1e15) fail(t, "a whole number, 1 or more");
        return (long long)v;
    }
    void who(TaskAction& a) {
        const Token& t = next();
        if (t.kind == Token::Word && t.text == "anyone") { a.who = TaskAction::Anyone; return; }
        if (t.kind == Token::Number && t.unit == 0) {
            if (t.num < 1 || t.num > MAX_CREW_SLOTS || t.num != std::floor(t.num)) fail(t, "crew slots go from 1 to " + std::to_string(MAX_CREW_SLOTS));
            a.who = TaskAction::Slot;
            a.slot = (int)t.num - 1;
            return;
        }
        if (t.kind != Token::Word || t.text == "short" || t.text == "long" || t.text == "deep")
            fail(t, "who? a crew member's name, their slot number, or anyone");
        a.who = TaskAction::Name;
        a.name = t.raw;
        if (g_) {
            std::vector<std::string> names;
            for (const CrewMember& c : g_->crew) {
                if (lower(c.name) == t.text) return;
                names.push_back(c.name);
            }
            if (names.empty()) fail(t, "you have no crew yet");
            std::string hint;
            int best = 3;
            for (const std::string& nm : names)
                if (int d = editDistance(t.text, lower(nm)); d < best) { best = d; hint = " (did you mean " + nm + "?)"; }
            fail(t, "nobody called " + t.raw + " is on the crew" + (hint.empty() ? " (you have " + joinOr(names) + ")" : hint));
        }
    }
    void expectWord(const char* w, const std::string& usage) {
        if (!peekWord(w)) fail(peek(), "write it as: " + usage);
        next();
    }

    TaskAction action(TaskProgram&) {
        TaskAction a;
        const Token& t = next();
        if (t.kind == Token::End) fail(t, "do what? (repair, upgrade pickaxe, send anyone deep...)");
        if (t.kind != Token::Word) fail(t, "expected an action here");
        const std::string& w = t.text;
        for (auto& f : FORBIDDEN) if (w == f[0]) fail(t, f[1]);

        if (w == "upgrade") {
            a.kind = TaskAction::Upgrade;
            const Token& it = next();
            for (int i = 0; i < 4; ++i) if (it.kind == Token::Word && it.text == UPGRADE_IDS[i]) a.item = i;
            if (it.kind == Token::Word && it.text == "mining" && peekWord("bot")) { next(); a.item = 3; }
            if (a.item < 0)
                fail(it, "upgrade what? pickaxe, magnet, drone or bot" + (it.kind == Token::Word ? suggest(it.text, {"pickaxe", "magnet", "drone", "bot"}) : ""));
            if (peekWord("max")) { next(); a.all = true; }
            else if (peek().kind == Token::Number) a.n = amount(next(), "levels");
        } else if (w == "buy" || w == "sell") {
            bool buy = w == "buy";
            a.kind = buy ? TaskAction::BuyShards : TaskAction::SellShards;
            std::string usage = buy ? "buy shards <n|max>" : "sell shards <n|all>";
            if (!peekWord("shards") && !peekWord("shard")) {
                if (peek().kind == Token::Word && suggest(peek().text, {"pickaxe", "magnet", "drone", "bot"}) != "")
                    fail(peek(), "upgrades are bought with: upgrade " + peek().text);
                fail(peek(), "write it as: " + usage);
            }
            next();
            if (peekWord(buy ? "max" : "all")) { next(); a.all = true; }
            else a.n = amount(next(), "shards");
            if (a.n > MARKET_MAX_ORDER) fail(toks_[at_ - 1], "the market takes orders up to " + num(MARKET_MAX_ORDER) + " shards");
        } else if (w == "train" || w == "promote") {
            a.kind = w == "train" ? TaskAction::Train : TaskAction::Promote;
            who(a);
        } else if (w == "send") {
            a.kind = TaskAction::Send;
            who(a);
            const Token& tr = next();
            a.item = tr.kind == Token::Word ? expeditionByName(tr.text) : -1;
            if (a.item < 0) fail(tr, "which trip? short, long or deep");
        } else if (w == "repair" || w == "fix") {
            a.kind = TaskAction::Repair;
            if (peekWord("all")) next();
        } else if (w == "fund") {
            a.kind = TaskAction::Fund;
            expectWord("orrery", "fund orrery <n|all>");
            if (peekWord("all")) { next(); a.all = true; }
            else a.n = amount(next(), "shards");
        } else if (w == "equip" || w == "unequip") {
            a.kind = w == "equip" ? TaskAction::Equip : TaskAction::Unequip;
            std::string name;
            const Token& start = peek();
            int endPos = start.pos;
            while (peek().kind == Token::Word) { const Token& x = next(); name += x.text; endPos = x.pos + x.len; }
            if (name.rfind("the", 0) == 0 && name.size() > 3) name = name.substr(3);
            std::vector<std::string> ids;
            for (int i = 0; i < GADGET_COUNT; ++i) {
                std::string full;
                for (char ch : std::string(GADGETS[i].name)) if (std::isalnum((unsigned char)ch)) full += (char)std::tolower((unsigned char)ch);
                if (name == GADGETS[i].id || name == full) a.item = i;
                ids.push_back(GADGETS[i].id);
            }
            if (a.item < 0) {
                if (name.empty()) fail(start, "which gadget? (fogmachine, forgedpapers...)");
                throw Fail{"there's no gadget called that" + suggest(name, ids), start.pos, std::max(1, endPos - start.pos)};
            }
        } else if (w == "autobuyer" || w == "auto") {
            a.kind = TaskAction::Autobuyer;
            if (peekWord("buyer")) next();
            if (peekWord("on")) a.on = true;
            else if (peekWord("off")) a.on = false;
            else fail(peek(), "autobuyer on, or autobuyer off");
            next();
        } else if (w == "notify") {
            a.kind = TaskAction::Notify;
            const Token& s = next();
            if (s.kind != Token::String) fail(s, "notify takes a message in quotes: notify \"heat is high\"");
            a.text = trim(s.text);
            if (a.text.empty()) fail(s, "the message is empty");
            if (a.text.size() > TASK_NOTE_MAX) fail(s, "keep the message under " + std::to_string(TASK_NOTE_MAX) + " characters");
        } else {
            std::vector<std::string> verbs = {"upgrade", "buy", "sell", "train", "promote", "send", "repair", "fund", "equip", "unequip", "autobuyer", "notify"};
            std::string hint = suggest(w, verbs);
            fail(t, "tasks can't \"" + t.raw + "\"" + (hint.empty() ? ": see the list of actions" : hint));
        }
        return a;
    }
};

// ===== Running =====
double eval(const TaskProgram& p, int i, GameState& g) {
    const TaskNode& n = p.nodes[(size_t)i];
    switch (n.kind) {
        case TaskNode::Num: return n.num;
        case TaskNode::Val: return VALUES[n.val].get(g);
        case TaskNode::Neg: return -eval(p, n.a, g);
        case TaskNode::Not: return eval(p, n.a, g) == 0 ? 1 : 0;
        case TaskNode::Bin: break;
    }
    if (n.op == '&') return eval(p, n.a, g) != 0 && eval(p, n.b, g) != 0 ? 1 : 0;
    if (n.op == '|') return eval(p, n.a, g) != 0 || eval(p, n.b, g) != 0 ? 1 : 0;
    double a = eval(p, n.a, g), b = eval(p, n.b, g);
    switch (n.op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b == 0 ? 0 : a / b;
        case '<': return a < b;
        case 'l': return a <= b;
        case '>': return a > b;
        case 'g': return a >= b;
        case '=': return a == b;
        case '!': return a != b;
    }
    return 0;
}

// The crew members an action may pick from, in order.
std::vector<int> candidates(const GameState& g, const TaskAction& a, std::string& why) {
    std::vector<int> out;
    switch (a.who) {
        case TaskAction::Anyone:
            for (int i = 0; i < (int)g.crew.size(); ++i) out.push_back(i);
            if (out.empty()) why = "you have no crew";
            break;
        case TaskAction::Slot:
            if (a.slot < (int)g.crew.size()) out.push_back(a.slot);
            else why = "crew slot " + std::to_string(a.slot + 1) + " is empty";
            break;
        case TaskAction::Name:
            for (int i = 0; i < (int)g.crew.size(); ++i) if (lower(g.crew[(size_t)i].name) == lower(a.name)) out.push_back(i);
            if (out.empty()) why = a.name + " isn't on the crew anymore";
            break;
        case TaskAction::Nobody: break;
    }
    return out;
}

// Why the action can't happen right now ("" = it can). `member` gets the crew member it'll act on.
// The fee has already been taken out of g.tokens, so everything here is checked against what's left.
std::string blocked(GameState& g, const TaskAction& a, int& member) {
    member = -1;
    auto locked = [&](Feature f) { return isUnlocked(g, f) ? std::string() : std::string(FEATURES[(int)f].name) + " isn't open yet"; };
    switch (a.kind) {
        case TaskAction::Upgrade: {
            UpgradeRef u = upgrades(g)[(size_t)a.item];
            if (*u.lvl >= MAX_LEVEL) return std::string("the ") + UPGRADE_NAMES[a.item] + " is maxed out";
            if (g.tokens < u.cost) return std::string("a ") + UPGRADE_NAMES[a.item] + " level costs " + compact(u.cost);
            return "";
        }
        case TaskAction::BuyShards: {
            if (auto l = locked(Feature::Market); !l.empty()) return l;
            if (a.all) return maxAffordable(g) > 0 ? "" : "you can't afford a shard";
            long long cost = buyCost(g, a.n);
            return cost <= g.tokens ? "" : count(a.n, "shard") + " cost " + compact(cost);
        }
        case TaskAction::SellShards: {
            if (auto l = locked(Feature::Market); !l.empty()) return l;
            if (g.shards <= 0) return "you have no shards";
            return a.all || g.shards >= a.n ? "" : "you have " + count(g.shards, "shard");
        }
        case TaskAction::Train:
        case TaskAction::Promote:
        case TaskAction::Send: {
            if (auto l = locked(Feature::Crew); !l.empty()) return l;
            std::string why;
            std::vector<int> list = candidates(g, a, why);
            std::string first;   // the reason for the first one, if nobody can
            for (int i : list) {
                const CrewMember& c = g.crew[(size_t)i];
                std::string r;
                if (isAway(c)) r = c.name + (a.kind == TaskAction::Send ? " is already away" : " is away") + " (back in " + duration(c.away) + ")";
                else if (a.kind == TaskAction::Train && c.level >= levelCap(g, c)) r = c.name + " is at their level cap";
                else if (a.kind == TaskAction::Train && g.shards < trainCost(c)) r = "training " + c.name + " costs " + count(trainCost(c), "shard");
                else if (a.kind == TaskAction::Promote && c.level < levelCap(g, c)) r = c.name + " isn't at their level cap yet";
                else if (a.kind == TaskAction::Promote && g.shards < promoteCost(c)) r = "promoting " + c.name + " costs " + count(promoteCost(c), "shard");
                if (r.empty()) { member = i; return ""; }
                if (first.empty()) first = r;
            }
            if (!why.empty()) return why;
            if (a.who == TaskAction::Anyone && list.size() > 1)
                return a.kind == TaskAction::Send ? "everyone is away" : std::string("nobody can be ") + (a.kind == TaskAction::Train ? "trained" : "promoted") + " right now";
            return first;
        }
        case TaskAction::Repair: {
            std::vector<Repair> list = repairs(g);
            if (list.empty()) return "nothing is broken";
            for (const Repair& r : list) if (g.tokens >= r.tokens && g.shards >= r.shards) return "";
            return "you can't pay for a repair";
        }
        case TaskAction::Fund: {
            if (auto l = locked(Feature::Orrery); !l.empty()) return l;
            if (g.orreryStage >= ORRERY_STAGE_COUNT) return "the Orrery is complete";
            if (a.all) return orrerySpareShards(g) > 0 ? "" : "no spare shards (the next payday needs " + count(totalWages(g), "shard") + ")";
            return g.shards >= a.n ? "" : "you have " + count(g.shards, "shard");
        }
        case TaskAction::Equip:
        case TaskAction::Unequip: {
            if (auto l = locked(Feature::Workshop); !l.empty()) return l;
            std::string name = std::string("the ") + GADGETS[a.item].name;
            bool in = gadgetEquipped(g, (Gadget)a.item);
            if (a.kind == TaskAction::Unequip) return in ? "" : name + " isn't equipped";
            if (!hasBit(g.gadgetsOwned, a.item)) return "you haven't built " + name;
            if (in) return name + " is already equipped";
            return freeGadgetSlot(g) >= 0 ? "" : "every gadget slot is taken";
        }
        case TaskAction::Autobuyer:
            if (!g.autoUnlocked) return "you don't have the Auto-Buyer";
            return g.autoOn == a.on ? std::string("the Auto-Buyer is already ") + (a.on ? "on" : "off") : "";
        case TaskAction::Notify:
            return "";
    }
    return "";
}

Notes perform(GameState& g, const TaskAction& a, int member) {
    switch (a.kind) {
        case TaskAction::Upgrade:    return buyUpgrades(g, (size_t)a.item, a.all ? MAX_LEVEL : (int)std::min<long long>(a.n, MAX_LEVEL));
        case TaskAction::BuyShards:  return buyShards(g, a.all ? maxAffordable(g) : a.n);
        case TaskAction::SellShards: return sellShards(g, a.all ? std::min(g.shards, MARKET_MAX_ORDER) : a.n);
        case TaskAction::Train:      return train(g, member);
        case TaskAction::Promote:    return promote(g, member);
        case TaskAction::Send:       return sendExpedition(g, member, a.item);
        case TaskAction::Repair:     return repair(g, -1);
        case TaskAction::Fund:       return buildOrrery(g, a.all ? orrerySpareShards(g) : a.n).notes;
        case TaskAction::Equip:      return equipGadget(g, a.item);
        case TaskAction::Unequip:    return unequipGadget(g, a.item);
        case TaskAction::Autobuyer:  return setAuto(g, a.on);
        case TaskAction::Notify:     return {{Sev::Event, a.text}};
    }
    return {};
}

// A fresh start for a task's trigger: a `when` counts its condition as false (so it fires at once
// if it's already true), an `every` checks right away.
void restart(GameState& g, Task& t) {
    t.wasTrue = false;
    t.pending = false;
    t.nextAt = g.playSeconds;
    t.waiting.clear();
}

bool needLicense(const GameState& g, Notes& out) {
    if (!isUnlocked(g, Feature::Tasks)) { out = {{Sev::Warn, lockedText(g, Feature::Tasks)}}; return true; }
    if (g.taskSlots == 0) {
        out = {{Sev::Warn, "Tasks need the Automation License (" + num(TASK_LICENSE_COST) + " shards): 'task license'."}};
        return true;
    }
    return false;
}

bool badIndex(const GameState& g, int idx, Notes& out) {
    if (idx >= 0 && idx < (int)g.tasks.size()) return false;
    out = {{Sev::Warn, g.tasks.empty() ? "You have no tasks." : "No task " + std::to_string(idx + 1) + " (you have " + std::to_string(g.tasks.size()) + ")."}};
    return true;
}

Notes rejected(const TaskCheck& c) { return {{Sev::Warn, "That task doesn't work: " + c.error + "."}}; }

}  // namespace

// ===== The public side =====
long long taskSlotCost(int owned) {
    if (owned >= TASK_SLOTS_MAX) return -1;
    if (owned < TASK_SLOTS_START) return TASK_LICENSE_COST;
    static const long long PRICES[TASK_SLOTS_MAX - TASK_SLOTS_START] = {1500, 3000, 5000, 8000, 12000};
    return PRICES[owned - TASK_SLOTS_START];
}

std::shared_ptr<const TaskProgram> compileCondition(const std::string& text, TaskCheck& check) {
    check = TaskCheck{};
    if (trim(text).empty()) { check.error = "the condition is missing, like: tokens > 20t"; check.pos = 0; return nullptr; }
    try {
        auto prog = std::make_shared<TaskProgram>(Parser(text, nullptr, "predict").parseCondition());
        check.ok = true;
        check.instructions = (int)prog->nodes.size();
        return prog;
    } catch (const Fail& f) {
        check.error = f.what;
        check.pos = f.pos;
        check.len = f.len;
        return nullptr;
    }
}

std::shared_ptr<const TaskProgram> compileExpression(const std::string& text, TaskCheck& check, char& kind) {
    check = TaskCheck{};
    kind = 'a';
    try {
        auto prog = std::make_shared<TaskProgram>(Parser(text, nullptr, "eval").parseExpression(kind));
        check.ok = true;
        check.instructions = (int)prog->nodes.size();
        return prog;
    } catch (const Fail& f) {
        check.error = f.what;
        check.pos = f.pos;
        check.len = f.len;
        return nullptr;
    }
}

double expressionValue(const TaskProgram& p, GameState& g) { return eval(p, p.cond, g); }

bool conditionHolds(const TaskProgram& p, GameState& g) { return p.cond < 0 || eval(p, p.cond, g) != 0; }

std::vector<std::string> conditionValues(const TaskProgram& p) {
    std::vector<std::string> out;
    for (const TaskNode& n : p.nodes)
        if (n.kind == TaskNode::Val && std::find(out.begin(), out.end(), VALUES[n.val].name) == out.end()) out.push_back(VALUES[n.val].name);
    return out;
}

std::shared_ptr<const TaskProgram> compileTask(const std::string& text, const GameState* g, TaskCheck& check) {
    check = TaskCheck{};
    std::string t = trim(text);
    if (t.empty()) { check.error = "the task is empty"; check.pos = 0; return nullptr; }
    if (t.size() > TASK_TEXT_MAX) { check.error = "keep a task under " + std::to_string(TASK_TEXT_MAX) + " characters"; check.pos = (int)TASK_TEXT_MAX; return nullptr; }
    try {
        // Positions are reported against `text` itself, so the caret lines up with what was typed.
        auto prog = std::make_shared<TaskProgram>(Parser(text, g).parse());
        check.ok = true;
        check.instructions = prog->instructions;
        check.every = prog->mode == TaskMode::Every ? prog->every : 0;
        check.repeats = prog->mode == TaskMode::While;
        return prog;
    } catch (const Fail& f) {
        check.error = f.what;
        check.pos = f.pos;
        check.len = f.len;
        return nullptr;
    }
}

TaskCheck checkTask(const std::string& text, const GameState* g) {
    TaskCheck c;
    compileTask(text, g, c);
    return c;
}

long long taskFee(const GameState& g, int instructions) {
    return std::max(TASK_FEE_MIN, (long long)std::llround((double)instructions * TASK_FEE_SECONDS * std::max(0.0, g.incomeRate)));
}

long long taskFeeBudget(const GameState& g) {
    return std::max(10 * TASK_FEE_MIN, (long long)std::llround(TASK_FEE_BUDGET * g.incomeRate * 60.0));
}

long long taskFeesLastMinute(const GameState& g) {
    long long sum = 0;
    for (long long f : g.taskFeeWindow) sum += f;
    return sum;
}

Notes buyTaskLicense(GameState& g) {
    if (!isUnlocked(g, Feature::Tasks)) return {{Sev::Warn, lockedText(g, Feature::Tasks)}};
    if (g.taskSlots > 0) return {{Sev::Info, "You already have the Automation License."}};
    if (g.shards < TASK_LICENSE_COST)
        return {{Sev::Warn, "The Automation License costs " + num(TASK_LICENSE_COST) + " shards. You have " + num(g.shards) + "."}};
    g.shards -= TASK_LICENSE_COST;
    g.taskSlots = TASK_SLOTS_START;
    return {{Sev::Good, "Bought the Automation License: " + std::to_string(TASK_SLOTS_START) + " task slots. Write your first task in the Tasks tab, or 'task add ...'."}};
}

Notes buyTaskSlot(GameState& g) {
    Notes out;
    if (needLicense(g, out)) return out;
    long long cost = taskSlotCost(g.taskSlots);
    if (cost < 0) return {{Sev::Info, "You have every slot there is (" + std::to_string(TASK_SLOTS_MAX) + ")."}};
    if (g.shards < cost) return {{Sev::Warn, "Another task slot costs " + num(cost) + " shards. You have " + num(g.shards) + "."}};
    g.shards -= cost;
    g.taskSlots += 1;
    return {{Sev::Good, "Bought task slot " + std::to_string(g.taskSlots) + " for " + num(cost) + " shards."}};
}

Notes addTask(GameState& g, const std::string& text) {
    Notes out;
    if (needLicense(g, out)) return out;
    if ((int)g.tasks.size() >= g.taskSlots)
        return {{Sev::Warn, "All " + std::to_string(g.taskSlots) + " task slots are in use. Remove one, or buy another slot."}};
    TaskCheck c;
    auto prog = compileTask(text, &g, c);
    if (!prog) return rejected(c);
    Task t;
    t.text = trim(text);
    t.prog = prog;
    restart(g, t);
    g.tasks.push_back(t);
    return {{Sev::Good, "Task " + std::to_string(g.tasks.size()) + " added. Each run costs about " + compact(taskFee(g, c.instructions)) + " tokens."}};
}

Notes editTask(GameState& g, int idx, const std::string& text) {
    Notes out;
    if (needLicense(g, out) || badIndex(g, idx, out)) return out;
    TaskCheck c;
    auto prog = compileTask(text, &g, c);
    if (!prog) return rejected(c);
    Task& t = g.tasks[(size_t)idx];
    t.text = trim(text);
    t.prog = prog;
    t.error.clear();
    t.runs = t.fees = 0;
    t.lastRun = -1;
    restart(g, t);
    return {{Sev::Good, "Task " + std::to_string(idx + 1) + " changed."}};
}

Notes removeTask(GameState& g, int idx) {
    Notes out;
    if (badIndex(g, idx, out)) return out;
    g.tasks.erase(g.tasks.begin() + idx);
    return {{Sev::Info, "Task " + std::to_string(idx + 1) + " removed."}};
}

Notes setTaskOn(GameState& g, int idx, bool on) {
    Notes out;
    if (badIndex(g, idx, out)) return out;
    Task& t = g.tasks[(size_t)idx];
    if (t.on == on) return {{Sev::Info, "Task " + std::to_string(idx + 1) + " is already " + (on ? "on." : "off.")}};
    if (on && !t.error.empty()) return {{Sev::Warn, "Task " + std::to_string(idx + 1) + " can't run: " + t.error + ". Edit it first."}};
    t.on = on;
    if (on) restart(g, t);
    return {{Sev::Good, "Task " + std::to_string(idx + 1) + (on ? " is on." : " is off.")}};
}

Notes setTaskQuiet(GameState& g, int idx, bool quiet) {
    Notes out;
    if (badIndex(g, idx, out)) return out;
    g.tasks[(size_t)idx].quiet = quiet;
    return {{Sev::Good, "Task " + std::to_string(idx + 1) + (quiet ? " runs quietly now (no log lines)." : " says what it does in the log again.")}};
}

Notes moveTask(GameState& g, int idx, int to) {
    Notes out;
    if (badIndex(g, idx, out) || badIndex(g, to, out)) return out;
    Task t = g.tasks[(size_t)idx];
    g.tasks.erase(g.tasks.begin() + idx);
    g.tasks.insert(g.tasks.begin() + to, t);
    return {{Sev::Good, "Moved task " + std::to_string(idx + 1) + " to " + std::to_string(to + 1) + ". (Tasks run top to bottom.)"}};
}

void compileTasks(GameState& g) {
    for (Task& t : g.tasks) {
        TaskCheck c;
        t.prog = compileTask(t.text, nullptr, c);
        t.error = t.prog ? "" : c.error;
    }
}

void tasksTick(GameState& g, MessageLog& log) {
    g.taskFeeWindow[(size_t)(g.playSeconds % 60)] = 0;   // this second's share of the last minute starts empty
    if (g.taskSlots == 0 || !isUnlocked(g, Feature::Tasks)) return;
    for (size_t i = 0; i < g.tasks.size() && (int)i < g.taskSlots; ++i) {
        Task& t = g.tasks[i];
        if (!t.prog && t.error.empty()) {
            TaskCheck c;
            t.prog = compileTask(t.text, nullptr, c);
            t.error = t.prog ? "" : c.error;
        }
        if (!t.on || !t.prog) continue;
        std::shared_ptr<const TaskProgram> keep = t.prog;   // the action may not touch the task list, but be safe
        const TaskProgram& p = *keep;

        bool want = false;
        if (p.mode == TaskMode::Every) {
            if (g.playSeconds < t.nextAt) continue;
            t.nextAt = g.playSeconds + p.every;
            want = p.cond < 0 || eval(p, p.cond, g) != 0;
        } else {
            bool cond = p.cond < 0 || eval(p, p.cond, g) != 0;
            if (p.mode == TaskMode::When) {
                if (cond && !t.wasTrue) t.pending = true;
                if (!cond) t.pending = false;
                t.wasTrue = cond;
                want = t.pending;
            } else {
                want = cond;
            }
        }
        if (!want) { t.waiting.clear(); continue; }

        long long fee = taskFee(g, p.instructions);
        std::string why;
        int member = -1;
        if (g.tokens < fee) {   // say what's in the way of the action itself first, if anything is
            why = blocked(g, p.act, member);
            if (why.empty()) why = "can't pay the fee (" + compact(fee) + ")";
        } else if (taskFeesLastMinute(g) + fee > taskFeeBudget(g)) {
            why = "task fees hit their limit for this minute (a fifth of your income)";
        } else {
            g.tokens -= fee;
            why = blocked(g, p.act, member);
            if (!why.empty()) g.tokens += fee;
        }
        if (!why.empty()) { t.waiting = why; continue; }   // `when` stays pending; `every` tries again next interval

        Notes notes = perform(g, p.act, member);
        t.pending = false;
        t.waiting.clear();
        t.runs += 1;
        t.fees = addTokens(t.fees, fee);
        t.lastRun = g.playSeconds;
        g.taskFeeWindow[(size_t)(g.playSeconds % 60)] += fee;
        if (t.quiet && p.act.kind != TaskAction::Notify) continue;
        for (size_t k = 0; k < notes.size(); ++k) {
            std::string line = (k == 0 ? "Task " + std::to_string(i + 1) + ": " : "  ") + notes[k].text;
            if (k == 0 && p.act.kind != TaskAction::Notify) {
                if (!line.empty() && line.back() == '.') line.pop_back();
                line += " (fee " + compact(fee) + ").";
            }
            log.push(notes[k].sev, line);
        }
    }
}

std::string taskStatus(const GameState& g, const Task& t) {
    if (!t.error.empty()) return "can't run: " + t.error;
    if (!t.on) return "off";
    std::string s;
    if (!t.waiting.empty()) s = "waiting: " + t.waiting;
    else if (t.runs == 0) s = "hasn't run yet";
    else s = "ran " + std::to_string(t.runs) + (t.runs == 1 ? " time" : " times") + ", last " +
             (g.playSeconds - t.lastRun < 2 ? std::string("just now") : duration(g.playSeconds - t.lastRun) + " ago");
    if (t.fees > 0) s += "; " + compact(t.fees) + " in fees";
    if (t.prog && t.prog->mode == TaskMode::Every && t.nextAt > g.playSeconds) s += "; next check in " + duration(t.nextAt - g.playSeconds);
    return s;
}

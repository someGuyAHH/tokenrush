#pragma once
// Tasks: one-line rules that run Console-style actions on their own (the late-game automation).
//
//   when shards >= 5k and home > 0 do send anyone deep
//   while broken do repair
//   every 10m if tokens >= 5 * cost.pickaxe do upgrade pickaxe 10
//
// The three ways a task can trigger:
//   when <condition> do <action>     the second the condition turns true (false -> true). If the
//                                    action can't happen then, it waits while the condition stays true.
//   while <condition> do <action>    every second the condition is true and the action can happen
//   every <time> [if <condition>] do <action>
//                                    once per interval, if the condition holds and the action can happen
//
// A task never runs an action that can't happen, and never pays for one; it waits, and says why.
// It reads only the numbers listed in TASK_VALUES (what the screen already shows), and does only
// what TASK_ACTIONS lists. Each run costs a fee: instructions x TASK_FEE_SECONDS of income.
// Tasks run once a second, top to bottom, and only while the game is open.

#include <memory>
#include <string>
#include <vector>

#include "state.hpp"

// ----- Prices and limits -----
inline constexpr long long TASK_LICENSE_COST = 7500;   // shards, once: the Automation License, with 3 slots
inline constexpr int TASK_SLOTS_START = 3;
inline constexpr int TASK_SLOTS_MAX = 8;
inline constexpr double TASK_FEE_SECONDS = 0.3;        // income-seconds per instruction, each run
inline constexpr long long TASK_FEE_MIN = 10;          // tokens
inline constexpr double TASK_FEE_BUDGET = 0.20;        // fees never take more than this share of a minute's income
inline constexpr double TASK_STARTUP_SECONDS = 1.0;    // income-seconds per instruction, to start a task: adding it,
inline constexpr long long TASK_STARTUP_MIN = 100;     // saving an edit while it's on, or resuming it (suspending is free)
inline constexpr size_t TASK_TEXT_MAX = 160;
inline constexpr int TASK_EVERY_MIN = 5;               // seconds
inline constexpr int TASK_EVERY_MAX = 24 * 3600;
inline constexpr size_t TASK_NOTE_MAX = 80;            // a notify message

long long taskSlotCost(int slotsOwned);                // shards for the next slot (-1 at the maximum)

// ----- The language, for the help pages and the Tasks tab -----
struct TaskValueInfo { const char* name; const char* what; };
struct TaskActionInfo { const char* usage; const char* what; };
extern const TaskValueInfo TASK_VALUES[];
extern const int TASK_VALUE_COUNT;
extern const TaskActionInfo TASK_ACTIONS[];
extern const int TASK_ACTION_COUNT;

// What compiling a task's text found: a mistake (with where it is), or what the task costs.
struct TaskCheck {
    bool ok = false;
    std::string error;       // in plain words
    int pos = -1;            // where in the text (a character index), -1 = nowhere in particular
    int len = 1;             // how much of it
    int instructions = 0;    // what the fee is counted in
    int every = 0;           // seconds, for `every` tasks (0 otherwise)
    bool repeats = false;    // a `while` task: can run every second
};

// `g` (optional) lets the check catch names that don't exist, like a crew member you don't have.
TaskCheck checkTask(const std::string& text, const GameState* g = nullptr);
std::shared_ptr<const TaskProgram> compileTask(const std::string& text, const GameState* g, TaskCheck& check);
long long taskFee(const GameState& g, int instructions);   // tokens, per run, at today's income
long long taskStartupCost(const GameState& g, int instructions);   // tokens, once, each time it starts
long long taskStartupCost(const GameState& g, const Task& t);       // ...for a task as it is

// `predict when <condition>`: the same language, a condition alone, plus three values only
// predict reads (earned, mines, jackpots).
std::shared_ptr<const TaskProgram> compileCondition(const std::string& text, TaskCheck& check);
bool conditionHolds(const TaskProgram& p, GameState& g);
std::vector<std::string> conditionValues(const TaskProgram& p);   // the values it reads, by name

// `eval <expression>`: the same language (predict's values too), worked out right now. `kind` says
// what it comes to: 'b' true or false, 't' a time (seconds), 'a' a number or an amount.
std::shared_ptr<const TaskProgram> compileExpression(const std::string& text, TaskCheck& check, char& kind);
double expressionValue(const TaskProgram& p, GameState& g);

// ----- Managing tasks (each returns what to tell the player) -----
Notes buyTaskLicense(GameState& g);
Notes buyTaskSlot(GameState& g);
Notes addTask(GameState& g, const std::string& text);
Notes editTask(GameState& g, int idx, const std::string& text);
Notes removeTask(GameState& g, int idx);
Notes setTaskOn(GameState& g, int idx, bool on);   // off: suspended (free); on: resumed (its startup cost)
Notes setTaskQuiet(GameState& g, int idx, bool quiet);
Notes moveTask(GameState& g, int idx, int to);

void tasksTick(GameState& g, MessageLog& log);          // once per second (worldTick)
void compileTasks(GameState& g);                        // after loading: compile every task's text
std::string taskStatus(const GameState& g, const Task& t);   // "ran 14x, last 2m ago; 5.6B in fees" / "waiting: ..."
long long taskFeesLastMinute(const GameState& g);
long long taskFeeBudget(const GameState& g);           // fees stop here, per minute (a fifth of income)

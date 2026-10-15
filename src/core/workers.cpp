#include "workers.hpp"

#include <chrono>
#include <random>

#include "crew.hpp"
#include "game.hpp"
#include "market.hpp"
#include "save.hpp"
#include "ticks.hpp"
#include "util.hpp"

using namespace std::chrono_literals;

void worldThread(std::stop_token st, Shared& s) {
    std::mt19937 rng(makeSeed());
    while (sleepFor(st, 1s)) {
        std::lock_guard lk(s.m);
        worldTick(s.g, s.log, nowSec(), rng);
    }
}

void marketThread(std::stop_token st, Shared& s) {
    std::mt19937 rng(makeSeed() ^ 0x85ebca6bu);
    while (sleepFor(st, 1s)) {
        std::lock_guard lk(s.m);
        marketTick(s.g, s.log, rng);
    }
}

void automationThread(std::stop_token st, Shared& s) {
    std::mt19937 rng(makeSeed() ^ 0x9e3779b9u);
    AutomationClocks clocks;
    while (sleepFor(st, 1s)) {
        std::lock_guard lk(s.m);
        automationTick(s.g, s.log, nowSec(), rng, clocks);
    }
}

void crewThread(std::stop_token st, Shared& s) {
    std::mt19937 rng(makeSeed() ^ 0xc2b2ae35u);
    while (sleepFor(st, 1s)) {
        std::lock_guard lk(s.m);
        crewTick(s.g, s.log, nowSec(), rng);
    }
}

void autosaveThread(std::stop_token st, Shared& s) {
    while (sleepFor(st, std::chrono::seconds(AUTOSAVE_SECONDS))) {
        if (!saveNow(s)) s.log.push(Sev::Bad, "Autosave FAILED.");
    }
}

#pragma once
// Everything the player can DO, with no printing and no input. Each action takes the state
// (the caller holds Shared::m), changes it, and reports back. The front end turns the results
// into colored text; a full-screen UI can turn the same results into buttons and toasts.

#include <array>
#include <ctime>
#include <random>

#include "state.hpp"

// ----- Mining -----
struct MineResult {
    long long gain = 0;
    int base = 0;
    double mult = 1.0;
    bool jackpot = false;
    bool bonus = false;
    bool meteor = false;   // Meteor Shower doubled it
};
MineResult mine(GameState& g, std::time_t t, std::mt19937& rng);

// ----- Shop -----
Notes buyUpgrade(GameState& g, size_t idx);   // index into upgrades()
Notes buyUpgrades(GameState& g, size_t idx, int n);      // up to n levels, as many as you can afford
long long upgradesCost(const GameState& g, size_t idx, int n); // what the next n levels cost together
Notes buyLicense(GameState& g);
Notes setAuto(GameState& g, bool on);

// ----- Collectibles and cosmetics -----
Notes buyCurio(GameState& g, int idx);
Notes choosePrompt(GameState& g, int idx);   // buys it first if needed; idx PROMPT_COUNT = your custom prompt

// Themes: bought once with shards (the free ones are everyone's), then switch freely.
// idx CUSTOM_THEME is the profile's custom theme.
bool themeOwned(const GameState& g, int idx);
Notes chooseTheme(GameState& g, int idx);   // buys it first if needed (the custom one starts as a copy of the current theme)

// The custom theme: bought once (CUSTOM_THEME_COST shards), starting as a copy of a theme you own;
// after that every color can be changed for free.
Notes buyCustomTheme(GameState& g, int base);
Notes setCustomColor(GameState& g, Hue h, Rgb c);
Notes copyIntoCustom(GameState& g, int base);   // start over from another theme's colors
bool parseHexColor(const std::string& text, Rgb& out);   // "#ff8800", "ff8800" or "#f80"
std::string hexColor(Rgb c);                             // "#ff8800"

// The custom prompt: any 1-5 printable characters, for 30x the priciest prompt style.
// Buying it is a one-off; after that, changing the text is free.
inline constexpr size_t CUSTOM_PROMPT_MAX = 5;
long long customPromptCost();
bool validCustomPrompt(const std::string& text, std::string& why);
Notes setCustomPrompt(GameState& g, const std::string& text);
std::string promptOf(const GameState& g);    // what to print before the cursor, e.g. "> "

// The prompt's color: bought once with shards (4x the custom prompt's price, in shards at their
// fair price), then free to change. Any prompt style, custom or not, is drawn in it.
long long promptColorCost();
Notes setPromptColor(GameState& g, const std::string& text);   // "#ff8800" (buying it the first time), or "off" / "on"

// ----- The Workshop (gadgets) -----
Notes craftGadget(GameState& g, int idx);     // build it (needs its curio and shards); equipped if a slot is free
Notes equipGadget(GameState& g, int idx);
Notes unequipGadget(GameState& g, int idx);
int freeGadgetSlot(const GameState& g);       // -1 when all are taken

// ----- The Monolith -----
struct BuildResult {
    Notes notes;
    long long poured = 0;
    bool finished = false;   // this build completed the whole Monolith
};
BuildResult buildMonolith(GameState& g, long long amount);

// ----- The Orrery (shards) -----
long long orreryRemaining(const GameState& g);              // shards left for the current stage (0 when done)
long long orrerySpareShards(const GameState& g);            // shards you can put in without missing the next payday
BuildResult buildOrrery(GameState& g, long long shards);    // `finished` = the whole Orrery just completed

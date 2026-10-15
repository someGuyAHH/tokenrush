#pragma once
// The Constellations: what's left to do once the Orrery is complete. Each one is a hard feat;
// doing it lights its stars on the sky map. They give no power, just the picture and the record.
//
// Some feats are noticed as they happen (spotStar from the action that did it); others are
// about how things stand, and starsCheck looks at the state. Either way a constellation only
// lights once the Constellations are open.

#include <string>
#include <vector>

#include "state.hpp"

enum class Constellation : int {
    Miner, Twins, Hearth, Scales, Lantern, Wanderer, Crown, Phoenix, Hourglass, Magpie, Count,
};
inline constexpr int CONSTELLATION_COUNT = (int)Constellation::Count;

struct StarPoint { int x, y; };
struct ConstellationInfo {
    const char* id;          // what the save file uses
    const char* name;
    const char* feat;        // what lights it
    Hue hue;                 // its color on the map, from the theme
    std::vector<StarPoint> stars;   // inside its patch of sky (SKY_SLOT_W x SKY_STAR_ROWS)
};
extern const ConstellationInfo CONSTELLATIONS[];   // indexed by Constellation

// The sky map: two bands of five constellations, each in a patch SKY_SLOT_W wide, with the
// name under its stars.
inline constexpr int SKY_SLOT_W = 14;
inline constexpr int SKY_STAR_ROWS = 4;
inline constexpr int SKY_W = SKY_SLOT_W * 5;
inline constexpr int SKY_H = (SKY_STAR_ROWS + 1) * 2;

enum class SkyCell { Empty, Dust, Star, Label };
struct SkyGlyph {
    std::string text = " ";
    SkyCell kind = SkyCell::Empty;
    int constellation = -1;
    bool lit = false;
};
std::vector<std::vector<SkyGlyph>> skyMap(const GameState& g);   // SKY_H rows of SKY_W glyphs

inline constexpr long long HOURGLASS_TOKENS = 1'000'000'000;   // in one hour, hands off the mine
inline constexpr int HEARTH_PAYDAYS = 10;
inline constexpr int LANTERN_STOPS = 3;
inline constexpr int CROWN_CLIENTS = 3;
inline constexpr int PHOENIX_SECONDS = 20 * 60;

bool starLit(const GameState& g, Constellation c);
void spotStar(GameState& g, Constellation c);         // an action just did a feat (lights on the next starsCheck)
std::string starProgress(const GameState& g, int c);  // "4/10 paydays", or "" when there's nothing to count
void starsCheck(GameState& g, MessageLog& log);       // light whatever was done (called from checkGoals)
void hourglassTick(GameState& g);                     // once per second: the hands-off hour

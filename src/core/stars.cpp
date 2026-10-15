#include "stars.hpp"

#include <algorithm>
#include <bit>

#include "crew.hpp"
#include "game.hpp"
#include "util.hpp"

// Star positions are inside each constellation's patch: x 0..SKY_SLOT_W-2, y 0..SKY_STAR_ROWS-1.
const ConstellationInfo CONSTELLATIONS[] = {
    {"miner",     "The Miner",     "Reach a streak of 250 during a Meteor Shower",                         Hue::Tokens,
     {{1, 0}, {4, 1}, {7, 2}, {10, 3}, {5, 3}}},
    {"twins",     "The Twins",     "Finish two contracts within 10 seconds of each other",                 Hue::Shards,
     {{2, 0}, {2, 3}, {9, 0}, {9, 3}}},
    {"hearth",    "The Hearth",    "Keep a crew of 5 through 10 paydays in a row, nobody leaving",         Hue::Heat,
     {{2, 3}, {2, 1}, {6, 0}, {10, 1}, {10, 3}}},
    {"scales",    "The Scales",    "Sell 100+ shards in one order at 3x the fair price or better",         Hue::Accent,
     {{6, 0}, {2, 1}, {10, 1}, {2, 3}, {10, 3}}},
    {"lantern",   "The Lantern",   "Stop 3 disasters in a row",                                            Hue::Warn,
     {{6, 0}, {4, 1}, {8, 1}, {6, 2}, {4, 3}, {8, 3}}},
    {"wanderer",  "The Wanderer",  "Have 3 crew members away on deep expeditions at once",                 Hue::Good,
     {{1, 3}, {4, 1}, {7, 2}, {10, 0}}},
    {"crown",     "The Crown",     "Finish signature jobs for 3 different clients",                        Hue::Event,
     {{1, 1}, {3, 0}, {6, 1}, {9, 0}, {11, 1}, {6, 3}}},
    {"phoenix",   "The Phoenix",   "Bring your heat from 10 down to 0 within 20 minutes",                  Hue::Bad,
     {{6, 0}, {2, 1}, {10, 1}, {6, 2}, {3, 3}, {9, 3}}},
    {"hourglass", "The Hourglass", "Earn 1B tokens in an hour without mining by hand",                     Hue::Infected,
     {{2, 0}, {10, 0}, {6, 1}, {6, 2}, {2, 3}, {10, 3}}},
    {"magpie",    "The Magpie",    "Own every curio, the Dealer's too",                                    Hue::Text,
     {{1, 2}, {4, 1}, {8, 1}, {11, 2}, {6, 3}}},
};
static_assert(std::size(CONSTELLATIONS) == CONSTELLATION_COUNT);

bool starLit(const GameState& g, Constellation c) { return hasBit(g.starsLit, (int)c); }
void spotStar(GameState& g, Constellation c) { g.starsFound |= 1u << (int)c; }

namespace {

int deepAway(const GameState& g) {
    return (int)std::count_if(g.crew.begin(), g.crew.end(), [](const CrewMember& c) { return isAway(c) && c.trip == 2; });
}

}  // namespace

std::string starProgress(const GameState& g, int c) {
    switch ((Constellation)c) {
        case Constellation::Hearth:   return std::to_string(std::min(g.fullCrewPaydays, HEARTH_PAYDAYS)) + "/" + std::to_string(HEARTH_PAYDAYS) + " paydays";
        case Constellation::Lantern:  return std::to_string(std::min(g.stoppedInRow, LANTERN_STOPS)) + "/" + std::to_string(LANTERN_STOPS) + " in a row";
        case Constellation::Wanderer: return std::to_string(deepAway(g)) + "/3 away";
        case Constellation::Crown:    return std::to_string(std::popcount(g.signatureClients)) + "/" + std::to_string(CROWN_CLIENTS) + " clients";
        case Constellation::Hourglass: {
            long long mins = std::max(0LL, g.playSeconds - g.handsOffSince) / 60;
            return std::to_string(std::min(mins, 60LL)) + "/60 min, " + compact(std::max(0LL, g.lifetimeEarned - g.handsOffFrom)) + "/" +
                   compact(HOURGLASS_TOKENS);
        }
        case Constellation::Magpie:   return std::to_string(std::popcount(g.curiosOwned)) + "/" + std::to_string(CURIO_COUNT) + " curios";
        default:                      return "";
    }
}

void hourglassTick(GameState& g) {
    // One hour at a time, hands off the mine the whole way. Mining (or a failed hour) starts it over.
    bool reset = g.manualMines != g.handsOffMines || g.handsOffSince > g.playSeconds;
    if (!reset && g.playSeconds - g.handsOffSince >= 3600) {
        if (g.lifetimeEarned - g.handsOffFrom >= HOURGLASS_TOKENS) spotStar(g, Constellation::Hourglass);
        reset = true;
    }
    if (reset) {
        g.handsOffSince = g.playSeconds;
        g.handsOffFrom = g.lifetimeEarned;
        g.handsOffMines = g.manualMines;
    }
}

void starsCheck(GameState& g, MessageLog& log) {
    // The feats about how things stand.
    if (deepAway(g) >= 3) spotStar(g, Constellation::Wanderer);
    if (std::popcount(g.signatureClients) >= CROWN_CLIENTS) spotStar(g, Constellation::Crown);
    if (std::popcount(g.curiosOwned) >= CURIO_COUNT) spotStar(g, Constellation::Magpie);
    if (g.fullCrewPaydays >= HEARTH_PAYDAYS) spotStar(g, Constellation::Hearth);
    if (g.stoppedInRow >= LANTERN_STOPS) spotStar(g, Constellation::Lantern);
    if (g.heat >= 10) g.heat10At = g.playSeconds;
    if (g.heat == 0 && g.heat10At >= 0) {
        if (g.playSeconds - g.heat10At <= PHOENIX_SECONDS) spotStar(g, Constellation::Phoenix);
        g.heat10At = -1;
    }

    std::uint32_t fresh = g.starsFound & ~g.starsLit;
    g.starsFound = 0;
    if (!isUnlocked(g, Feature::Stars)) return;   // feats only count once there's a sky to light
    for (int i = 0; i < CONSTELLATION_COUNT; ++i) {
        if (!hasBit(fresh, i)) continue;
        g.starsLit |= 1u << i;
        log.push(Sev::Event, std::string("CONSTELLATION: ") + CONSTELLATIONS[i].name + " lights up! (" + CONSTELLATIONS[i].feat + ". " +
                             std::to_string(std::popcount(g.starsLit)) + "/" + std::to_string(CONSTELLATION_COUNT) + ")");
    }
}

std::vector<std::vector<SkyGlyph>> skyMap(const GameState& g) {
    std::vector<std::vector<SkyGlyph>> sky((size_t)SKY_H, std::vector<SkyGlyph>((size_t)SKY_W));
    // A little dust between the constellations, always in the same places.
    unsigned seed = 0x2545F491u;
    for (int i = 0; i < 14; ++i) {
        seed = seed * 1103515245u + 12345u;
        int x = (int)(seed >> 8) % SKY_W, y = (int)(seed >> 20) % SKY_H;
        if (y % (SKY_STAR_ROWS + 1) == SKY_STAR_ROWS) continue;   // keep the label rows clear
        sky[(size_t)y][(size_t)x] = {".", SkyCell::Dust, -1, false};
    }
    for (int c = 0; c < CONSTELLATION_COUNT; ++c) {
        const ConstellationInfo& info = CONSTELLATIONS[c];
        bool lit = hasBit(g.starsLit, c);
        int x0 = (c % 5) * SKY_SLOT_W + 1, y0 = (c / 5) * (SKY_STAR_ROWS + 1);
        for (const StarPoint& p : info.stars)
            sky[(size_t)(y0 + p.y)][(size_t)(x0 + p.x)] = {lit ? "*" : "+", SkyCell::Star, c, lit};   // unlit: the shape, faintly
        std::string name = info.name;
        int lx = x0 + std::max(0, (SKY_SLOT_W - 2 - (int)name.size()) / 2);
        for (size_t k = 0; k < name.size() && lx + (int)k < SKY_W; ++k)
            sky[(size_t)(y0 + SKY_STAR_ROWS)][(size_t)(lx + (int)k)] = {std::string(1, name[k]), SkyCell::Label, c, lit};
    }
    return sky;
}

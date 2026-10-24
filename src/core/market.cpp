#include "market.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "crew.hpp"
#include "game.hpp"
#include "stars.hpp"
#include "util.hpp"

namespace {

// "164.2", or "27.2K" once a price runs into the thousands (log lines stay short).
std::string priceText(double p) { return p < 1000.0 ? fixed(p, 1) : compact(std::llround(p)); }


constexpr double BASE_SPREAD     = 0.05;
constexpr double SPREAD_PER_HEAT = 0.02;
constexpr double SPREAD_PER_BROKER_LEVEL = 0.006;
constexpr double MIN_SPREAD      = 0.01;
constexpr double BASE_IMPACT     = 0.0025;
constexpr double REVERSION       = 0.004;   // per second, toward the fair price (log space)
constexpr double VOLATILITY      = 0.012;   // per second (log space)
constexpr double SHOCK_CHANCE    = 1.0 / 450.0;
constexpr double REPORT_MOVE     = 0.12;    // log a move once the price is 12% away from the last report
constexpr double MIN_PRICE       = 10.0;
constexpr double MAX_PRICE       = 100'000.0;
constexpr int    HISTORY_EVERY   = 5;       // seconds between chart samples
constexpr size_t HISTORY_LEN     = 60;      // so the chart covers 5 minutes

long long clampToLL(double v) {
    if (!(v < 9.0e18)) return std::numeric_limits<long long>::max();
    return (long long)v;
}

}  // namespace

double marketSpread(const GameState& g) {
    double s = BASE_SPREAD + SPREAD_PER_HEAT * g.heat - SPREAD_PER_BROKER_LEVEL * roleLevels(g, Role::Broker);
    s = std::max(MIN_SPREAD, s);
    return infected(g, Strain::Static) ? 2.0 * s : s;
}

double marketImpact(const GameState& g) {
    double cut = std::min(0.5, 0.08 * roleLevels(g, Role::Broker));
    return BASE_IMPACT * (1.0 - cut) * (gadgetOn(g, Gadget::MoonAnchor) ? 0.25 : 1.0);   // your orders barely move it
}

// The Golden Ledger cooks the books both ways: 15% more for what you sell, 15% more for what you buy.
static double ledger(const GameState& g) { return gadgetOn(g, Gadget::GoldenLedger) ? 1.15 : 1.0; }
double buyPrice(const GameState& g)  { return g.shardPrice * (1.0 + marketSpread(g) / 2.0) * ledger(g); }
double sellPrice(const GameState& g) { return g.shardPrice * (1.0 - marketSpread(g) / 2.0) * ledger(g); }

// Each shard moves the price by a factor r, so a big order is a geometric series.
long long buyCost(const GameState& g, long long n) {
    if (n <= 0) return 0;
    double r = 1.0 + marketImpact(g);
    double total = buyPrice(g) * (std::pow(r, (double)n) - 1.0) / (r - 1.0);
    return clampToLL(std::ceil(total));
}

long long sellValue(const GameState& g, long long n) {
    if (n <= 0) return 0;
    double r = 1.0 + marketImpact(g);
    double total = sellPrice(g) * (1.0 - std::pow(r, -(double)n)) / (1.0 - 1.0 / r);
    return clampToLL(std::floor(total));
}

long long maxAffordable(const GameState& g) {
    long long lo = 0, hi = std::min(MARKET_MAX_ORDER, g.marketStock);
    while (lo < hi) {
        long long mid = (lo + hi + 1) / 2;
        if (buyCost(g, mid) <= g.tokens) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static_assert(std::size(MARKET_STAGE_SCALE) == ORRERY_STAGE_COUNT + 1);
static double stageScale(const GameState& g) { return MARKET_STAGE_SCALE[std::clamp(g.orreryStage, 0, ORRERY_STAGE_COUNT)]; }
long long marketStockMax(const GameState& g) { return std::llround((double)MARKET_STOCK_MAX * stageScale(g)); }
long long marketRestock(const GameState& g) { return std::llround((double)MARKET_RESTOCK_PER_SEC * stageScale(g)); }

std::string restockText(const GameState& g) {
    return "it restocks " + std::to_string(marketRestock(g)) + " a second (" + compact(marketRestock(g) * 3600) + " an hour)";
}

bool pricyOrder(const GameState& g, long long n) {
    return n > 0 && (double)buyCost(g, n) / (double)n > 3.0 * buyPrice(g);
}

Notes buyShards(GameState& g, long long n) {
    if (!isUnlocked(g, Feature::Market)) return {{Sev::Warn, lockedText(g, Feature::Market)}};
    if (n <= 0) return {{Sev::Warn, "Buy how many?"}};
    if (n > MARKET_MAX_ORDER) return {{Sev::Warn, "The market only takes orders up to " + num(MARKET_MAX_ORDER) + " shards."}};
    if (n > g.marketStock) {
        if (g.marketStock == 0) return {{Sev::Warn, "The market is sold out: " + restockText(g) + "."}};
        return {{Sev::Warn, "The market only has " + count(g.marketStock, "shard") + " for sale; " + restockText(g) + "."}};
    }
    long long cost = buyCost(g, n);
    if (cost > g.tokens) {
        return {{Sev::Warn, count(n, "shard") + " would cost " + num(cost) + " tokens. You have " + num(g.tokens) +
                            " (enough for " + num(maxAffordable(g)) + ")."}};
    }

    g.tokens -= cost;
    g.shards += n;
    g.marketStock -= n;
    g.shardsTraded += n;
    g.shardsBought += n;
    g.tokensOnBuys = addTokens(g.tokensOnBuys, cost);
    g.tradeCost = addTokens(g.tradeCost, std::max(0LL, cost - toTokens(g.shardPrice * (double)n)));
    g.shardPrice = std::clamp(g.shardPrice * std::pow(1.0 + marketImpact(g), (double)n), MIN_PRICE, MAX_PRICE);
    g.lastReportedPrice = g.shardPrice;   // your own trade isn't news
    return {{Sev::Good, "Bought " + count(n, "shard") + " for " + compact(cost) + " tokens (avg " +
                        priceText((double)cost / (double)n) + "). Price is now " + priceText(g.shardPrice) + "."}};
}

Notes sellShards(GameState& g, long long n) {
    if (!isUnlocked(g, Feature::Market)) return {{Sev::Warn, lockedText(g, Feature::Market)}};
    if (n <= 0) return {{Sev::Warn, "Sell how many?"}};
    if (n > g.shards) return {{Sev::Warn, "You only have " + count(g.shards, "shard") + "."}};
    if (n > MARKET_MAX_ORDER) return {{Sev::Warn, "The market only takes orders up to " + num(MARKET_MAX_ORDER) + " shards."}};

    long long value = sellValue(g, n);
    g.shards -= n;
    g.marketStock = std::min(marketStockMax(g), g.marketStock + n);   // yours to buy back
    g.tokens = addTokens(g.tokens, value);   // not earn(): turning shards back into tokens isn't new income
    g.shardsTraded += n;
    g.shardsSold += n;
    g.tokensFromSells = addTokens(g.tokensFromSells, value);
    g.tradeCost = addTokens(g.tradeCost, std::max(0LL, toTokens(g.shardPrice * (double)n) - value));
    if (n >= 100 && (double)value >= 3.0 * SHARD_FAIR_PRICE * (double)n) spotStar(g, Constellation::Scales);
    g.shardPrice = std::clamp(g.shardPrice / std::pow(1.0 + marketImpact(g), (double)n), MIN_PRICE, MAX_PRICE);
    g.lastReportedPrice = g.shardPrice;
    return {{Sev::Good, "Sold " + count(n, "shard") + " for " + compact(value) + " tokens (avg " +
                        priceText((double)value / (double)n) + "). Price is now " + priceText(g.shardPrice) + "."}};
}

void marketTick(GameState& g, MessageLog& log, std::mt19937& rng) {
    std::normal_distribution<double> noise(0.0, 1.0);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    g.marketStock = std::min(marketStockMax(g), g.marketStock + marketRestock(g));

    double lp = std::log(std::max(g.shardPrice, MIN_PRICE));
    double calm = gadgetOn(g, Gadget::MoonAnchor) ? 2.0 : 1.0;   // ...but the tides pull twice as hard
    double vol = (infected(g, Strain::Static) ? 2.0 * VOLATILITY : VOLATILITY) * calm;
    lp += REVERSION * (std::log(SHARD_FAIR_PRICE) - lp) + vol * noise(rng);

    bool news = isUnlocked(g, Feature::Market);   // nobody cares about the price before the market opens
    if (uni(rng) < SHOCK_CHANCE) {
        double size = (0.15 + 0.20 * uni(rng)) * calm;
        bool up = uni(rng) < 0.5;
        lp += up ? std::log(1.0 + size) : std::log(1.0 - size);
        g.shardPrice = std::clamp(std::exp(lp), MIN_PRICE, MAX_PRICE);
        g.lastReportedPrice = g.shardPrice;
        if (news) log.push(Sev::Event, std::string(up ? "MARKET: Shard rush! Price jumps to " : "MARKET: Crash! Price drops to ") +
                             priceText(g.shardPrice) + ".", LogKind::Market);
    } else {
        g.shardPrice = std::clamp(std::exp(lp), MIN_PRICE, MAX_PRICE);
        double move = g.shardPrice / g.lastReportedPrice - 1.0;
        if (std::abs(move) >= REPORT_MOVE) {
            if (news) log.push(Sev::Info, std::string("Market: shard price ") + (move > 0 ? "rose" : "fell") +
                                " to " + priceText(g.shardPrice) + ".", LogKind::Market);
            g.lastReportedPrice = g.shardPrice;
        }
    }

    if (++g.historyClock >= HISTORY_EVERY) {
        g.historyClock = 0;
        g.priceHistory.push_back(g.shardPrice);
        if (g.priceHistory.size() > HISTORY_LEN) g.priceHistory.pop_front();
    }
}

#pragma once
// Static game data: events, curios, prompt styles, the Monolith, goals, titles, crew and contracts.
// The tables live in content.cpp. The *_COUNT constants here are checked against the
// real table sizes with static_asserts there, so a forgotten update fails the build.

#include <string>
#include <vector>

// ----- Events -----
enum EventId : int { EV_NONE = 0, EV_METEOR = 1, EV_OVERCLOCK = 2, EV_GOLDRUSH = 3, EV_PAYDAY = 4, EV_COUNT = 5 };

struct EventInfo { const char* name; const char* desc; int duration; };
extern const EventInfo EVENTS[];   // indexed by EventId

struct EventFx { double mining = 1.0, passive = 1.0, odds = 1.0, crew = 1.0; };
EventFx eventFx(int ev);

// ----- Curios (collectibles, pure flex) -----
// The first three sets are in the shop. The last one, Under the Counter, isn't: those come from
// the Curio Dealer (an offer) or turn up on crew expeditions, and cost is what they're worth.
inline constexpr int CURIO_SET_COUNT = 4;
inline constexpr int CURIO_COUNT = 16;
inline constexpr int SHOP_CURIO_COUNT = 12;        // CURIOS[0..11] are in the shop
inline constexpr int DEALER_SET = 3;
struct CurioInfo { const char* name; int set; long long cost; const char* flavor; };
inline bool inShop(int curio) { return curio < SHOP_CURIO_COUNT; }
std::string theCurio(int curio);   // "the Lava Lamp", "the First Token Ever", "a Spare Moon": for the middle of a sentence
extern const char* const CURIO_SETS[];
extern const CurioInfo CURIOS[];

// ----- Prompt styles (cosmetic) -----
inline constexpr int PROMPT_COUNT = 6;
struct PromptStyle { const char* id; const char* text; long long cost; };   // id is what the save file uses
extern const PromptStyle PROMPTS[];

// ----- Themes (cosmetic, per profile) -----
// Every color the two front ends use comes from a theme's palette of roles. Classic is the
// original look: the front ends keep their original terminal colors for it (so it looks the
// same on any terminal); every other theme is 24-bit color (ESC[38;2;r;g;bm), background included
// in the full-screen edition.
struct Rgb { unsigned char r, g, b; };
enum class Hue : int {
    Text, Muted, Accent, Good, Bad, Warn, Event, Tokens, Shards, Heat,
    Background, Panel, Button, ButtonText, ButtonOff, ButtonOffText, Infected, Count,
};
inline constexpr int HUE_COUNT = (int)Hue::Count;
inline constexpr int THEME_COUNT = 6;
struct ThemeInfo {
    const char* id;       // what the save file uses
    const char* name;
    long long cost;       // in shards; 0 = free for everyone
    const char* blurb;
    bool classic;         // the original look (the front ends use their old color codes)
    Rgb hue[HUE_COUNT];   // indexed by Hue
};
extern const ThemeInfo THEMES[];

// ----- The Monolith (endgame megaproject) -----
inline constexpr int MONO_STAGE_COUNT = 5;
struct MonoStage { const char* name; long long cost; const char* art[2]; };
extern const MonoStage MONO[];

// ----- The Orrery (late-game megaproject, built with shards) -----
// Every stage unlocks a perk. Shards are the point: buying lots of them at once drives the
// price up exponentially, so token wealth can't rush it; time (and the market) does.
inline constexpr int ORRERY_STAGE_COUNT = 5;
struct OrreryStage {
    const char* name;
    long long cost;      // in shards
    const char* perk;    // what finishing the stage unlocks
    int labelRow;        // the picture row its name is printed next to
};
extern const OrreryStage ORRERY[];

// The Orrery is one picture. Each row comes with a mask of the same length (in characters,
// not bytes) saying which stage, '1' to '5', draws that character; ' ' is empty space.
inline constexpr int ORRERY_ART_ROWS = 12;
struct ArtRow { const char* glyphs; const char* stages; };
extern const ArtRow ORRERY_ART[];

// The picture, ready to draw: each row is split into runs of characters that look alike.
// Stages not built yet show as a dotted outline (and the Sun's stars stay dark).
enum class ArtKind { Blank, Line, Planet, Star, Core };
struct ArtRun {
    std::string text;
    int stage = -1;       // 0-based; -1 for empty space
    bool built = false;
    ArtKind kind = ArtKind::Blank;
};
std::vector<std::vector<ArtRun>> orreryPicture(int stagesBuilt);

// ----- Goals, renown, titles -----
enum class Stat {
    Lifetime, ManualMines, Jackpots, BestStreak, BotTotal,
    EventsSeen, CuriosOwned, MonolithStage, UpgradeLevels, PlayMinutes,
    ShardsTraded, ContractsDone, CrewHired, CrewMined, OrreryStage, CrewSize,
    BestStanding, ShopCurios, GadgetsBuilt, Expeditions,
};

struct Goal { const char* name; const char* desc; Stat stat; long long target; int renown; };

// Secret goals: hidden until you stumble into them (only a hint shows), and they pay shards
// instead of renown, so they never stand between you and a title.
enum class Secret : int {
    RubberDuck, ColdFeet, WitchingHour, LuckySevens, WellRested, GlassCannon, GoldenHour, Hoard,
    Overkill, CleanHands, Speedrun, DownToZero, LoyalToAFault, UnderTheCounter, Firefighter, Count,
};
inline constexpr int SECRET_COUNT = (int)Secret::Count;
static_assert(SECRET_COUNT <= 32, "GameState::secretsDone is a 32-bit mask");
struct SecretGoal { const char* id; const char* name; const char* hint; const char* desc; long long shards; };
extern const SecretGoal SECRETS[];   // indexed by Secret
inline constexpr int GOAL_COUNT = 29;
static_assert(GOAL_COUNT <= 32, "GameState::goalsDone is a 32-bit mask");
extern const Goal GOALS[];

struct Title { int renown; const char* name; };
inline constexpr int TITLE_COUNT = 10;
extern const Title TITLES[];

// ----- Infection -----
// A quarter of new profiles are Infected: one strain makes that game harder for good, until
// the Orrery is complete (that cures it). Nothing carries over to or from other profiles.
enum class Strain : int { None, Fever, Rust, Static, Mutiny, Leech, Amnesia, Count };
inline constexpr int STRAIN_COUNT = (int)Strain::Count;
inline constexpr double INFECTION_CHANCE = 0.25;
struct StrainInfo {
    const char* id;     // what the save file uses
    const char* name;
    const char* desc;   // what it does to you
};
extern const StrainInfo STRAINS[];   // indexed by Strain

// ----- Unlocks -----
// A new player starts with mining, the shop and the goals. Everything else opens up as titles
// are earned, in this order (the TUI's tabs follow it too).
// The Constellations are the exception: they open when the Orrery is complete, not with a title.
enum class Feature : int { Curios, Monolith, Market, Contracts, Crew, Orrery, Workshop, Stars, Tasks, Count };
inline constexpr int FEATURE_COUNT = (int)Feature::Count;
struct FeatureInfo {
    const char* name;    // "the Market"
    const char* id;      // what the save file uses
    int title;           // index into TITLES: earning that title opens it (-1: something else does, see game.cpp)
    const char* blurb;   // one line on what it is
};
extern const FeatureInfo FEATURES[];   // indexed by Feature

// ----- Gadgets (the Workshop) -----
// Built once from a curio you own (it stays on the shelf) and some shards, then equipped in one
// of GADGET_SLOTS slots. Every gadget is a trade-off: something better, something worse.
enum class Gadget : int {
    DebugDuck, TuningFork, LavaCore, ForgedPapers, LuckyPenny, NightShift, GoldenLedger, LightningRod, FogMachine, MoonAnchor, Count,
};
inline constexpr int GADGET_COUNT = (int)Gadget::Count;
inline constexpr int GADGET_SLOTS = 3;
struct GadgetInfo {
    const char* id;      // what the save file uses
    const char* name;
    int curio;           // index into CURIOS: the one it's built from
    long long cost;      // in shards
    const char* plus;    // what gets better
    const char* minus;   // what gets worse
};
extern const GadgetInfo GADGETS[];   // indexed by Gadget

// ----- Crew -----
enum class Role : int { Miner, Broker, Scout, Fixer, Count };
inline constexpr int ROLE_COUNT = (int)Role::Count;

struct RoleInfo {
    const char* id;          // what the save file uses
    const char* name;
    long long hireCost;      // in shards, for a level 1 hire
    const char* perk;        // one line for the crew screen
};
extern const RoleInfo ROLES[];   // indexed by Role

// Every candidate rolls a trait (or none). Traits are trade-offs, not straight upgrades.
enum class Trait : int { None, Diligent, Greedy, Frugal, QuickLearner, Ambitious, Loyal, NightOwl, Count };
inline constexpr int TRAIT_COUNT = (int)Trait::Count;
struct TraitInfo {
    const char* id;     // what the save file uses
    const char* name;   // "Night Owl"
    const char* desc;   // one line for the crew screen
};
extern const TraitInfo TRAITS[];   // indexed by Trait

inline constexpr int CREW_NAME_COUNT = 32;
extern const char* const CREW_NAMES[];

// ----- Contracts -----
// Count also marks "unknown" in old saves. New kinds go at the end: saves store the id, not the number.
enum class ContractKind : int { Earn, Mine, Jackpots, Streak, BotEarn, Trade, Hold, CoolOff, HandsOff, Count };
inline constexpr int CONTRACT_KIND_COUNT = (int)ContractKind::Count;

struct ContractKindInfo {
    const char* id;          // what the save file uses
    const char* format;      // "%s" is replaced by the target, e.g. "Mine %s times"
    const char* single;      // the wording when the target is 1
    bool endurance;          // won by lasting until the timer runs out without breaking a rule
    const char* rule;        // endurance jobs: what breaks it (for the screens)
};
extern const ContractKindInfo CONTRACT_KINDS[];   // indexed by ContractKind

// Clients have a style (what they ask for and how they pay), a rival, and a standing with you
// (see contracts.hpp) that rises when you deliver and falls when you don't.
enum class ClientStyle : int { Shady, Miners, Merchants, SmallFry, Count };
struct ClientStyleInfo { const char* name; const char* desc; };
extern const ClientStyleInfo CLIENT_STYLES[];   // indexed by ClientStyle

inline constexpr int CLIENT_COUNT = 12;
struct ClientInfo {
    const char* id;          // what the save file uses
    const char* name;
    ClientStyle style;
    int rival;               // index into CLIENTS: working for one annoys the other
};
extern const ClientInfo CLIENTS[];

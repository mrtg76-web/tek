#pragma once
// Project data model: Project -> 8 Sequencers -> 16 Patterns -> 8 Tracks -> 128 Steps.
// Mono and Poly modes use track 0 only; Multitrack uses all 8 tracks (manual 1.6, 6.x).
#include <array>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include "Scales.h"

namespace tek {

constexpr int kNumSeqs = 8;
constexpr int kNumPatterns = 16;
constexpr int kMaxSteps = 128;
constexpr int kNumTracks = 8;
constexpr int kMaxPolyNotes = 7;
constexpr int kNumGrooves = 14;

enum class Mode : uint8_t { Mono = 0, Poly, Multi, Count };
inline const char* modeLabel(Mode m) {
    switch (m) { case Mode::Mono: return "MONO"; case Mode::Poly: return "POLY"; case Mode::Multi: return "MULT"; default: return "?"; }
}

enum class Dir : uint8_t { Forward = 0, Reverse, Pendulum, Pong, Random, Drunk, Count };
inline const char* dirLabel(Dir d) {
    switch (d) {
        case Dir::Forward: return "FWD"; case Dir::Reverse: return "REV"; case Dir::Pendulum: return "PEND";
        case Dir::Pong: return "PONG"; case Dir::Random: return "RND"; case Dir::Drunk: return "DRNK"; default: return "?";
    }
}

// Time divisions (manual 4.2): Bar, 1/2, 1/4, 1/8, 1/8t, 1/16, 1/16t, 1/32, 1/32t, 1/64
enum class Division : uint8_t { Bar = 0, Half, Quarter, Eighth, EighthT, Sixteenth, SixteenthT, ThirtySecond, ThirtySecondT, SixtyFourth, Count };
inline double divisionBeats(Division d) { // length of one step in quarter notes
    switch (d) {
        case Division::Bar: return 4.0; case Division::Half: return 2.0; case Division::Quarter: return 1.0;
        case Division::Eighth: return 0.5; case Division::EighthT: return 1.0 / 3.0; case Division::Sixteenth: return 0.25;
        case Division::SixteenthT: return 1.0 / 6.0; case Division::ThirtySecond: return 0.125;
        case Division::ThirtySecondT: return 1.0 / 12.0; case Division::SixtyFourth: return 0.0625; default: return 0.25;
    }
}
inline const char* divisionLabel(Division d) {
    static const char* l[] = {"BAR","1/2","1/4","1/8","1/8t","1/16","1/16t","1/32","1/32t","1/64"};
    int i = (int) d; return (i >= 0 && i < 10) ? l[i] : "?";
}

// Trigger conditions (manual 3.11)
enum class TrigType : uint8_t { Prob = 0, AB, NotAB, Pre, NotPre, First, NotFirst, Fixed, Locked, Flow, NotFlow };
struct Trig {
    TrigType type = TrigType::Prob;
    uint8_t a = 100;   // Prob: percent 0..100 ; AB: cycle A
    uint8_t b = 0;     // AB: cycle count B
    bool operator==(const Trig& o) const { return type == o.type && a == o.a && b == o.b; }
};
std::string trigLabel(const Trig& t);
// Ordered list used when turning the Trig knob: 0%..100% in 5% steps, then logic conditions.
int trigListSize();
Trig trigFromListIndex(int i);
int trigToListIndex(const Trig& t);

constexpr uint8_t kGateLegato = 100;

struct Step {
    bool active = false;
    bool muted = false;
    uint8_t numNotes = 0;
    std::array<int8_t, kMaxPolyNotes> notes {}; // scale degrees relative to sequencer root (mono/poly) or track base (multi)
    uint8_t velocity = 75;   // 1..127
    int8_t octave = 0;       // per-step octave offset
    uint8_t gate = 50;       // 2..99 %, 100 = legato
    uint8_t tie = 0;         // number of following steps this note is held across
    Trig trig;
    uint8_t repeats = 1;     // 1..15 (1 = no repeat)
    uint8_t repDur = 1;      // 1..15 steps the repeats are spread across
    int8_t ramp = 0;         // -70..70 % velocity ramp across repeats
    int8_t accel = 0;        // -28..28 % repeat acceleration
    uint8_t retrig = 1;      // 1..8 ratchets within the gate
    int8_t offset = 0;       // -45..45 % of step timing offset
    int8_t rng = 0;          // 0..24 random note range (scale intervals) for repeats
    uint8_t repProb = 100;   // repeat probability %
    uint8_t velLocked = 0;   // multitrack: velocity locked against global edits

    bool hasNote(int degree) const {
        for (int i = 0; i < numNotes; ++i) if (notes[(size_t) i] == degree) return true;
        return false;
    }
    void clear() { *this = Step(); }
};

struct Track {
    std::array<Step, kMaxSteps> steps;
    uint8_t start = 0, end = 15;   // inclusive step range
    Dir dir = Dir::Forward;
    Division div = Division::Sixteenth;
    uint8_t rndDiv = 0;            // 0..100 % random time division
    // multitrack specific
    uint8_t channel = 0;           // 0..15
    uint8_t baseNote = 36;         // MIDI note of degree 0
    uint8_t swing = 0;             // 0 = "Same" (use pattern swing), else 10..90
    bool muted = false;
    // defaults used when placing new steps (lower bound of the global range)
    uint8_t defVel = 75, defGate = 50, defRepeats = 1;
    int length() const { return (int) end - (int) start + 1; }
};

struct Pattern {
    std::array<Track, kNumTracks> tracks;
    uint8_t swing = 50;            // 10..90, 50 = none (saved with pattern, manual 4.8)
    bool isEmpty() const;
};

struct Sequencer {
    Mode mode = Mode::Mono;
    uint8_t channel = 0;           // 0..15 (mono/poly)
    uint8_t scale = 0;             // index into scaleLibrary()
    uint8_t rootPc = 0;            // 0..11
    int8_t baseOctave = 2;         // -2..7, C2 = 48 (Ableton numbering)
    uint8_t punchIn = 0;           // 0 Glob,1 Off,2 P.End,3 Beat,4 1/2 Bar,5 Bar,6 2 Bars,7 4 Bars,8 8 Bars
    uint8_t resetBars = 0;         // 0 = off, else 1..64
    uint8_t programChange = 0;     // 0 = off, else 1..128
    uint8_t bank = 0;              // 0 = off, else 1..128
    uint8_t transpose = 0;         // 0 Off, 1 Same, 2..17 = channel 1..16
    uint8_t nVoices = 7;           // poly voices 1..7
    uint8_t color = 0;             // 0..7 palette index (default = sequencer index)
    int8_t nudgeMs = 0;            // -100..100 ms
    int8_t timeBend = 0;           // -120..120 = -12.0..+12.0 %
    bool active = true;            // inactive = transport stopped for this seq
    bool muted = false;            // muted = runs but silent
    uint8_t curPattern = 0;
    // groove (manual 4.9)
    uint8_t groove = 0;            // 0 = none, 1..kNumGrooves
    int16_t grooveAccent = 0;      // -120..120 %
    int16_t grooveTime = 0;        // -120..120 %
    int8_t grooveOffset = 0;       // -15..15 steps
    uint8_t grooveLen = 16;        // 1..16
    uint8_t grooveRand = 0;        // 0..100 %
    std::vector<Pattern> patterns = std::vector<Pattern>(kNumPatterns); // heap: keeps compilers happy

    Pattern& pat() { return patterns[curPattern]; }
    const Pattern& pat() const { return patterns[curPattern]; }
    int baseMidi() const { return (baseOctave + 2) * 12 + rootPc; }
};

enum class OutputMode : uint8_t { Host = 0, Port, Both, Count };
inline const char* outputModeLabel(OutputMode m) {
    switch (m) { case OutputMode::Host: return "HOST"; case OutputMode::Port: return "PORT"; case OutputMode::Both: return "BOTH"; default: return "?"; }
}

struct Project {
    std::array<Sequencer, kNumSeqs> seqs;
    double bpm = 120.0;            // internal clock tempo
    bool internalClock = false;    // false = follow host transport
    OutputMode output = OutputMode::Both;

    Project() { reset(); }
    void reset();
};

// Groove templates: 16 timing offsets (fraction of a step, -0.5..0.5) and velocity factors (-1..1)
struct GrooveTemplate { const char* name; std::array<float, 16> timing; std::array<float, 16> velocity; };
const GrooveTemplate& grooveTemplate(int index /* 1..kNumGrooves */);

// Serialisation (plain text, versioned). Returns false if the data could not be parsed.
std::string serialize(const Project& p);
bool deserialize(const std::string& s, Project& p);

const char* punchInLabel(int v);
uint32_t seqColor(int paletteIndex); // 0xRRGGBB

} // namespace tek

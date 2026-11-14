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

enum class Mode : uint8_t { Mono = 0, Poly, Multi, Chord, Stoch, Matr, Domino, Saga, Player, Count };
inline const char* modeLabel(Mode m) {
    switch (m) { case Mode::Mono: return "MONO"; case Mode::Poly: return "POLY"; case Mode::Multi: return "MULT"; case Mode::Chord: return "CHRD"; case Mode::Stoch: return "STCH"; case Mode::Matr: return "MATR"; case Mode::Domino: return "DOMI"; case Mode::Saga: return "SAGA"; case Mode::Player: return "PLYR"; default: return "?"; }
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
    // chord mode (notes[0] = root degree of the chord)
    uint8_t chordType = 0;   // index into the chord type table
    uint8_t voicing = 0;     // 0..5 (No Inv, I Inv, II Inv, Open, I Open, II Open)
    uint8_t spread = 0;      // 0..6 extra upper-octave tones
    int8_t strum = 0;        // -100..100 %
    uint8_t dur = 1;         // saga: stage duration in steps (1..32)
    // accumulator (manual 6.13): pitch evolves each time the step plays
    int8_t accAmt = 0;       // -24..24 scale intervals per trigger (0 = off)
    uint8_t accMode = 0;     // 0 Wrap, 1 Clip, 2 Pendulum, 3 Hold
    int8_t accMax = 7;       // 0..24  (step mode limits)
    int8_t accMin = 0;       // -24..0
    uint8_t accTarget = 0;   // 0 All (step + repeats), 1 Step, 2 Repeats
    uint8_t accPctMode = 0;  // 0 Ignore, 1 Skip (when the probability fails)
    uint8_t accProb = 100;   // probability the accumulator advances

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
    uint8_t accTrackMode = 0;      // AcmM: 0 Step (limits per step), 1 Trck (limits below for every step)
    int8_t accMax = 7, accMin = 0;
    // loop inside the pattern (manual 5.2): 255 = no loop
    uint8_t loopStart = 255, loopEnd = 255;
    uint8_t loopRate = 255;        // 255 = same division as the track, else Division
    bool hasLoop() const { return loopStart != 255 && loopEnd != 255; }
    int length() const { return (int) end - (int) start + 1; }
};

// Stochastic mode (manual 6.8): note probabilities + randomisation controls, and the generated phrase
// buffer (kept in the project so a locked phrase is saved with the set).
struct Stochastic {
    std::array<uint8_t, 12> noteLevel {}; // 0 = off, 1..8 = probability weight (columns 1-12, semitones from root)
    uint8_t octUp = 0, octDown = 0;       // 0..4 octaves (column 13)
    uint8_t divRow = 2;                   // 0..7 rhythm range, 2 = base division only (column 14)
    uint8_t pitchRand = 3;                // 0..7 -> 0..100 % steps re-generated per cycle (column 15)
    uint8_t rhythmRand = 3;               // 0..7 (column 16)
    bool valid = false;                   // buffer generated?
    std::array<int8_t, kMaxSteps> pitch {};  // semitones from root (incl. octave)
    std::array<uint8_t, kMaxSteps> rhythm {}; // 0 rest, 1 normal, 2/3/4/6/8 ratchets, 20+n = hold n steps, 99 covered by hold
    Stochastic() { noteLevel[0] = 8; }
};

// Matriceal mode (manual 6.9): 4 tracks x 10 independent parameter lanes of 16 steps.
enum MatLaneId { kLaneTrig = 0, kLaneNote, kLaneIntv, kLanePuls, kLaneVel, kLaneOct, kLaneGate, kLaneProb, kLaneRetrig, kLaneGlide, kNumMatLanes };
constexpr int kNumMatTracks = 4;
int matLaneDefault(int lane);          // default value of a lane step
int matLaneMin(int lane);
int matLaneMax(int lane);
const char* matLaneName(int lane);

struct MatLane {
    std::array<int8_t, 16> val {};     // step values (see matLaneDefault)
    std::array<uint8_t, 16> prob {};   // per step probability % (note, interval, octave, retrig, glide)
    std::array<uint8_t, 16> aux {};    // pulses: 0 first, 1 all, 2 tie
    uint8_t start = 0, end = 15;
    Division div = Division::Sixteenth;
    Dir dir = Dir::Forward;
};
struct MatTrack {
    std::array<MatLane, kNumMatLanes> lanes;
    uint8_t channel = 0;
    uint8_t baseNote = 48;             // "Ofs"
    uint8_t rlen = 0;                  // reset length in trigger steps (+ pulses), 0 = off
    bool muted = false;
    int8_t intMin = -7, intMax = 7;    // interval accumulator range
    std::array<uint8_t, kNumMatLanes> rnd {}; // randomisation amount % per lane
    uint8_t eucLen = 16, eucPulses = 4;
    MatTrack();
};
struct Matriceal { std::array<MatTrack, kNumMatTracks> tracks; };

// Euclidean rhythm (Bjorklund): writes `len` booleans with `pulses` hits spread evenly, rotated by `rot`.
void euclid(int len, int pulses, int rot, bool* out);

// MIDI Player mode (manual 6.12): note on/off data imported from a .mid file, 96 PPQN, looped.
constexpr int kClipPpqn = 96;
constexpr int kMaxClipNotes = 2000;
struct ClipNote { uint32_t tick; uint32_t len; uint8_t note, vel; };
struct MidiClip {
    std::vector<ClipNote> notes;    // sorted by tick
    uint32_t lengthTicks = 0;       // loop length (whole bars)
    std::string name;
};

// Builds a clip from notes timed at `ppqn` ticks per quarter: converts to 96 PPQN, sorts, caps the note
// count and rounds the loop length up to whole bars.
MidiClip makeClip(std::vector<ClipNote> notes, int ppqn, const std::string& name);

// ---------------------------------------------------------------- modulation (manual 7.x)
constexpr int kNumModLanes = 8, kNumLfos = 2, kNumModMaps = 8;
// Internal (non destructive) destinations. Every source adds a bipolar -127..127 value.
enum ModDest : uint8_t { MD_Off = 0, MD_Vel, MD_Pitch, MD_Oct, MD_Gate, MD_Rept, MD_Trig, MD_Div, MD_GrvVel, MD_GrvTime,
                         MD_ArpDiv, MD_ArpGate, MD_ArpOct, MD_ArpAccent, MD_Lfo1Rate, MD_Lfo1Amt, MD_Lfo2Rate, MD_Lfo2Amt,
                         MD_RpVel, MD_RpOct, MD_RpGate, MD_RpNote, MD_RpTrig, MD_ChType, MD_ChVoic, MD_ChSpread, MD_ChStrum,
                         MD_AccAmt, MD_Count };
const char* modDestLabel(int d);
// MIDI output destination: 0 Off, 1..128 = CC 0..127, 129 Pitch Bend, 130 Aftertouch, 131 Program Change
constexpr int kModOutPB = 129, kModOutAT = 130, kModOutPC = 131, kModOutCount = 132;
std::string modOutLabel(int v);
// Global (destructive) parameter destinations of the mod matrix
enum ModParam : uint8_t { MP_Off = 0, MP_Mute, MP_Enable, MP_Scale, MP_Root, MP_Swing, MP_Div, MP_Dir, MP_Count };
const char* modParamLabel(int p);

struct ModLane {
    std::array<int8_t, kMaxSteps> val {};
    uint8_t start = 0, end = 15;
    Division div = Division::Sixteenth;
    bool linked = true;             // follow track 1's range and division
    uint8_t cc1 = 0, cc2 = 0;       // MIDI destinations (see kModOut*)
    int8_t max = 127, min = 0, offset = 0, random = 0;
    uint8_t int1 = 0, int2 = 0;     // internal destinations
    int8_t amt1 = 100, amt2 = 100, off1 = 0, off2 = 0;
    uint8_t smooth = 0;             // 0..100 % glide into the next step
    bool muted = true;
    bool isDefault(int laneIndex) const;
};
extern const uint8_t kLaneDefaultCC[kNumModLanes];
struct Lfo {
    uint8_t wave = 0;               // see lfoWaveLabel
    int8_t rate = -5;               // < 0: clocked (index -rate-1, see lfoRateLabel), > 0: free running
    uint8_t amt = 0;                // 0..100 % (internal + MIDI)
    bool retrig = false;
    uint8_t phase = 0;              // 0..100 %
    int8_t offset = 0;              // -127..127 (MIDI only)
    uint8_t inDst = 0, dst = 0;     // internal / MIDI destination
};
constexpr int kNumLfoWaves = 7;
const char* lfoWaveLabel(int w);
std::string lfoRateLabel(int r);
double lfoClockBeats(int r);        // cycle length in beats for a clocked rate
double lfoHz(int r);                // free running rate
struct ModMap {
    uint8_t type = 0;               // 0 Off, 1 CC, 2 Note
    uint8_t num = 1;
    uint8_t chan = 0;               // 0 Same (sequencer channel), 1 All, 2..17 = channel 1..16
    uint8_t ccOut = 0;              // forwarded CC (0 off, 1..128 = CC 0..127)
    int8_t amt1 = 100, off1 = 0;
    uint8_t int1 = 0, int2 = 0;
    int8_t amt2 = 100, off2 = 0;
    uint8_t par = 0;                // ModParam
};

struct Pattern {
    std::array<Track, kNumTracks> tracks;
    std::array<ModLane, kNumModLanes> lanes;
    Stochastic stoch;
    Matriceal mat;
    MidiClip clip;
    uint8_t swing = 50;            // 10..90, 50 = none (saved with pattern, manual 4.8)
    bool isEmpty() const;
};

// Arpeggiator (manual 5.3). One settings block per sequencer: `type` drives the keyboard arp,
// `seqType` the poly / chord sequencer arp (step notes are arpeggiated).
enum ArpType { kArpOff = 0, kArpUp, kArpDown, kArpUpDown, kArp2Up2Down, kArpAltIn, kArpAltOut, kArpAltInOut,
               kArpPinky, kArpThumb, kArpRandom, kArpRandOnce, kArpOrder, kNumArpTypes };
const char* arpTypeLabel(int t);
struct ArpSettings {
    uint8_t type = 0, seqType = 0;
    int8_t octV = 0;               // -7..7
    uint8_t gate = 50;             // 2..100
    Division div = Division::Sixteenth;
    int8_t accent = 0;             // -100..100 % (groove velocity profile)
    uint8_t len = 16, puls = 16, rept = 0; // euclidean
    int8_t gtime = 0;              // -100..100 % groove timing
    uint8_t groove = 1;            // 1..kNumGrooves
    uint8_t reset = 3;             // 0 Off, 1 aPos, 2 Grve, 3 All
    bool latch = false, hold = false;
};

// Euclidean pattern generator settings (manual 4.10)
struct GenSettings {
    uint8_t eLen = 16, ePuls = 4; int8_t eRot = 0; int8_t dens = 0;      // dens -100..100 %
    int8_t nteR = 0; uint8_t ntePct = 30; int8_t octR = 0; bool eInv = false; // nteR -24..24 (neg = both ways), octR -3..3
    uint8_t tiePct = 0, eLen2 = 0; int8_t eRot2 = 0; uint8_t ntOrd = 0; // eLen2 0 = layer 2 off; ntOrd 0 Lin 1 Rnd 2 PingP 3 Shuf
    uint8_t lock = 0, ePuls2 = 0, logic = 0, velPct = 0;                 // lock 0 Off 1 Pitch 2 Rhythm 3 Both; logic 0 OR 1 AND 2 XOR 3 Accnt
    uint32_t seed = 1;
};
// Random pattern generator settings (manual 4.11)
struct RandGenSettings {
    int8_t bias = 0, rang = 12; uint8_t rand = 25; int8_t den = 70;      // den 1..100 %, 0 Human, -1 Order, -2 Pitch, -3 Flip
    bool scal = false; uint8_t vg = 10, ties = 10;
};
const char* genLogicLabel(int v);
const char* genOrderLabel(int v);
const char* genLockLabel(int v);
std::string randDenLabel(int v);

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
    uint8_t bassMode = 0;          // chord mode bass note: 0 Off, 1 On (same channel), 2..17 = channel 1..16
    uint8_t chMx = 0;              // channel multiplex: 0 Off, 1..7 = 2..8 Lo, 8..14 = 2..8 Hi (manual 6.14)
    int8_t harm = -1;              // harmonizer: -1 off, 0..7 = leader sequencer (scale shown as HARMON)
    uint8_t voice = 1;             // domino: 0 Mono, 1 Poly
    ArpSettings arp;
    // random perform (manual 5.6): non destructive variations, -100..100 (negative = one sided)
    int8_t rpVel = 0, rpOct = 0, rpGate = 0, rpNote = 0, rpRetri = 0;
    uint8_t rpRept = 0, rpTrig = 100, rpScope = 0, rpMask = 0; // rpMask: multitrack tracks excluded
    // perform view (manual 5.7)
    int8_t perfTrans = 0;          // -7..7 scale degrees
    int8_t perfRot = 0;            // -7..7 steps
    GenSettings gen;
    RandGenSettings rgen;
    std::array<Lfo, kNumLfos> lfo;
    std::array<ModMap, kNumModMaps> maps;
    uint8_t kbVel = 75;            // keyboard view velocity
    int8_t kbBase = 0;             // keyboard base octave -2..4 (C0 default) (tracks[0] = 32 note steps, tracks[1] = 64 trigger steps)
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
    int8_t scaleMaster = -1;       // -1 off, else the sequencer whose scale + root every other one follows
    // recording settings (manual 4.12)
    bool recQt = true;             // quantize live recording
    uint8_t recQtPct = 100;        // 0..100 %
    bool recOvdb = true;           // overdub (false: replace as the playhead passes)
    uint8_t recMode = 0;           // 0 Loop, 1 1Shot, 2 Xtend
    uint8_t recCountIn = 0;        // 0 Off, 1 Beat, 2 1/2 Bar, 3 Bar, 4 2 Bars
    uint8_t metroCh = 0;           // 0 off, 1..16 MIDI channel for the metronome
    uint8_t metroMode = 0;         // 0 Always while recording, 1 count-in only

    Project() { reset(); }
    void reset();
};

// Groove templates: 16 timing offsets (fraction of a step, -0.5..0.5) and velocity factors (-1..1)
struct GrooveTemplate { const char* name; std::array<float, 16> timing; std::array<float, 16> velocity; };
const GrooveTemplate& grooveTemplate(int index /* 1..kNumGrooves */);

// Serialisation (plain text, versioned). Returns false if the data could not be parsed.
std::string serialize(const Project& p, int onlySeq = -1, int onlyPattern = -1); // onlySeq/onlyPattern: templates
bool deserialize(const std::string& s, Project& p);

const char* punchInLabel(int v);
std::string chMxLabel(int v);
const char* accModeLabel(int v);
// MIDI channel for the voice with pitch rank `rankLow` (0 = lowest) of `count` voices
uint8_t chMxChannel(int base, int chMx, int rankLow, int count);
uint32_t seqColor(int paletteIndex); // 0xRRGGBB

} // namespace tek

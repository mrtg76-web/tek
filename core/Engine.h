#pragma once
// Playback engine: turns the Project into timed MIDI events, locked to a musical timeline (PPQ).
// Pure C++, no framework dependency, so it can be unit tested headless.
#include "Model.h"
#include <atomic>
#include <vector>

namespace tek {

struct MidiOut {
    int sampleOffset;
    uint8_t status, data1, data2;
};

struct MidiIn {
    uint8_t status, data1, data2;
};

struct Transport {
    bool playing = false;
    double ppq = 0.0;     // position in quarter notes at the start of the block
    double bpm = 120.0;
};

class Engine {
public:
    explicit Engine(Project& p);

    void prepare(double sampleRate);
    // Must be called with the project lock held.
    void process(const Transport& host, int numSamples, const std::vector<MidiIn>& in, std::vector<MidiOut>& out);

    // --- runtime controls (call with the project lock held) ---
    void setInternalPlaying(bool playing);      // used when Project::internalClock is true
    bool internalPlaying() const { return intPlaying; }
    void requestPattern(int seq, int pattern);  // switch at the next bar (immediately when stopped)
    int pendingPattern(int seq) const { return pendingPat[(size_t) seq]; }
    void panic();                               // all notes off on every channel at the next block
    void resetSeq(int seq);                     // restart a sequencer from its first step

    std::atomic<bool> flowHeld { false };
    std::array<uint8_t, kNumSeqs> soloMask {};  // multitrack solo bits (not saved)

    // --- state for the UI (lock free) ---
    double clipProgress(int seq) const { return rt[(size_t) seq][0].clipProgressUi.load(std::memory_order_relaxed); }
    int dominoNotePos(int seq) const { return rt[(size_t) seq][0].domNoteUi.load(std::memory_order_relaxed); }
    int matLanePos(int seq, int track, int lane) const { return rt[(size_t) seq][(size_t) track].lanePos[(size_t) lane].load(std::memory_order_relaxed); }
    int playhead(int seq, int track) const { return rt[(size_t) seq][(size_t) track].playheadUi.load(std::memory_order_relaxed); }
    bool isRunning() const { return running.load(std::memory_order_relaxed); }
    double currentBpm() const { return bpmUi.load(std::memory_order_relaxed); }
    double currentPpq() const { return ppqUi.load(std::memory_order_relaxed); }

    // Deterministic step position for a step counter (exposed for tests).
    static int positionFor(Dir dir, int start, int end, int64_t counter);

private:
    struct TrackRt {
        bool init = false;
        int64_t nextIdx = 0;       // grid index of the next step to schedule
        double nextGrid = 0.0;     // ppq of the next step start
        int64_t counter = 0;       // steps since the last reset period start
        int64_t sinceStart = 0;    // steps scheduled since transport start
        int64_t resetBlock = -1;
        int pos = -1;              // last position (for random / drunk)
        bool lastTrig = true;      // last evaluated trig condition (for PRE)
        // matriceal
        double matActive = 0.0;    // elapsed ppq excluding pulse holds (drives the parameter lanes)
        int matHold = 0;           // remaining held pulses
        uint8_t matHoldType = 0;   // 0 first, 1 all, 2 tie
        int matAccum = 0;          // interval accumulator
        int64_t matTicks = 0;      // trigger steps + pulses since reset (RLen)
        std::array<int64_t, kNumMatLanes> laneCnt {};
        std::array<std::atomic<int>, kNumMatLanes> lanePos {};
        int lastNote = -1, lastVel = 0; uint8_t lastCh = 0; double lastDur = 0.0;
        int64_t domNote = -1;      // domino: note section step counter
        std::atomic<int> domNoteUi { -1 };
        std::atomic<double> clipProgressUi { 0.0 };
        std::atomic<int> playheadUi { -1 };
    };

    struct Ev {
        double ppq;
        uint8_t kind;      // 0 note on, 1 note off, 2 playhead, 3 raw
        uint8_t ch, d1, d2;
        uint8_t seq, track;
        int16_t pos;
    };

    void startAll(double ppq);
    void stopAll(std::vector<MidiOut>& out, int offset);
    void initTrack(int s, int t, double ppq);
    double stepLength(const Sequencer& q, const Track& tr) const;
    void scheduleStep(int s, int t, double grid, double len, double bpm);
    void push(const Ev& e) { queue.push_back(e); }
    void emitNoteOn(std::vector<MidiOut>& out, int off, int ch, int note, int vel);
    void emitNoteOff(std::vector<MidiOut>& out, int off, int ch, int note);
    uint32_t rand32() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
    float rand01() { return (float) (rand32() & 0xffffff) / (float) 0x1000000; }
    int tracksFor(const Sequencer& q) const { return q.mode == Mode::Multi ? kNumTracks : q.mode == Mode::Matr ? kNumMatTracks : 1; }
    bool evalTrig(const Step& st, TrackRt& r, int len);
    void regenStoch(Stochastic& sc, const Track& tr, int i, bool pitch, bool rhythm);
    void playStoch(int s, int pos, double grid, double L, double bpm, bool parity, int64_t counter);
    void playMatriceal(int s, int t, double grid, double L, double bpm);
    void playDomino(int s, double grid, double L, double bpm);
    void playSaga(int s, double grid, double L, double bpm);
    void playClip(int s, double grid, double L, double bpm);
    double trackStepLength(int s, int t) const;

    Project& proj;
    double sr = 44100.0;
    bool wasPlaying = false;
    bool intPlaying = false;
    double intPpq = 0.0;
    double expectedPpq = -1.0;
    bool panicPending = false;
    std::array<int, kNumSeqs> pendingPat {};
    std::array<bool, kNumSeqs> resetPending {};
    std::array<int, kNumSeqs> transposeOffset {};
    std::array<std::array<TrackRt, kNumTracks>, kNumSeqs> rt;
    std::vector<Ev> queue;
    std::vector<Ev> due;
    std::array<std::array<uint8_t, 128>, 16> noteCount {};
    uint32_t rngState = 0x12345678u;
    std::atomic<bool> running { false };
    std::atomic<double> bpmUi { 120.0 };
    std::atomic<double> ppqUi { 0.0 };
};

} // namespace tek

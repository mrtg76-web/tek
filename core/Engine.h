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

    // --- live playing (keyboard view, MIDI input, preview); call with the project lock held ---
    void liveNote(int ch, int note, int vel, bool on);       // immediate note on / off
    void previewNote(int ch, int note, int vel);             // short audition note
    void liveRaw(uint8_t status, uint8_t d1, uint8_t d2);    // pitch bend, pressure, CC...
    void arpKey(int seq, int note, bool down);               // keyboard arp input
    void arpClear(int seq);
    int arpNotes(int seq, int* out, int max) const;          // currently held / latched arp notes
    int kbTarget = 0;                                        // sequencer receiving MIDI input notes (selected seq)
    std::array<double, kNumSeqs> rollDiv {};                 // roller interval in beats (0 = off), keyboard view
    std::array<uint8_t, kNumSeqs> multiFill {};              // perform: multitrack fill intensity 0..4
    std::array<uint8_t, kNumSeqs> matFill {};                // perform: matriceal trigger fill 0..12
    uint8_t seqSolo = 0;                                     // perform: soloed sequencers (bit mask)
    bool seqAudible(int s) const { const auto& q = proj.seqs[(size_t) s]; return q.active && !q.muted && (seqSolo == 0 || (seqSolo >> s) & 1); }

    // --- recording (manual 4.12); call with the project lock held ---
    void recInput(int note, int vel, bool on);          // a played note (keyboard view or MIDI input)
    void recArm(int seq, int track, bool waitForNote);  // stopped: starts with the transport (after the count-in); playing: now
    void recStop();
    int recState() const;                               // 0 off, 1 armed, 2 count-in, 3 recording
    double countInLeft() const;                         // beats left in the count-in
    int recSeqIndex() const { return recSeq; }
    void stepRecStart(int seq, int track);
    void stepRecStop() { stepRec = false; }
    bool stepRecording() const { return stepRec; }
    int stepRecCursor() const { return stepCursor; }
    void stepRecMove(int delta);
    void stepRecReset();
    void stepRecRest();
    void stepRecTie();
    int captureCount() const { return (int) capture.size(); }
    void captureClear() { capture.clear(); }
    int captureToPattern(int seq, int track);           // returns the number of steps written

    std::atomic<bool> flowHeld { false };
    std::array<uint8_t, kNumSeqs> soloMask {};  // multitrack solo bits (not saved)

    // --- state for the UI (lock free) ---
    double clipProgress(int seq) const { return rt[(size_t) seq][0].clipProgressUi.load(std::memory_order_relaxed); }
    int dominoNotePos(int seq) const { return rt[(size_t) seq][0].domNoteUi.load(std::memory_order_relaxed); }
    int matLanePos(int seq, int track, int lane) const { return rt[(size_t) seq][(size_t) track].lanePos[(size_t) lane].load(std::memory_order_relaxed); }
    int playhead(int seq, int track) const { return rt[(size_t) seq][(size_t) track].playheadUi.load(std::memory_order_relaxed); }
    double playheadPpq(int seq, int track) const { return rt[(size_t) seq][(size_t) track].phPpq.load(std::memory_order_relaxed); }
    double playheadLen(int seq, int track) const { return rt[(size_t) seq][(size_t) track].phLen.load(std::memory_order_relaxed); }
    bool isRunning() const { return running.load(std::memory_order_relaxed); }
    double currentBpm() const { return bpmUi.load(std::memory_order_relaxed); }
    double currentPpq() const { return ppqUi.load(std::memory_order_relaxed); }

    // Deterministic step position for a step counter (exposed for tests).
    static int positionFor(Dir dir, int start, int end, int64_t counter);
    static double countInBeatsFor(int v);

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
        std::array<int8_t, kMaxSteps> accVal {};   // accumulator value per step
        std::array<int8_t, kMaxSteps> accDir {};   // pendulum direction per step
        std::atomic<int> playheadUi { -1 };
        std::atomic<double> phPpq { 0.0 }, phLen { 0.0 }; // start and length of the step under the playhead (UI prediction)
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
    void runArps(double from, double to, double bpm);
    int buildArpPattern(const ArpSettings& a, int type, const int* notes, int n, int* out, int seqIdx);
    double trackStepLength(int s, int t) const;
    int accNext(const Step& st, const Track& tr, TrackRt& r, int idx, bool advance);
    int degPitch(const Sequencer& q, int s, int base, int degree) const;
    void recordHarm(int s, const int* notes, int n);
    template <typename V> void assignChMx(const Sequencer& q, V* voices, int first, int n) const;
    void processRecording(double ppq, double blockEnd, double bpm, bool playing, int numSamples, double ppqPerSample, std::vector<MidiOut>& out);
    void writeRecNote(int s, int t, int idx, int note, int vel, bool add);
    int recTrackFor(int s, int note) const;
    void applyGlobals(std::vector<MidiOut>& out);

    Project& proj;
    double sr = 44100.0;
    bool wasPlaying = false;
    bool intPlaying = false;
    double intPpq = 0.0;
    double freePpq = 0.0;          // free-running clock for the arpeggiators while the transport is stopped
    void emitDue(double blockStart, double blockEnd, double ppqPerSample, int numSamples, std::vector<MidiOut>& out);
    double expectedPpq = -1.0;
    bool panicPending = false;
    std::array<int, kNumSeqs> pendingPat {};
    std::array<bool, kNumSeqs> resetPending {};
    std::array<int, kNumSeqs> transposeOffset {};
    std::array<std::array<int, 8>, kNumSeqs> harmNotes {};   // last chord played by each (leader) sequencer
    std::array<int, kNumSeqs> harmCount {};
    struct LiveEv { uint8_t st, d1, d2; };
    std::vector<LiveEv> liveQueue;
    struct PreviewOff { int ch, note; int64_t samplesLeft; };
    std::vector<PreviewOff> previewOffs;
    // recording
    struct RecIn { int note, vel; bool on; };
    std::vector<RecIn> recQueue;
    bool recArmed = false, recOn = false, recWait = false;
    int recSeq = 0, recTrack = 0;
    double recStartPpq = 0.0, lastPpq = 0.0;
    struct RecHeld { int note, s, t, idx; double ppq, len; };
    std::vector<RecHeld> recHeld;
    std::array<double, kMaxSteps> recWritten {};       // ppq when a step was last recorded (no-overdub pass)
    bool stepRec = false; int stepSeq = 0, stepTrack = 0, stepCursor = 0, stepHeld = 0; bool stepFresh = true;
    struct Cap { int note, vel, group; };
    std::vector<Cap> capture;
    int64_t sampleClock = 0, lastCapSample = -100000; int capGroup = 0;
    std::array<int, kNumSeqs> sentPc {};
    struct ArpRt {
        int held[32] {}; int nHeld = 0;     // in press order
        int down = 0;                       // physically held keys
        bool changed = false;
        int64_t tick = 0, grooveBase = 0;
        int pos = 0, repLeft = 0;
        double nextGrid = -1.0;
        int randOrder[512] {}; int nRand = 0; uint32_t sig = 0;
    };
    std::array<ArpRt, kNumSeqs> arpRt;
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

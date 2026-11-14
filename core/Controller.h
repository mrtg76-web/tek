#pragma once
// Hardware emulation: buttons, 4 push encoders and the 16x8 pad grid drive the model exactly like
// the panel (manual 1.10, 1.11, 3.x, 4.x, 6.1-6.5). The UI only forwards input and draws a Frame.
#include "Engine.h"
#include "Generators.h"
#include <deque>
#include <string>
#include <vector>

namespace tek {

enum class Btn : uint8_t {
    Shift, Back, Mute, Perf, Gen, Mod, Flow, Arranger, Keyboard, Arp, Lfo, Step, Page, Track, Random,
    Load, Save, Copy, Paste, Init, End, P16, P32, P48, P64, Undo, Play, Stop, Rec,
    Seq1, Seq2, Seq3, Seq4, Seq5, Seq6, Seq7, Seq8, Count
};
constexpr int kNumBtns = (int) Btn::Count;
const char* btnLabel(Btn b);
const char* btnSubLabel(Btn b); // secondary (shift) function

struct Frame {
    std::array<uint32_t, 16 * 8> pads {};   // 0xRRGGBB, index = y * 16 + x, y = 0 is the top row
    std::array<uint32_t, kNumBtns> leds {};
    std::string header, headerRight;        // display top line
    std::string line1, line2;               // display main area
    std::array<std::string, 4> labels, values;
    std::string message;                    // transient popup
    int progress = 0;                       // bar progress 0..15 (16ths in the bar)
    // playhead prediction for remote displays (filled only when Controller::predict is on)
    struct Pred { int row, idx, start, end, page; double ppq, len; };
    std::array<Pred, 8> pred {};
    int nPred = 0;
    double ppq = 0.0, bpm = 120.0;
    bool running = false;
};

class Controller {
public:
    Controller(Project& p, Engine& e);

    // Input. nowMs is a monotonic millisecond clock.
    void padDown(int x, int y, double nowMs);
    void padUp(int x, int y, double nowMs);
    void button(Btn b, bool down, double nowMs);
    void encoderTurn(int knob, int delta, double nowMs);
    void encoderPress(int knob, bool down, double nowMs);

    void render(Frame& f, double nowMs) const;
    bool predict = false; // render(): leave forward playheads out of the pads and describe them in Frame::pred

    int selectedSeq() const { return sel; }
    void resetUiState();          // call after loading a project
    bool takeMidiLoadRequest() { bool r = midiLoadReq; midiLoadReq = false; return r; } // UI opens a file chooser
    int midiLoadTarget() const { return midiLoadSeq; }
    void setClip(int seq, MidiClip clip, double nowMs);
    void showMessage(const std::string& m, double nowMs) { msg(m, nowMs, 2500.0); }
    // file work the host does off the audio lock (templates, project files, reloads)
    struct Job { enum Type { ReloadPattern, ReloadProject, LoadProjectFile, SaveProjectFile, SaveTemplate, LoadTemplate } type; int seq, pattern, slot; };
    std::vector<Job> takeJobs() { std::vector<Job> j; j.swap(jobs); return j; }
    void setTemplateMask(uint32_t m) { templateMask = m; }
    static constexpr int kTemplateSlots = 32;

private:
    enum class Menu { None, Setup, TrackM, StepM, Nudge, Bend, Groove, Arp, RandPerf, Gen, RandGen, RecSet, Project };
    enum class GridMode { Steps, Column, Load, Keys, Perform, Save };

    struct HeldPad { int x, y; double downMs; bool consumed; };

    Sequencer& seq() { return proj.seqs[(size_t) sel]; }
    const Sequencer& seq() const { return proj.seqs[(size_t) sel]; }
    bool isMulti() const { return seq().mode == Mode::Multi; }
    bool isChord() const { return seq().mode == Mode::Chord; }
    bool isStoch() const { return seq().mode == Mode::Stoch; }
    bool isMatr() const { return seq().mode == Mode::Matr; }
    bool isDomino() const { return seq().mode == Mode::Domino; }
    bool isSaga() const { return seq().mode == Mode::Saga; }
    bool isPlayer() const { return seq().mode == Mode::Player; }
    bool hasKeys() const { return isDomino() || isSaga(); }
    int noteRows() const { return isSaga() ? 6 : 2; }
    MatTrack& matTrack(int k) { return seq().pat().mat.tracks[(size_t) k]; }
    const MatTrack& matTrack(int k) const { return seq().pat().mat.tracks[(size_t) k]; }
    int matLaneOf(int k) const { return matLane[(size_t) sel][(size_t) k]; }
    int stepPages() const { return isChord() ? 4 : ((seq().mode == Mode::Mono || seq().mode == Mode::Poly) ? 5 : 3); }
    bool hasAcc() const { auto m = seq().mode; return m == Mode::Mono || m == Mode::Poly || m == Mode::Domino || m == Mode::Saga; }
    bool lanesView() const { return isMulti() && !pianoRoll[(size_t) sel]; }
    int trackIndex() const { return isMulti() ? selTrack[(size_t) sel] : (isDomino() ? 1 : 0); }
    Track& track() { return seq().pat().tracks[(size_t) trackIndex()]; }
    const Track& track() const { return seq().pat().tracks[(size_t) trackIndex()]; }
    int& viewDeg() { return viewDegree[(size_t) sel][(size_t) trackIndex()]; }
    int viewDegC() const { return viewDegree[(size_t) sel][(size_t) trackIndex()]; }
    int stepAt(int x) const { return page[(size_t) sel] * 16 + x; }
    int degreeAtRow(int y) const { return viewDegC() + (7 - y); }
    bool held(Btn b) const { return heldBtn[(size_t) b] || (b == Btn::Mute && muteLatched); }
    void msg(const std::string& m, double nowMs, double dur = 1200.0) { message = m; messageUntil = nowMs + dur; }

    void pushUndo();
    void doUndo(double nowMs);
    void doRedo(double nowMs);

    void toggleStep(int x, int y, double nowMs);
    void placeNote(Track& tr, int idx, int degree);
    void tieSteps(const HeldPad& first, int x, int y, double nowMs);
    void transposeAll(int x, int y, double nowMs);
    void columnPad(int x, int y, double nowMs);
    void loadPad(int x, int y, double nowMs);
    void stochPad(int x, int y, double nowMs);
    void matPadDown(int x, int y, double nowMs);
    void domPadDown(int x, int y, double nowMs);
    void domTap(int x, int y, double nowMs);
    void domKnob(int knob, int delta, double nowMs);
    void renderDomino(Frame& f) const;
    void renderDominoDisplay(Frame& f) const;
    void renderSaga(Frame& f) const;
    void renderPlayer(Frame& f) const;
    void keyPad(int x, int y, bool down, double nowMs);
    int keyNote(int x, int y) const;
    void renderKeys(Frame& f) const;
    void editArp(int knob, int delta, double nowMs);
    void loopPad(int x, int y, double nowMs);
    void editLoop(int knob, int delta, double nowMs);
    void preview(int note, int ch, int vel);
    bool genMode() const { auto m = seq().mode; return m == Mode::Mono || m == Mode::Poly || m == Mode::Chord || m == Mode::Multi; }
    int genTrack() const { return isMulti() ? trackIndex() : 0; }
    void editGen(int knob, int delta, bool apply, double nowMs);
    void genApply();
    void editRandGen(int knob, int delta, double nowMs);
    void editRecSet(int knob, int delta);
    void editProject(int knob, int delta);
    void projExecute(double nowMs);
    void recTap(double nowMs);
    void savePad(int x, int y, double nowMs);
    void renderSave(Frame& f, double nowMs) const;
    void editRandPerf(int knob, int delta, double nowMs);
    void flowStep(int x, int y, double nowMs);
    void perfPad(int x, int y, bool down, double nowMs);
    void perfLoop(int s, int n, bool on);
    void renderPerform(Frame& f) const;
    void takeSnapshot(double nowMs);
    void endSnapshot(bool restore, double nowMs);
    static double rollBeats(int row);
    bool seqArpContext() const { return (seq().mode == Mode::Poly || seq().mode == Mode::Chord) && grid != GridMode::Keys; }
    static void editAcc(Step& st, int page, int knob, int delta);
    static std::string chordFromNotes(const int* notes, int n);
    std::string scaleText() const;
    std::string rootText() const;
    void renderSagaDisplay(Frame& f) const;
    Track& noteTrack() { return seq().pat().tracks[isDomino() ? 0 : (size_t) trackIndex()]; }
    const Track& noteTrack() const { return seq().pat().tracks[isDomino() ? 0 : (size_t) trackIndex()]; }
    void matTap(int x, int y, double nowMs);
    void matKnob(int knob, int delta, double nowMs);
    void matKnobTap(int knob, double nowMs);
    void matRandomize(int k, int lane, double nowMs);
    void renderMatr(Frame& f) const;
    void renderMatrDisplay(Frame& f) const;
    void renderStoch(Frame& f) const;
    std::unique_ptr<Pattern> stochToPattern() const;
    void editHeldSteps(int knob, int delta, double nowMs);
    void editGlobal(int knob, int delta, double nowMs);
    void editShift(int knob, int delta, double nowMs);
    void editMenu(int knob, int delta, double nowMs);
    void tapKnob(int knob, double nowMs);
    void seqButton(int s, double nowMs);
    void pageButton(int p, double nowMs);
    void changeMode(Mode m, double nowMs);
    void shiftPattern(int dx, int dy, double nowMs);
    void expandCondense(bool expand, double nowMs);
    std::vector<int> heldStepIndices() const;
    void relative(uint8_t Step::*field, int delta, int lo, int hi, const char* name, double nowMs);
    std::string rangeText(uint8_t Step::*field) const;
    std::string stepHeader(int idx) const;
    void renderSteps(Frame& f, double nowMs) const;
    void renderLanes(Frame& f, double nowMs) const;
    void renderColumn(Frame& f) const;
    void renderLoad(Frame& f, double nowMs) const;
    void renderDisplay(Frame& f, double nowMs) const;

    Project& proj;
    Engine& eng;

    int sel = 0;
    Menu menu = Menu::None;
    int menuPage = 0;
    GridMode grid = GridMode::Steps;
    int columnParam = 0; // 0 vel, 1 gate, 2 repeat
    std::array<int, kNumSeqs> page {};
    std::array<int, kNumSeqs> selTrack {};
    std::array<bool, kNumSeqs> pianoRoll {};
    std::array<int, kNumSeqs> matSel {};
    std::array<int, kNumSeqs> domOct {};
    std::array<int, kNumSeqs> domVel {};
    std::array<std::vector<int>, kNumSeqs> domLast {};                       // selected matriceal track
    std::array<std::array<int, kNumMatTracks>, kNumSeqs> matLane {}; // selected lane per track
    std::array<std::array<int, kNumTracks>, kNumSeqs> viewDegree {};
    bool follow = true;
    bool scrollMode = false;
    bool knobUpper = true;      // nudge / bend menus: SQ1-4 or SQ5-8
    int pendingMode = -1;
    int stepPage = 0;

    std::array<bool, kNumBtns> heldBtn {};
    std::array<double, kNumBtns> lastTap {};
    bool muteLatched = false;
    bool btnConsumed[kNumBtns] {};
    std::vector<HeldPad> heldPads;
    std::array<bool, 4> encDown {};
    std::array<bool, 4> encConsumed {};
    std::array<double, 4> encDownMs {};

    // clipboards
    std::vector<std::pair<int, Step>> stepClip; // (offset from anchor, step)
    bool stepClipFresh = false;
    int clipAnchor = 0;
    std::array<Step, 16> pageClip {};
    bool pageClipValid = false;
    std::unique_ptr<Pattern> patClip;
    int loadCopySrcSeq = -1, loadCopySrcPat = -1;

    struct UndoEntry { int seq, pat; std::unique_ptr<Pattern> data; };
    std::deque<UndoEntry> undoStack, redoStack;

    std::string message;
    double messageUntil = 0.0;
    std::vector<double> tapTimes;
    bool midiLoadReq = false;
    bool previewOn = false;
    bool loopView = false;
    int loopAnchor = -1;
    int kbBend = 0, kbAt = 0;
    std::array<uint8_t, kNumSeqs> lastArpType {};
    struct KeyHeld { int x, y, note, ch; bool arp; };
    std::vector<KeyHeld> keyHeld;
    double keyboardDownMs = 0.0;
    int midiLoadSeq = 0;
    mutable uint8_t predMask = 0;  // tracks whose playhead is predicted instead of drawn
    double randDownMs = 0.0, perfDownMs = 0.0;
    // perform view state
    struct PerfHeld { int x, y; bool latched; };
    std::vector<PerfHeld> perfHeld;
    std::array<uint8_t, kNumSeqs> perfLoopLen {};                     // live loop length per seq (0 = none)
    std::array<std::array<std::pair<uint8_t, uint8_t>, kNumTracks>, kNumSeqs> perfLoopSave {};
    std::array<bool, kNumSeqs> fillLatched {}, loopLatched {};
    std::unique_ptr<std::array<Sequencer, kNumSeqs>> snapshot;     // manual 5.9
    bool genUndo = false;
    double recDownMs = 0.0, lastRecTapMs = -1e9;
    bool recTapToggled = false;
    int projItem = 0, projSlot = 0, projConfirm = -1;
    double projConfirmUntil = 0.0;
    struct LastTap { int x = -1, y = -1; double ms = -1e9; } lastSaveTap;
    uint32_t templateMask = 0;
    std::vector<Job> jobs;
public:
    bool hasSnapshot() const { return snapshot != nullptr; }
};

} // namespace tek

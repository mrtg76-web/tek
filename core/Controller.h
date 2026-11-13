#pragma once
// Hardware emulation: buttons, 4 push encoders and the 16x8 pad grid drive the model exactly like
// the panel (manual 1.10, 1.11, 3.x, 4.x, 6.1-6.5). The UI only forwards input and draws a Frame.
#include "Engine.h"
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

    int selectedSeq() const { return sel; }
    void resetUiState();          // call after loading a project

private:
    enum class Menu { None, Setup, TrackM, StepM, Nudge, Bend, Groove };
    enum class GridMode { Steps, Column, Load };

    struct HeldPad { int x, y; double downMs; bool consumed; };

    Sequencer& seq() { return proj.seqs[(size_t) sel]; }
    const Sequencer& seq() const { return proj.seqs[(size_t) sel]; }
    bool isMulti() const { return seq().mode == Mode::Multi; }
    bool isChord() const { return seq().mode == Mode::Chord; }
    int stepPages() const { return isChord() ? 4 : 3; }
    bool lanesView() const { return isMulti() && !pianoRoll[(size_t) sel]; }
    int trackIndex() const { return isMulti() ? selTrack[(size_t) sel] : 0; }
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
};

} // namespace tek

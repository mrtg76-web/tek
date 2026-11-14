#include "../core/Controller.h"
#include <cstdio>
#include <memory>

using namespace tek;

static int cfail = 0;
#define CCHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++cfail; } } while (0)

namespace {
struct Rig {
    std::unique_ptr<Project> p = std::make_unique<Project>();
    Engine e { *p };
    Controller c { *p, e };
    double t = 1000.0;
    void tap(int x, int y) { c.padDown(x, y, t); t += 50; c.padUp(x, y, t); t += 400; }
    void press(Btn b) { c.button(b, true, t); t += 30; c.button(b, false, t); t += 400; }
    void hold(Btn b) { c.button(b, true, t); t += 30; }
    void release(Btn b) { c.button(b, false, t); t += 400; }
    Track& tr0() { return p->seqs[0].pat().tracks[0]; }
};
}

int runControllerTests() {
    { // tap creates a note at the row's degree, tap again removes it
        Rig r;
        r.tap(0, 7);                // bottom row = degree 0
        CCHECK(r.tr0().steps[0].active && r.tr0().steps[0].notes[0] == 0);
        r.tap(3, 5);                // row from bottom 2
        CCHECK(r.tr0().steps[3].notes[0] == 2);
        r.tap(3, 4);                // mono: replaces the note in the column
        CCHECK(r.tr0().steps[3].numNotes == 1 && r.tr0().steps[3].notes[0] == 3);
        r.tap(0, 7);
        CCHECK(!r.tr0().steps[0].active);
        r.press(Btn::Undo);
        CCHECK(r.tr0().steps[0].active);
        r.c.button(Btn::Shift, true, r.t); r.press(Btn::Undo); r.c.button(Btn::Shift, false, r.t);
        CCHECK(!r.tr0().steps[0].active);
    }
    { // hold first pad + tap last pad = tie
        Rig r;
        r.c.padDown(2, 6, r.t); r.t += 100;
        r.c.padDown(6, 6, r.t); r.t += 50;
        r.c.padUp(6, 6, r.t); r.c.padUp(2, 6, r.t); r.t += 400;
        CCHECK(r.tr0().steps[2].active && r.tr0().steps[2].tie == 4);
        CCHECK(!r.tr0().steps[4].active);
    }
    { // hold a step and turn knobs: per step edit, long hold doesn't toggle
        Rig r;
        r.tap(1, 7);
        r.c.padDown(1, 7, r.t); r.t += 600;
        r.c.encoderTurn(0, 10, r.t);
        r.c.encoderTurn(3, -2, r.t);
        r.c.padUp(1, 7, r.t); r.t += 400;
        CCHECK(r.tr0().steps[1].active);
        CCHECK(r.tr0().steps[1].velocity == 85);
        CCHECK(r.tr0().steps[1].trig.type == TrigType::Prob && r.tr0().steps[1].trig.a == 90);
        // page 2 of the step submenu
        r.c.padDown(1, 7, r.t); r.t += 600;
        r.press(Btn::Page);
        r.c.encoderTurn(0, 3, r.t);
        r.c.padUp(1, 7, r.t); r.t += 400;
        CCHECK(r.tr0().steps[1].repeats == 4);
    }
    { // global velocity is relative
        Rig r;
        r.tap(0, 7); r.tap(1, 7);
        r.tr0().steps[1].velocity = 100;
        r.c.encoderTurn(0, 5, r.t);
        CCHECK(r.tr0().steps[0].velocity == 80 && r.tr0().steps[1].velocity == 105);
        CCHECK(r.tr0().defVel == 80);
    }
    { // init / end
        Rig r;
        r.hold(Btn::End); r.tap(7, 0); r.release(Btn::End);
        CCHECK(r.tr0().end == 7);
        r.hold(Btn::Init); r.tap(2, 0); r.release(Btn::Init);
        CCHECK(r.tr0().start == 2);
        r.press(Btn::P32); // page 2
        r.hold(Btn::End); r.tap(3, 0); r.release(Btn::End);
        CCHECK(r.tr0().end == 19);
    }
    { // mode change via the setup menu needs confirmation
        Rig r;
        r.hold(Btn::Shift); r.press(Btn::Seq2); r.release(Btn::Shift);
        CCHECK(r.c.selectedSeq() == 1);
        r.c.encoderTurn(3, 2, r.t);
        CCHECK(r.p->seqs[1].mode == Mode::Mono);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 20);
        CCHECK(r.p->seqs[1].mode == Mode::Multi);
        r.press(Btn::Back);
        // lanes view: tap row 3 (track 5 from bottom = y 3)
        r.tap(4, 3);
        CCHECK(r.p->seqs[1].pat().tracks[4].steps[4].active);
        // mute track via mute + column 1
        r.hold(Btn::Mute); r.tap(0, 3); r.release(Btn::Mute);
        CCHECK(r.p->seqs[1].pat().tracks[4].muted);
        Frame f; r.c.render(f, r.t);
        CCHECK(!f.header.empty());
    }
    { // poly: several notes per step, voice limit
        Rig r;
        r.p->seqs[0].mode = Mode::Poly;
        r.p->seqs[0].nVoices = 3;
        for (int y = 7; y >= 3; --y) r.tap(0, y);
        auto& st = r.tr0().steps[0];
        CCHECK(st.numNotes == 3);
        CCHECK(st.notes[0] == 2 && st.notes[2] == 4);
    }
    { // copy / paste steps keeps relative positions
        Rig r;
        r.tap(0, 7); r.tap(2, 6);
        r.hold(Btn::Copy); r.tap(0, 0); r.tap(2, 0); r.release(Btn::Copy);
        r.hold(Btn::Paste); r.tap(8, 0); r.release(Btn::Paste);
        CCHECK(r.tr0().steps[8].active && r.tr0().steps[10].active && r.tr0().steps[10].notes[0] == 1);
    }
    { // shift + page buttons rotate, shift+page expand
        Rig r;
        r.tap(0, 7);
        r.hold(Btn::Shift); r.press(Btn::P64); r.release(Btn::Shift);
        CCHECK(r.tr0().steps[1].active && !r.tr0().steps[0].active);
        r.hold(Btn::Shift); r.press(Btn::Page); r.release(Btn::Shift);
        CCHECK(r.tr0().end == 31 && r.tr0().div == Division::ThirtySecond && r.tr0().steps[2].active);
    }
    { // column editor sets velocity by row
        Rig r;
        r.c.encoderPress(0, true, r.t); r.c.encoderPress(0, false, r.t + 10); r.t += 400;
        r.tap(5, 0); // top row = 127
        CCHECK(r.tr0().steps[5].active && r.tr0().steps[5].velocity == 127);
    }
    { // load view switches patterns
        Rig r;
        r.press(Btn::Load);
        r.tap(3, 2); // seq 3, pattern 4 (stopped: immediate)
        CCHECK(r.p->seqs[2].curPattern == 3);
        Frame f; r.c.render(f, r.t);
    }
    { // transpose: hold seq button + tap pad
        Rig r;
        r.tap(0, 7);
        r.hold(Btn::Seq1); r.tap(0, 5); r.release(Btn::Seq1);
        CCHECK(r.tr0().steps[0].notes[0] == 2);
    }
    { // chord mode: tap places a chord root, step page 4 edits chord type, chord -> poly expands
        Rig r;
        r.hold(Btn::Shift); r.press(Btn::Seq1); r.release(Btn::Shift);
        r.c.encoderTurn(3, 3, r.t);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 20);
        CCHECK(r.p->seqs[0].mode == Mode::Chord);
        r.press(Btn::Back);
        r.p->seqs[0].scale = 1;
        r.tap(0, 6);  // degree 1 = D minor
        r.tap(0, 4);  // same column, another row replaces (one chord per step)
        auto& st = r.tr0().steps[0];
        CCHECK(st.numNotes == 1 && st.notes[0] == 3);
        r.c.padDown(0, 4, r.t); r.t += 600;
        r.press(Btn::Page); r.press(Btn::Page); r.press(Btn::Page); // page 4
        r.c.encoderTurn(1, 1, r.t);   // type -> 7th
        r.c.encoderTurn(2, 1, r.t);   // voicing -> I inversion
        r.c.padUp(0, 4, r.t); r.t += 400;
        CCHECK(st.chordType == 1 && st.voicing == 1);
        Frame f; r.c.render(f, r.t);
        // global bass via shift + step
        r.hold(Btn::Shift); r.press(Btn::Step); r.release(Btn::Shift);
        r.c.encoderTurn(3, 1, r.t);
        CCHECK(r.p->seqs[0].bassMode == 1);
        r.press(Btn::Back);
        // convert to poly: F maj7 1st inv -> 4 notes
        r.hold(Btn::Shift); r.press(Btn::Seq1); r.release(Btn::Shift);
        r.c.encoderTurn(3, -2, r.t);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 20);
        CCHECK(r.p->seqs[0].mode == Mode::Poly);
        CCHECK(r.tr0().steps[0].numNotes == 4);
    }
    { // stochastic: grid is a control surface; copy the phrase into a mono sequencer
        Rig r;
        r.p->seqs[0].mode = Mode::Stoch;
        auto& sc = r.p->seqs[0].pat().stoch;
        r.tap(4, 0);              // E at 100 %
        CCHECK(sc.noteLevel[4] == 8);
        r.tap(4, 0);              // same level again = off
        CCHECK(sc.noteLevel[4] == 0);
        r.tap(12, 2);             // +2 octaves
        CCHECK(sc.octUp == 2);
        r.tap(14, 7);             // pitch randomisation 0 % = locked
        CCHECK(sc.pitchRand == 0);
        r.tap(15, 0);             // rhythm 100 %
        CCHECK(sc.rhythmRand == 7);
        CCHECK(!r.tr0().steps[4].active); // pads did not create steps
        Frame f; r.c.render(f, r.t);
        CCHECK(f.labels[3] == "MOD");
        // fake a generated phrase and copy it to sequencer 2
        sc.valid = true; sc.rhythm[0] = 1; sc.pitch[0] = 7; sc.rhythm[3] = 22; sc.pitch[3] = 12;
        r.hold(Btn::Copy); r.press(Btn::Seq1); r.release(Btn::Copy);
        r.hold(Btn::Paste); r.press(Btn::Seq2); r.release(Btn::Paste);
        auto& t2 = r.p->seqs[1].pat().tracks[0];
        CCHECK(t2.steps[0].active && t2.steps[0].notes[0] == 7 && t2.steps[3].tie == 1 && t2.steps[3].notes[0] == 12);
    }
    { // matriceal: 4 sections, lane pads, step editing
        Rig r;
        r.p->seqs[0].mode = Mode::Matr;
        auto& mat = r.p->seqs[0].pat().mat;
        r.tap(5, 0);                     // section 2 (track 2), step 2 -> trigger on
        CCHECK(mat.tracks[1].lanes[kLaneTrig].val[1] == 1);
        r.tap(5, 4);                     // track 2 lane pad row 4 col 1 = NOTE
        r.c.padDown(5, 0, r.t); r.t += 600;
        r.c.encoderTurn(0, 3, r.t);      // note +3
        r.c.encoderTurn(1, -4, r.t);     // prob 80 %
        r.c.padUp(5, 0, r.t); r.t += 400;
        CCHECK(mat.tracks[1].lanes[kLaneNote].val[1] == 3 && mat.tracks[1].lanes[kLaneNote].prob[1] == 80);
        r.tap(5, 0);                     // tap on a set note step resets it
        CCHECK(mat.tracks[1].lanes[kLaneNote].val[1] == 0);
        // lane settings: hold lane pad + knob 3/4
        r.c.padDown(5, 4, r.t); r.t += 600; r.c.encoderTurn(2, -1, r.t); r.c.encoderTurn(3, 1, r.t); r.c.padUp(5, 4, r.t); r.t += 400;
        CCHECK(mat.tracks[1].lanes[kLaneNote].div == Division::EighthT && mat.tracks[1].lanes[kLaneNote].dir == Dir::Reverse);
        // euclid on the trigger lane of track 1
        r.tap(0, 4);                     // track 1 trigger lane
        r.c.padDown(0, 0, r.t); r.t += 600; r.c.encoderTurn(0, -8, r.t); r.c.encoderTurn(1, -1, r.t); r.c.padUp(0, 0, r.t); r.t += 400;
        int cnt = 0; for (int i = 0; i < 16; ++i) cnt += mat.tracks[0].lanes[kLaneTrig].val[(size_t) i];
        CCHECK(mat.tracks[0].lanes[kLaneTrig].end == 7 && cnt == 3);
        // mute track 3 via its track pad
        r.hold(Btn::Mute); r.tap(9, 7); r.release(Btn::Mute);
        CCHECK(mat.tracks[2].muted);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("T", 0) == 0);
    }
    { // domino: trigger section, note section + keyboard
        Rig r;
        r.p->seqs[0].mode = Mode::Domino;
        r.p->seqs[0].scale = 1;
        auto& pat = r.p->seqs[0].pat();
        r.tap(2, 2);                          // trigger step 3
        CCHECK(pat.tracks[1].steps[2].active);
        r.c.padDown(0, 0, r.t); r.t += 100;   // hold note step 1
        r.tap(2, 7);                          // keyboard: degree 2 (E)
        r.tap(4, 7);                          // poly: add degree 4 (G)
        r.c.padUp(0, 0, r.t); r.t += 400;
        CCHECK(pat.tracks[0].steps[0].active && pat.tracks[0].steps[0].numNotes == 2 && pat.tracks[0].steps[0].notes[1] == 4);
        r.tap(5, 6);                          // upper octave key = degree 12, becomes the "last" note
        r.tap(3, 0);                          // tap empty note step places the last note
        CCHECK(pat.tracks[0].steps[3].active && pat.tracks[0].steps[3].notes[0] == 12);
        // trigger repeats via hold + knob 1
        r.c.padDown(2, 2, r.t); r.t += 600; r.c.encoderTurn(0, 2, r.t); r.c.padUp(2, 2, r.t); r.t += 400;
        CCHECK(pat.tracks[1].steps[2].repeats == 3);
        // trigger section length with END
        r.hold(Btn::End); r.tap(7, 3); r.release(Btn::End);
        CCHECK(pat.tracks[1].end == 23);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Notes", 0) == 0);
    }
    { // saga: tap outside the track sets length, hold + knob 4 sets stage duration
        Rig r;
        r.p->seqs[0].mode = Mode::Saga;
        auto& tr = r.tr0();
        r.tap(3, 1);                       // stage 20 -> track length 1-20
        CCHECK(tr.end == 19 && !tr.steps[19].active);
        r.tap(2, 0);                       // activate stage 3
        CCHECK(tr.steps[2].active && tr.steps[2].numNotes == 1);
        r.c.padDown(2, 0, r.t); r.t += 600;
        r.c.encoderTurn(3, 3, r.t);        // dur 4
        r.tap(4, 7);                       // add a keyboard note -> chord
        r.c.padUp(2, 0, r.t); r.t += 400;
        CCHECK(tr.steps[2].dur == 4 && tr.steps[2].numNotes == 2);
        r.tap(2, 0);                       // deactivate keeps the duration
        CCHECK(!tr.steps[2].active && tr.steps[2].dur == 4);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.find("T.Durat:23") != std::string::npos);
    }
    { // accumulator pages and harmonizer selection
        Rig r;
        r.tap(0, 7);
        r.c.padDown(0, 7, r.t); r.t += 600;
        r.press(Btn::Page); r.press(Btn::Page); r.press(Btn::Page); // page 4 = accumulator
        r.c.encoderTurn(0, 2, r.t);   // Amt +2
        r.c.encoderTurn(1, 2, r.t);   // Mode -> Pendulum
        r.press(Btn::Page);           // page 5
        r.c.encoderTurn(0, 1, r.t);   // target Step
        r.c.padUp(0, 7, r.t); r.t += 400;
        auto& st = r.tr0().steps[0];
        CCHECK(st.accAmt == 2 && st.accMode == 2 && st.accTarget == 1);
        r.p->seqs[3].mode = Mode::Chord;
        r.hold(Btn::Shift); r.c.encoderTurn(2, -1, r.t); r.release(Btn::Shift);
        CCHECK(r.p->seqs[0].harm == 3);
        r.hold(Btn::Shift); r.c.encoderTurn(3, 1, r.t); r.release(Btn::Shift);
        CCHECK(r.p->seqs[0].harm == 4);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line2.rfind("HARMON", 0) == 0);
        r.hold(Btn::Shift); r.c.encoderTurn(2, 1, r.t); r.release(Btn::Shift);
        CCHECK(r.p->seqs[0].harm == -1);
    }
    { // keyboard view, arp menu, loop view
        Rig r;
        r.press(Btn::Arp);                     // mono: opens the arp menu + keyboard view
        r.c.encoderTurn(0, 1, r.t);            // type Up
        CCHECK(r.p->seqs[0].arp.type == kArpUp);
        r.c.padDown(0, 7, r.t);                // C0 key
        r.c.padDown(4, 7, r.t);                // E0
        int notes[8];
        CCHECK(r.e.arpNotes(0, notes, 8) == 2 && notes[0] == 24);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Keyboard Arp", 0) == 0);
        r.c.padUp(0, 7, r.t); r.c.padUp(4, 7, r.t);
        CCHECK(r.e.arpNotes(0, notes, 8) == 0);
        r.press(Btn::Arp);                     // close the menu
        r.c.render(f, r.t);
        CCHECK(f.line2 == "rows = octaves");
        r.press(Btn::Keyboard);                // back to the sequencer
        // loop view: hold INIT, press END, then two pads
        r.hold(Btn::Init); r.press(Btn::End); r.release(Btn::Init);
        r.c.padDown(2, 0, r.t); r.c.padDown(5, 0, r.t); r.c.padUp(5, 0, r.t); r.c.padUp(2, 0, r.t);
        CCHECK(r.tr0().loopStart == 2 && r.tr0().loopEnd == 5);
        r.c.encoderTurn(0, 1, r.t);            // Len x2
        CCHECK(r.tr0().loopEnd == 9);
        r.press(Btn::Init);                    // clear
        CCHECK(!r.tr0().hasLoop());
        r.press(Btn::Back);
        r.tap(1, 7);                           // normal editing again
        CCHECK(r.tr0().steps[1].active);
    }
    { // flow steps, random perform menu, perform view, snapshots, roller pads
        Rig r;
        r.hold(Btn::Flow); r.tap(2, 7); r.release(Btn::Flow);
        CCHECK(r.tr0().steps[2].active && r.tr0().steps[2].trig.type == TrigType::Flow);
        r.press(Btn::Random);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Random Perform", 0) == 0);
        r.c.encoderTurn(0, 4, r.t);
        CCHECK(r.p->seqs[0].rpVel == 20);
        r.c.encoderPress(0, true, r.t); r.c.encoderPress(0, false, r.t + 50); r.t += 400;
        CCHECK(r.p->seqs[0].rpVel == 0);
        r.press(Btn::Random);
        r.press(Btn::Perf);                    // perform view
        r.tap(11, 2);                          // seq 3 transpose +3
        CCHECK(r.p->seqs[2].perfTrans == 3);
        r.hold(Btn::Shift); r.tap(6, 2); r.release(Btn::Shift);
        CCHECK(r.p->seqs[2].perfRot == -2);
        r.tap(0, 4);
        CCHECK(r.p->seqs[4].muted);
        r.c.render(f, r.t);
        CCHECK(f.pads[2 * 16 + 11] == 0xffffff);
        r.press(Btn::Perf);
        // snapshot: LOAD + SAVE, edit, LOAD recalls
        r.hold(Btn::Load); r.press(Btn::Save); r.release(Btn::Load);
        CCHECK(r.c.hasSnapshot());
        r.tap(9, 7);
        CCHECK(r.tr0().steps[9].active);
        r.press(Btn::Load);
        CCHECK(!r.c.hasSnapshot() && !r.tr0().steps[9].active && r.p->seqs[2].perfTrans == 3);
        r.hold(Btn::Load); r.press(Btn::Save); r.release(Btn::Load);
        r.tap(9, 7);
        r.hold(Btn::Shift); r.press(Btn::Load); r.release(Btn::Shift); // keep the changes
        CCHECK(!r.c.hasSnapshot() && r.tr0().steps[9].active);
        // roller column in the keyboard view
        r.press(Btn::Keyboard);
        r.c.padDown(12, 2, r.t);
        CCHECK(r.e.rollDiv[0] == 0.25);
        r.c.padDown(0, 7, r.t);
        int notes[4];
        CCHECK(r.e.arpNotes(0, notes, 4) == 1);
        r.c.padUp(12, 2, r.t);
        CCHECK(r.e.rollDiv[0] == 0.0 && r.e.arpNotes(0, notes, 4) == 0);
        r.c.padUp(0, 7, r.t);
    }
    { // generators, recording keys, save view, project menu
        Rig r;
        r.press(Btn::Gen);
        r.c.encoderTurn(1, -3, r.t);              // ePuls 4 -> 1: applied immediately
        int n = 0; for (int i = 0; i < 16; ++i) n += r.tr0().steps[(size_t) i].active;
        CCHECK(n == 1);
        r.hold(Btn::Shift); r.c.encoderTurn(1, 3, r.t); r.release(Btn::Shift); // set only
        n = 0; for (int i = 0; i < 16; ++i) n += r.tr0().steps[(size_t) i].active;
        CCHECK(n == 1 && r.p->seqs[0].gen.ePuls == 4);
        r.press(Btn::Undo);                        // one undo restores the empty pattern
        n = 0; for (int i = 0; i < 16; ++i) n += r.tr0().steps[(size_t) i].active;
        CCHECK(n == 0);
        r.press(Btn::Gen);
        // random generator: tap knob 4 generates
        r.hold(Btn::Shift); r.press(Btn::Random); r.release(Btn::Shift);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Random Generator", 0) == 0);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        n = 0; for (int i = 0; i < 16; ++i) n += r.tr0().steps[(size_t) i].active;
        CCHECK(n > 3);
        r.hold(Btn::Shift); r.press(Btn::Random); r.release(Btn::Shift);
        // REC tap arms (transport stopped), tap again disarms
        r.press(Btn::Rec);
        CCHECK(r.e.recState() == 1);
        r.press(Btn::Rec);
        CCHECK(r.e.recState() == 0);
        // REC + PLAY: step recording
        r.hold(Btn::Rec); r.press(Btn::Play); r.release(Btn::Rec);
        CCHECK(r.e.stepRecording());
        r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Step REC", 0) == 0);
        r.hold(Btn::Rec); r.press(Btn::Play); r.release(Btn::Rec);
        CCHECK(!r.e.stepRecording());
        // save view: double tap copies the current pattern to slot 3
        r.press(Btn::Save);
        r.tap(3, 0);
        CCHECK(r.p->seqs[0].patterns[3].isEmpty());
        r.c.padDown(3, 0, r.t); r.c.padUp(3, 0, r.t + 20); r.t += 100;
        r.c.padDown(3, 0, r.t); r.c.padUp(3, 0, r.t + 20); r.t += 400;
        CCHECK(!r.p->seqs[0].patterns[3].isEmpty());
        r.press(Btn::Save);
        // project menu: template save makes a job, clear needs a second tap
        r.hold(Btn::Shift); r.press(Btn::Perf); r.release(Btn::Shift);
        r.c.encoderTurn(0, 4, r.t);                // item 5: save as template
        r.c.encoderTurn(1, 2, r.t);                // slot 3
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        auto jobs = r.c.takeJobs();
        CCHECK(jobs.size() == 1 && jobs[0].type == Controller::Job::SaveTemplate && jobs[0].slot == 2);
        r.c.encoderTurn(0, -1, r.t);               // item 4: clear project
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        CCHECK(r.tr0().steps[0].active || n > 0);   // not yet
        const bool before = !r.p->seqs[0].patterns[3].isEmpty();
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        CCHECK(before && r.p->seqs[0].patterns[3].isEmpty());
    }
    { // modulation view, LFO menu, matrix learn, quick assign
        Rig r;
        r.press(Btn::Mod);
        r.tap(3, 0);                               // top row = max
        auto& ln = r.p->seqs[0].pat().lanes[0];
        CCHECK(ln.val[3] == 127 && !ln.muted);
        r.hold(Btn::Shift); r.tap(4, 1); r.release(Btn::Shift); // negative, 2/8 of 127
        CCHECK(ln.val[4] == -31);
        r.c.encoderTurn(0, -5, r.t);              // global: every step -10 (clamped to min 0)
        CCHECK(ln.val[3] == 117 && ln.val[4] == 0);
        r.c.encoderPress(1, true, r.t); r.c.encoderPress(1, false, r.t + 900); r.t += 1000; // long press: mute lane 2
        CCHECK(r.p->seqs[0].pat().lanes[1].muted == false);
        r.press(Btn::Page);                        // lane settings
        r.c.encoderTurn(0, 6, r.t);                // Int Dest 1
        r.c.encoderTurn(1, 1, r.t);
        CCHECK(r.p->seqs[0].pat().lanes[0].int1 == MD_Vel); // knob 1 turned last: lane 1 is selected
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line2.find("Int Dest 1: Vel") != std::string::npos);
        r.press(Btn::Page); r.press(Btn::Mod);
        r.press(Btn::Lfo);
        r.c.encoderTurn(2, 10, r.t);
        CCHECK(r.p->seqs[0].lfo[0].amt == 20);
        r.press(Btn::Lfo);
        r.hold(Btn::Shift); r.press(Btn::Mod); r.release(Btn::Shift);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        CCHECK(r.e.learnSeq == 0 && r.e.learnMap == 0);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        CCHECK(r.e.learnSeq == -1);
        r.hold(Btn::Shift); r.press(Btn::Mod); r.release(Btn::Shift);
        // quick assign: arp gate <- LFO 1
        r.press(Btn::Arp);
        r.hold(Btn::Shift); r.c.encoderPress(2, true, r.t); r.c.encoderPress(2, false, r.t + 900); r.t += 1000; r.release(Btn::Shift);
        r.c.render(f, r.t);
        CCHECK(f.line1 == "QUICK ASSIGN: ArpGat");
        r.c.encoderTurn(0, 1, r.t);
        CCHECK(r.p->seqs[0].lfo[0].inDst == MD_ArpGate && r.p->seqs[0].lfo[0].amt == 50);
        r.c.encoderTurn(0, 3, r.t);                // -> mod lane 2
        CCHECK(r.p->seqs[0].lfo[0].inDst == 0 && r.p->seqs[0].pat().lanes[1].int1 == MD_ArpGate);
        r.c.encoderPress(3, true, r.t); r.c.encoderPress(3, false, r.t + 50); r.t += 400;
        r.c.render(f, r.t);
        CCHECK(f.line1.rfind("Keyboard Arp", 0) == 0);
    }
    { // arranger view and the drum generator keys
        Rig r;
        r.press(Btn::Arranger);
        r.tap(0, 0);                               // empty slot: current pattern added
        CCHECK(r.p->song().lanes[0][0].pat == 0 && r.p->song().lanes[0][0].rept == 1);
        r.c.padDown(0, 0, r.t);
        r.c.encoderTurn(3, 3, r.t);                // repeats 4
        r.c.encoderTurn(2, 2, r.t);                // pattern 3
        r.c.padUp(0, 0, r.t); r.t += 400;
        CCHECK(r.p->song().lanes[0][0].rept == 4 && r.p->song().lanes[0][0].pat == 2);
        r.press(Btn::Page);                        // pattern grid: append pattern 6 to lane 2
        r.tap(5, 1);
        CCHECK(r.p->song().lanes[1][0].pat == 5);
        r.tap(5, 1);
        CCHECK(r.p->song().lanes[1][1].pat == 5);
        r.press(Btn::Page);
        r.hold(Btn::Arranger); r.tap(4, 3); r.release(Btn::Arranger); // slot column
        int filled = 0; for (int l = 0; l < 8; ++l) filled += r.p->song().lanes[(size_t) l][4].pat >= 0;
        CCHECK(filled == 8 && r.p->song().lanes[0][4].pat == 0);
        r.hold(Btn::Shift); r.press(Btn::Arranger); r.release(Btn::Shift);
        CCHECK(r.p->arrOn);
        Frame f; r.c.render(f, r.t);
        CCHECK(f.line1.rfind("ARR: ARMED  Song 1", 0) == 0);
        r.press(Btn::Arranger);
        // drum generator: multitrack lanes, RANDOM + column 2
        r.p->seqs[0].mode = Mode::Multi;
        r.hold(Btn::Random); r.tap(1, 7); r.release(Btn::Random);
        CCHECK(r.p->seqs[0].drum.on[0]);
        r.hold(Btn::Seq1); r.tap(1, 6); r.release(Btn::Seq1);
        r.c.encoderTurn(0, -5, r.t);
        r.c.render(f, r.t);
        CCHECK(r.p->seqs[0].drum.dens[1] == 54 && f.line1.rfind("Drum Gen T2 Snare", 0) == 0);
    }
    return cfail;
}

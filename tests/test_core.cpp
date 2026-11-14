// Headless tests for the TEK core (engine + controller). Build: see CMakeLists (target tek_tests).
#include "../core/Engine.h"
#include "../core/Generators.h"
#include "../core/Controller.h"
#include "../core/Chords.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <memory>

using namespace tek;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Note { double ppq; int ch, note, vel; bool on; };

// Runs the engine for `beats` quarter notes at 120 bpm, 48 kHz, 512 sample blocks.
static std::vector<Note> run(Engine& e, double beats, double startPpq = 0.0, int block = 512, bool window = true) {
    std::vector<Note> notes;
    const double sr = 48000.0, bpm = 120.0;
    const double ppqPerSample = bpm / 60.0 / sr;
    e.prepare(sr);
    double ppq = startPpq;
    std::vector<MidiIn> in;
    std::vector<MidiOut> out;
    while (ppq < startPpq + beats) {
        out.clear();
        Transport t; t.playing = true; t.ppq = ppq; t.bpm = bpm;
        e.process(t, block, in, out);
        for (auto& m : out) {
            int type = m.status & 0xF0;
            double at = ppq + m.sampleOffset * ppqPerSample;
            if (window && type == 0x90 && at >= startPpq + beats - 1e-3) continue; // outside the requested window
            if (type == 0x90 || type == 0x80)
                notes.push_back({ppq + m.sampleOffset * ppqPerSample, m.status & 15, m.data1, m.data2, type == 0x90});
        }
        ppq += block * ppqPerSample;
    }
    out.clear();
    Transport stop; stop.playing = false; stop.ppq = ppq; stop.bpm = bpm;
    e.process(stop, block, in, out);
    for (auto& m : out) if ((m.status & 0xF0) == 0x80) notes.push_back({ppq, m.status & 15, m.data1, 0, false});
    return notes;
}

static std::vector<Note> ons(const std::vector<Note>& v) { std::vector<Note> r; for (auto& n : v) if (n.on) r.push_back(n); return r; }

static void setNote(Track& tr, int i, int degree, int vel = 100) {
    auto& s = tr.steps[(size_t) i];
    s.active = true; s.numNotes = 1; s.notes[0] = (int8_t) degree; s.velocity = (uint8_t) vel;
}

static void testDirections() {
    // start 0, end 3
    int fwd[8], rev[8], pend[8], pong[8];
    for (int c = 0; c < 8; ++c) {
        fwd[c] = Engine::positionFor(Dir::Forward, 0, 3, c);
        rev[c] = Engine::positionFor(Dir::Reverse, 0, 3, c);
        pend[c] = Engine::positionFor(Dir::Pendulum, 0, 3, c);
        pong[c] = Engine::positionFor(Dir::Pong, 0, 3, c);
    }
    int efwd[8] = {0,1,2,3,0,1,2,3}, erev[8] = {3,2,1,0,3,2,1,0}, epend[8] = {0,1,2,3,3,2,1,0}, epong[8] = {0,1,2,3,2,1,0,1};
    for (int c = 0; c < 8; ++c) { CHECK(fwd[c] == efwd[c]); CHECK(rev[c] == erev[c]); CHECK(pend[c] == epend[c]); CHECK(pong[c] == epong[c]); }
}

static void testBasicTiming() {
    auto p = std::make_unique<Project>();
    auto& tr = p->seqs[0].pat().tracks[0];
    setNote(tr, 0, 0); setNote(tr, 4, 2); setNote(tr, 8, 4); setNote(tr, 12, 7);
    Engine e(*p);
    auto n = ons(run(e, 4.0));
    CHECK(n.size() == 4);
    if (n.size() == 4) {
        CHECK(std::fabs(n[0].ppq - 0.0) < 0.01);
        CHECK(std::fabs(n[1].ppq - 1.0) < 0.01);
        CHECK(std::fabs(n[2].ppq - 2.0) < 0.01);
        CHECK(std::fabs(n[3].ppq - 3.0) < 0.01);
        CHECK(n[0].note == 48); // C2 base, chromatic
        CHECK(n[1].note == 50);
        CHECK(n[3].note == 55);
        CHECK(n[0].vel == 100);
    }
    // note-offs balance note-ons
    auto all = run(e, 8.0, 0.0, 512, false);
    int bal = 0; for (auto& x : all) bal += x.on ? 1 : -1;
    CHECK(bal == 0);
}

static void testScaleAndChannel() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[2];
    q.scale = 1; // major
    q.rootPc = 2; q.baseOctave = 3; // D3 = 62
    auto& tr = q.pat().tracks[0];
    setNote(tr, 0, 0); setNote(tr, 1, 2); setNote(tr, 2, 7);
    Engine e(*p);
    auto n = ons(run(e, 1.0));
    CHECK(n.size() == 3);
    if (n.size() == 3) {
        CHECK(n[0].note == 62); CHECK(n[1].note == 66); CHECK(n[2].note == 74);
        CHECK(n[0].ch == 2);
    }
}

static void testSwing() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.pat().swing = 75;
    auto& tr = q.pat().tracks[0];
    setNote(tr, 0, 0); setNote(tr, 1, 0);
    Engine e(*p);
    auto n = ons(run(e, 0.5));
    CHECK(n.size() == 2);
    if (n.size() == 2) CHECK(std::fabs(n[1].ppq - (0.25 + 0.125)) < 0.01); // 75% swing: second 16th at 3/4 of the pair
}

static void testProbabilityAndConditions() {
    auto p = std::make_unique<Project>();
    auto& tr = p->seqs[0].pat().tracks[0];
    tr.end = 3; // 4-step pattern = 1 beat cycle
    setNote(tr, 0, 0);
    tr.steps[0].trig = {TrigType::AB, 2, 4}; // 2nd of every 4 cycles
    Engine e(*p);
    auto n = ons(run(e, 8.0)); // 8 cycles -> plays on cycle index 1 and 5
    CHECK(n.size() == 2);
    if (n.size() == 2) { CHECK(std::fabs(n[0].ppq - 1.0) < 0.01); CHECK(std::fabs(n[1].ppq - 5.0) < 0.01); }

    tr.steps[0].trig = {TrigType::Prob, 0, 0};
    Engine e2(*p);
    CHECK(ons(run(e2, 4.0)).empty());

    tr.steps[0].trig = {TrigType::First, 0, 0};
    Engine e3(*p);
    CHECK(ons(run(e3, 4.0)).size() == 1);

    tr.steps[0].trig = {TrigType::Flow, 0, 0};
    Engine e4(*p);
    CHECK(ons(run(e4, 2.0)).empty());
    e4.flowHeld = true;
    { auto r4 = ons(run(e4, 2.0)); if (r4.size() != 2) for (auto& x : r4) std::printf("flow %.3f %d\n", x.ppq, x.note); CHECK(r4.size() == 2); }
}

static void testRepeatsRetrigTie() {
    auto p = std::make_unique<Project>();
    auto& tr = p->seqs[0].pat().tracks[0];
    setNote(tr, 0, 0);
    tr.steps[0].repeats = 4; tr.steps[0].repDur = 4;
    Engine e(*p);
    auto n = ons(run(e, 1.0));
    CHECK(n.size() == 4);
    if (n.size() == 4) CHECK(std::fabs(n[1].ppq - 0.25) < 0.01);

    tr.steps[0].repeats = 1; tr.steps[0].retrig = 3; tr.steps[0].gate = 99;
    Engine e2(*p);
    CHECK(ons(run(e2, 1.0)).size() == 3);

    tr.steps[0].retrig = 1; tr.steps[0].tie = 3; tr.steps[0].gate = 50;
    Engine e3(*p);
    auto all = run(e3, 1.0);
    double onT = -1, offT = -1;
    for (auto& x : all) { if (x.on && onT < 0) onT = x.ppq; if (!x.on && offT < 0) offT = x.ppq; }
    CHECK(onT >= 0 && offT > onT + 0.95); // held across 4 sixteenths
}

static void testMultitrackAndPoly() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[1];
    q.mode = Mode::Multi;
    for (int t = 0; t < 3; ++t) { auto& tr = q.pat().tracks[(size_t) t]; tr.channel = (uint8_t) (9); setNote(tr, 0, 0); }
    q.pat().tracks[1].div = Division::Eighth;
    setNote(q.pat().tracks[1], 1, 0);
    Engine e(*p);
    auto n = ons(run(e, 1.0));
    CHECK(n.size() == 4);
    bool sawEighth = false;
    for (auto& x : n) if (x.note == 38 && std::fabs(x.ppq - 0.5) < 0.01) sawEighth = true;
    CHECK(sawEighth);

    q.pat().tracks[2].muted = true;
    Engine e2(*p);
    CHECK(ons(run(e2, 0.2)).size() == 2);

    auto p2 = std::make_unique<Project>();
    auto& pq = p2->seqs[0];
    pq.mode = Mode::Poly;
    auto& st = pq.pat().tracks[0].steps[0];
    st.active = true; st.numNotes = 3; st.notes = {0, 4, 7, 0, 0, 0, 0};
    Engine e3(*p2);
    CHECK(ons(run(e3, 0.2)).size() == 3);
    pq.nVoices = 2;
    Engine e4(*p2);
    CHECK(ons(run(e4, 0.2)).size() == 2);
}

static void testTimelineAlignment() {
    auto p = std::make_unique<Project>();
    auto& tr = p->seqs[0].pat().tracks[0];
    setNote(tr, 4, 0);
    Engine e(*p);
    auto n = ons(run(e, 2.0, 16.0)); // start at bar 5: step 4 should play at ppq 17
    CHECK(!n.empty());
    if (!n.empty()) CHECK(std::fabs(n[0].ppq - 17.0) < 0.01);
}

static void testSerialization() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[3];
    q.mode = Mode::Poly; q.scale = 5; q.rootPc = 7; q.nudgeMs = -12;
    auto& st = q.patterns[2].tracks[0].steps[17];
    st.active = true; st.numNotes = 2; st.notes[0] = -3; st.notes[1] = 4; st.velocity = 99; st.trig = {TrigType::NotAB, 3, 5};
    st.repeats = 6; st.ramp = -40;
    q.patterns[2].tracks[0].end = 40;
    std::string s = serialize(*p);
    auto p2 = std::make_unique<Project>();
    CHECK(deserialize(s, *p2));
    auto& q2 = p2->seqs[3];
    CHECK(q2.mode == Mode::Poly); CHECK(q2.scale == 5); CHECK(q2.rootPc == 7); CHECK(q2.nudgeMs == -12);
    auto& st2 = q2.patterns[2].tracks[0].steps[17];
    CHECK(st2.active); CHECK(st2.numNotes == 2); CHECK(st2.notes[0] == -3); CHECK(st2.notes[1] == 4);
    CHECK(st2.velocity == 99); CHECK(st2.trig.type == TrigType::NotAB); CHECK(st2.trig.a == 3); CHECK(st2.repeats == 6);
    CHECK(st2.ramp == -40); CHECK(q2.patterns[2].tracks[0].end == 40);
    CHECK(serialize(*p2) == s);
}

static void testScales() {
    CHECK(degreeToSemis(1, 7) == 12);
    CHECK(degreeToSemis(1, -1) == -1);
    CHECK(degreeToSemis(1, -7) == -12);
    CHECK(degreeToSemis(0, 13) == 13);
    CHECK(semisToDegree(1, 12) == 7);
}

static void testChords() {
    int v[8];
    auto eq = [&](int n, std::vector<int> want) { return std::vector<int>(v, v + n) == want; };
    CHECK(eq(buildChord(1, 0, 0, 0, 0, v), {0, 4, 7}));          // C major
    CHECK(eq(buildChord(1, 1, 0, 0, 0, v), {2, 5, 9}));          // D minor
    CHECK(eq(buildChord(1, 6, 0, 0, 0, v), {11, 14, 17}));       // B dim
    CHECK(eq(buildChord(1, 0, 1, 0, 0, v), {0, 4, 7, 11}));      // Cmaj7
    CHECK(eq(buildChord(1, 0, 0, 1, 0, v), {4, 7, 12}));         // 1st inversion
    CHECK(eq(buildChord(1, 0, 0, 2, 0, v), {7, 12, 16}));        // 2nd inversion
    CHECK(eq(buildChord(1, 0, 0, 3, 0, v), {0, 7, 16}));         // open
    CHECK(eq(buildChord(1, 0, 0, 0, 2, v), {0, 4, 7, 12, 16}));  // spread 2
    CHECK(eq(buildChord(1, 0, 9, 0, 0, v), {0, 5, 7}));          // sus4
    CHECK(buildChord(1, 0, 4, 0, 6, v) <= 8);                    // 13th + spread capped at 8 notes
    CHECK(chordName(1, 48, 1, 0) == "D2 minor");
    CHECK(chordName(1, 48, 4, 1) == "G2 major 7");
    CHECK(chordName(2, 48, 1, 0) == "D2 dim");                   // ii of minor

    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.mode = Mode::Chord; q.scale = 1; q.bassMode = 1;
    auto& st = q.pat().tracks[0].steps[0];
    st.active = true; st.numNotes = 1; st.notes[0] = 4; st.velocity = 90; // G major
    Engine e(*p);
    auto n = ons(run(e, 0.2));
    std::vector<int> notes; for (auto& x : n) notes.push_back(x.note);
    std::sort(notes.begin(), notes.end());
    CHECK((notes == std::vector<int>{43, 55, 59, 62}));

    q.bassMode = 0; q.chMx = 2; st.strum = 100; // "3 Lo"
    Engine e2(*p);
    auto n2 = ons(run(e2, 0.25));
    CHECK(n2.size() == 3);
    if (n2.size() == 3) {
        CHECK(n2[0].note == 55 && n2[0].ch == 0);
        CHECK(n2[1].note == 59 && n2[1].ch == 1 && n2[1].ppq > n2[0].ppq + 0.05);
        CHECK(n2[2].ch == 2);
    }
}

static void testStochastic() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.mode = Mode::Stoch;
    auto& sc = q.pat().stoch;
    sc.noteLevel = {}; sc.noteLevel[7] = 8; // only G
    sc.pitchRand = 0; sc.rhythmRand = 0;  // generate once, then locked
    Engine e(*p);
    auto n = ons(run(e, 8.0));
    CHECK(!n.empty());
    bool allG = true; for (auto& x : n) if (x.note != 55) allG = false;
    CHECK(allG);
    CHECK(sc.valid);
    // locked: the second bar repeats the first
    std::vector<double> b1, b2;
    for (auto& x : n) { if (x.ppq < 3.99) b1.push_back(x.ppq); else if (x.ppq < 7.99) b2.push_back(x.ppq - 4.0); }
    if (b1.size() != b2.size()) { for (auto x : b1) std::printf("%.2f ", x); std::printf("\n"); for (auto x : b2) std::printf("%.2f ", x); std::printf("\n"); }
    CHECK(b1.size() == b2.size());
    for (size_t i = 0; i < b1.size() && i < b2.size(); ++i) CHECK(std::fabs(b1[i] - b2[i]) < 0.01);
    // octave range spreads pitches, randomisation changes the phrase
    sc.noteLevel[0] = 8; sc.octUp = 2; sc.pitchRand = 7; sc.rhythmRand = 7; sc.valid = false;
    Engine e2(*p);
    auto n2 = ons(run(e2, 16.0));
    int maxNote = 0, minNote = 127;
    for (auto& x : n2) { maxNote = std::max(maxNote, x.note); minNote = std::min(minNote, x.note); }
    CHECK(minNote >= 48 && maxNote <= 79 && maxNote > 60);
    // serialization keeps the phrase
    auto s1 = serialize(*p);
    auto p2 = std::make_unique<Project>();
    CHECK(deserialize(s1, *p2));
    CHECK(p2->seqs[0].pat().stoch.valid && p2->seqs[0].pat().stoch.rhythm == sc.rhythm && p2->seqs[0].pat().stoch.octUp == 2);
}

static void testMatriceal() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.mode = Mode::Matr;
    auto& mt = q.pat().mat.tracks[0];
    auto& trig = mt.lanes[kLaneTrig];
    for (int i : {0, 4, 8, 12}) trig.val[(size_t) i] = 1;
    auto& note = mt.lanes[kLaneNote];
    note.end = 2; note.val[0] = 0; note.val[1] = 2; note.val[2] = 4; // 3-step note lane against a 16-step trigger lane
    Engine e(*p);
    auto n = ons(run(e, 4.0));
    CHECK(n.size() == 4);
    if (n.size() == 4) { CHECK(n[0].note == 48); CHECK(n[1].note == 50); CHECK(n[2].note == 52); CHECK(n[3].note == 48);
                         CHECK(std::fabs(n[1].ppq - 1.0) < 0.01); }

    // pulses: step 0 held for 3 pulses (type All) -> 3 notes, lanes frozen meanwhile
    auto p2 = std::make_unique<Project>();
    auto& q2 = p2->seqs[0]; q2.mode = Mode::Matr;
    auto& m2 = q2.pat().mat.tracks[0];
    m2.lanes[kLaneTrig].val[0] = 1; m2.lanes[kLaneTrig].val[1] = 1;
    m2.lanes[kLanePuls].val[0] = 3; m2.lanes[kLanePuls].aux[0] = 1;
    Engine e2(*p2);
    auto n2 = ons(run(e2, 1.0)); // 4 sixteenths: pulse, pulse, pulse, then step 1
    CHECK(n2.size() == 4);
    m2.lanes[kLanePuls].aux[0] = 0; // First: only the first pulse sounds
    Engine e3(*p2);
    CHECK(ons(run(e3, 1.0)).size() == 2);

    // interval accumulator wraps between min and max; RLen restarts
    auto p3 = std::make_unique<Project>();
    auto& q3 = p3->seqs[0]; q3.mode = Mode::Matr;
    auto& m3 = q3.pat().mat.tracks[0];
    for (int i = 0; i < 16; ++i) m3.lanes[kLaneTrig].val[(size_t) i] = 1;
    m3.lanes[kLaneIntv].val.fill(1); m3.intMin = 0; m3.intMax = 2;
    Engine e4(*p3);
    auto n4 = ons(run(e4, 1.5));
    CHECK(n4.size() == 6);
    if (n4.size() == 6) { CHECK(n4[0].note == 49); CHECK(n4[1].note == 50); CHECK(n4[2].note == 48); CHECK(n4[3].note == 49); }
    m3.rlen = 2;
    Engine e5(*p3);
    auto n5 = ons(run(e5, 1.0));
    if (n5.size() == 4) { CHECK(n5[0].note == 49); CHECK(n5[1].note == 50); CHECK(n5[2].note == 49); }

    // serialization
    auto s1 = serialize(*p);
    auto pr = std::make_unique<Project>();
    CHECK(deserialize(s1, *pr));
    CHECK(pr->seqs[0].pat().mat.tracks[0].lanes[kLaneNote].end == 2 && pr->seqs[0].pat().mat.tracks[0].lanes[kLaneNote].val[2] == 4);
    CHECK(serialize(*pr) == s1);

    bool e16[16]; euclid(8, 3, 0, e16);
    int cnt = 0; for (int i = 0; i < 8; ++i) cnt += e16[i]; CHECK(cnt == 3 && e16[0]);
}

static void testDomino() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.mode = Mode::Domino;
    auto& noteT = q.pat().tracks[0];
    auto& trigT = q.pat().tracks[1];
    trigT.end = 3; noteT.end = 2;
    for (int i : {0, 2}) { trigT.steps[(size_t) i].active = true; trigT.steps[(size_t) i].numNotes = 1; }
    for (int i = 0; i < 3; ++i) setNote(noteT, i, i * 2);
    Engine e(*p);
    auto n = ons(run(e, 2.0));
    CHECK(n.size() == 4);
    if (n.size() == 4) { CHECK(n[0].note == 48); CHECK(n[1].note == 50); CHECK(n[2].note == 52); CHECK(n[3].note == 48);
                         CHECK(std::fabs(n[1].ppq - 0.5) < 0.01); }
    // a trigger repeat advances the note section too
    trigT.steps[0].repeats = 3; trigT.steps[0].repDur = 1;
    Engine e2(*p);
    auto n2 = ons(run(e2, 0.4));
    CHECK(n2.size() == 3);
    if (n2.size() == 3) { CHECK(n2[0].note == 48); CHECK(n2[1].note == 50); CHECK(n2[2].note == 52); }
    // mono voice plays only the first note of a chord step
    trigT.steps[0].repeats = 1;
    noteT.steps[0].numNotes = 3; noteT.steps[0].notes = {0, 4, 7, 0, 0, 0, 0};
    Engine e3(*p);
    CHECK(ons(run(e3, 0.2)).size() == 3);
    q.voice = 0;
    Engine e4(*p);
    CHECK(ons(run(e4, 0.2)).size() == 1);
}

static void testSaga() {
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[0];
    q.mode = Mode::Saga;
    auto& tr = q.pat().tracks[0];
    tr.end = 2;
    setNote(tr, 0, 0); setNote(tr, 1, 2); setNote(tr, 2, 4);
    tr.steps[1].dur = 3; tr.steps[2].dur = 2;     // cycle = 1 + 3 + 2 = 6 sixteenths
    Engine e(*p);
    auto n = ons(run(e, 3.0));
    CHECK(n.size() == 6);
    if (n.size() == 6) {
        CHECK(std::fabs(n[0].ppq - 0.0) < 0.01); CHECK(std::fabs(n[1].ppq - 0.25) < 0.01); CHECK(std::fabs(n[2].ppq - 1.0) < 0.01);
        CHECK(std::fabs(n[3].ppq - 1.5) < 0.01); CHECK(n[1].note == 50); CHECK(n[2].note == 52);
    }
    // note length follows the stage duration (gate 50 % of 3 sixteenths)
    auto all = run(e, 1.0, 0.0, 512, false);
    double on1 = -1, off1 = -1;
    for (auto& x : all) { if (x.note == 50 && x.on && on1 < 0) on1 = x.ppq; if (x.note == 50 && !x.on && on1 >= 0 && off1 < 0) off1 = x.ppq; }
    CHECK(on1 >= 0 && std::fabs((off1 - on1) - 0.375) < 0.02);
    // serialization keeps durations
    auto p2 = std::make_unique<Project>();
    CHECK(deserialize(serialize(*p), *p2));
    CHECK(p2->seqs[0].pat().tracks[0].steps[1].dur == 3);
}

static void testPlayer() {
    // 480 ppqn source: quarter notes C3, E3 and an eighth G3 at beat 2.5 -> one bar at 96 ppqn
    std::vector<ClipNote> src = {{0, 480, 60, 100}, {480, 480, 64, 90}, {1200, 240, 67, 80}};
    auto clip = makeClip(src, 480, "test");
    CHECK(clip.lengthTicks == 384);
    CHECK(clip.notes.size() == 3 && clip.notes[1].tick == 96 && clip.notes[2].tick == 240 && clip.notes[2].len == 48);
    auto p = std::make_unique<Project>();
    auto& q = p->seqs[2];
    q.mode = Mode::Player;
    q.pat().clip = clip;
    Engine e(*p);
    auto n = ons(run(e, 8.0));
    CHECK(n.size() == 6); // two loops
    if (n.size() == 6) {
        CHECK(n[0].note == 60 && n[0].ch == 2); CHECK(std::fabs(n[1].ppq - 1.0) < 0.01); CHECK(std::fabs(n[2].ppq - 2.5) < 0.01);
        CHECK(std::fabs(n[3].ppq - 4.0) < 0.01);
    }
    auto p2 = std::make_unique<Project>();
    CHECK(deserialize(serialize(*p), *p2));
    CHECK(p2->seqs[2].pat().clip.notes.size() == 3 && p2->seqs[2].pat().clip.name == "test" && p2->seqs[2].pat().clip.lengthTicks == 384);
}

static std::vector<int> accSeq(int mode, int amt, int mx, int mn, int cycles) {
    auto p = std::make_unique<Project>();
    auto& tr = p->seqs[0].pat().tracks[0];
    tr.end = 3;
    setNote(tr, 0, 0);
    auto& st = tr.steps[0];
    st.accAmt = (int8_t) amt; st.accMode = (uint8_t) mode; st.accMax = (int8_t) mx; st.accMin = (int8_t) mn;
    Engine e(*p);
    std::vector<int> r;
    for (auto& x : ons(run(e, cycles))) r.push_back(x.note - 48);
    return r;
}

static void testAccumulatorHarmonizer() {
    CHECK((accSeq(0, 1, 2, 0, 5) == std::vector<int>{0, 1, 2, 0, 1}));    // wrap
    CHECK((accSeq(0, 3, 7, -2, 5) == std::vector<int>{0, 3, 6, -1, 2}));  // wrap example from the manual (+3: 0,3,6,-1,2)
    CHECK((accSeq(1, 1, 2, 0, 5) == std::vector<int>{0, 1, 2, 0, 1}));    // clip
    CHECK((accSeq(2, 1, 2, 0, 6) == std::vector<int>{0, 1, 2, 1, 0, 1})); // pendulum
    CHECK((accSeq(3, 1, 2, 0, 5) == std::vector<int>{0, 1, 2, 2, 2}));    // hold

    // harmonizer: sequencer 2 follows the D minor chord of sequencer 1
    auto p = std::make_unique<Project>();
    auto& lead = p->seqs[0];
    lead.mode = Mode::Chord; lead.scale = 1;
    for (int i = 0; i < 16; ++i) setNote(lead.pat().tracks[0], i, 1);
    auto& fol = p->seqs[1];
    fol.harm = 0;
    for (int i = 0; i < 4; ++i) setNote(fol.pat().tracks[0], i, i);
    Engine e(*p);
    std::vector<int> fn;
    for (auto& x : ons(run(e, 1.0))) if (x.ch == 1) fn.push_back(x.note);
    CHECK((fn == std::vector<int>{50, 53, 57, 62}));

    // channel multiplex "3 Hi": highest note on the base channel
    auto p2 = std::make_unique<Project>();
    auto& pq = p2->seqs[0];
    pq.mode = Mode::Poly; pq.chMx = 9;
    auto& st = pq.pat().tracks[0].steps[0];
    st.active = true; st.numNotes = 3; st.notes = {0, 4, 7, 0, 0, 0, 0};
    Engine e2(*p2);
    auto n2 = ons(run(e2, 0.2));
    std::map<int, int> chOf; for (auto& x : n2) chOf[x.note] = x.ch;
    CHECK(chOf[55] == 0 && chOf[52] == 1 && chOf[48] == 2);
    CHECK(chMxLabel(9) == "3 Hi" && chMxLabel(1) == "2 Lo");
}

static std::vector<int> arpRun(int type, int octV, int len, int puls, std::vector<int> keys, double beats) {
    auto p = std::make_unique<Project>();
    auto& a = p->seqs[0].arp;
    a.type = (uint8_t) type; a.octV = (int8_t) octV; a.len = (uint8_t) len; a.puls = (uint8_t) puls;
    Engine e(*p);
    for (int k : keys) e.arpKey(0, k, true);
    std::vector<int> r;
    for (auto& x : ons(run(e, beats))) r.push_back(x.note);
    return r;
}

static void testLiveArpLoop() {
    // live note while stopped
    auto p = std::make_unique<Project>();
    Engine e(*p);
    e.prepare(48000);
    std::vector<MidiIn> in; std::vector<MidiOut> out;
    e.liveNote(3, 64, 99, true);
    Transport stop; stop.playing = false;
    e.process(stop, 256, in, out);
    CHECK(out.size() == 1 && out[0].status == 0x93 && out[0].data1 == 64 && out[0].data2 == 99);
    // MIDI input plays the selected sequencer (thru on its channel)
    out.clear(); e.kbTarget = 2;
    in.push_back({0x90, 60, 100});
    e.process(stop, 256, in, out);
    CHECK(out.size() == 1 && out[0].status == 0x92);

    CHECK((arpRun(kArpUp, 0, 16, 16, {64, 60, 67}, 1.0) == std::vector<int>{60, 64, 67, 60}));
    CHECK((arpRun(kArpDown, 0, 16, 16, {60, 64, 67}, 1.0) == std::vector<int>{67, 64, 60, 67}));
    CHECK((arpRun(kArpUp, 1, 16, 16, {60, 64, 67}, 1.5) == std::vector<int>{60, 64, 67, 72, 76, 79}));
    CHECK((arpRun(kArpUpDown, 0, 16, 16, {60, 64, 67}, 1.0) == std::vector<int>{60, 64, 67, 64}));
    CHECK((arpRun(kArpOrder, 0, 16, 16, {67, 60, 64}, 0.75) == std::vector<int>{67, 60, 64}));
    CHECK((arpRun(kArpThumb, 0, 16, 16, {60, 64, 67}, 1.0) == std::vector<int>{60, 64, 60, 67}));
    CHECK(arpRun(kArpUp, 0, 4, 1, {60, 64, 67}, 2.0).size() == 2);   // euclid 1 pulse in 4

    // the keyboard arp also runs while the transport is stopped (free clock at the host tempo)
    {
        auto pa = std::make_unique<Project>();
        pa->seqs[0].arp.type = kArpUp;
        Engine ea(*pa); ea.prepare(48000);
        ea.arpKey(0, 60, true); ea.arpKey(0, 64, true);
        int ons = 0, bal = 0;
        for (int b = 0; b < 100; ++b) {
            std::vector<MidiIn> in; std::vector<MidiOut> out;
            Transport tp; tp.playing = b >= 50; tp.ppq = (b - 50) * 512 * 2.0 / 48000; tp.bpm = 120;
            if (b == 80) { ea.arpKey(0, 60, false); ea.arpKey(0, 64, false); }
            ea.process(tp, 512, in, out);
            for (auto& m : out) { if ((m.status & 0xf0) == 0x90) { ++ons; ++bal; } if ((m.status & 0xf0) == 0x80) --bal; }
        }
        CHECK(ons >= 6); // ~1 beat stopped + ~0.6 beat playing at 1/16
        CHECK(bal == 0);
    }

    // hold keeps released notes
    auto ph = std::make_unique<Project>();
    ph->seqs[0].arp.type = kArpUp; ph->seqs[0].arp.hold = true;
    Engine eh(*ph);
    eh.arpKey(0, 60, true); eh.arpKey(0, 60, false);
    int buf[8];
    CHECK(eh.arpNotes(0, buf, 8) == 1);
    CHECK(ons(run(eh, 0.5)).size() == 2);

    // sequencer arp: a poly step arpeggiated at 1/32
    auto pp = std::make_unique<Project>();
    auto& pq = pp->seqs[0];
    pq.mode = Mode::Poly; pq.arp.seqType = kArpUp; pq.arp.div = Division::ThirtySecond;
    auto& st = pq.pat().tracks[0].steps[0];
    st.active = true; st.numNotes = 3; st.notes = {7, 0, 4, 0, 0, 0, 0};
    Engine ep(*pp);
    auto n = ons(run(ep, 0.25));
    CHECK(n.size() == 2 && n[0].note == 48 && n[1].note == 52);

    // loop inside the pattern
    auto pl = std::make_unique<Project>();
    auto& tr = pl->seqs[0].pat().tracks[0];
    for (int i = 0; i < 16; ++i) setNote(tr, i, i);
    tr.loopStart = 4; tr.loopEnd = 5;
    Engine el(*pl);
    std::vector<int> ln; for (auto& x : ons(run(el, 1.0))) ln.push_back(x.note - 48);
    CHECK((ln == std::vector<int>{4, 5, 4, 5}));
    auto s1 = serialize(*pl); auto p2 = std::make_unique<Project>();
    CHECK(deserialize(s1, *p2) && p2->seqs[0].pat().tracks[0].loopEnd == 5);
}

static void testPerformTools() {
    // perform transpose (scale degrees) and rotate
    {
        auto p = std::make_unique<Project>();
        auto& tr = p->seqs[0].pat().tracks[0];
        for (int i = 0; i < 4; ++i) setNote(tr, i, i);
        tr.end = 3;
        p->seqs[0].perfTrans = 2;            // chromatic default: +2 degrees = +2 semitones
        Engine e(*p);
        std::vector<int> n; for (auto& x : ons(run(e, 1.0))) n.push_back(x.note - 48);
        CHECK((n == std::vector<int>{2, 3, 4, 5}));
        p->seqs[0].perfTrans = 0; p->seqs[0].perfRot = 1; // shift right by one step
        Engine e2(*p);
        n.clear(); for (auto& x : ons(run(e2, 1.0))) n.push_back(x.note - 48);
        CHECK((n == std::vector<int>{3, 0, 1, 2}));
    }
    // random perform: trig 0% silences, vel spread stays in range, one-sided octave goes down only
    {
        auto p = std::make_unique<Project>();
        auto& tr = p->seqs[0].pat().tracks[0];
        for (int i = 0; i < 16; ++i) setNote(tr, i, 0, 100);
        p->seqs[0].rpTrig = 0;
        Engine e(*p);
        CHECK(ons(run(e, 4.0)).empty());
        p->seqs[0].rpTrig = 100; p->seqs[0].rpVel = 50; p->seqs[0].rpOct = -100;
        Engine e2(*p);
        auto n = ons(run(e2, 4.0));
        CHECK(n.size() == 16);
        bool velOk = true, varied = false, octOk = true;
        for (auto& x : n) { velOk &= x.vel >= 49 && x.vel <= 127; varied |= x.vel != 100; octOk &= x.note == 36; }
        CHECK(velOk && varied && octOk);
        auto s1 = serialize(*p); auto p2 = std::make_unique<Project>();
        CHECK(deserialize(s1, *p2) && p2->seqs[0].rpVel == 50 && p2->seqs[0].rpOct == -100);
    }
    // sequencer solo
    {
        auto p = std::make_unique<Project>();
        setNote(p->seqs[0].pat().tracks[0], 0, 0);
        setNote(p->seqs[1].pat().tracks[0], 0, 0);
        p->seqs[1].channel = 1;
        Engine e(*p);
        e.seqSolo = 2;
        auto n = ons(run(e, 0.25));
        CHECK(n.size() == 1 && n[0].ch == 1);
    }
    // multitrack fill retriggers every step
    {
        auto p = std::make_unique<Project>();
        p->seqs[0].mode = Mode::Multi;
        setNote(p->seqs[0].pat().tracks[0], 0, 0);
        Engine e(*p);
        e.multiFill[0] = 3;               // x4
        CHECK(ons(run(e, 0.25)).size() == 4);
    }
    // roller: held notes retrigger at the roll rate, also while stopped
    {
        auto p = std::make_unique<Project>();
        Engine e(*p); e.prepare(48000);
        e.rollDiv[0] = 0.25;
        e.arpKey(0, 60, true); e.arpKey(0, 64, true);
        int ons = 0, bal = 0;
        for (int b = 0; b < 47; ++b) { // ~1 beat at 120 bpm, stopped
            std::vector<MidiIn> in; std::vector<MidiOut> out;
            Transport tp; tp.playing = false; tp.bpm = 120;
            if (b == 46) { e.arpKey(0, 60, false); e.arpKey(0, 64, false); }
            e.process(tp, 512, in, out);
            for (auto& m : out) { if ((m.status & 0xf0) == 0x90) { ++ons; ++bal; } if ((m.status & 0xf0) == 0x80) --bal; }
        }
        CHECK(ons >= 6 && ons % 2 == 0); // both notes together, ~4 hits
    }
}

// runs the engine block by block, calling `hook(ppq)` before each block
template <typename F> static std::vector<Note> runHook(Engine& e, double beats, F hook, bool playing = true) {
    std::vector<Note> notes;
    const double sr = 48000.0, bpm = 120.0, pps = bpm / 60.0 / sr;
    e.prepare(sr);
    double ppq = 0.0;
    std::vector<MidiIn> in; std::vector<MidiOut> out;
    while (ppq < beats) {
        out.clear();
        hook(ppq);
        Transport t; t.playing = playing; t.ppq = ppq; t.bpm = bpm;
        e.process(t, 256, in, out);
        for (auto& m : out) { int ty = m.status & 0xF0; if (ty == 0x90 || ty == 0x80) notes.push_back({ppq + m.sampleOffset * pps, m.status & 15, m.data1, m.data2, ty == 0x90}); }
        ppq += 256 * pps;
    }
    return notes;
}

static void testGeneratorsRecording() {
    // euclid rhythm + logic
    {
        GenSettings g; g.eLen = 16; g.ePuls = 4;
        bool on[16], acc[16];
        euclidRhythm(g, 16, on, acc);
        CHECK(on[0] && on[4] && on[8] && on[12] && !on[1] && !on[2]);
        g.eLen2 = 16; g.ePuls2 = 2; g.eRot2 = 2; g.logic = 0; // OR adds 2 and 10
        euclidRhythm(g, 16, on, acc);
        CHECK(on[2] && on[10] && on[0]);
        g.logic = 3;                                          // Accnt: placement from layer 1 only
        g.eRot2 = 0; euclidRhythm(g, 16, on, acc);
        CHECK(on[0] && acc[0] && on[4] && !acc[4]);
        g.eLen2 = 0; g.eInv = true; euclidRhythm(g, 16, on, acc);
        CHECK(!on[0] && on[1]);
    }
    // euclid generate: writes the range, keeps fixed steps, lock rhythm keeps positions
    {
        auto p = std::make_unique<Project>();
        auto& q = p->seqs[0];
        auto& tr = q.pat().tracks[0];
        setNote(tr, 3, 5); tr.steps[3].trig = Trig {TrigType::Fixed, 0, 0};
        q.gen.eLen = 16; q.gen.ePuls = 4; q.gen.nteR = 7; q.gen.ntePct = 100;
        euclidGenerate(q, 0);
        int n = 0; for (int i = 0; i < 16; ++i) n += tr.steps[(size_t) i].active;
        CHECK(n == 5 && tr.steps[0].active && tr.steps[3].notes[0] == 5);
        bool varied = false; for (int i : {0, 4, 8, 12}) varied |= tr.steps[(size_t) i].notes[0] != 0;
        CHECK(varied);
        q.gen.lock = 2; q.gen.ePuls = 8; // rhythm locked: same positions
        euclidGenerate(q, 0);
        CHECK(tr.steps[4].active && !tr.steps[2].active);
    }
    // random generate: density, flip, order
    {
        auto p = std::make_unique<Project>();
        auto& q = p->seqs[0];
        auto& tr = q.pat().tracks[0];
        q.rgen.den = 100; q.rgen.ties = 0;
        randomGenerate(q, 0, 12345);
        int n = 0; for (int i = 0; i < 16; ++i) n += tr.steps[(size_t) i].active;
        CHECK(n == 16);
        const int first = tr.steps[0].notes[0], last = tr.steps[15].notes[0];
        q.rgen.den = -3; randomGenerate(q, 0, 1);                // flip
        CHECK(tr.steps[0].notes[0] == last && tr.steps[15].notes[0] == first);
        int sumA = 0, sumB = 0; for (int i = 0; i < 16; ++i) sumA += tr.steps[(size_t) i].notes[0];
        q.rgen.den = -1; randomGenerate(q, 0, 99);               // order: same notes, shuffled
        for (int i = 0; i < 16; ++i) sumB += tr.steps[(size_t) i].notes[0];
        CHECK(sumA == sumB);
        q.rgen.den = 20; randomGenerate(q, 0, 7);
        n = 0; for (int i = 0; i < 16; ++i) n += tr.steps[(size_t) i].active;
        CHECK(n > 0 && n < 10);
    }
    // live recording: punch in while playing, quantized to the nearest step, duration -> gate / tie
    {
        auto p = std::make_unique<Project>();
        Engine e(*p);
        bool armed = false, on1 = false, off1 = false, on2 = false, off2 = false;
        runHook(e, 4.0, [&](double ppq) {
            if (!armed && ppq > 0.05) { e.recArm(0, 0, false); armed = true; }
            if (!on1 && ppq >= 0.49) { e.recInput(62, 90, true); on1 = true; }        // ~step 2 (0.5)
            if (!off1 && ppq >= 0.60) { e.recInput(62, 0, false); off1 = true; }      // ~half a step
            if (!on2 && ppq >= 1.24) { e.recInput(55, 100, true); on2 = true; }       // step 5, an octave below C2 root... G1
            if (!off2 && ppq >= 1.80) { e.recInput(55, 0, false); off2 = true; }      // ~2.2 steps -> tie
        });
        auto& tr = p->seqs[0].pat().tracks[0];
        CHECK(tr.steps[2].active && tr.steps[2].notes[0] == 2 && tr.steps[2].velocity == 90);
        CHECK(tr.steps[2].gate > 20 && tr.steps[2].gate < 70);
        CHECK(tr.steps[5].active && tr.steps[5].octave == 0 && tr.steps[5].notes[0] == 7 && tr.steps[5].tie >= 1);
    }
    // count-in: armed while stopped, recording starts one bar after the transport
    {
        auto p = std::make_unique<Project>();
        p->recCountIn = 3; p->metroCh = 10;
        Engine e(*p);
        e.recArm(0, 0, false);
        CHECK(e.recState() == 1);
        bool a = false, b = false; int clicks = 0;
        auto n = runHook(e, 6.0, [&](double ppq) {
            if (!a && ppq >= 1.0) { e.recInput(60, 100, true); e.recInput(60, 0, false); a = true; }   // during the count-in
            if (!b && ppq >= 5.5) { e.recInput(64, 100, true); e.recInput(64, 0, false); b = true; }   // after it
        });
        for (auto& x : n) if (x.on && x.ch == 9) ++clicks;
        auto& tr = p->seqs[0].pat().tracks[0];
        CHECK(!tr.steps[4].active && tr.steps[6].active);
        CHECK(clicks >= 5);
    }
    // step recording + capture buffer
    {
        auto p = std::make_unique<Project>();
        p->seqs[0].mode = Mode::Poly; p->seqs[0].nVoices = 4;
        Engine e(*p);
        e.stepRecStart(0, 0);
        int k = 0;
        runHook(e, 2.0, [&](double) {
            const int kk = k++;
            if (kk % 20) return; // ~100 ms apart
            switch (kk / 20) {
                case 1: e.recInput(60, 100, true); e.recInput(64, 100, true); break; // chord on step 1
                case 2: e.recInput(60, 0, false); e.recInput(64, 0, false); break;
                case 3: e.stepRecRest(); break;
                case 4: e.recInput(67, 100, true); break;
                case 5: e.recInput(67, 0, false); break;
                default: break;
            }
        }, false);
        auto& tr = p->seqs[0].pat().tracks[0];
        CHECK(tr.steps[0].numNotes == 2 && !tr.steps[1].active && tr.steps[2].notes[0] == 7 && e.stepRecCursor() == 3);
        CHECK(e.captureCount() == 3);
        e.stepRecStop();
        CHECK(e.captureToPattern(0, 0) == 2);
        CHECK(tr.steps[0].numNotes == 2 && tr.steps[1].notes[0] == 7 && tr.end == 1);
    }
    // Xtend recording grows the range; pattern switch "In" = Beat
    {
        auto p = std::make_unique<Project>();
        auto& tr = p->seqs[0].pat().tracks[0];
        tr.end = 3; p->recMode = 2;
        Engine e(*p);
        bool armed = false;
        runHook(e, 2.0, [&](double ppq) { if (!armed && ppq > 0.01) { e.recArm(0, 0, false); armed = true; } });
        CHECK(tr.end >= 6);
        auto p2 = std::make_unique<Project>();
        p2->seqs[0].punchIn = 3;
        Engine e2(*p2);
        bool req = false;
        runHook(e2, 1.2, [&](double ppq) { if (!req && ppq > 0.3) { e2.requestPattern(0, 5); req = true; } });
        CHECK(p2->seqs[0].curPattern == 5);
    }
    // settings round trip
    {
        auto p = std::make_unique<Project>();
        p->seqs[2].gen.ePuls = 9; p->seqs[2].rgen.den = -2; p->recMode = 1; p->scaleMaster = 3;
        auto txt = serialize(*p); auto p2 = std::make_unique<Project>();
        CHECK(deserialize(txt, *p2) && p2->seqs[2].gen.ePuls == 9 && p2->seqs[2].rgen.den == -2 && p2->recMode == 1 && p2->scaleMaster == 3);
        auto one = serialize(*p, 2, 0);
        CHECK(one.find("SEQ 0 ") == std::string::npos && one.find("SEQ 2 ") != std::string::npos);
    }
}

int runControllerTests(); // defined in test_controller.cpp

int main() {
    testScales();
    testDirections();
    testBasicTiming();
    testScaleAndChannel();
    testSwing();
    testProbabilityAndConditions();
    testRepeatsRetrigTie();
    testMultitrackAndPoly();
    testTimelineAlignment();
    testSerialization();
    testChords();
    testStochastic();
    testMatriceal();
    testDomino();
    testSaga();
    testPlayer();
    testAccumulatorHarmonizer();
    testLiveArpLoop();
    testPerformTools();
    testGeneratorsRecording();
    failures += runControllerTests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

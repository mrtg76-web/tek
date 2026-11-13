// Headless tests for the TEK core (engine + controller). Build: see CMakeLists (target tek_tests).
#include "../core/Engine.h"
#include "../core/Controller.h"
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
    failures += runControllerTests();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

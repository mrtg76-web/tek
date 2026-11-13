#include "Model.h"
#include <sstream>
#include <vector>

namespace tek {

// ---------------------------------------------------------------- trig conditions
static std::vector<Trig> buildTrigList() {
    std::vector<Trig> v;
    for (int p = 0; p <= 100; p += 5) v.push_back({TrigType::Prob, (uint8_t) p, 0});
    for (int b = 2; b <= 8; ++b) for (int a = 1; a <= b; ++a) v.push_back({TrigType::AB, (uint8_t) a, (uint8_t) b});
    for (int b = 2; b <= 8; ++b) for (int a = 1; a <= b; ++a) v.push_back({TrigType::NotAB, (uint8_t) a, (uint8_t) b});
    v.push_back({TrigType::Pre, 0, 0});
    v.push_back({TrigType::NotPre, 0, 0});
    v.push_back({TrigType::First, 0, 0});
    v.push_back({TrigType::NotFirst, 0, 0});
    v.push_back({TrigType::Fixed, 0, 0});
    v.push_back({TrigType::Locked, 0, 0});
    v.push_back({TrigType::Flow, 0, 0});
    v.push_back({TrigType::NotFlow, 0, 0});
    return v;
}
static const std::vector<Trig>& trigList() { static const auto l = buildTrigList(); return l; }
int trigListSize() { return (int) trigList().size(); }
Trig trigFromListIndex(int i) {
    const auto& l = trigList();
    if (i < 0) i = 0;
    if (i >= (int) l.size()) i = (int) l.size() - 1;
    return l[(size_t) i];
}
int trigToListIndex(const Trig& t) {
    const auto& l = trigList();
    if (t.type == TrigType::Prob) return (t.a + 2) / 5; // nearest 5 %
    for (size_t i = 0; i < l.size(); ++i) if (l[i] == t) return (int) i;
    return 20;
}
std::string trigLabel(const Trig& t) {
    switch (t.type) {
        case TrigType::Prob: return std::to_string(t.a) + "%";
        case TrigType::AB: return std::to_string(t.a) + ":" + std::to_string(t.b);
        case TrigType::NotAB: return "!" + std::to_string(t.a) + ":" + std::to_string(t.b);
        case TrigType::Pre: return "PRE";
        case TrigType::NotPre: return "!PRE";
        case TrigType::First: return "1st";
        case TrigType::NotFirst: return "!1st";
        case TrigType::Fixed: return "FIXD";
        case TrigType::Locked: return "LOCK";
        case TrigType::Flow: return "FLOW";
        case TrigType::NotFlow: return "!FLW";
    }
    return "?";
}

// ---------------------------------------------------------------- project
bool Pattern::isEmpty() const {
    for (auto& t : tracks) for (auto& s : t.steps) if (s.active) return false;
    return true;
}

void Project::reset() {
    for (int i = 0; i < kNumSeqs; ++i) {
        auto& s = seqs[(size_t) i];
        s = Sequencer();
        s.channel = (uint8_t) i;
        s.color = (uint8_t) i;
        for (auto& p : s.patterns)
            for (int t = 0; t < kNumTracks; ++t) {
                auto& tr = p.tracks[(size_t) t];
                tr.channel = (uint8_t) i;
                // General MIDI / Ableton drum rack friendly defaults: C1, D1, F#1, A#1, ...
                static const uint8_t drum[8] = {36, 38, 42, 46, 39, 45, 48, 51};
                tr.baseNote = drum[t];
            }
    }
    bpm = 120.0;
    internalClock = false;
    output = OutputMode::Both;
}

const char* punchInLabel(int v) {
    static const char* l[] = {"Glob","OFF","P.End","Beat","1/2B","Bar","2Bar","4Bar","8Bar"};
    return (v >= 0 && v < 9) ? l[v] : "?";
}

uint32_t seqColor(int i) {
    // purple, blue, mauve, yellow, orange, pink, turquoise, aqua (manual 3.2)
    static const uint32_t c[8] = {0x9b5cff, 0x3d7bff, 0xd27cff, 0xffd23d, 0xff8a2b, 0xff4fa3, 0x2be0c8, 0x46c8ff};
    return c[((i % 8) + 8) % 8];
}

// ---------------------------------------------------------------- grooves
// Original templates (approximations of common feels; not copies of the hardware library).
const GrooveTemplate& grooveTemplate(int index) {
    static const GrooveTemplate g[kNumGrooves] = {
        {"LightSwing", {0,.08f,0,.08f,0,.08f,0,.08f,0,.08f,0,.08f,0,.08f,0,.08f},
                       {.2f,-.2f,0,-.2f,.1f,-.2f,0,-.2f,.2f,-.2f,0,-.2f,.1f,-.2f,0,-.2f}},
        {"HardSwing",  {0,.2f,0,.2f,0,.2f,0,.2f,0,.2f,0,.2f,0,.2f,0,.2f},
                       {.3f,-.3f,0,-.3f,.2f,-.3f,0,-.3f,.3f,-.3f,0,-.3f,.2f,-.3f,0,-.3f}},
        {"Shuffle8",   {0,0,.15f,0,0,0,.15f,0,0,0,.15f,0,0,0,.15f,0},
                       {.3f,-.4f,-.1f,-.4f,.2f,-.4f,-.1f,-.4f,.3f,-.4f,-.1f,-.4f,.2f,-.4f,-.1f,-.4f}},
        {"Push",       {0,-.05f,-.08f,-.05f,0,-.05f,-.08f,-.05f,0,-.05f,-.08f,-.05f,0,-.05f,-.08f,-.05f},
                       {.1f,0,.1f,0,.1f,0,.1f,0,.1f,0,.1f,0,.1f,0,.1f,0}},
        {"Lazy",       {0,.06f,.1f,.06f,0,.06f,.1f,.06f,0,.06f,.1f,.06f,0,.06f,.1f,.06f},
                       {.1f,-.2f,-.1f,-.2f,.1f,-.2f,-.1f,-.2f,.1f,-.2f,-.1f,-.2f,.1f,-.2f,-.1f,-.2f}},
        {"Accent1&3",  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
                       {.5f,-.3f,-.2f,-.3f,-.1f,-.3f,-.2f,-.3f,.4f,-.3f,-.2f,-.3f,-.1f,-.3f,-.2f,-.3f}},
        {"OffbeatAcc", {0,0,.03f,0,0,0,.03f,0,0,0,.03f,0,0,0,.03f,0},
                       {-.2f,-.3f,.5f,-.3f,-.2f,-.3f,.5f,-.3f,-.2f,-.3f,.5f,-.3f,-.2f,-.3f,.5f,-.3f}},
        {"Clave32",    {0,0,0,.04f,0,0,.04f,0,0,0,0,0,0,0,0,0},
                       {.5f,-.4f,-.4f,.4f,-.4f,-.4f,.4f,-.4f,-.4f,-.4f,.4f,-.4f,.4f,-.4f,-.4f,-.4f}},
        {"Tresillo",   {0,0,0,.05f,0,0,.05f,0,0,0,0,.05f,0,0,.05f,0},
                       {.5f,-.4f,-.3f,.4f,-.3f,-.4f,.4f,-.4f,.3f,-.4f,-.3f,.4f,-.3f,-.4f,.4f,-.4f}},
        {"Humanize",   {0,.02f,-.03f,.04f,-.01f,.03f,-.02f,.01f,.02f,-.03f,.01f,.03f,-.02f,.02f,-.01f,.03f},
                       {.1f,-.1f,.05f,-.15f,.12f,-.08f,.0f,-.12f,.1f,-.05f,.04f,-.1f,.08f,-.12f,.02f,-.06f}},
        {"DiscoHat",   {0,0,.02f,0,0,0,.02f,0,0,0,.02f,0,0,0,.02f,0},
                       {-.3f,-.2f,.4f,-.2f,-.3f,-.2f,.4f,-.2f,-.3f,-.2f,.4f,-.2f,-.3f,-.2f,.4f,-.2f}},
        {"Dancehall",  {0,0,0,.06f,0,0,0,.06f,0,0,0,.06f,0,0,0,.06f},
                       {.4f,-.4f,-.3f,.3f,-.2f,-.4f,.3f,-.2f,.4f,-.4f,-.3f,.3f,-.2f,-.4f,.3f,-.2f}},
        {"LatinShake", {0,.04f,.02f,.06f,0,.04f,.02f,.06f,0,.04f,.02f,.06f,0,.04f,.02f,.06f},
                       {.2f,-.3f,.1f,-.1f,.2f,-.3f,.1f,-.1f,.2f,-.3f,.1f,-.1f,.2f,-.3f,.1f,-.1f}},
        {"Drunk",      {0,.1f,-.06f,.12f,.04f,-.08f,.1f,.02f,-.04f,.12f,-.02f,.08f,.06f,-.1f,.04f,.1f},
                       {.2f,-.3f,.1f,-.2f,.3f,-.1f,-.2f,.1f,.2f,-.3f,.0f,-.1f,.25f,-.2f,.05f,-.15f}},
    };
    if (index < 1) index = 1;
    if (index > kNumGrooves) index = kNumGrooves;
    return g[index - 1];
}

// ---------------------------------------------------------------- serialisation
// Format: one record per line, whitespace separated ints. Only non-default steps are written.
static void writeStep(std::ostream& o, int s, int p, int t, int i, const Step& st) {
    o << "ST " << s << ' ' << p << ' ' << t << ' ' << i << ' ' << (int) st.active << ' ' << (int) st.muted << ' '
      << (int) st.velocity << ' ' << (int) st.octave << ' ' << (int) st.gate << ' ' << (int) st.tie << ' '
      << (int) st.trig.type << ' ' << (int) st.trig.a << ' ' << (int) st.trig.b << ' ' << (int) st.repeats << ' '
      << (int) st.repDur << ' ' << (int) st.ramp << ' ' << (int) st.accel << ' ' << (int) st.retrig << ' '
      << (int) st.offset << ' ' << (int) st.rng << ' ' << (int) st.repProb << ' ' << (int) st.velLocked << ' '
      << (int) st.numNotes;
    for (int n = 0; n < st.numNotes; ++n) o << ' ' << (int) st.notes[(size_t) n];
    o << '\n';
}

static bool stepIsDefault(const Step& s) {
    Step d;
    return !s.active && !s.muted && s.numNotes == 0 && s.velocity == d.velocity && s.octave == 0 && s.gate == d.gate
        && s.tie == 0 && s.trig == d.trig && s.repeats == 1 && s.repDur == 1 && s.ramp == 0 && s.accel == 0
        && s.retrig == 1 && s.offset == 0 && s.rng == 0 && s.repProb == 100 && s.velLocked == 0
        && s.chordType == 0 && s.voicing == 0 && s.spread == 0 && s.strum == 0;
}

std::string serialize(const Project& p) {
    std::ostringstream o;
    o << "TEK 1\n";
    o << "PRJ " << (int) (p.bpm * 10.0 + 0.5) << ' ' << (int) p.internalClock << ' ' << (int) p.output << '\n';
    for (int s = 0; s < kNumSeqs; ++s) {
        const auto& q = p.seqs[(size_t) s];
        o << "SEQ " << s << ' ' << (int) q.mode << ' ' << (int) q.channel << ' ' << (int) q.scale << ' ' << (int) q.rootPc << ' '
          << (int) q.baseOctave << ' ' << (int) q.punchIn << ' ' << (int) q.resetBars << ' ' << (int) q.programChange << ' '
          << (int) q.bank << ' ' << (int) q.transpose << ' ' << (int) q.nVoices << ' ' << (int) q.color << ' '
          << (int) q.nudgeMs << ' ' << (int) q.timeBend << ' ' << (int) q.active << ' ' << (int) q.muted << ' '
          << (int) q.curPattern << ' ' << (int) q.groove << ' ' << q.grooveAccent << ' ' << q.grooveTime << ' '
          << (int) q.grooveOffset << ' ' << (int) q.grooveLen << ' ' << (int) q.grooveRand << '\n';
        o << "SQ2 " << s << ' ' << (int) q.bassMode << ' ' << (int) q.chMx << '\n';
        for (int pi = 0; pi < kNumPatterns; ++pi) {
            const auto& pat = q.patterns[(size_t) pi];
            o << "PAT " << s << ' ' << pi << ' ' << (int) pat.swing << '\n';
            for (int t = 0; t < kNumTracks; ++t) {
                const auto& tr = pat.tracks[(size_t) t];
                o << "TRK " << s << ' ' << pi << ' ' << t << ' ' << (int) tr.start << ' ' << (int) tr.end << ' ' << (int) tr.dir << ' '
                  << (int) tr.div << ' ' << (int) tr.rndDiv << ' ' << (int) tr.channel << ' ' << (int) tr.baseNote << ' '
                  << (int) tr.swing << ' ' << (int) tr.muted << ' ' << (int) tr.defVel << ' ' << (int) tr.defGate << ' '
                  << (int) tr.defRepeats << '\n';
                for (int i = 0; i < kMaxSteps; ++i) {
                    const auto& st = tr.steps[(size_t) i];
                    if (stepIsDefault(st)) continue;
                    writeStep(o, s, pi, t, i, st);
                    if (st.chordType || st.voicing || st.spread || st.strum)
                        o << "SC " << s << ' ' << pi << ' ' << t << ' ' << i << ' ' << (int) st.chordType << ' ' << (int) st.voicing
                          << ' ' << (int) st.spread << ' ' << (int) st.strum << '\n';
                }
            }
        }
    }
    o << "END\n";
    return o.str();
}

template <typename T> static T clampv(int v, int lo, int hi) { return (T) (v < lo ? lo : (v > hi ? hi : v)); }

bool deserialize(const std::string& str, Project& p) {
    std::istringstream in(str);
    std::string line;
    if (!std::getline(in, line) || line.rfind("TEK", 0) != 0) return false;
    p.reset();
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string tag; l >> tag;
        std::vector<int> v; int x;
        while (l >> x) v.push_back(x);
        auto at = [&](size_t i) { return i < v.size() ? v[i] : 0; };
        if (tag == "PRJ" && v.size() >= 3) {
            p.bpm = at(0) / 10.0; if (p.bpm < 20) p.bpm = 20; if (p.bpm > 300) p.bpm = 300;
            p.internalClock = at(1) != 0;
            p.output = clampv<OutputMode>(at(2), 0, (int) OutputMode::Count - 1);
        } else if (tag == "SEQ" && v.size() >= 24) {
            int s = at(0); if (s < 0 || s >= kNumSeqs) continue;
            auto& q = p.seqs[(size_t) s];
            q.mode = clampv<Mode>(at(1), 0, (int) Mode::Count - 1);
            q.channel = clampv<uint8_t>(at(2), 0, 15);
            q.scale = clampv<uint8_t>(at(3), 0, numScales() - 1);
            q.rootPc = clampv<uint8_t>(at(4), 0, 11);
            q.baseOctave = clampv<int8_t>(at(5), -2, 7);
            q.punchIn = clampv<uint8_t>(at(6), 0, 8);
            q.resetBars = clampv<uint8_t>(at(7), 0, 64);
            q.programChange = clampv<uint8_t>(at(8), 0, 128);
            q.bank = clampv<uint8_t>(at(9), 0, 128);
            q.transpose = clampv<uint8_t>(at(10), 0, 17);
            q.nVoices = clampv<uint8_t>(at(11), 1, 7);
            q.color = clampv<uint8_t>(at(12), 0, 7);
            q.nudgeMs = clampv<int8_t>(at(13), -100, 100);
            q.timeBend = clampv<int8_t>(at(14), -120, 120);
            q.active = at(15) != 0;
            q.muted = at(16) != 0;
            q.curPattern = clampv<uint8_t>(at(17), 0, kNumPatterns - 1);
            q.groove = clampv<uint8_t>(at(18), 0, kNumGrooves);
            q.grooveAccent = clampv<int16_t>(at(19), -120, 120);
            q.grooveTime = clampv<int16_t>(at(20), -120, 120);
            q.grooveOffset = clampv<int8_t>(at(21), -15, 15);
            q.grooveLen = clampv<uint8_t>(at(22), 1, 16);
            q.grooveRand = clampv<uint8_t>(at(23), 0, 100);
        } else if (tag == "SQ2" && v.size() >= 3) {
            int s = at(0); if (s < 0 || s >= kNumSeqs) continue;
            p.seqs[(size_t) s].bassMode = clampv<uint8_t>(at(1), 0, 17);
            p.seqs[(size_t) s].chMx = clampv<uint8_t>(at(2), 0, 1);
        } else if (tag == "SC" && v.size() >= 8) {
            int s = at(0), pi = at(1), t = at(2), i = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks || i < 0 || i >= kMaxSteps) continue;
            auto& st = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t].steps[(size_t) i];
            st.chordType = clampv<uint8_t>(at(4), 0, 11);
            st.voicing = clampv<uint8_t>(at(5), 0, 5);
            st.spread = clampv<uint8_t>(at(6), 0, 6);
            st.strum = clampv<int8_t>(at(7), -100, 100);
        } else if (tag == "PAT" && v.size() >= 3) {
            int s = at(0), pi = at(1);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns) continue;
            p.seqs[(size_t) s].patterns[(size_t) pi].swing = clampv<uint8_t>(at(2), 10, 90);
        } else if (tag == "TRK" && v.size() >= 15) {
            int s = at(0), pi = at(1), t = at(2);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks) continue;
            auto& tr = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t];
            tr.start = clampv<uint8_t>(at(3), 0, kMaxSteps - 1);
            tr.end = clampv<uint8_t>(at(4), 0, kMaxSteps - 1);
            if (tr.end < tr.start) tr.end = tr.start;
            tr.dir = clampv<Dir>(at(5), 0, (int) Dir::Count - 1);
            tr.div = clampv<Division>(at(6), 0, (int) Division::Count - 1);
            tr.rndDiv = clampv<uint8_t>(at(7), 0, 100);
            tr.channel = clampv<uint8_t>(at(8), 0, 15);
            tr.baseNote = clampv<uint8_t>(at(9), 0, 127);
            tr.swing = clampv<uint8_t>(at(10), 0, 90);
            tr.muted = at(11) != 0;
            tr.defVel = clampv<uint8_t>(at(12), 1, 127);
            tr.defGate = clampv<uint8_t>(at(13), 2, 100);
            tr.defRepeats = clampv<uint8_t>(at(14), 1, 15);
        } else if (tag == "ST" && v.size() >= 23) {
            int s = at(0), pi = at(1), t = at(2), i = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks || i < 0 || i >= kMaxSteps) continue;
            auto& st = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t].steps[(size_t) i];
            st.active = at(4) != 0; st.muted = at(5) != 0;
            st.velocity = clampv<uint8_t>(at(6), 1, 127);
            st.octave = clampv<int8_t>(at(7), -9, 9);
            st.gate = clampv<uint8_t>(at(8), 2, 100);
            st.tie = clampv<uint8_t>(at(9), 0, kMaxSteps - 1);
            st.trig.type = clampv<TrigType>(at(10), 0, (int) TrigType::NotFlow);
            st.trig.a = clampv<uint8_t>(at(11), 0, 100);
            st.trig.b = clampv<uint8_t>(at(12), 0, 8);
            st.repeats = clampv<uint8_t>(at(13), 1, 15);
            st.repDur = clampv<uint8_t>(at(14), 1, 15);
            st.ramp = clampv<int8_t>(at(15), -70, 70);
            st.accel = clampv<int8_t>(at(16), -28, 28);
            st.retrig = clampv<uint8_t>(at(17), 1, 8);
            st.offset = clampv<int8_t>(at(18), -45, 45);
            st.rng = clampv<int8_t>(at(19), 0, 24);
            st.repProb = clampv<uint8_t>(at(20), 0, 100);
            st.velLocked = clampv<uint8_t>(at(21), 0, 1);
            int n = clampv<int>(at(22), 0, kMaxPolyNotes);
            st.numNotes = (uint8_t) n;
            for (int k = 0; k < n; ++k) st.notes[(size_t) k] = clampv<int8_t>(at(23 + (size_t) k), -120, 120);
        }
    }
    return true;
}

} // namespace tek

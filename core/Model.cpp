#include "Model.h"
#include <sstream>
#include <algorithm>
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

// ---------------------------------------------------------------- matriceal
int matLaneDefault(int l) {
    switch (l) { case kLanePuls: case kLaneRetrig: return 1; case kLaneVel: return 75; case kLaneGate: return 50; case kLaneProb: return 100; default: return 0; }
}
int matLaneMin(int l) {
    switch (l) { case kLaneTrig: return 0; case kLaneNote: return -24; case kLaneIntv: return -12; case kLanePuls: return 1; case kLaneVel: return 1;
                 case kLaneOct: return -4; case kLaneGate: return 2; case kLaneProb: return 0; case kLaneRetrig: return 0; default: return 0; }
}
int matLaneMax(int l) {
    switch (l) { case kLaneTrig: return 1; case kLaneNote: return 24; case kLaneIntv: return 12; case kLanePuls: return 9; case kLaneVel: return 127;
                 case kLaneOct: return 4; case kLaneGate: return 101; case kLaneProb: return 100; case kLaneRetrig: return 6; default: return 100; }
}
const char* matLaneName(int l) {
    static const char* n[kNumMatLanes] = {"TRIGGER", "NOTE", "INTERV", "PULSES", "VEL", "OCT", "GATE", "TRIG PROB", "RETRIG", "CV GLIDE"};
    return n[std::clamp(l, 0, kNumMatLanes - 1)];
}
MatTrack::MatTrack() {
    for (int l = 0; l < kNumMatLanes; ++l) {
        lanes[(size_t) l].val.fill((int8_t) std::min(127, matLaneDefault(l)));
        lanes[(size_t) l].prob.fill(100);
        rnd[(size_t) l] = l == kLaneTrig || l == kLaneNote || l == kLaneIntv || l == kLanePuls || l == kLaneVel ? 50 : 33;
    }
}

void euclid(int len, int pulses, int rot, bool* out) {
    len = std::clamp(len, 1, 16);
    pulses = std::clamp(pulses, 0, len);
    for (int i = 0; i < len; ++i) {
        int j = ((i - rot) % len + len) % len;
        out[i] = pulses > 0 && ((j * pulses) % len) < pulses;
    }
}

// ---------------------------------------------------------------- midi clip
MidiClip makeClip(std::vector<ClipNote> notes, int ppqn, const std::string& name) {
    MidiClip c;
    c.name = name;
    if (ppqn <= 0) ppqn = kClipPpqn;
    for (auto& n : notes) {
        n.tick = (uint32_t) ((uint64_t) n.tick * kClipPpqn / (uint64_t) ppqn);
        n.len = std::max<uint32_t>(1, (uint32_t) ((uint64_t) n.len * kClipPpqn / (uint64_t) ppqn));
    }
    std::stable_sort(notes.begin(), notes.end(), [](const ClipNote& a, const ClipNote& b) { return a.tick < b.tick; });
    if (notes.size() > (size_t) kMaxClipNotes) notes.resize((size_t) kMaxClipNotes);
    uint32_t end = 0;
    for (auto& n : notes) end = std::max(end, n.tick + n.len);
    const uint32_t bar = kClipPpqn * 4;
    c.lengthTicks = std::max<uint32_t>(bar, (end + bar - 1) / bar * bar);
    c.notes = std::move(notes);
    return c;
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
        for (auto& p : s.patterns) {
            for (auto& mt : p.mat.tracks) mt.channel = (uint8_t) i;
            for (int t = 0; t < kNumTracks; ++t) {
                auto& tr = p.tracks[(size_t) t];
                tr.channel = (uint8_t) i;
                // General MIDI / Ableton drum rack friendly defaults: C1, D1, F#1, A#1, ...
                static const uint8_t drum[8] = {36, 38, 42, 46, 39, 45, 48, 51};
                tr.baseNote = drum[t];
            }
        }
    }
    bpm = 120.0;
    internalClock = false;
    output = OutputMode::Both;
}

std::string chMxLabel(int v) {
    if (v <= 0) return "Off";
    if (v <= 7) return std::to_string(v + 1) + " Lo";
    return std::to_string(v - 6) + " Hi";
}
const char* accModeLabel(int v) {
    static const char* l[] = {"Wrap", "Clip", "Pend", "Hold"};
    return l[std::clamp(v, 0, 3)];
}
uint8_t chMxChannel(int base, int chMx, int rankLow, int count) {
    if (chMx <= 0) return (uint8_t) base;
    int span = chMx <= 7 ? chMx + 1 : chMx - 6;
    int rank = chMx <= 7 ? rankLow : count - 1 - rankLow;
    return (uint8_t) ((base + rank % span) % 16);
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
        && s.chordType == 0 && s.voicing == 0 && s.spread == 0 && s.strum == 0 && s.dur == 1
        && s.accAmt == 0 && s.accMode == 0 && s.accMax == 7 && s.accMin == 0 && s.accTarget == 0 && s.accPctMode == 0 && s.accProb == 100;
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
        o << "SQ2 " << s << ' ' << (int) q.bassMode << ' ' << (int) q.chMx << ' ' << (int) q.voice << ' ' << (int) q.harm << '\n';
        for (int pi = 0; pi < kNumPatterns; ++pi) {
            const auto& pat = q.patterns[(size_t) pi];
            o << "PAT " << s << ' ' << pi << ' ' << (int) pat.swing << '\n';
            if (!pat.clip.notes.empty()) {
                std::string nm = pat.clip.name;
                for (auto& c : nm) if (c == ' ' || c == '\n' || c == '\r' || c == '\t') c = '_';
                o << "CLN " << s << ' ' << pi << ' ' << (nm.empty() ? "-" : nm) << '\n';
                o << "CLP " << s << ' ' << pi << ' ' << pat.clip.lengthTicks << ' ' << pat.clip.notes.size();
                for (auto& n : pat.clip.notes) o << ' ' << n.tick << ' ' << n.len << ' ' << (int) n.note << ' ' << (int) n.vel;
                o << '\n';
            }
            {
                static const MatTrack defTrack;
                for (int t = 0; t < kNumMatTracks; ++t) {
                    const auto& mt = pat.mat.tracks[(size_t) t];
                    std::ostringstream m;
                    m << ' ' << (int) mt.channel << ' ' << (int) mt.baseNote << ' ' << (int) mt.rlen << ' ' << (int) mt.muted << ' '
                      << (int) mt.intMin << ' ' << (int) mt.intMax << ' ' << (int) mt.eucLen << ' ' << (int) mt.eucPulses;
                    for (auto r : mt.rnd) m << ' ' << (int) r;
                    std::ostringstream d;
                    d << ' ' << (int) defTrack.channel << ' ' << (int) defTrack.baseNote << ' ' << (int) defTrack.rlen << ' ' << (int) defTrack.muted << ' '
                      << (int) defTrack.intMin << ' ' << (int) defTrack.intMax << ' ' << (int) defTrack.eucLen << ' ' << (int) defTrack.eucPulses;
                    for (auto r : defTrack.rnd) d << ' ' << (int) r;
                    if (m.str() != d.str() || mt.channel != s) o << "MTK " << s << ' ' << pi << ' ' << t << m.str() << '\n';
                    for (int l = 0; l < kNumMatLanes; ++l) {
                        const auto& ln = mt.lanes[(size_t) l];
                        const auto& dl = defTrack.lanes[(size_t) l];
                        if (ln.val == dl.val && ln.prob == dl.prob && ln.aux == dl.aux && ln.start == 0 && ln.end == 15 && ln.div == dl.div && ln.dir == dl.dir) continue;
                        o << "MLN " << s << ' ' << pi << ' ' << t << ' ' << l << ' ' << (int) ln.start << ' ' << (int) ln.end << ' ' << (int) ln.div << ' ' << (int) ln.dir;
                        for (int k = 0; k < 16; ++k) o << ' ' << (int) ln.val[(size_t) k] << ' ' << (int) ln.prob[(size_t) k] << ' ' << (int) ln.aux[(size_t) k];
                        o << '\n';
                    }
                }
            }
            {
                const auto& st = pat.stoch;
                Stochastic d;
                bool def = !st.valid && st.noteLevel == d.noteLevel && st.octUp == 0 && st.octDown == 0 && st.divRow == d.divRow
                        && st.pitchRand == d.pitchRand && st.rhythmRand == d.rhythmRand;
                if (!def) {
                    o << "STO " << s << ' ' << pi;
                    for (auto l : st.noteLevel) o << ' ' << (int) l;
                    o << ' ' << (int) st.octUp << ' ' << (int) st.octDown << ' ' << (int) st.divRow << ' ' << (int) st.pitchRand
                      << ' ' << (int) st.rhythmRand << ' ' << (int) st.valid;
                    if (st.valid) for (int i = 0; i < kMaxSteps; ++i) o << ' ' << (int) st.pitch[(size_t) i] << ' ' << (int) st.rhythm[(size_t) i];
                    o << '\n';
                }
            }
            for (int t = 0; t < kNumTracks; ++t) {
                const auto& tr = pat.tracks[(size_t) t];
                o << "TRK " << s << ' ' << pi << ' ' << t << ' ' << (int) tr.start << ' ' << (int) tr.end << ' ' << (int) tr.dir << ' '
                  << (int) tr.div << ' ' << (int) tr.rndDiv << ' ' << (int) tr.channel << ' ' << (int) tr.baseNote << ' '
                  << (int) tr.swing << ' ' << (int) tr.muted << ' ' << (int) tr.defVel << ' ' << (int) tr.defGate << ' '
                  << (int) tr.defRepeats << '\n';
                if (tr.accTrackMode || tr.accMax != 7 || tr.accMin) o << "TA " << s << ' ' << pi << ' ' << t << ' ' << (int) tr.accTrackMode << ' ' << (int) tr.accMax << ' ' << (int) tr.accMin << '\n';
                for (int i = 0; i < kMaxSteps; ++i) {
                    const auto& st = tr.steps[(size_t) i];
                    if (stepIsDefault(st)) continue;
                    writeStep(o, s, pi, t, i, st);
                    if (st.chordType || st.voicing || st.spread || st.strum)
                        o << "SC " << s << ' ' << pi << ' ' << t << ' ' << i << ' ' << (int) st.chordType << ' ' << (int) st.voicing
                          << ' ' << (int) st.spread << ' ' << (int) st.strum << '\n';
                    if (st.dur != 1) o << "SD " << s << ' ' << pi << ' ' << t << ' ' << i << ' ' << (int) st.dur << '\n';
                    if (st.accAmt || st.accMode || st.accMax != 7 || st.accMin || st.accTarget || st.accPctMode || st.accProb != 100)
                        o << "SA " << s << ' ' << pi << ' ' << t << ' ' << i << ' ' << (int) st.accAmt << ' ' << (int) st.accMode << ' ' << (int) st.accMax
                          << ' ' << (int) st.accMin << ' ' << (int) st.accTarget << ' ' << (int) st.accPctMode << ' ' << (int) st.accProb << '\n';
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
        if (tag == "CLN") {
            int s = -1, pi = -1; std::string nm;
            l >> s >> pi >> nm;
            if (s >= 0 && s < kNumSeqs && pi >= 0 && pi < kNumPatterns) p.seqs[(size_t) s].patterns[(size_t) pi].clip.name = nm == "-" ? "" : nm;
            continue;
        }
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
            p.seqs[(size_t) s].chMx = clampv<uint8_t>(at(2), 0, 14);
            if (v.size() >= 4) p.seqs[(size_t) s].voice = clampv<uint8_t>(at(3), 0, 1);
            if (v.size() >= 5) p.seqs[(size_t) s].harm = clampv<int8_t>(at(4), -1, 7);
        } else if (tag == "SA" && v.size() >= 11) {
            int s = at(0), pi = at(1), t = at(2), i = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks || i < 0 || i >= kMaxSteps) continue;
            auto& st = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t].steps[(size_t) i];
            st.accAmt = clampv<int8_t>(at(4), -24, 24); st.accMode = clampv<uint8_t>(at(5), 0, 3);
            st.accMax = clampv<int8_t>(at(6), 0, 24); st.accMin = clampv<int8_t>(at(7), -24, 0);
            st.accTarget = clampv<uint8_t>(at(8), 0, 2); st.accPctMode = clampv<uint8_t>(at(9), 0, 1); st.accProb = clampv<uint8_t>(at(10), 0, 100);
        } else if (tag == "TA" && v.size() >= 6) {
            int s = at(0), pi = at(1), t = at(2);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks) continue;
            auto& tr = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t];
            tr.accTrackMode = clampv<uint8_t>(at(3), 0, 1); tr.accMax = clampv<int8_t>(at(4), 0, 24); tr.accMin = clampv<int8_t>(at(5), -24, 0);
        } else if (tag == "SD" && v.size() >= 5) {
            int s = at(0), pi = at(1), t = at(2), i = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks || i < 0 || i >= kMaxSteps) continue;
            p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t].steps[(size_t) i].dur = clampv<uint8_t>(at(4), 1, 32);
        } else if (tag == "SC" && v.size() >= 8) {
            int s = at(0), pi = at(1), t = at(2), i = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumTracks || i < 0 || i >= kMaxSteps) continue;
            auto& st = p.seqs[(size_t) s].patterns[(size_t) pi].tracks[(size_t) t].steps[(size_t) i];
            st.chordType = clampv<uint8_t>(at(4), 0, 11);
            st.voicing = clampv<uint8_t>(at(5), 0, 5);
            st.spread = clampv<uint8_t>(at(6), 0, 6);
            st.strum = clampv<int8_t>(at(7), -100, 100);
        } else if (tag == "STO" && v.size() >= 20) {
            int s = at(0), pi = at(1);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns) continue;
            auto& st = p.seqs[(size_t) s].patterns[(size_t) pi].stoch;
            for (int k = 0; k < 12; ++k) st.noteLevel[(size_t) k] = clampv<uint8_t>(at(2 + (size_t) k), 0, 8);
            st.octUp = clampv<uint8_t>(at(14), 0, 4); st.octDown = clampv<uint8_t>(at(15), 0, 4);
            st.divRow = clampv<uint8_t>(at(16), 0, 7); st.pitchRand = clampv<uint8_t>(at(17), 0, 7);
            st.rhythmRand = clampv<uint8_t>(at(18), 0, 7);
            st.valid = at(19) != 0 && v.size() >= 20 + 2 * (size_t) kMaxSteps;
            if (st.valid) for (int i = 0; i < kMaxSteps; ++i) {
                st.pitch[(size_t) i] = clampv<int8_t>(at(20 + 2 * (size_t) i), -96, 96);
                st.rhythm[(size_t) i] = clampv<uint8_t>(at(21 + 2 * (size_t) i), 0, 99);
            }
        } else if (tag == "CLP" && v.size() >= 4) {
            int s = at(0), pi = at(1);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns) continue;
            auto& clip = p.seqs[(size_t) s].patterns[(size_t) pi].clip;
            clip.lengthTicks = (uint32_t) std::max(0, at(2));
            int n = std::clamp(at(3), 0, kMaxClipNotes);
            clip.notes.clear();
            for (int k = 0; k < n && 4 + 4 * (size_t) k + 3 < v.size(); ++k) {
                ClipNote cn;
                cn.tick = (uint32_t) std::max(0, at(4 + 4 * (size_t) k)); cn.len = (uint32_t) std::max(1, at(5 + 4 * (size_t) k));
                cn.note = clampv<uint8_t>(at(6 + 4 * (size_t) k), 0, 127); cn.vel = clampv<uint8_t>(at(7 + 4 * (size_t) k), 1, 127);
                clip.notes.push_back(cn);
            }
        } else if (tag == "MTK" && v.size() >= 21) {
            int s = at(0), pi = at(1), t = at(2);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumMatTracks) continue;
            auto& mt = p.seqs[(size_t) s].patterns[(size_t) pi].mat.tracks[(size_t) t];
            mt.channel = clampv<uint8_t>(at(3), 0, 15); mt.baseNote = clampv<uint8_t>(at(4), 0, 127);
            mt.rlen = clampv<uint8_t>(at(5), 0, 64); mt.muted = at(6) != 0;
            mt.intMin = clampv<int8_t>(at(7), -24, 0); mt.intMax = clampv<int8_t>(at(8), 0, 24);
            mt.eucLen = clampv<uint8_t>(at(9), 1, 16); mt.eucPulses = clampv<uint8_t>(at(10), 0, 16);
            for (int l = 0; l < kNumMatLanes; ++l) mt.rnd[(size_t) l] = clampv<uint8_t>(at(11 + (size_t) l), 0, 100);
        } else if (tag == "MLN" && v.size() >= 8 + 48) {
            int s = at(0), pi = at(1), t = at(2), l = at(3);
            if (s < 0 || s >= kNumSeqs || pi < 0 || pi >= kNumPatterns || t < 0 || t >= kNumMatTracks || l < 0 || l >= kNumMatLanes) continue;
            auto& ln = p.seqs[(size_t) s].patterns[(size_t) pi].mat.tracks[(size_t) t].lanes[(size_t) l];
            ln.start = clampv<uint8_t>(at(4), 0, 15); ln.end = clampv<uint8_t>(at(5), 0, 15);
            if (ln.end < ln.start) ln.end = ln.start;
            ln.div = clampv<Division>(at(6), 0, (int) Division::Count - 1);
            ln.dir = clampv<Dir>(at(7), 0, (int) Dir::Count - 1);
            for (int k = 0; k < 16; ++k) {
                ln.val[(size_t) k] = clampv<int8_t>(at(8 + 3 * (size_t) k), matLaneMin(l), std::min(127, matLaneMax(l)));
                ln.prob[(size_t) k] = clampv<uint8_t>(at(9 + 3 * (size_t) k), 0, 100);
                ln.aux[(size_t) k] = clampv<uint8_t>(at(10 + 3 * (size_t) k), 0, 2);
            }
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

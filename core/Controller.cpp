#include "Controller.h"
#include "Chords.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tek {

static const double kTapMs = 350.0;     // a pad released before this counts as a tap
static const double kDoubleTapMs = 350.0;

const char* btnLabel(Btn b) {
    static const char* l[] = {"SHIFT","BACK","MUTE","PERF","GEN","MOD","FLOW","ARRANGER","KEYBOARD","ARP","LFO","STEP","PAGE",
        "TRACK","RANDOM","LOAD","SAVE","COPY","PASTE","INIT","END","16","32","48","64","UNDO","PLAY","STOP","REC",
        "1","2","3","4","5","6","7","8"};
    return l[(int) b];
}
const char* btnSubLabel(Btn b) {
    switch (b) {
        case Btn::Back: return "config"; case Btn::Mute: return "nudge"; case Btn::Perf: return "project";
        case Btn::Gen: return "cv gate"; case Btn::Mod: return "ext mod"; case Btn::Flow: return "groove";
        case Btn::Arranger: return "on/off"; case Btn::Keyboard: return "preview"; case Btn::Arp: return "hold";
        case Btn::Lfo: return "condense"; case Btn::Step: return "chord"; case Btn::Page: return "expand";
        case Btn::Track: return "follow"; case Btn::Random: return "generate"; case Btn::Load: return "reload";
        case Btn::Save: return "quick"; case Btn::Copy: return "duplicate"; case Btn::Paste: return "clear";
        case Btn::Init: return "x2 div"; case Btn::End: return "/2 div"; case Btn::P16: return "<"; case Btn::P32: return "^";
        case Btn::P48: return "v"; case Btn::P64: return ">"; case Btn::Undo: return "redo"; case Btn::Play: return "tap";
        case Btn::Stop: return "sync"; case Btn::Rec: return "settings";
        default: return "";
    }
}

static uint32_t dim(uint32_t c, float k) {
    int r = (int) (((c >> 16) & 255) * k), g = (int) (((c >> 8) & 255) * k), b = (int) ((c & 255) * k);
    return (uint32_t) ((std::min(r, 255) << 16) | (std::min(g, 255) << 8) | std::min(b, 255));
}
static uint32_t mix(uint32_t a, uint32_t b, float k) {
    auto ch = [&](int s) { return (int) (((a >> s) & 255) * (1 - k) + ((b >> s) & 255) * k); };
    return (uint32_t) ((ch(16) << 16) | (ch(8) << 8) | ch(0));
}
template <typename T> static T clampT(int v, int lo, int hi) { return (T) std::clamp(v, lo, hi); }

Controller::Controller(Project& p, Engine& e) : proj(p), eng(e) { lastTap.fill(-1.0e9); resetUiState(); }

void Controller::resetUiState() {
    menu = Menu::None; grid = GridMode::Steps; heldPads.clear(); heldBtn.fill(false); muteLatched = false;
    page.fill(0); selTrack.fill(0); pianoRoll.fill(false);
    for (auto& a : viewDegree) a.fill(0);
    undoStack.clear(); redoStack.clear();
    pendingMode = -1; stepPage = 0; scrollMode = false;
}

// ============================================================================ undo
void Controller::pushUndo() {
    UndoEntry u { sel, seq().curPattern, std::make_unique<Pattern>(seq().pat()) };
    undoStack.push_back(std::move(u));
    while (undoStack.size() > 15) undoStack.pop_front();
    redoStack.clear();
}
void Controller::doUndo(double nowMs) {
    if (undoStack.empty()) { msg("NOTHING TO UNDO", nowMs); return; }
    auto u = std::move(undoStack.back()); undoStack.pop_back();
    auto& target = proj.seqs[(size_t) u.seq].patterns[(size_t) u.pat];
    redoStack.push_back({u.seq, u.pat, std::make_unique<Pattern>(target)});
    target = *u.data;
    msg("UNDO", nowMs);
}
void Controller::doRedo(double nowMs) {
    if (redoStack.empty()) { msg("NOTHING TO REDO", nowMs); return; }
    auto u = std::move(redoStack.back()); redoStack.pop_back();
    auto& target = proj.seqs[(size_t) u.seq].patterns[(size_t) u.pat];
    undoStack.push_back({u.seq, u.pat, std::make_unique<Pattern>(target)});
    target = *u.data;
    msg("REDO", nowMs);
}

// ============================================================================ step editing helpers
void Controller::placeNote(Track& tr, int idx, int degree) {
    auto& st = tr.steps[(size_t) idx];
    // splitting a tie that covers this step
    for (int i = std::max(0, idx - 127); i < idx; ++i) {
        auto& s = tr.steps[(size_t) i];
        if (s.active && s.tie > 0 && i + s.tie >= idx) s.tie = (uint8_t) (idx - i - 1);
    }
    if (!st.active) {
        Step fresh;
        fresh.velocity = tr.defVel; fresh.gate = tr.defGate; fresh.repeats = tr.defRepeats;
        st = fresh;
        st.active = true;
    }
    if (seq().mode == Mode::Poly) {
        if (st.numNotes >= seq().nVoices) { // replace the oldest note
            for (int i = 1; i < st.numNotes; ++i) st.notes[(size_t) i - 1] = st.notes[(size_t) i];
            st.numNotes--;
        }
        st.notes[st.numNotes++] = (int8_t) degree;
    } else {
        st.numNotes = 1;
        st.notes[0] = (int8_t) degree;
    }
}

void Controller::toggleStep(int x, int y, double nowMs) {
    if (isMatr()) { matTap(x, y, nowMs); return; }
    pushUndo();
    if (lanesView()) {
        int t = 7 - y;
        auto& tr = seq().pat().tracks[(size_t) t];
        int idx = stepAt(x);
        auto& st = tr.steps[(size_t) idx];
        if (st.active) st.clear();
        else placeNote(tr, idx, 0);
        return;
    }
    auto& tr = track();
    int idx = stepAt(x);
    int deg = degreeAtRow(y);
    auto& st = tr.steps[(size_t) idx];
    if (st.active && st.hasNote(deg)) {
        if (seq().mode == Mode::Poly && st.numNotes > 1) {
            int w = 0;
            for (int i = 0; i < st.numNotes; ++i) if (st.notes[(size_t) i] != deg) st.notes[(size_t) w++] = st.notes[(size_t) i];
            st.numNotes = (uint8_t) w;
        } else st.clear();
    } else {
        placeNote(tr, idx, deg);
        if (isChord()) msg(chordName(seq().scale, seq().baseMidi() + st.octave * 12, deg, st.chordType), nowMs);
    }
}

void Controller::tieSteps(const HeldPad& first, int x, int y, double nowMs) {
    int a = stepAt(first.x), b = stepAt(x);
    Track* trp;
    int deg;
    if (lanesView()) {
        if (first.y != y) return;
        trp = &seq().pat().tracks[(size_t) (7 - y)];
        deg = 0;
    } else {
        if (first.y != y) return;
        trp = &track();
        deg = degreeAtRow(y);
    }
    auto& tr = *trp;
    pushUndo();
    if (b > a) {
        auto& st = tr.steps[(size_t) a];
        if (!st.active || (!lanesView() && !st.hasNote(deg))) placeNote(tr, a, deg);
        for (int i = a + 1; i <= b; ++i) if (tr.steps[(size_t) i].active) tr.steps[(size_t) i].clear();
        tr.steps[(size_t) a].tie = (uint8_t) (b - a);
        msg("TIE " + std::to_string(b - a + 1) + " STEPS", nowMs);
    } else if (b < a) {
        // inverted tie gesture: fill the steps between with the same note (Config default "Fill")
        if (!tr.steps[(size_t) a].active) placeNote(tr, a, deg);
        Step src = tr.steps[(size_t) a];
        src.tie = 0;
        for (int i = b; i < a; ++i) tr.steps[(size_t) i] = src;
        msg("FILL", nowMs);
    }
}

void Controller::transposeAll(int x, int y, double nowMs) {
    auto& tr = track();
    int idx = stepAt(x);
    int ref = -1;
    for (int i = idx; i < kMaxSteps && ref < 0; ++i) if (tr.steps[(size_t) i].active && tr.steps[(size_t) i].numNotes) ref = i;
    if (ref < 0) return;
    const auto& rs = tr.steps[(size_t) ref];
    int lowest = rs.notes[0];
    for (int i = 1; i < rs.numNotes; ++i) lowest = std::min<int>(lowest, rs.notes[(size_t) i]);
    int delta = degreeAtRow(y) - lowest;
    if (delta == 0) return;
    pushUndo();
    for (auto& st : tr.steps)
        if (st.active) for (int i = 0; i < st.numNotes; ++i) st.notes[(size_t) i] = clampT<int8_t>(st.notes[(size_t) i] + delta, -120, 120);
    msg(std::string("TRANSPOSE ") + (delta > 0 ? "+" : "") + std::to_string(delta), nowMs);
}

std::vector<int> Controller::heldStepIndices() const {
    std::vector<int> v;
    for (auto& h : heldPads) {
        int idx = stepAt(h.x);
        if (std::find(v.begin(), v.end(), idx) == v.end()) v.push_back(idx);
    }
    return v;
}

// ============================================================================ pads
void Controller::padDown(int x, int y, double nowMs) {
    if (x < 0 || x > 15 || y < 0 || y > 7) return;
    if (grid == GridMode::Load) { loadPad(x, y, nowMs); return; }
    if (grid == GridMode::Column) { columnPad(x, y, nowMs); return; }

    if (isMatr()) { matPadDown(x, y, nowMs); return; }
    const int idx = stepAt(x);
    // track selection in multitrack lanes (column 1 selects, other columns select + edit)
    if (lanesView() && !held(Btn::Mute)) {
        int t = 7 - y;
        if (t != selTrack[(size_t) sel]) {
            selTrack[(size_t) sel] = t;
            if (x == 0) { heldPads.push_back({x, y, nowMs, true}); msg("TRACK " + std::to_string(t + 1), nowMs); return; }
        }
    }
    auto& tr = lanesView() ? seq().pat().tracks[(size_t) (7 - y)] : track();

    if (held(Btn::Init) || held(Btn::End)) {
        pushUndo();
        if (held(Btn::Init)) {
            int len = tr.end - tr.start;
            tr.start = (uint8_t) idx;
            if (tr.start > tr.end) tr.end = (uint8_t) std::min(kMaxSteps - 1, idx + len);
            msg("START " + std::to_string(idx + 1), nowMs);
            btnConsumed[(int) Btn::Init] = true;
        } else {
            tr.end = (uint8_t) idx;
            if (tr.end < tr.start) tr.start = tr.end;
            msg("END " + std::to_string(idx + 1), nowMs);
            btnConsumed[(int) Btn::End] = true;
        }
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    if (held(Btn::Mute)) {
        btnConsumed[(int) Btn::Mute] = true;
        if (isMulti() && x == 0) { int t = 7 - y; auto& mt = seq().pat().tracks[(size_t) t]; mt.muted = !mt.muted; msg(std::string("TRACK ") + std::to_string(t + 1) + (mt.muted ? " MUTED" : " ON"), nowMs); }
        else if (isMulti() && x == 15) { int t = 7 - y; eng.soloMask[(size_t) sel] ^= (uint8_t) (1u << t); msg("SOLO " + std::to_string(t + 1), nowMs); }
        else if (!isMulti() && x == 0) { seq().muted = !seq().muted; msg(seq().muted ? "SEQ MUTED" : "SEQ UNMUTED", nowMs); }
        else {
            auto& st = tr.steps[(size_t) idx];
            if (st.active) { pushUndo(); st.muted = !st.muted; msg(st.muted ? "STEP MUTED" : "STEP UNMUTED", nowMs); }
        }
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    if (isStoch()) { stochPad(x, y, nowMs); heldPads.push_back({x, y, nowMs, true}); return; }
    if (held(Btn::Copy)) {
        btnConsumed[(int) Btn::Copy] = true;
        if (!stepClipFresh) { stepClip.clear(); stepClipFresh = true; clipAnchor = idx; }
        stepClip.push_back({idx - clipAnchor, tr.steps[(size_t) idx]});
        msg("COPY " + std::to_string(stepClip.size()) + " STEP(S)", nowMs);
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    if (held(Btn::Paste)) {
        btnConsumed[(int) Btn::Paste] = true;
        if (!stepClip.empty()) {
            pushUndo();
            for (auto& [off, st] : stepClip) {
                int dst = idx + off;
                if (dst >= 0 && dst < kMaxSteps) tr.steps[(size_t) dst] = st;
            }
            msg("PASTE", nowMs);
        }
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    // holding the selected sequencer button + pad: transpose all notes (mono / poly / piano roll)
    if (heldBtn[(size_t) ((int) Btn::Seq1 + sel)] && !lanesView()) {
        btnConsumed[(int) Btn::Seq1 + sel] = true;
        transposeAll(x, y, nowMs);
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    // second pad while one is held: tie / inverted tie on the same row
    for (auto& h : heldPads) {
        if (h.y == y && h.x != x) {
            tieSteps(h, x, y, nowMs);
            h.consumed = true;
            heldPads.push_back({x, y, nowMs, true});
            return;
        }
    }
    if (held(Btn::Shift)) { stepPage = 1; heldPads.push_back({x, y, nowMs, false}); return; }
    heldPads.push_back({x, y, nowMs, false});
}

void Controller::padUp(int x, int y, double nowMs) {
    for (size_t i = 0; i < heldPads.size(); ++i) {
        auto h = heldPads[i];
        if (h.x != x || h.y != y) continue;
        heldPads.erase(heldPads.begin() + (long) i);
        if (!h.consumed && grid == GridMode::Steps && nowMs - h.downMs < kTapMs) toggleStep(x, y, nowMs);
        break;
    }
    if (heldPads.empty()) stepPage = 0;
}

void Controller::columnPad(int x, int y, double nowMs) {
    auto& tr = lanesView() ? track() : track();
    int idx = stepAt(x);
    auto& st = tr.steps[(size_t) idx];
    int r = 7 - y;
    pushUndo();
    if (isStoch()) {
        if (columnParam == 0) { st.velocity = (uint8_t) std::clamp((int) std::lround(r * 127.0 / 7.0), 1, 127); msg("VEL " + std::to_string(st.velocity), nowMs); }
        else if (columnParam == 1) { st.gate = r == 7 ? kGateLegato : (uint8_t) std::clamp(r * 14 + 2, 2, 99); msg("GATE " + std::to_string(st.gate), nowMs); }
        return;
    }
    if (columnParam != 2 && r == 0) { if (st.active) st.clear(); return; }
    if (!st.active) placeNote(tr, idx, st.numNotes ? st.notes[0] : (lanesView() ? 0 : viewDegC()));
    if (columnParam == 0) { st.velocity = (uint8_t) std::clamp((int) std::lround(r * 127.0 / 7.0), 1, 127); msg("VEL " + std::to_string(st.velocity), nowMs); }
    else if (columnParam == 1) {
        if (r == 7) { st.gate = kGateLegato; msg("GATE LEG", nowMs); }
        else { st.gate = (uint8_t) std::clamp(r * 16, 2, 99); msg("GATE " + std::to_string(st.gate), nowMs); }
    } else {
        if (held(Btn::Shift)) { st.repeats = (uint8_t) std::clamp(r * 2 + 1, 1, 15); msg("REPEAT " + std::to_string(st.repeats), nowMs); }
        else { st.retrig = (uint8_t) (r + 1); msg("RETRIG " + std::to_string(st.retrig), nowMs); }
    }
}

void Controller::loadPad(int x, int y, double nowMs) {
    int s = y, p = x;
    auto& q = proj.seqs[(size_t) s];
    if (held(Btn::Copy)) { btnConsumed[(int) Btn::Copy] = true; patClip = std::make_unique<Pattern>(q.patterns[(size_t) p]); msg("COPIED S" + std::to_string(s + 1) + " P" + std::to_string(p + 1), nowMs); return; }
    if (held(Btn::Paste)) {
        btnConsumed[(int) Btn::Paste] = true;
        if (patClip) { int keep = sel; sel = s; uint8_t cur = q.curPattern; q.curPattern = (uint8_t) p; pushUndo(); q.curPattern = cur; sel = keep;
            q.patterns[(size_t) p] = *patClip; msg("PASTED S" + std::to_string(s + 1) + " P" + std::to_string(p + 1), nowMs); }
        return;
    }
    if (held(Btn::Shift)) { // clear pattern
        int keep = sel; sel = s; uint8_t cur = q.curPattern; q.curPattern = (uint8_t) p; pushUndo(); q.curPattern = cur; sel = keep;
        q.patterns[(size_t) p] = Pattern();
        for (auto& tr : q.patterns[(size_t) p].tracks) tr.channel = q.channel;
        msg("CLEARED", nowMs);
        return;
    }
    eng.requestPattern(s, p);
    sel = s;
    msg("S" + std::to_string(s + 1) + " PATTERN " + std::to_string(p + 1), nowMs);
}

void Controller::stochPad(int x, int y, double nowMs) {
    auto& sc = seq().pat().stoch;
    const int r = 7 - y;
    pushUndo();
    if (x < 12) {
        auto& l = sc.noteLevel[(size_t) x];
        l = (uint8_t) (l == r + 1 ? 0 : r + 1);
        int pc = (seq().rootPc + x) % 12;
        msg(std::string(pitchClassName(pc)) + "  " + std::to_string(l * 100 / 8) + "%", nowMs);
    } else if (x == 12) {
        if (r >= 4) { int up = r - 3; sc.octUp = (uint8_t) (sc.octUp == up ? 0 : up); }
        else { int dn = 4 - r; sc.octDown = (uint8_t) (sc.octDown == dn ? 0 : dn); }
        msg("OCTAVES +" + std::to_string(sc.octUp) + " / -" + std::to_string(sc.octDown), nowMs);
    } else if (x == 13) {
        static const char* names[8] = {"x4 long", "x2 long", "base", "1/2", "1/3", "1/4", "1/6", "1/8"};
        sc.divRow = (uint8_t) r;
        msg(std::string("RHYTHM RANGE: ") + names[std::min(r, 2)] + (r == 2 ? "" : std::string(" .. ") + names[std::max(r, 2)]), nowMs);
    } else if (x == 14) {
        sc.pitchRand = (uint8_t) r;
        msg(r == 0 ? "PITCH LOCKED" : "PITCH RANDOM " + std::to_string(r * 100 / 7) + "%", nowMs);
    } else {
        sc.rhythmRand = (uint8_t) r;
        msg(r == 0 ? "RHYTHM LOCKED" : "RHYTHM RANDOM " + std::to_string(r * 100 / 7) + "%", nowMs);
    }
}

// The generated stochastic phrase as a mono pattern (for copy -> paste into mono / poly sequencers)
std::unique_ptr<Pattern> Controller::stochToPattern() const {
    auto p = std::make_unique<Pattern>(seq().pat());
    auto& tr = p->tracks[0];
    const auto& sc = seq().pat().stoch;
    for (int i = 0; i < kMaxSteps; ++i) {
        Step prof = tr.steps[(size_t) i];
        auto& st = tr.steps[(size_t) i];
        st = Step();
        st.velocity = prof.velocity; st.gate = prof.gate;
        uint8_t code = sc.valid ? sc.rhythm[(size_t) i] : 0;
        if (i < tr.start || i > tr.end || code == 0 || code == 99) continue;
        st.active = true; st.numNotes = 1;
        st.notes[0] = (int8_t) std::clamp(semisToDegree(seq().scale, sc.pitch[(size_t) i]), -120, 120);
        if (code >= 20) st.tie = (uint8_t) (code - 21);
        else if (code > 1) { st.retrig = code; st.gate = 99; }
    }
    p->stoch = Stochastic();
    return p;
}

// ============================================================================ matriceal (manual 6.9)
// Grid: 4 sections of 4 columns (one per track). Rows 0-3: 16 steps of the section's selected lane,
// rows 4-6: the 10 parameter lane pads, row 7: track select / mute.
namespace {
const uint32_t kLaneColor[kNumMatLanes] = {0xffffff, 0xffd23d, 0xff6fb5, 0xff8a2b, 0x2be0c8, 0x3d7bff, 0x9b5cff, 0xc77dff, 0x30d070, 0x7fd8ff};
}

void Controller::matRandomize(int k, int lane, double nowMs) {
    auto& mt = matTrack(k);
    auto& ln = mt.lanes[(size_t) lane];
    pushUndo();
    uint32_t seed = (uint32_t) (nowMs * 1000.0) ^ 0x9e3779b9u;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    const int amt = mt.rnd[(size_t) lane];
    for (int i = ln.start; i <= ln.end; ++i) {
        if ((int) (rnd() % 100) >= amt) { ln.val[(size_t) i] = (int8_t) std::min(127, matLaneDefault(lane)); continue; }
        int lo = matLaneMin(lane), hi = std::min(127, matLaneMax(lane));
        if (lane == kLaneNote) { lo = -7; hi = 7; }
        if (lane == kLaneIntv) { lo = -3; hi = 3; }
        if (lane == kLaneOct) { lo = -1; hi = 1; }
        if (lane == kLanePuls) hi = 4;
        if (lane == kLaneRetrig) { lo = 1; hi = 4; }
        if (lane == kLaneGate) hi = 99;
        ln.val[(size_t) i] = (int8_t) (lo + (int) (rnd() % (uint32_t) (hi - lo + 1)));
    }
    msg(std::string("RANDOM ") + matLaneName(lane) + " " + std::to_string(amt) + "%", nowMs);
}

void Controller::matPadDown(int x, int y, double nowMs) {
    const int k = x / 4, cx = x % 4;
    auto& mt = matTrack(k);
    if (y == 7) { // track pad
        if (held(Btn::Mute)) { btnConsumed[(int) Btn::Mute] = true; mt.muted = !mt.muted; msg("TRACK " + std::to_string(k + 1) + (mt.muted ? " MUTED" : " ON"), nowMs); heldPads.push_back({x, y, nowMs, true}); return; }
        matSel[(size_t) sel] = k;
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    if (y >= 4) { // parameter lane pad
        int lane = (y - 4) * 4 + cx;
        if (lane >= kNumMatLanes) { heldPads.push_back({x, y, nowMs, true}); return; }
        matSel[(size_t) sel] = k;
        matLane[(size_t) sel][(size_t) k] = lane;
        if (held(Btn::Random)) { btnConsumed[(int) Btn::Random] = true; matRandomize(k, lane, nowMs); }
        else msg(std::string("T") + std::to_string(k + 1) + " " + matLaneName(lane), nowMs);
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    // step pad
    const int step = y * 4 + cx;
    matSel[(size_t) sel] = k;
    auto& ln = mt.lanes[(size_t) matLaneOf(k)];
    if (held(Btn::Init) || held(Btn::End)) {
        pushUndo();
        if (held(Btn::Init)) { int len = ln.end - ln.start; ln.start = (uint8_t) step; if (ln.start > ln.end) ln.end = (uint8_t) std::min(15, step + len); btnConsumed[(int) Btn::Init] = true; }
        else { ln.end = (uint8_t) step; if (ln.end < ln.start) ln.start = ln.end; btnConsumed[(int) Btn::End] = true; }
        msg(std::string(matLaneName(matLaneOf(k))) + " " + std::to_string(ln.start + 1) + "-" + std::to_string(ln.end + 1), nowMs);
        heldPads.push_back({x, y, nowMs, true});
        return;
    }
    heldPads.push_back({x, y, nowMs, false});
}

void Controller::matTap(int x, int y, double nowMs) {
    if (y > 3) return;
    const int k = x / 4, step = y * 4 + x % 4;
    const int lane = matLaneOf(k);
    auto& ln = matTrack(k).lanes[(size_t) lane];
    pushUndo();
    if (lane == kLaneTrig) { ln.val[(size_t) step] = ln.val[(size_t) step] ? 0 : 1; return; }
    if (ln.val[(size_t) step] != std::min(127, matLaneDefault(lane))) { ln.val[(size_t) step] = (int8_t) std::min(127, matLaneDefault(lane)); ln.prob[(size_t) step] = 100; msg("STEP RESET", nowMs); }
    else msg("HOLD STEP + KNOB 1 TO SET", nowMs);
}

void Controller::matKnob(int knob, int delta, double nowMs) {
    const auto& h = heldPads.front();
    const int k = h.x / 4;
    auto& mt = matTrack(k);
    for (auto& hp : heldPads) hp.consumed = true;
    pushUndo();
    if (h.y == 7) { // track settings: Ch, Ofs, RLen, Clr
        if (knob == 0) mt.channel = clampT<uint8_t>(mt.channel + delta, 0, 15);
        if (knob == 1) mt.baseNote = clampT<uint8_t>(mt.baseNote + delta, 0, 127);
        if (knob == 2) mt.rlen = clampT<uint8_t>(mt.rlen + delta, 0, 64);
        return;
    }
    const int lane = h.y >= 4 ? (h.y - 4) * 4 + h.x % 4 : matLaneOf(k);
    if (lane >= kNumMatLanes) return;
    auto& ln = mt.lanes[(size_t) lane];
    if (h.y >= 4) { // lane settings: (Min, Max), Div, Dir
        if (knob == 0 && lane == kLaneIntv) mt.intMin = clampT<int8_t>(mt.intMin + delta, -24, 0);
        if (knob == 1 && lane == kLaneIntv) mt.intMax = clampT<int8_t>(mt.intMax + delta, 0, 24);
        if (knob == 2) ln.div = clampT<Division>((int) ln.div + delta, 0, (int) Division::Count - 1);
        if (knob == 3) ln.dir = clampT<Dir>((int) ln.dir + delta, 0, (int) Dir::Count - 1);
        return;
    }
    if (knob == 3) { mt.rnd[(size_t) lane] = clampT<uint8_t>(mt.rnd[(size_t) lane] + delta * 5, 0, 100); return; }
    if (lane == kLaneTrig) { // Len / Puls: euclidean generator
        if (knob == 0) mt.eucLen = clampT<uint8_t>(mt.eucLen + delta, 1, 16);
        if (knob == 1) mt.eucPulses = clampT<uint8_t>(mt.eucPulses + delta, 0, mt.eucLen);
        if (knob <= 1) {
            if (mt.eucPulses > mt.eucLen) mt.eucPulses = mt.eucLen;
            bool e[16];
            euclid(mt.eucLen, mt.eucPulses, 0, e);
            for (int i = 0; i < 16; ++i) ln.val[(size_t) i] = 0;
            for (int i = 0; i < mt.eucLen; ++i) ln.val[(size_t) ((ln.start + i) % 16)] = e[i] ? 1 : 0;
            ln.end = (uint8_t) std::min(15, ln.start + mt.eucLen - 1);
            msg("EUCLID " + std::to_string(mt.eucPulses) + "/" + std::to_string(mt.eucLen), nowMs);
        }
        return;
    }
    for (auto& hp : heldPads) {
        if (hp.y > 3 || hp.x / 4 != k) continue;
        int step = hp.y * 4 + hp.x % 4;
        if (knob == 0) ln.val[(size_t) step] = (int8_t) std::clamp(ln.val[(size_t) step] + delta, matLaneMin(lane), std::min(127, matLaneMax(lane)));
        if (knob == 1) {
            if (lane == kLanePuls) ln.aux[(size_t) step] = clampT<uint8_t>(ln.aux[(size_t) step] + delta, 0, 2);
            else ln.prob[(size_t) step] = clampT<uint8_t>(ln.prob[(size_t) step] + delta * 5, 0, 100);
        }
    }
}

void Controller::matKnobTap(int knob, double nowMs) {
    const auto& h = heldPads.front();
    const int k = h.x / 4;
    for (auto& hp : heldPads) hp.consumed = true;
    auto& mt = matTrack(k);
    if (h.y == 7) {
        if (knob == 3) { pushUndo(); auto ch = mt.channel; auto base = mt.baseNote; mt = MatTrack(); mt.channel = ch; mt.baseNote = base; msg("TRACK " + std::to_string(k + 1) + " CLEARED", nowMs); }
        return;
    }
    if (h.y >= 4) return;
    const int lane = matLaneOf(k);
    auto& ln = mt.lanes[(size_t) lane];
    if (knob == 2) { pushUndo(); ln.val.fill((int8_t) std::min(127, matLaneDefault(lane))); ln.prob.fill(100); ln.aux.fill(0); msg(std::string(matLaneName(lane)) + " CLEARED", nowMs); }
    else if (knob == 3) matRandomize(k, lane, nowMs);
    else if (knob == 0 && lane != kLaneTrig) {
        pushUndo();
        for (auto& hp : heldPads) if (hp.y <= 3 && hp.x / 4 == k) ln.val[(size_t) (hp.y * 4 + hp.x % 4)] = (int8_t) std::min(127, matLaneDefault(lane));
    }
}

void Controller::renderMatr(Frame& f) const {
    const auto& q = seq();
    for (int k = 0; k < kNumMatTracks; ++k) {
        const auto& mt = matTrack(k);
        const int lane = matLaneOf(k);
        const auto& ln = mt.lanes[(size_t) lane];
        const bool selT = matSel[(size_t) sel] == k;
        const uint32_t lc = kLaneColor[lane];
        const int ph = eng.isRunning() ? (lane == kLaneTrig ? eng.playhead(sel, k) : eng.matLanePos(sel, k, lane)) : -1;
        const int def = std::min(127, matLaneDefault(lane));
        const int span = std::max(1, std::max(std::abs(std::min(127, matLaneMax(lane)) - def), std::abs(def - matLaneMin(lane))));
        for (int st = 0; st < 16; ++st) {
            const int x = k * 4 + st % 4, y = st / 4;
            const bool inRange = st >= ln.start && st <= ln.end;
            int v = ln.val[(size_t) st];
            uint32_t c = inRange ? 0x34373b : 0x101010;
            if (lane == kLaneTrig) { if (v) c = inRange ? 0xffffff : 0x505050; }
            else if (v != def) {
                float a = 0.3f + 0.7f * std::min(1.0f, std::abs(v - def) / (float) span);
                bool neg = (lane == kLaneNote || lane == kLaneIntv || lane == kLaneOct) && v < def;
                if (lane == kLaneProb) a = 0.3f + 0.7f * (1.0f - v / 100.0f);
                c = dim(neg ? 0xff3030 : lc, inRange ? a : a * 0.4f);
            }
            if (st == ph && inRange) c = c == 0x34373b ? 0x6a6d73 : mix(c, 0xffffff, 0.45f);
            f.pads[(size_t) (y * 16 + x)] = c;
        }
        for (int l = 0; l < 12; ++l) {
            const int x = k * 4 + l % 4, y = 4 + l / 4;
            uint32_t c = 0;
            if (l < kNumMatLanes) c = l == lane ? kLaneColor[l] : dim(kLaneColor[l], selT ? 0.35f : 0.22f);
            f.pads[(size_t) (y * 16 + x)] = c;
        }
        for (int i = 0; i < 4; ++i) {
            uint32_t c = selT ? (mt.muted ? 0xc03030 : 0xffffff) : (mt.muted ? 0x000000 : 0x505050);
            f.pads[(size_t) (7 * 16 + k * 4 + i)] = c;
        }
    }
    (void) q;
}

void Controller::renderMatrDisplay(Frame& f) const {
    const int k = heldPads.empty() ? matSel[(size_t) sel] : heldPads.front().x / 4;
    const auto& mt = matTrack(k);
    const int lane = matLaneOf(k);
    const auto& ln = mt.lanes[(size_t) lane];
    auto set = [&](const char* a, const std::string& av, const char* b, const std::string& bv, const char* c, const std::string& cv, const char* d, const std::string& dv) {
        f.labels = {a, b, c, d}; f.values = {av, bv, cv, dv};
    };
    auto onoff = [](int v) { return v == 0 ? std::string("OFF") : std::to_string(v); };
    f.line1 = "T" + std::to_string(k + 1) + " " + matLaneName(lane) + "   " + divisionLabel(ln.div) + " " + dirLabel(ln.dir) + "  " + std::to_string(ln.start + 1) + "-" + std::to_string(ln.end + 1);
    f.line2 = "Ch" + std::to_string(mt.channel + 1) + "  Ofs " + noteName(mt.baseNote) + (mt.muted ? "  MUTED" : "");
    if (heldPads.empty()) { set("TRK", std::to_string(k + 1), "LANE", matLaneName(lane), "RLen", onoff(mt.rlen), "Rnd", std::to_string(mt.rnd[(size_t) lane]) + "%"); return; }
    const auto& h = heldPads.front();
    if (h.y == 7) { f.line1 = "Track " + std::to_string(k + 1) + " Menu"; set("Ch", std::to_string(mt.channel + 1), "Ofs", noteName(mt.baseNote), "RLen", onoff(mt.rlen), "Clr", "tap"); return; }
    if (h.y >= 4) {
        int hl = (h.y - 4) * 4 + h.x % 4;
        if (hl >= kNumMatLanes) return;
        const auto& hln = mt.lanes[(size_t) hl];
        f.line1 = std::string(matLaneName(hl)) + "  lane settings";
        if (hl == kLaneIntv) set("Min", std::to_string(mt.intMin), "Max", std::to_string(mt.intMax), "Div", divisionLabel(hln.div), "Dir", dirLabel(hln.dir));
        else set("-", "", "-", "", "Div", divisionLabel(hln.div), "Dir", dirLabel(hln.dir));
        return;
    }
    const int step = h.y * 4 + h.x % 4;
    const int v = ln.val[(size_t) step];
    f.line1 = std::string(matLaneName(lane)) + " - Step " + std::to_string(step + 1);
    const std::string rnd = std::to_string(mt.rnd[(size_t) lane]) + "%";
    switch (lane) {
        case kLaneTrig: set("Len", std::to_string(mt.eucLen), "Puls", std::to_string(mt.eucPulses), "Clr", "tap", "Rnd", rnd); break;
        case kLaneNote: set("Note", noteName(mt.baseNote + degreeToSemis(seq().scale, v)), "Prob", std::to_string(ln.prob[(size_t) step]) + "%", "Clr", "tap", "Rnd", rnd); break;
        case kLaneIntv: set("Intv", (v > 0 ? "+" : "") + std::to_string(v), "Prob", std::to_string(ln.prob[(size_t) step]) + "%", "Clr", "tap", "Rnd", rnd); break;
        case kLanePuls: { static const char* t[3] = {"First", "All", "Tie"}; set("Puls", std::to_string(v), "Type", t[std::min<int>(2, ln.aux[(size_t) step])], "Clr", "tap", "Rnd", rnd); break; }
        case kLaneVel: set("Vel", std::to_string(v), "-", "", "Clr", "tap", "Rnd", rnd); break;
        case kLaneOct: set("Oct", (v > 0 ? "+" : "") + std::to_string(v), "Prob", std::to_string(ln.prob[(size_t) step]) + "%", "Clr", "tap", "Rnd", rnd); break;
        case kLaneGate: set("Gate", v >= 101 ? "TIE" : v == 100 ? "LEG" : std::to_string(v), "-", "", "Clr", "tap", "Rnd", rnd); break;
        case kLaneProb: set("Prob", std::to_string(v) + "%", "-", "", "Clr", "tap", "Rnd", rnd); break;
        case kLaneRetrig: set("Rep", v == 0 ? "SKIP" : std::to_string(v), "Prob", std::to_string(ln.prob[(size_t) step]) + "%", "Clr", "tap", "Rnd", rnd); break;
        default: set("Glid", std::to_string(v), "Prob", std::to_string(ln.prob[(size_t) step]) + "%", "Clr", "tap", "Rnd", rnd); break;
    }
}

// ============================================================================ encoders
void Controller::relative(uint8_t Step::*field, int delta, int lo, int hi, const char* name, double nowMs) {
    auto& tr = track();
    pushUndo();
    bool any = false;
    for (int i = 0; i < kMaxSteps; ++i) {
        auto& st = tr.steps[(size_t) i];
        if (!st.active && !(isStoch() && i >= tr.start && i <= tr.end)) continue;
        if (field == &Step::velocity && st.velLocked) continue;
        any = true;
        st.*field = (uint8_t) std::clamp((int) (st.*field) + delta, lo, hi);
    }
    if (field == &Step::velocity) tr.defVel = (uint8_t) std::clamp((int) tr.defVel + delta, lo, hi);
    if (field == &Step::gate) tr.defGate = (uint8_t) std::clamp((int) tr.defGate + delta, lo, hi);
    if (field == &Step::repeats) tr.defRepeats = (uint8_t) std::clamp((int) tr.defRepeats + delta, lo, hi);
    (void) any;
    msg(std::string("Global ") + name + ": " + rangeText(field), nowMs);
}

std::string Controller::rangeText(uint8_t Step::*field) const {
    const auto& tr = track();
    int lo = 999, hi = -1;
    for (auto& st : tr.steps) if (st.active) { lo = std::min<int>(lo, st.*field); hi = std::max<int>(hi, st.*field); }
    if (hi < 0) {
        int d = field == &Step::velocity ? tr.defVel : field == &Step::gate ? tr.defGate : tr.defRepeats;
        return std::to_string(d);
    }
    auto fmt = [&](int v) { return (field == &Step::gate && v >= kGateLegato) ? std::string("LEG") : std::to_string(v); };
    return lo == hi ? fmt(lo) : fmt(lo) + "-" + fmt(hi);
}

void Controller::editHeldSteps(int knob, int delta, double nowMs) {
    auto idxs = heldStepIndices();
    if (idxs.empty()) return;
    pushUndo();
    for (auto& h : heldPads) h.consumed = true;
    std::string shown;
    for (int idx : idxs) {
        Track& tr = lanesView() ? seq().pat().tracks[(size_t) (7 - heldPads.front().y)] : track();
        auto& st = tr.steps[(size_t) idx];
        if (stepPage == 0) {
            switch (knob) {
                case 0: st.velocity = clampT<uint8_t>(st.velocity + delta, 1, 127); shown = "Vel " + std::to_string(st.velocity); break;
                case 1:
                    if (isMulti()) { for (int n = 0; n < st.numNotes; ++n) st.notes[(size_t) n] = clampT<int8_t>(st.notes[(size_t) n] + delta, -60, 60);
                        if (!st.numNotes) placeNote(tr, idx, 0);
                        shown = "Note " + noteName(tr.baseNote + degreeToSemis(seq().scale, st.notes[0])); }
                    else { st.octave = clampT<int8_t>(st.octave + delta, -4, 4); shown = "Oct " + std::to_string(st.octave); }
                    break;
                case 2: st.gate = clampT<uint8_t>(st.gate + delta, 2, kGateLegato); shown = st.gate >= kGateLegato ? "Gate LEG" : "Gate " + std::to_string(st.gate); break;
                case 3: st.trig = trigFromListIndex(trigToListIndex(st.trig) + delta); shown = "Trig " + trigLabel(st.trig); break;
            }
        } else if (stepPage == 1) {
            switch (knob) {
                case 0: st.repeats = clampT<uint8_t>(st.repeats + delta, 1, 15); shown = "Rept " + std::to_string(st.repeats); break;
                case 1: st.repDur = clampT<uint8_t>(st.repDur + delta, 1, 15); shown = "Durat x" + std::to_string(st.repDur); break;
                case 2: st.ramp = clampT<int8_t>(st.ramp + delta * 5, -70, 70); shown = "Ramp " + std::to_string(st.ramp) + "%"; break;
                case 3: st.accel = clampT<int8_t>(st.accel + delta * 2, -28, 28); shown = "Accel " + std::to_string(st.accel) + "%"; break;
            }
        } else if (stepPage == 2) {
            switch (knob) {
                case 0: st.offset = clampT<int8_t>(st.offset + delta, -45, 45); break;
                case 1:
                    if (isChord()) st.strum = clampT<int8_t>(st.strum + delta * 5, -100, 100);
                    else st.retrig = clampT<uint8_t>(st.retrig + delta, 1, 8);
                    break;
                case 2: st.rng = clampT<int8_t>(st.rng + delta, 0, 24); break;
                case 3: st.repProb = clampT<uint8_t>(st.repProb + delta * 5, 0, 100); break;
            }
        } else { // page 4 (chord): spread, type, voicing
            switch (knob) {
                case 0: st.spread = clampT<uint8_t>(st.spread + delta, 0, 6); break;
                case 1: st.chordType = clampT<uint8_t>(st.chordType + delta, 0, kNumChordTypes - 1); break;
                case 2: st.voicing = clampT<uint8_t>(st.voicing + delta, 0, kNumVoicings - 1); break;
                default: break;
            }
        }
    }
    (void) nowMs;
    (void) shown;
}

void Controller::editGlobal(int knob, int delta, double nowMs) {
    if (isMatr()) { (void) knob; (void) delta; msg("HOLD A STEP, LANE OR TRACK PAD", nowMs); return; }
    switch (knob) {
        case 0: relative(&Step::velocity, delta, 1, 127, "Vel", nowMs); break;
        case 1:
            if (lanesView()) { // shift every note of the selected track by scale degrees
                pushUndo();
                for (auto& st : track().steps) if (st.active) for (int n = 0; n < st.numNotes; ++n) st.notes[(size_t) n] = clampT<int8_t>(st.notes[(size_t) n] + delta, -60, 60);
                msg("TRACK NOTES " + std::string(delta > 0 ? "+" : "") + std::to_string(delta), nowMs);
            } else if (scrollMode || isMulti()) {
                viewDeg() = std::clamp(viewDeg() + delta, -60, 60);
                msg("VIEW " + noteName((isMulti() ? track().baseNote : seq().baseMidi()) + degreeToSemis(seq().scale, viewDeg())), nowMs);
            } else {
                seq().baseOctave = clampT<int8_t>(seq().baseOctave + delta, -2, 7);
                msg("OCTAVE " + noteName(seq().baseMidi()), nowMs);
            }
            break;
        case 2: relative(&Step::gate, delta, 2, kGateLegato, "Gate", nowMs); break;
        case 3:
            if (isStoch()) { msg("MOD LANE: PHASE 4", nowMs); break; }
            if (grid == GridMode::Column && columnParam == 2) relative(&Step::retrig, delta, 1, 8, "Retrig", nowMs);
            else relative(&Step::repeats, delta, 1, 15, "Repeat", nowMs);
            break;
    }
}

void Controller::editShift(int knob, int delta, double nowMs) {
    auto& q = seq();
    switch (knob) {
        case 0:
            proj.bpm = std::clamp(proj.bpm + delta, 20.0, 300.0);
            msg(proj.internalClock ? "BPM " + std::to_string((int) proj.bpm) : "HOST TEMPO (INT BPM " + std::to_string((int) proj.bpm) + ")", nowMs);
            break;
        case 1:
            pushUndo();
            if (isMulti() && track().swing != 0) { track().swing = clampT<uint8_t>(track().swing + delta, 10, 90); msg("TRACK SWING " + std::to_string(track().swing), nowMs); }
            else { q.pat().swing = clampT<uint8_t>(q.pat().swing + delta, 10, 90); msg("SWING " + std::to_string(q.pat().swing), nowMs); }
            break;
        case 2: q.scale = clampT<uint8_t>(q.scale + delta, 0, numScales() - 1); msg(scaleLibrary()[q.scale].label, nowMs); break;
        case 3: {
            int midi = std::clamp(q.baseMidi() + delta, 0, 119);
            q.rootPc = (uint8_t) (midi % 12); q.baseOctave = (int8_t) (midi / 12 - 2);
            msg("ROOT " + noteName(q.baseMidi()), nowMs);
            break;
        }
    }
}

void Controller::editMenu(int knob, int delta, double nowMs) {
    auto& q = seq();
    auto& tr = track();
    switch (menu) {
        case Menu::Setup:
            if (menuPage == 0) {
                if (knob == 0) { if (isMulti()) tr.channel = clampT<uint8_t>(tr.channel + delta, 0, 15); else q.channel = clampT<uint8_t>(q.channel + delta, 0, 15); }
                if (knob == 1) proj.output = clampT<OutputMode>((int) proj.output + delta, 0, (int) OutputMode::Count - 1);
                if (knob == 2) q.programChange = clampT<uint8_t>(q.programChange + delta, 0, 128);
                if (knob == 3) pendingMode = std::clamp((pendingMode < 0 ? (int) q.mode : pendingMode) + delta, 0, (int) Mode::Count - 1);
            } else if (menuPage == 1) {
                if (knob == 0) q.punchIn = clampT<uint8_t>(q.punchIn + delta, 0, 8);
                if (knob == 1) q.resetBars = clampT<uint8_t>(q.resetBars + delta, 0, 64);
                if (knob == 2) q.bank = clampT<uint8_t>(q.bank + delta, 0, 128);
                if (knob == 3) q.transpose = clampT<uint8_t>(q.transpose + delta, 0, 17);
            } else {
                if (knob == 0) { if (q.mode == Mode::Poly) q.nVoices = clampT<uint8_t>(q.nVoices + delta, 1, 7);
                                 else if (isMulti()) tr.swing = (uint8_t) (tr.swing == 0 ? (delta > 0 ? 10 : 0) : (tr.swing + delta < 10 ? 0 : std::min(90, tr.swing + delta))); }
                if (knob == 1 && (q.mode == Mode::Poly || q.mode == Mode::Chord)) q.chMx = clampT<uint8_t>(q.chMx + delta, 0, 1);
                if (knob == 3) q.color = clampT<uint8_t>(q.color + delta, 0, 7);
            }
            break;
        case Menu::TrackM:
            pushUndo();
            if (isMatr()) {
                auto& mt = matTrack(matSel[(size_t) sel]);
                if (knob == 0) mt.channel = clampT<uint8_t>(mt.channel + delta, 0, 15);
                if (knob == 1) mt.baseNote = clampT<uint8_t>(mt.baseNote + delta, 0, 127);
                if (knob == 2) mt.rlen = clampT<uint8_t>(mt.rlen + delta, 0, 64);
            } else if (isMulti()) {
                if (menuPage == 0) {
                    if (knob == 0) tr.channel = clampT<uint8_t>(tr.channel + delta, 0, 15);
                    if (knob == 1) tr.baseNote = clampT<uint8_t>(tr.baseNote + delta, 0, 127);
                    if (knob == 2) tr.div = clampT<Division>((int) tr.div + delta, 0, (int) Division::Count - 1);
                    if (knob == 3) tr.dir = clampT<Dir>((int) tr.dir + delta, 0, (int) Dir::Count - 1);
                } else {
                    if (knob == 0) tr.rndDiv = clampT<uint8_t>(tr.rndDiv + delta * 5, 0, 100);
                    if (knob == 1) tr.swing = (uint8_t) (tr.swing == 0 ? (delta > 0 ? 10 : 0) : (tr.swing + delta < 10 ? 0 : std::min(90, tr.swing + delta)));
                }
            } else {
                if (knob == 0) viewDeg() = std::clamp(viewDeg() + delta, -60, 60);
                if (knob == 1) tr.dir = clampT<Dir>((int) tr.dir + delta, 0, (int) Dir::Count - 1);
                if (knob == 2) tr.rndDiv = clampT<uint8_t>(tr.rndDiv + delta * 5, 0, 100);
                if (knob == 3) tr.div = clampT<Division>((int) tr.div + delta, 0, (int) Division::Count - 1);
            }
            break;
        case Menu::StepM: {
            pushUndo();
            if (menuPage == 2 && knob == 3) { q.bassMode = clampT<uint8_t>(q.bassMode + delta, 0, 17); break; }
            for (auto& st : tr.steps) {
                if (!st.active) continue;
                if (menuPage == 0) {
                    if (knob == 0) st.repeats = clampT<uint8_t>(st.repeats + delta, 1, 15);
                    if (knob == 1) st.repDur = clampT<uint8_t>(st.repDur + delta, 1, 15);
                    if (knob == 2) st.ramp = clampT<int8_t>(st.ramp + delta * 5, -70, 70);
                    if (knob == 3) st.accel = clampT<int8_t>(st.accel + delta * 2, -28, 28);
                } else if (menuPage == 1) {
                    if (knob == 0) st.offset = clampT<int8_t>(st.offset + delta, -45, 45);
                    if (knob == 1) { if (isChord()) st.strum = clampT<int8_t>(st.strum + delta * 5, -100, 100); else st.retrig = clampT<uint8_t>(st.retrig + delta, 1, 8); }
                    if (knob == 2) st.rng = clampT<int8_t>(st.rng + delta, 0, 24);
                    if (knob == 3) st.repProb = clampT<uint8_t>(st.repProb + delta * 5, 0, 100);
                } else {
                    if (knob == 0) st.spread = clampT<uint8_t>(st.spread + delta, 0, 6);
                    if (knob == 1) st.chordType = clampT<uint8_t>(st.chordType + delta, 0, kNumChordTypes - 1);
                    if (knob == 2) st.voicing = clampT<uint8_t>(st.voicing + delta, 0, kNumVoicings - 1);
                }
            }
            break;
        }
        case Menu::Nudge:
        case Menu::Bend: {
            bool all = held(Btn::Shift);
            for (int s = 0; s < kNumSeqs; ++s) {
                if (!all && s != knob + (knobUpper ? 0 : 4)) continue;
                auto& x = proj.seqs[(size_t) s];
                if (menu == Menu::Nudge) x.nudgeMs = clampT<int8_t>(x.nudgeMs + delta, -100, 100);
                else x.timeBend = clampT<int8_t>(x.timeBend + delta, -120, 120);
            }
            break;
        }
        case Menu::Groove:
            if (menuPage == 0) {
                if (knob == 0) q.grooveAccent = clampT<int16_t>(q.grooveAccent + delta * 5, -120, 120);
                if (knob == 1) q.grooveTime = clampT<int16_t>(q.grooveTime + delta * 5, -120, 120);
                if (knob == 2) q.grooveOffset = clampT<int8_t>(q.grooveOffset + delta, -15, 15);
                if (knob == 3) q.grooveLen = clampT<uint8_t>(q.grooveLen + delta, 1, 16);
            } else {
                if (knob == 0) q.grooveRand = clampT<uint8_t>(q.grooveRand + delta * 5, 0, 100);
                if (knob == 1) q.groove = clampT<uint8_t>(q.groove + delta, 0, kNumGrooves);
            }
            break;
        default: break;
    }
    (void) nowMs;
}

void Controller::encoderTurn(int knob, int delta, double nowMs) {
    if (knob < 0 || knob > 3 || delta == 0) return;
    if (encDown[(size_t) knob]) encConsumed[(size_t) knob] = true;
    if (isMatr() && grid == GridMode::Steps && !heldPads.empty() && menu == Menu::None) { matKnob(knob, delta, nowMs); return; }
    if (grid == GridMode::Steps && !heldPads.empty() && menu == Menu::None) { editHeldSteps(knob, delta, nowMs); return; }
    if (held(Btn::Init) || held(Btn::End)) {
        btnConsumed[(int) Btn::Init] = btnConsumed[(int) Btn::End] = true;
        auto& tr = track();
        pushUndo();
        if (knob == 1) { int len = tr.end - tr.start; tr.start = clampT<uint8_t>(tr.start + delta, 0, kMaxSteps - 1); if (tr.start > tr.end) tr.end = (uint8_t) std::min(kMaxSteps - 1, tr.start + len); }
        if (knob == 2) { tr.end = clampT<uint8_t>(tr.end + delta, 0, kMaxSteps - 1); if (tr.end < tr.start) tr.start = tr.end; }
        msg("START " + std::to_string(tr.start + 1) + "  END " + std::to_string(tr.end + 1), nowMs);
        return;
    }
    if (menu != Menu::None) { editMenu(knob, delta, nowMs); return; }
    if (held(Btn::Shift)) { btnConsumed[(int) Btn::Shift] = true; editShift(knob, delta, nowMs); return; }
    editGlobal(knob, delta, nowMs);
}

void Controller::tapKnob(int knob, double nowMs) {
    if (isMatr() && grid == GridMode::Steps && !heldPads.empty() && menu == Menu::None) { matKnobTap(knob, nowMs); return; }
    // with a held step
    if (grid == GridMode::Steps && !heldPads.empty() && menu == Menu::None) {
        for (auto& h : heldPads) h.consumed = true;
        auto idxs = heldStepIndices();
        Track& tr = lanesView() ? seq().pat().tracks[(size_t) (7 - heldPads.front().y)] : track();
        pushUndo();
        for (int idx : idxs) {
            auto& st = tr.steps[(size_t) idx];
            if (stepPage == 0 && knob == 0) {
                if (isMulti()) { // Normal -> Vel Locked -> Muted -> Normal
                    if (!st.velLocked && !st.muted) st.velLocked = 1;
                    else if (st.velLocked) { st.velLocked = 0; st.muted = true; }
                    else st.muted = false;
                } else st.muted = !st.muted;
            }
            if (stepPage == 0 && knob == 3) st.trig = (st.trig.type == TrigType::Fixed) ? Trig() : Trig {TrigType::Fixed, 0, 0};
            if (stepPage == 0 && knob == 2) st.tie = 0;
        }
        return;
    }
    if (menu == Menu::Setup && menuPage == 0 && knob == 3 && pendingMode >= 0) { changeMode((Mode) pendingMode, nowMs); pendingMode = -1; return; }
    if (menu == Menu::Nudge || menu == Menu::Bend) { knobUpper = !knobUpper; return; }
    if (menu != Menu::None) return;
    if (held(Btn::Shift)) { // reset secondary parameters
        btnConsumed[(int) Btn::Shift] = true;
        auto& q = seq();
        if (knob == 0) { proj.bpm = 120.0; msg("BPM 120", nowMs); }
        if (knob == 1) { pushUndo(); q.pat().swing = 50; msg("SWING 50", nowMs); }
        if (knob == 2) { q.scale = 0; msg("CHROMATIC", nowMs); }
        if (knob == 3) { q.rootPc = 0; q.baseOctave = 2; msg("ROOT C2", nowMs); }
        return;
    }
    if (knob == 1) {
        if (isMulti()) { pianoRoll[(size_t) sel] = !pianoRoll[(size_t) sel]; grid = GridMode::Steps; msg(pianoRoll[(size_t) sel] ? "PIANO ROLL" : "TRACK LANES", nowMs); }
        else { scrollMode = !scrollMode; msg(scrollMode ? "KNOB 2: SCROLL" : "KNOB 2: OCTAVE", nowMs); }
        return;
    }
    if (isStoch() && knob == 3) { msg("MOD LANE: PHASE 4", nowMs); return; }
    if (isMatr()) { msg("HOLD A STEP, LANE OR TRACK PAD", nowMs); return; }
    int param = knob == 0 ? 0 : knob == 2 ? 1 : 2;
    if (grid == GridMode::Column && columnParam == param) grid = GridMode::Steps;
    else { grid = GridMode::Column; columnParam = param; }
}

void Controller::encoderPress(int knob, bool down, double nowMs) {
    if (knob < 0 || knob > 3) return;
    if (down) { encDown[(size_t) knob] = true; encConsumed[(size_t) knob] = false; encDownMs[(size_t) knob] = nowMs; return; }
    encDown[(size_t) knob] = false;
    if (!encConsumed[(size_t) knob]) tapKnob(knob, nowMs);
}

// ============================================================================ buttons
void Controller::changeMode(Mode m, double nowMs) {
    auto& q = seq();
    if (q.mode == m) return;
    pushUndo();
    if (q.mode == Mode::Chord && m == Mode::Poly) { // chords become editable poly notes
        for (auto& p : q.patterns) for (auto& st : p.tracks[0].steps) {
            if (!st.active || !st.numNotes) continue;
            int semis[kMaxChordNotes];
            int n = std::min(buildChord(q.scale, st.notes[0], st.chordType, st.voicing, st.spread, semis), kMaxPolyNotes);
            for (int k = 0; k < n; ++k) st.notes[(size_t) k] = clampT<int8_t>(semisToDegree(q.scale, semis[k]), -120, 120);
            st.numNotes = (uint8_t) n;
        }
    }
    if (m == Mode::Chord) // a chord step keeps its lowest note as the chord root
        for (auto& p : q.patterns) for (auto& st : p.tracks[0].steps) {
            if (st.numNotes > 1) { int lo = st.notes[0]; for (int k = 1; k < st.numNotes; ++k) lo = std::min<int>(lo, st.notes[(size_t) k]); st.notes[0] = (int8_t) lo; st.numNotes = 1; }
        }
    if (m == Mode::Mono) // keep the first note of each step
        for (auto& p : q.patterns) for (auto& st : p.tracks[0].steps) if (st.numNotes > 1) st.numNotes = 1;
    q.mode = m;
    selTrack[(size_t) sel] = 0;
    msg(std::string("MODE ") + modeLabel(m), nowMs);
}

void Controller::seqButton(int s, double nowMs) {
    if (held(Btn::Mute) && held(Btn::Shift)) { eng.resetSeq(s); btnConsumed[(int) Btn::Mute] = true; msg("RESET S" + std::to_string(s + 1), nowMs); return; }
    if (held(Btn::Mute)) {
        btnConsumed[(int) Btn::Mute] = true;
        auto& q = proj.seqs[(size_t) s];
        q.muted = !q.muted;
        msg("S" + std::to_string(s + 1) + (q.muted ? " MUTED" : " UNMUTED"), nowMs);
        return;
    }
    if (held(Btn::Copy)) {
        btnConsumed[(int) Btn::Copy] = true;
        if (proj.seqs[(size_t) s].mode == Mode::Stoch) { int keep = sel; sel = s; patClip = stochToPattern(); sel = keep; msg("COPIED PHRASE " + std::to_string(s + 1), nowMs); }
        else { patClip = std::make_unique<Pattern>(proj.seqs[(size_t) s].pat()); msg("COPIED SEQ " + std::to_string(s + 1), nowMs); }
        return;
    }
    if (held(Btn::Paste)) {
        btnConsumed[(int) Btn::Paste] = true;
        if (patClip) { int keep = sel; sel = s; pushUndo(); sel = keep; proj.seqs[(size_t) s].pat() = *patClip; msg("PASTED SEQ " + std::to_string(s + 1), nowMs); }
        return;
    }
    if (held(Btn::Shift)) {
        btnConsumed[(int) Btn::Shift] = true;
        if (menu == Menu::Setup && sel == s) menuPage = (menuPage + 1) % 3;
        else { menu = Menu::Setup; menuPage = 0; }
        sel = s; pendingMode = -1;
        return;
    }
    if (sel != s) { sel = s; pendingMode = -1; if (grid == GridMode::Column) grid = GridMode::Steps; }
}

void Controller::pageButton(int p, double nowMs) {
    auto& tr = track();
    if (held(Btn::Shift)) { // move sequence: 16 left, 32 up, 48 down, 64 right
        btnConsumed[(int) Btn::Shift] = true;
        static const int dx[4] = {-1, 0, 0, 1}, dy[4] = {0, 1, -1, 0};
        shiftPattern(dx[p], dy[p], nowMs);
        return;
    }
    if (held(Btn::Copy)) {
        btnConsumed[(int) Btn::Copy] = true;
        int base = (page[(size_t) sel] >= 4 ? 4 : 0) + p;
        for (int i = 0; i < 16; ++i) pageClip[(size_t) i] = tr.steps[(size_t) (base * 16 + i)];
        pageClipValid = true; msg("COPY PAGE " + std::to_string(base + 1), nowMs); return;
    }
    if (held(Btn::Paste)) {
        btnConsumed[(int) Btn::Paste] = true;
        if (!pageClipValid) return;
        pushUndo();
        int base = (page[(size_t) sel] >= 4 ? 4 : 0) + p;
        for (int i = 0; i < 16; ++i) tr.steps[(size_t) (base * 16 + i)] = pageClip[(size_t) i];
        msg("PASTE PAGE " + std::to_string(base + 1), nowMs); return;
    }
    bool high = heldBtn[(size_t) ((int) Btn::Seq1 + sel)];
    if (high) btnConsumed[(int) Btn::Seq1 + sel] = true;
    Btn b = (Btn) ((int) Btn::P16 + p);
    bool dbl = nowMs - lastTap[(size_t) b] < kDoubleTapMs && (page[(size_t) sel] % 4) == p;
    int target = p + ((high || (dbl && page[(size_t) sel] < 4)) ? 4 : 0);
    if (held(Btn::Init) || held(Btn::End)) { // move the start/end range to another page
        btnConsumed[(int) Btn::Init] = btnConsumed[(int) Btn::End] = true;
        pushUndo();
        int len = tr.end - tr.start;
        int off = tr.start % 16;
        tr.start = (uint8_t) (target * 16 + off);
        tr.end = (uint8_t) std::min(kMaxSteps - 1, tr.start + len);
        msg("RANGE " + std::to_string(tr.start + 1) + "-" + std::to_string(tr.end + 1), nowMs);
        return;
    }
    page[(size_t) sel] = target;
}

void Controller::shiftPattern(int dx, int dy, double nowMs) {
    auto& tr = track();
    pushUndo();
    int a = tr.start, b = tr.end;
    // restrict to two held columns if present
    if (heldPads.size() >= 2) { a = stepAt(std::min(heldPads[0].x, heldPads[1].x)); b = stepAt(std::max(heldPads[0].x, heldPads[1].x)); for (auto& h : heldPads) h.consumed = true; }
    int len = b - a + 1;
    if (isStoch() && dx != 0 && len > 1) {
        auto& sc = seq().pat().stoch;
        std::vector<int8_t> tp(sc.pitch.begin() + a, sc.pitch.begin() + b + 1);
        std::vector<uint8_t> tr2(sc.rhythm.begin() + a, sc.rhythm.begin() + b + 1);
        for (int i = 0; i < len; ++i) { int d = a + ((i + dx) % len + len) % len; sc.pitch[(size_t) d] = tp[(size_t) i]; sc.rhythm[(size_t) d] = tr2[(size_t) i]; }
        msg(dx < 0 ? "PHRASE <" : "PHRASE >", nowMs);
        return;
    }
    if (dx != 0 && len > 1) {
        std::vector<Step> tmp(tr.steps.begin() + a, tr.steps.begin() + b + 1);
        for (int i = 0; i < len; ++i) tr.steps[(size_t) (a + ((i + dx) % len + len) % len)] = tmp[(size_t) i];
        msg(dx < 0 ? "ROTATE <" : "ROTATE >", nowMs);
    }
    if (dy != 0) {
        for (int i = a; i <= b; ++i) {
            auto& st = tr.steps[(size_t) i];
            if (st.active) for (int n = 0; n < st.numNotes; ++n) st.notes[(size_t) n] = clampT<int8_t>(st.notes[(size_t) n] + dy, -120, 120);
        }
        msg(dy > 0 ? "TRANSPOSE UP" : "TRANSPOSE DOWN", nowMs);
    }
}

void Controller::expandCondense(bool expand, double nowMs) {
    auto& tr = track();
    pushUndo();
    if (expand) {
        if (tr.div == Division::SixtyFourth || tr.div == Division::ThirtySecondT) { msg("MAX RESOLUTION", nowMs); return; }
        int len = tr.length();
        if (tr.start + len * 2 > kMaxSteps) { msg("MAX 128 STEPS", nowMs); return; }
        std::vector<Step> tmp(tr.steps.begin() + tr.start, tr.steps.begin() + tr.end + 1);
        for (int i = tr.start; i < tr.start + len * 2; ++i) tr.steps[(size_t) i].clear();
        for (int i = 0; i < len; ++i) { auto st = tmp[(size_t) i]; if (st.tie) st.tie = (uint8_t) std::min(127, st.tie * 2 + 1); tr.steps[(size_t) (tr.start + i * 2)] = st; }
        tr.end = (uint8_t) (tr.start + len * 2 - 1);
        tr.div = (Division) ((int) tr.div + ((tr.div == Division::EighthT || tr.div == Division::SixteenthT) ? 2 : (tr.div == Division::Quarter || tr.div == Division::Eighth || tr.div == Division::Sixteenth) ? 2 : 1));
        if (tr.div >= Division::Count) tr.div = Division::SixtyFourth;
        msg("EXPAND x2", nowMs);
    } else {
        if (tr.div == Division::Bar) { msg("MIN RESOLUTION", nowMs); return; }
        int len = tr.length();
        int nlen = std::max(1, len / 2);
        std::vector<Step> tmp(tr.steps.begin() + tr.start, tr.steps.begin() + tr.end + 1);
        for (int i = tr.start; i <= tr.end; ++i) tr.steps[(size_t) i].clear();
        for (int i = 0; i < nlen; ++i) { auto st = tmp[(size_t) (i * 2)]; if (!st.active && i * 2 + 1 < len) st = tmp[(size_t) (i * 2 + 1)]; st.tie = (uint8_t) (st.tie / 2); tr.steps[(size_t) (tr.start + i)] = st; }
        tr.end = (uint8_t) (tr.start + nlen - 1);
        int d = (int) tr.div;
        d -= (tr.div == Division::SixteenthT || tr.div == Division::ThirtySecondT || tr.div == Division::Sixteenth || tr.div == Division::ThirtySecond || tr.div == Division::SixtyFourth) ? 2 : 1;
        if (tr.div == Division::SixtyFourth) d = (int) Division::ThirtySecond;
        tr.div = (Division) std::max(0, d);
        msg("CONDENSE /2", nowMs);
    }
}

void Controller::button(Btn b, bool down, double nowMs) {
    const int bi = (int) b;
    if (!down) {
        heldBtn[(size_t) bi] = false;
        if (b == Btn::Flow) eng.flowHeld = false;
        if (b == Btn::Copy) stepClipFresh = false;
        if (b == Btn::Track && !btnConsumed[bi] && nowMs - lastTap[(size_t) bi] > 600 && menu == Menu::TrackM) menu = Menu::None; // momentary view
        return;
    }
    heldBtn[(size_t) bi] = true;
    btnConsumed[bi] = false;
    const bool shift = held(Btn::Shift) && b != Btn::Shift;
    const bool dbl = nowMs - lastTap[(size_t) bi] < kDoubleTapMs;
    const double prevTap = lastTap[(size_t) bi];
    lastTap[(size_t) bi] = nowMs;
    if (shift) btnConsumed[(int) Btn::Shift] = true;

    if (b >= Btn::Seq1 && b <= Btn::Seq8) { seqButton(bi - (int) Btn::Seq1, nowMs); return; }
    if (b >= Btn::P16 && b <= Btn::P64) { lastTap[(size_t) bi] = prevTap; pageButton(bi - (int) Btn::P16, nowMs); lastTap[(size_t) bi] = nowMs; return; }

    switch (b) {
        case Btn::Back:
            if (shift) { msg("CONFIG: SEE SETUP PAGES", nowMs); break; }
            if (dbl || (menu == Menu::None && grid == GridMode::Steps)) { menu = Menu::None; grid = GridMode::Steps; heldPads.clear(); scrollMode = false; }
            else if (menu != Menu::None) { menu = Menu::None; pendingMode = -1; }
            else grid = GridMode::Steps;
            break;
        case Btn::Mute:
            if (shift) {
                if (menu == Menu::Nudge && nowMs - prevTap < 500) menu = Menu::Bend;
                else menu = (menu == Menu::Nudge || menu == Menu::Bend) ? Menu::None : Menu::Nudge;
                knobUpper = true;
            } else if (dbl) { muteLatched = !muteLatched; msg(muteLatched ? "MUTE LATCHED" : "MUTE UNLATCHED", nowMs); }
            break;
        case Btn::Flow:
            if (shift) { menu = menu == Menu::Groove ? Menu::None : Menu::Groove; menuPage = 0; }
            else eng.flowHeld = true;
            break;
        case Btn::Step:
            if (shift && isChord()) { menu = Menu::StepM; menuPage = 2; break; } // chord global
            menu = menu == Menu::StepM ? Menu::None : Menu::StepM; menuPage = 0;
            break;
        case Btn::Track:
            if (shift) { follow = !follow; msg(follow ? "FOLLOW ON" : "FOLLOW OFF", nowMs); }
            else { menu = menu == Menu::TrackM ? Menu::None : Menu::TrackM; menuPage = 0; }
            break;
        case Btn::Page:
            if (shift) { expandCondense(true, nowMs); break; }
            if (!heldPads.empty()) { stepPage = (stepPage + 1) % stepPages(); for (auto& h : heldPads) h.consumed = true; break; }
            if (menu == Menu::Setup) menuPage = (menuPage + 1) % 3;
            else if (menu == Menu::TrackM && isMulti()) menuPage = (menuPage + 1) % 2;
            else if (menu == Menu::StepM) menuPage = (menuPage + 1) % (isChord() ? 3 : 2);
            else if (menu == Menu::Groove) menuPage = (menuPage + 1) % 2;
            else if (menu == Menu::Nudge) menu = Menu::Bend;
            else if (menu == Menu::Bend) menu = Menu::Nudge;
            break;
        case Btn::Lfo:
            if (shift) expandCondense(false, nowMs);
            else msg("LFO: PHASE 4", nowMs);
            break;
        case Btn::Init:
            if (shift) { auto& tr = track(); pushUndo(); if (tr.div < Division::SixtyFourth) tr.div = (Division) ((int) tr.div + 1); msg(std::string("DIV ") + divisionLabel(tr.div), nowMs); }
            break;
        case Btn::End:
            if (shift) { auto& tr = track(); pushUndo(); if (tr.div > Division::Bar) tr.div = (Division) ((int) tr.div - 1); msg(std::string("DIV ") + divisionLabel(tr.div), nowMs); }
            break;
        case Btn::Copy:
            if (shift) { // duplicate the start..end range after itself
                auto& tr = track(); pushUndo();
                int len = tr.length();
                int n = std::min(len, kMaxSteps - 1 - tr.end);
                for (int i = 0; i < n; ++i) tr.steps[(size_t) (tr.end + 1 + i)] = tr.steps[(size_t) (tr.start + i)];
                tr.end = (uint8_t) (tr.end + n);
                msg("DUPLICATE", nowMs);
            }
            break;
        case Btn::Paste:
            if (shift && isStoch()) { pushUndo(); seq().pat().stoch.valid = false; msg("NEW PHRASE", nowMs); break; }
            if (shift && isMatr()) { pushUndo(); auto& mt = matTrack(matSel[(size_t) sel]); auto ch = mt.channel; auto base = mt.baseNote; mt = MatTrack(); mt.channel = ch; mt.baseNote = base; msg("TRACK CLEARED", nowMs); break; }
            if (shift) {
                pushUndo();
                if (isMulti()) { for (auto& st : track().steps) st.clear(); msg("CLEAR TRACK", nowMs); }
                else { for (auto& st : track().steps) st.clear(); msg("CLEAR", nowMs); }
            }
            break;
        case Btn::Undo: if (shift) doRedo(nowMs); else doUndo(nowMs); break;
        case Btn::Play:
            if (shift) {
                tapTimes.push_back(nowMs);
                if (tapTimes.size() > 4) tapTimes.erase(tapTimes.begin());
                if (tapTimes.size() >= 2) {
                    double avg = (tapTimes.back() - tapTimes.front()) / (tapTimes.size() - 1);
                    if (avg > 150 && avg < 3000) { proj.bpm = std::clamp(60000.0 / avg, 20.0, 300.0); msg("TAP " + std::to_string((int) std::lround(proj.bpm)), nowMs); }
                }
                break;
            }
            if (proj.internalClock) eng.setInternalPlaying(true);
            else msg("HOST CLOCK: USE ABLETON PLAY", nowMs);
            break;
        case Btn::Stop:
            if (shift) {
                if (eng.isRunning()) { msg("STOP FIRST", nowMs); break; }
                proj.internalClock = !proj.internalClock;
                msg(proj.internalClock ? "CLOCK: INTERNAL" : "CLOCK: HOST", nowMs);
                break;
            }
            if (dbl) { eng.panic(); msg("ALL NOTES OFF", nowMs); }
            if (proj.internalClock) eng.setInternalPlaying(false);
            break;
        case Btn::Load: grid = grid == GridMode::Load ? GridMode::Steps : GridMode::Load; break;
        case Btn::Save: msg("SAVED WITH THE ABLETON SET", nowMs); break;
        case Btn::Perf: case Btn::Gen: case Btn::Mod: case Btn::Arranger: case Btn::Keyboard:
        case Btn::Random:
            if (isMatr()) { msg("HOLD RANDOM + TAP A LANE PAD", nowMs); break; }
            msg(std::string(btnLabel(b)) + ": COMING IN A LATER PHASE", nowMs);
            break;
        case Btn::Arp: case Btn::Rec:
            msg(std::string(btnLabel(b)) + ": COMING IN A LATER PHASE", nowMs);
            break;
        default: break;
    }
}

// ============================================================================ rendering
std::string Controller::stepHeader(int idx) const {
    const auto& tr = lanesView() && !heldPads.empty() ? seq().pat().tracks[(size_t) (7 - heldPads.front().y)] : track();
    const auto& st = tr.steps[(size_t) idx];
    std::string h = isMulti() ? "T" + std::to_string((lanesView() && !heldPads.empty() ? 7 - heldPads.front().y : trackIndex()) + 1) + "-" : "Stp ";
    h += std::to_string(idx + 1);
    if (st.active && st.numNotes && isChord()) {
        h += " " + chordName(seq().scale, seq().baseMidi() + st.octave * 12, st.notes[0], st.chordType);
    } else if (st.active && st.numNotes) {
        int base = isMulti() ? tr.baseNote : seq().baseMidi();
        h += " " + noteName(base + degreeToSemis(seq().scale, st.notes[0]) + st.octave * 12);
        if (st.numNotes > 1) h += "+" + std::to_string(st.numNotes - 1);
    }
    if (st.muted) h += " (M)";
    if (st.trig.type == TrigType::Locked) h += " LOCK";
    h += "  " + std::to_string(stepPage + 1) + "/" + std::to_string(stepPages());
    return h;
}

void Controller::renderDisplay(Frame& f, double nowMs) const {
    const auto& q = seq();
    const auto& tr = track();
    char buf[64];
    std::snprintf(buf, sizeof buf, "S%d %s %d%% %s %s", sel + 1, modeLabel(q.mode), (int) q.pat().swing, follow ? "F" : " ",
                  proj.internalClock ? "INT" : "HOST");
    f.header = buf;
    std::snprintf(buf, sizeof buf, "%.1f", eng.currentBpm());
    f.headerRight = buf;
    std::string patTxt = "Pattern " + std::to_string(q.curPattern + 1);
    if (eng.pendingPattern(sel) >= 0) patTxt += " > " + std::to_string(eng.pendingPattern(sel) + 1);
    if (isMulti()) patTxt += "  T" + std::to_string(trackIndex() + 1);
    f.line1 = patTxt + "   " + divisionLabel(tr.div) + "  " + std::to_string(tr.start + 1) + "-" + std::to_string(tr.end + 1);
    f.line2 = std::string(scaleLibrary()[q.scale].label) + "  " + noteName(q.baseMidi()) + (q.muted ? "  MUTED" : "");
    double ppq = eng.currentPpq();
    f.progress = (int) std::floor(std::fmod(std::max(0.0, ppq), 4.0) * 4.0);

    auto set = [&](const char* a, const std::string& av, const char* b, const std::string& bv, const char* c, const std::string& cv, const char* d, const std::string& dv) {
        f.labels = {a, b, c, d}; f.values = {av, bv, cv, dv};
    };
    auto onoff = [](int v) { return v == 0 ? std::string("OFF") : std::to_string(v); };

    if (menu == Menu::Setup) {
        std::snprintf(buf, sizeof buf, "SEQ%d - Setup%s %d/3", sel + 1, isMulti() ? (" [T:" + std::to_string(trackIndex() + 1) + "]").c_str() : "", menuPage + 1);
        f.line1 = buf; f.line2 = "";
        if (menuPage == 0) {
            int ch = isMulti() ? tr.channel : q.channel;
            std::string modeTxt = pendingMode >= 0 && pendingMode != (int) q.mode ? std::string(modeLabel((Mode) pendingMode)) + "*" : modeLabel(q.mode);
            set(isMulti() ? "T.Ch" : "Ch", std::to_string(ch + 1), "Out", outputModeLabel(proj.output), "PC", onoff(q.programChange), "Mode", modeTxt);
            if (pendingMode >= 0 && pendingMode != (int) q.mode) f.line2 = "Press knob 4 to confirm mode";
        } else if (menuPage == 1) {
            std::string tr2 = q.transpose == 0 ? "OFF" : q.transpose == 1 ? "Same" : "Ch" + std::to_string(q.transpose - 1);
            set("In", punchInLabel(q.punchIn), "Reset", onoff(q.resetBars), "Bnk", onoff(q.bank), "Trnsp", tr2);
            f.line2 = "In/Bnk/PC: stored, used in phase 3";
        } else {
            std::string a = q.mode == Mode::Poly ? std::to_string(q.nVoices) : isMulti() ? (tr.swing == 0 ? "Same" : std::to_string(tr.swing)) : "-";
            bool mx = q.mode == Mode::Poly || q.mode == Mode::Chord;
            set(q.mode == Mode::Poly ? "nVoice" : isMulti() ? "T.Swg" : "-", a, mx ? "ChMx" : "-", mx ? (q.chMx ? "On" : "Off") : "", "-", "", "Color", std::to_string(q.color + 1));
        }
    } else if (menu == Menu::TrackM) {
        if (isMatr()) {
            const auto& mt = matTrack(matSel[(size_t) sel]);
            f.line1 = "Track " + std::to_string(matSel[(size_t) sel] + 1) + " Menu";
            set("Ch", std::to_string(mt.channel + 1), "Ofs", noteName(mt.baseNote), "RLen", onoff(mt.rlen), "-", "");
        } else if (isMulti()) {
            f.line1 = "Track " + std::to_string(trackIndex() + 1) + " Menu  " + std::to_string(menuPage + 1) + "/2";
            if (menuPage == 0) set("Ch", std::to_string(tr.channel + 1), "Ofs", noteName(tr.baseNote), "Div", divisionLabel(tr.div), "Dir", dirLabel(tr.dir));
            else set("rDiv", onoff(tr.rndDiv), "T.Swg", tr.swing == 0 ? "Same" : std::to_string(tr.swing), "-", "", "-", "");
        } else {
            f.line1 = "Track Menu";
            set("Oct", std::to_string(viewDegC()), "Dir", dirLabel(tr.dir), "rDiv", onoff(tr.rndDiv), "Div", divisionLabel(tr.div));
        }
    } else if (menu == Menu::StepM) {
        const Step* ref = nullptr;
        for (auto& st : tr.steps) if (st.active) { ref = &st; break; }
        Step d; if (!ref) ref = &d;
        const int pages = isChord() ? 3 : 2;
        f.line1 = (menuPage == 2 ? std::string("Chord Global  ") : std::string("Step Global  ")) + std::to_string(menuPage + 1) + "/" + std::to_string(pages);
        auto bassTxt = [&]() { return q.bassMode == 0 ? std::string("OFF") : q.bassMode == 1 ? std::string("ON") : "Ch" + std::to_string(q.bassMode - 1); };
        if (menuPage == 0) set("Rept", std::to_string(ref->repeats), "Durat", "x" + std::to_string(ref->repDur), "Ramp", std::to_string(ref->ramp) + "%", "Accel", std::to_string(ref->accel) + "%");
        else if (menuPage == 1) set("Offst", std::to_string(ref->offset) + "%", isChord() ? "Strm" : "Retrg", isChord() ? std::to_string(ref->strum) + "%" : std::to_string(ref->retrig), "Rng", std::to_string(ref->rng) + "iv", "Rpt%", std::to_string(ref->repProb));
        else set("Sprd", std::to_string(ref->spread), "Type", chordTypeLabel(ref->chordType), "Voic", voicingLabel(ref->voicing), "Bass", bassTxt());
    } else if (menu == Menu::Nudge || menu == Menu::Bend) {
        f.line1 = menu == Menu::Nudge ? "Time Offset [ms]" : "Time Bend [%]";
        f.line2 = "Tap knob: SQ1-4 / SQ5-8   Shift: all";
        int o = knobUpper ? 0 : 4;
        for (int k = 0; k < 4; ++k) {
            auto& x = proj.seqs[(size_t) (o + k)];
            f.labels[(size_t) k] = "SQ " + std::to_string(o + k + 1);
            if (menu == Menu::Nudge) f.values[(size_t) k] = (x.nudgeMs > 0 ? "+" : "") + std::to_string(x.nudgeMs);
            else { std::snprintf(buf, sizeof buf, "%+.1f", x.timeBend / 10.0); f.values[(size_t) k] = buf; }
        }
    } else if (menu == Menu::Groove) {
        f.line1 = "Groove  " + std::to_string(menuPage + 1) + "/2";
        f.line2 = q.groove ? grooveTemplate(q.groove).name : "-";
        if (menuPage == 0) set("Accnt", std::to_string(q.grooveAccent) + "%", "Time", std::to_string(q.grooveTime) + "%", "Offst", std::to_string(q.grooveOffset), "Len", std::to_string(q.grooveLen));
        else set("Rand", std::to_string(q.grooveRand) + "%", "Grov", q.groove ? std::to_string(q.groove) : "-", "-", "", "-", "");
    } else if (grid == GridMode::Steps && !heldPads.empty()) {
        auto idxs = heldStepIndices();
        const auto& htr = lanesView() ? seq().pat().tracks[(size_t) (7 - heldPads.front().y)] : tr;
        const auto& st = htr.steps[(size_t) idxs.front()];
        f.line1 = idxs.size() > 1 ? "Multipress (" + std::to_string(idxs.size()) + ")  " + std::to_string(stepPage + 1) + "/" + std::to_string(stepPages()) : stepHeader(idxs.front());
        f.line2 = st.tie ? "Tie " + std::to_string(st.tie + 1) + " steps" : "";
        if (stepPage == 0) {
            std::string vel = st.velLocked ? std::to_string(st.velocity) + "*" : std::to_string(st.velocity);
            std::string oct = isMulti() ? (st.numNotes ? noteName(htr.baseNote + degreeToSemis(q.scale, st.notes[0])) : "-") : std::to_string(st.octave);
            set("Vel", vel, isMulti() ? "Note" : "Oct", oct, "Gate", st.gate >= kGateLegato ? "LEG" : std::to_string(st.gate), "Trig", trigLabel(st.trig));
        } else if (stepPage == 1) set("Rept", std::to_string(st.repeats), "Durat", "x" + std::to_string(st.repDur), "Ramp", std::to_string(st.ramp) + "%", "Accel", std::to_string(st.accel) + "%");
        else if (stepPage == 2) set("Offst", std::to_string(st.offset) + "%", isChord() ? "Strm" : "Retrg", isChord() ? std::to_string(st.strum) + "%" : std::to_string(st.retrig), "Rng", std::to_string(st.rng) + "iv", "Rpt%", std::to_string(st.repProb));
        else set("Sprd", std::to_string(st.spread), "Type", chordTypeLabel(st.chordType), "Voicing", voicingLabel(st.voicing), "-", "");
    } else if (held(Btn::Shift)) {
        set("BPM", proj.internalClock ? std::to_string((int) proj.bpm) : "HOST", "SWNG", std::to_string(isMulti() && tr.swing ? tr.swing : q.pat().swing),
            "SCAL", scaleLibrary()[q.scale].label, "ROOT", noteName(q.baseMidi()));
    } else {
        std::string k2 = lanesView() ? "NOTE" : (scrollMode || isMulti()) ? "VIEW" : "OCT";
        std::string v2 = lanesView() ? noteName(tr.baseNote) : (scrollMode || isMulti()) ? std::to_string(viewDegC()) : noteName(q.baseMidi());
        bool retrigCol = grid == GridMode::Column && columnParam == 2;
        set("VEL", rangeText(&Step::velocity), k2.c_str(), v2, "GATE", rangeText(&Step::gate), retrigCol ? "RTRG" : "REPT",
            retrigCol ? rangeText(&Step::retrig) : rangeText(&Step::repeats));
        if (isStoch()) {
            const auto& sc = q.pat().stoch;
            f.labels[3] = "MOD"; f.values[3] = "-";
            int notes = 0; for (auto l : sc.noteLevel) if (l) ++notes;
            f.line2 = std::to_string(notes) + " notes  P " + (sc.pitchRand ? std::to_string(sc.pitchRand * 100 / 7) + "%" : std::string("LOCK"))
                    + "  R " + (sc.rhythmRand ? std::to_string(sc.rhythmRand * 100 / 7) + "%" : std::string("LOCK")) + "  " + scaleLibrary()[q.scale].label;
        }
        if (grid == GridMode::Column) f.line2 = columnParam == 0 ? "Velocity editor" : columnParam == 1 ? "Gate editor (top = LEG)" : "Retrig editor (Shift: repeats)";
        if (grid == GridMode::Load) { f.line1 = "LOAD: rows = sequencers, columns = patterns"; f.line2 = "Copy/Paste + pad, Shift + pad = clear"; }
    }
    if (isMatr() && menu == Menu::None && grid == GridMode::Steps) renderMatrDisplay(f);
    if (nowMs < messageUntil) f.message = message;
}

void Controller::renderSteps(Frame& f, double nowMs) const {
    (void) nowMs;
    const auto& q = seq();
    const auto& tr = track();
    const uint32_t col = seqColor(q.color);
    const int pg = page[(size_t) sel];
    const int ph = eng.playhead(sel, trackIndex());
    const int scaleN = (int) scaleLibrary()[q.scale].intervals.size();
    const bool showRange = held(Btn::Init) || held(Btn::End);
    for (int x = 0; x < 16; ++x) {
        const int idx = pg * 16 + x;
        const auto& st = tr.steps[(size_t) idx];
        const bool inRange = idx >= tr.start && idx <= tr.end;
        // tie coverage: which earlier step's notes extend over this column
        const Step* tieSrc = nullptr;
        for (int i = std::max(0, idx - 127); i < idx; ++i) {
            const auto& s = tr.steps[(size_t) i];
            if (s.active && s.tie > 0 && i + s.tie >= idx) tieSrc = &s;
        }
        std::array<bool, 8> chordTones {};
        if (isChord() && st.active && st.numNotes) { // show the other chord tones dimly
            int semis[kMaxChordNotes];
            int n = buildChord(q.scale, st.notes[0], st.chordType, st.voicing, st.spread, semis);
            for (int k = 0; k < n; ++k) {
                int d = semisToDegree(q.scale, semis[k]);
                int row = d - viewDegC();
                if (degreeToSemis(q.scale, d) == semis[k] && row >= 0 && row < 8 && d != st.notes[0]) chordTones[(size_t) (7 - row)] = true;
            }
        }
        bool above = false, below = false;
        if (st.active) for (int n = 0; n < st.numNotes; ++n) {
            int d = st.notes[(size_t) n];
            if (d > viewDegC() + 7) above = true;
            if (d < viewDegC()) below = true;
        }
        for (int y = 0; y < 8; ++y) {
            int deg = degreeAtRow(y);
            uint32_t c = 0;
            if (((deg % scaleN) + scaleN) % scaleN == 0) c = 0x3b3e43; // octave root row
            if (showRange && (idx == tr.start || idx == tr.end)) c = 0x3a2050;
            if (!inRange) c = 0;
            if (tieSrc && tieSrc->hasNote(deg)) c = 0x707070;
            if (chordTones[(size_t) y]) c = dim(col, 0.42f);
            if (st.active && st.hasNote(deg)) c = st.muted ? 0x4a1d5a : (inRange ? col : dim(col, 0.35f));
            if (c == 0 || c == 0x3b3e43) {
                if (y == 0 && above) c = 0x606060;
                if (y == 7 && below) c = 0x701818;
            }
            if (idx == ph && eng.isRunning()) c = c == 0 || c == 0x3b3e43 ? 0x5a5d63 : mix(c, 0xffffff, 0.45f);
            f.pads[(size_t) (y * 16 + x)] = c;
        }
    }
}

void Controller::renderStoch(Frame& f) const {
    const auto& q = seq();
    const auto& sc = q.pat().stoch;
    const uint32_t col = seqColor(q.color);
    const auto& iv = scaleLibrary()[q.scale].intervals;
    for (int x = 0; x < 12; ++x) {
        bool inScale = std::find(iv.begin(), iv.end(), x) != iv.end();
        int lvl = sc.noteLevel[(size_t) x];
        for (int y = 0; y < 8; ++y) {
            int r = 7 - y;
            uint32_t c = r == 0 ? (inScale ? 0x46494e : 0x4a1e1e) : 0;
            if (r < lvl) c = inScale ? dim(col, 0.45f + 0.55f * r / 7.0f) : dim(0xff3030, 0.4f + 0.5f * r / 7.0f);
            f.pads[(size_t) (y * 16 + x)] = c;
        }
    }
    for (int y = 0; y < 8; ++y) {
        int r = 7 - y;
        uint32_t c = (r == 3 || r == 4) ? 0x3b3e43 : 0;
        if (r >= 4 && r - 3 <= sc.octUp) c = 0x30c070;
        if (r <= 3 && 4 - r <= sc.octDown) c = 0x30a0c0;
        f.pads[(size_t) (y * 16 + 12)] = c;
        int a = std::min<int>(sc.divRow, 2), b = std::max<int>(sc.divRow, 2);
        f.pads[(size_t) (y * 16 + 13)] = (r >= a && r <= b) ? (r == 2 ? 0xffffff : 0xffb000) : (r == 2 ? 0x3b3e43 : 0);
        f.pads[(size_t) (y * 16 + 14)] = r <= sc.pitchRand ? (sc.pitchRand == 0 ? 0xff3030 : dim(0xd060ff, 0.4f + 0.6f * r / 7.0f)) : 0;
        f.pads[(size_t) (y * 16 + 15)] = r <= sc.rhythmRand ? (sc.rhythmRand == 0 ? 0xff3030 : dim(0x40d0ff, 0.4f + 0.6f * r / 7.0f)) : 0;
    }
}

void Controller::renderLanes(Frame& f, double nowMs) const {
    (void) nowMs;
    const auto& q = seq();
    const uint32_t col = seqColor(q.color);
    const int pg = page[(size_t) sel];
    const bool muteView = held(Btn::Mute);
    for (int y = 0; y < 8; ++y) {
        const int t = 7 - y;
        const auto& tr = q.pat().tracks[(size_t) t];
        const int ph = eng.playhead(sel, t);
        const bool selT = t == trackIndex();
        for (int x = 0; x < 16; ++x) {
            const int idx = pg * 16 + x;
            const auto& st = tr.steps[(size_t) idx];
            const bool inRange = idx >= tr.start && idx <= tr.end;
            uint32_t c = 0;
            if (selT && inRange) c = 0x34373b;
            bool tied = false;
            for (int i = std::max(0, idx - 127); i < idx; ++i) { const auto& s = tr.steps[(size_t) i]; if (s.active && s.tie > 0 && i + s.tie >= idx) tied = true; }
            if (tied) c = 0x606060;
            if (st.active) c = st.muted ? 0x4a1d5a : dim(col, (selT ? 1.0f : 0.55f) * (inRange ? 1.0f : 0.4f));
            if (tr.muted && st.active) c = dim(c, 0.3f);
            if (x == 0 && selT && !st.active) c = 0x8a7a10;
            if (idx == tr.end && selT && !st.active && inRange) c = 0x403a08;
            if (idx == ph && eng.isRunning() && inRange) c = (c == 0 || c == 0x34373b) ? 0x55585e : mix(c, 0xffffff, 0.45f);
            if (muteView && x == 0) c = tr.muted ? 0 : 0x2050ff;
            if (muteView && x == 15) c = (eng.soloMask[(size_t) sel] & (1u << t)) ? 0xffd000 : 0x202020;
            f.pads[(size_t) (y * 16 + x)] = c;
        }
    }
}

void Controller::renderColumn(Frame& f) const {
    const auto& q = seq();
    const auto& tr = track();
    const uint32_t col = seqColor(q.color);
    const int pg = page[(size_t) sel];
    const int ph = eng.playhead(sel, trackIndex());
    for (int x = 0; x < 16; ++x) {
        const int idx = pg * 16 + x;
        const auto& st = tr.steps[(size_t) idx];
        int h = 0; // number of lit rows from the bottom
        if (st.active || (isStoch() && idx >= tr.start && idx <= tr.end)) {
            if (columnParam == 0) h = std::clamp((int) std::lround(st.velocity * 7.0 / 127.0), 1, 7) + 1;
            else if (columnParam == 1) h = st.gate >= kGateLegato ? 8 : std::clamp(st.gate / 16, 0, 6) + 1;
            else h = std::clamp((int) st.retrig, 1, 8);
        }
        for (int y = 0; y < 8; ++y) {
            int r = 7 - y;
            uint32_t c = 0;
            if (h > 0 && r < h) c = (columnParam == 2 && st.repeats > 1 && r < (st.repeats + 1) / 2) ? 0xd03030 : dim(col, 0.4f + 0.6f * r / 7.0f);
            if (idx == ph && eng.isRunning()) c = c ? mix(c, 0xffffff, 0.4f) : 0x303030;
            f.pads[(size_t) (y * 16 + x)] = c;
        }
    }
}

void Controller::renderLoad(Frame& f, double nowMs) const {
    bool blink = std::fmod(nowMs, 400.0) < 200.0;
    for (int y = 0; y < 8; ++y) {
        const auto& q = proj.seqs[(size_t) y];
        uint32_t col = seqColor(q.color);
        for (int x = 0; x < 16; ++x) {
            uint32_t c = q.patterns[(size_t) x].isEmpty() ? 0x0c0c0c : dim(col, 0.3f);
            if (x == q.curPattern) c = col;
            if (eng.pendingPattern(y) == x) c = blink ? 0xffffff : col;
            f.pads[(size_t) (y * 16 + x)] = c;
        }
    }
}

void Controller::render(Frame& f, double nowMs) const {
    f.pads.fill(0);
    f.leds.fill(0);
    f.labels = {}; f.values = {}; f.message.clear();

    // follow the playhead page
    if (follow && eng.isRunning() && grid != GridMode::Load) {
        int ph = eng.playhead(sel, trackIndex());
        if (ph >= 0) const_cast<Controller*>(this)->page[(size_t) sel] = ph / 16;
    }

    if (grid == GridMode::Load) renderLoad(f, nowMs);
    else if (grid == GridMode::Column) renderColumn(f);
    else if (lanesView()) renderLanes(f, nowMs);
    else if (isStoch()) renderStoch(f);
    else if (isMatr()) renderMatr(f);
    else renderSteps(f, nowMs);
    renderDisplay(f, nowMs);

    // ---- button LEDs
    const double ppq = eng.currentPpq();
    const bool beat = eng.isRunning() ? std::fmod(std::max(0.0, ppq), 1.0) < 0.5 : true;
    for (int s = 0; s < kNumSeqs; ++s) {
        const auto& q = proj.seqs[(size_t) s];
        uint32_t c = q.muted || !q.active ? 0x1a1a1a : dim(seqColor(q.color), 0.45f);
        if (s == sel) c = beat ? 0xffffff : seqColor(q.color);
        f.leds[(size_t) ((int) Btn::Seq1 + s)] = c;
    }
    for (int b = 0; b < kNumBtns; ++b) if (heldBtn[(size_t) b] && b < (int) Btn::Seq1) f.leds[(size_t) b] = 0x30d0c0;
    if (muteLatched) f.leds[(int) Btn::Mute] = 0xff3030;
    const int pg = page[(size_t) sel];
    f.leds[(size_t) ((int) Btn::P16 + pg % 4)] = pg >= 4 ? 0x3d7bff : 0xffffff;
    f.leds[(int) Btn::Play] = eng.isRunning() ? (beat ? 0x30ff60 : 0x108020) : 0x202020;
    if (follow) f.leds[(int) Btn::Track] = std::max<uint32_t>(f.leds[(int) Btn::Track], 0x303030);
    if (menu == Menu::StepM) f.leds[(int) Btn::Step] = 0xffffff;
    if (menu == Menu::TrackM) f.leds[(int) Btn::Track] = 0xffffff;
    if (grid == GridMode::Load) f.leds[(int) Btn::Load] = 0xffffff;
    if (eng.flowHeld.load()) f.leds[(int) Btn::Flow] = 0xffa000;
    if (!undoStack.empty()) f.leds[(int) Btn::Undo] = std::max<uint32_t>(f.leds[(int) Btn::Undo], 0x404040);
    if (menu == Menu::Setup || menu == Menu::TrackM || menu == Menu::StepM || menu == Menu::Groove || !heldPads.empty())
        f.leds[(int) Btn::Page] = std::fmod(nowMs, 600.0) < 300.0 ? 0xffffff : 0x303030;
}

} // namespace tek

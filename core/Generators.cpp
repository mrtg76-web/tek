#include "Generators.h"
#include "Scales.h"
#include <algorithm>
#include <vector>

namespace tek {
namespace {

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int n) { return n <= 0 ? 0 : (int) (next() % (uint32_t) n); }
    bool chance(int pct) { return pct > 0 && range(100) < pct; }
};

void euclidN(int len, int pulses, int rot, bool* out) { // any length (1..128)
    len = std::max(1, len);
    pulses = std::clamp(pulses, 0, len);
    for (int i = 0; i < len; ++i) {
        int j = ((i - rot) % len + len) % len;
        out[i] = pulses > 0 && ((j * pulses) % len) < pulses;
    }
}

bool protectedStep(const Step& st) { return st.active && (st.trig.type == TrigType::Fixed || st.trig.type == TrigType::Locked); }

Step freshStep(const Track& tr) {
    Step st;
    st.velocity = tr.defVel; st.gate = tr.defGate; st.repeats = tr.defRepeats;
    st.active = true; st.numNotes = 1; st.notes[0] = 0;
    return st;
}

} // namespace

void euclidRhythm(const GenSettings& g, int n, bool* out, bool* accent) {
    bool l1[kMaxSteps], l2[kMaxSteps];
    const int len1 = std::clamp((int) g.eLen, 1, kMaxSteps);
    euclidN(len1, g.ePuls, g.eRot, l1);
    const bool two = g.eLen2 > 0;
    if (two) euclidN(g.eLen2, g.ePuls2, g.eRot2, l2);
    for (int i = 0; i < n; ++i) {
        bool a = l1[i % len1];
        bool b = two && l2[i % g.eLen2];
        bool r = a;
        if (two) switch (g.logic) {
            case 0: r = a || b; break;
            case 1: r = a && b; break;
            case 2: r = a != b; break;
            default: r = a; break; // Accnt: layer 1 places, layer 2 accents
        }
        if (g.eInv) r = !r;
        out[i] = r;
        accent[i] = two && g.logic == 3 && a && b;
    }
    // density: add or remove pulses, deterministically from the seed
    if (g.dens != 0) {
        Rng rng(g.seed * 2654435761u + 17u);
        for (int i = 0; i < n; ++i) {
            const int roll = rng.range(100);
            if (g.dens > 0 && !out[i] && roll < g.dens) out[i] = true;
            if (g.dens < 0 && out[i] && roll < -g.dens) out[i] = false;
        }
    }
}

void euclidGenerate(Sequencer& q, int track) {
    auto& tr = q.pat().tracks[(size_t) track];
    const auto& g = q.gen;
    const int n = tr.end - tr.start + 1;
    bool on[kMaxSteps], acc[kMaxSteps];
    euclidRhythm(g, n, on, acc);
    Rng rng(g.seed);
    const bool multi = q.mode == Mode::Multi;
    const bool lockPitch = g.lock == 1 || g.lock == 3, lockRhythm = g.lock == 2 || g.lock == 3;
    if (lockPitch && lockRhythm) return;

    // existing pitches (for Lock Pitch: re-use them in order on the new rhythm)
    std::vector<Step> oldSteps;
    for (int i = 0; i < n; ++i) { const auto& st = tr.steps[(size_t) (tr.start + i)]; if (st.active && st.numNotes) oldSteps.push_back(st); }

    // note pool for the randomised notes
    std::vector<int> pool;
    const int r = g.nteR;
    for (int d = (r < 0 ? r : 0); d <= std::abs(r); ++d) if (d != 0) pool.push_back(d);
    std::vector<int> order = pool;
    if (g.ntOrd == 3) for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[(size_t) rng.range((int) i)]);
    int cursor = 0, dirStep = 1, used = 0;
    auto nextPoolNote = [&]() {
        if (order.empty()) return 0;
        int d;
        if (g.ntOrd == 1) d = order[(size_t) rng.range((int) order.size())];
        else if (g.ntOrd == 2) { // ping pong
            d = order[(size_t) cursor];
            if (order.size() > 1) { if (cursor + dirStep < 0 || cursor + dirStep >= (int) order.size()) dirStep = -dirStep; cursor += dirStep; }
        } else { d = order[(size_t) cursor]; cursor = (cursor + 1) % (int) order.size(); }
        return d;
    };

    int lastActive = -1;
    for (int i = 0; i < n; ++i) {
        auto& st = tr.steps[(size_t) (tr.start + i)];
        if (protectedStep(st)) { lastActive = -1; continue; }
        if (lockRhythm) { // keep positions, re-pitch active steps
            if (!st.active || multi) continue;
            st.numNotes = 1;
            st.notes[0] = (int8_t) (rng.chance(g.ntePct) ? nextPoolNote() : 0);
            if (g.octR != 0 && rng.chance(50)) st.octave = (int8_t) (g.octR > 0 ? rng.range(g.octR + 1) : rng.range(-2 * g.octR + 1) + g.octR);
            continue;
        }
        if (!on[i]) {
            st.clear();
            continue;
        }
        Step ns = freshStep(tr);
        if (lockPitch && !oldSteps.empty()) { // keep the old notes in order on the new rhythm
            const Step& src = oldSteps[(size_t) (used++ % (int) oldSteps.size())];
            ns.numNotes = src.numNotes; ns.notes = src.notes; ns.octave = src.octave; ns.chordType = src.chordType; ns.voicing = src.voicing;
        } else if (!multi) {
            ns.notes[0] = (int8_t) (rng.chance(g.ntePct) ? nextPoolNote() : 0);
            if (g.octR != 0 && rng.chance(35)) ns.octave = (int8_t) (g.octR > 0 ? rng.range(g.octR + 1) : rng.range(-2 * g.octR + 1) + g.octR);
        }
        if (g.velPct > 0) ns.velocity = (uint8_t) std::clamp((int) ns.velocity + (rng.range(2 * g.velPct + 1) - g.velPct) * 127 / 100, 1, 127);
        if (acc[i]) ns.velocity = (uint8_t) std::min(127, ns.velocity + 30);
        st = ns;
        // tie the previous pulse up to this one
        if (lastActive >= 0 && g.tiePct > 0 && !multi && rng.chance(g.tiePct)) {
            auto& prev = tr.steps[(size_t) (tr.start + lastActive)];
            prev.tie = (uint8_t) (i - lastActive - 1);
            if (prev.tie > 0) prev.gate = kGateLegato;
        }
        lastActive = i;
    }
}

void randomGenerate(Sequencer& q, int track, uint32_t seed) {
    auto& tr = q.pat().tracks[(size_t) track];
    const auto& r = q.rgen;
    Rng rng(seed);
    const int n = tr.end - tr.start + 1;
    const bool multi = q.mode == Mode::Multi;
    auto stepAt = [&](int i) -> Step& { return tr.steps[(size_t) (tr.start + i)]; };

    if (r.den <= 0) { // modifiers of the existing pattern
        std::vector<int> idx;
        for (int i = 0; i < n; ++i) if (stepAt(i).active && !protectedStep(stepAt(i))) idx.push_back(i);
        if (r.den == 0) { // Human: gate + micro timing
            for (int i : idx) {
                auto& st = stepAt(i);
                st.offset = (int8_t) std::clamp(st.offset + rng.range(21) - 10, -45, 45);
                if (st.gate < kGateLegato) st.gate = (uint8_t) std::clamp(st.gate + rng.range(31) - 15, 2, 99);
            }
        } else if (r.den == -1 || r.den == -2) { // Order: shuffle the notes; Pitch: new notes, same order
            std::vector<Step> notes;
            for (int i : idx) notes.push_back(stepAt(i));
            if (r.den == -1) for (size_t i = notes.size(); i > 1; --i) std::swap(notes[i - 1], notes[(size_t) rng.range((int) i)]);
            for (size_t k = 0; k < idx.size(); ++k) {
                auto& st = stepAt(idx[k]);
                if (r.den == -1) { st.numNotes = notes[k].numNotes; st.notes = notes[k].notes; st.octave = notes[k].octave; }
                else if (!multi) { const int span = std::max(1, std::abs((int) r.rang)); st.notes[0] = (int8_t) (r.bias + (r.rang < 0 ? rng.range(2 * span + 1) - span : rng.range(span + 1))); st.numNotes = 1; }
                st.tie = 0;
            }
        } else { // Flip: reverse the sequence
            for (int a = 0, b = n - 1; a < b; ++a, --b) if (!protectedStep(stepAt(a)) && !protectedStep(stepAt(b))) std::swap(stepAt(a), stepAt(b));
            for (int i = 0; i < n; ++i) stepAt(i).tie = 0;
        }
        return;
    }

    if (r.scal && !multi) { q.scale = (uint8_t) (1 + rng.range((int) scaleLibrary().size() - 1)); q.rootPc = (uint8_t) rng.range(12); }
    const int span = std::max(0, std::abs((int) r.rang));
    auto newNote = [&]() { return r.bias + (span == 0 ? 0 : (r.rang < 0 ? rng.range(2 * span + 1) - span : rng.range(span + 1))); };
    std::vector<int> motif; // lower Rand = more repetition of earlier notes
    int lastActive = -1;
    for (int i = 0; i < n; ++i) {
        auto& st = stepAt(i);
        if (protectedStep(st)) { lastActive = -1; continue; }
        if (!rng.chance(r.den)) { st.clear(); continue; }
        Step ns = freshStep(tr);
        if (!multi) {
            int note;
            if (!motif.empty() && !rng.chance(std::max(5, (int) r.rand))) note = motif[(size_t) rng.range((int) motif.size())];
            else { note = newNote(); motif.push_back(note); if (motif.size() > 4) motif.erase(motif.begin()); }
            ns.notes[0] = (int8_t) std::clamp(note, -60, 60);
        }
        if (r.vg > 0) {
            ns.velocity = (uint8_t) std::clamp((int) ns.velocity + (rng.range(2 * r.vg + 1) - r.vg) * 127 / 100, 1, 127);
            ns.gate = (uint8_t) std::clamp((int) ns.gate + (rng.range(2 * r.vg + 1) - r.vg), 2, 99);
        }
        st = ns;
        if (lastActive >= 0 && !multi && rng.chance(r.ties)) {
            auto& prev = stepAt(lastActive);
            prev.tie = (uint8_t) (i - lastActive - 1);
            if (prev.tie > 0) prev.gate = kGateLegato;
        }
        lastActive = i;
    }
}

} // namespace tek

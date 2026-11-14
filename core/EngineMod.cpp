// Modulation (manual 7.x): mod lanes, LFOs and the MIDI mod matrix. Every source writes MIDI out directly and/or
// adds to the per sequencer internal destination sums (modSum) that scheduleStep / the arps read while playing.
#include "Engine.h"
#include "Scales.h"
#include <algorithm>
#include <cmath>

namespace tek {
namespace {
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline double hashUnit(uint32_t a, uint32_t b, uint32_t c) { return (hash3(a, b, c) & 0xffffff) / (double) 0xffffff * 2.0 - 1.0; } // -1..1
double wave(int w, double ph, uint32_t seed) { // -1..1
    const double f = ph - std::floor(ph);
    switch (w) {
        case 0: return f < 0.5 ? f * 4.0 - 1.0 : 3.0 - f * 4.0;            // triangle
        case 1: return std::sin(f * 6.283185307179586);                     // sine
        case 2: return f < 0.5 ? 1.0 : -1.0;                                // square
        case 3: return f * 2.0 - 1.0;                                       // ramp up
        case 4: return 1.0 - f * 2.0;                                       // fall
        case 5: return hashUnit(seed, (uint32_t) (int64_t) std::floor(ph), 7); // sample & hold
        default: {                                                          // smoothed noise
            const double x = ph * 4.0, i = std::floor(x), t = x - i;
            const double a = hashUnit(seed, (uint32_t) (int64_t) i, 3), b = hashUnit(seed, (uint32_t) (int64_t) i + 1, 3);
            return a + (b - a) * (t * t * (3.0 - 2.0 * t));
        }
    }
}
} // namespace

uint8_t Engine::seqOutChannel(int s) const {
    const auto& q = proj.seqs[(size_t) s];
    if (q.mode == Mode::Multi) return q.pat().tracks[0].channel;
    if (q.mode == Mode::Matr) return q.pat().mat.tracks[0].channel;
    return q.channel;
}

void Engine::sendModOut(int s, int dest, int value, int& last, std::vector<MidiOut>& out) {
    if (dest <= 0) return;
    const uint8_t ch = seqOutChannel(s);
    if (dest <= 128) { const int v = std::clamp(value, 0, 127); if (v != last) { last = v; out.push_back({0, (uint8_t) (0xB0 | ch), (uint8_t) (dest - 1), (uint8_t) v}); } }
    else if (dest == kModOutPB) {
        const int v = std::clamp(8192 + value * 64, 0, 16383);
        if (v != last) { last = v; out.push_back({0, (uint8_t) (0xE0 | ch), (uint8_t) (v & 127), (uint8_t) (v >> 7)}); }
    } else if (dest == kModOutAT) { const int v = std::clamp(value, 0, 127); if (v != last) { last = v; out.push_back({0, (uint8_t) (0xD0 | ch), (uint8_t) v, 0}); } }
    else if (dest == kModOutPC) { const int v = std::clamp(value, 0, 127); if (v != last) { last = v; out.push_back({0, (uint8_t) (0xC0 | ch), (uint8_t) v, 0}); } }
}

void Engine::modRecord(int s, int lane, int value) {
    latch.s = s; latch.lane = lane; latch.value = std::clamp(value, -127, 127);
    latch.until = lastPpq + 2.0; // keeps writing for two beats after the last knob move (latch + touch)
}

// incoming MIDI for the matrix: CC / note maps, learn, CC forwarding and global parameters
void Engine::modInput(uint8_t status, uint8_t d1, uint8_t d2, std::vector<MidiOut>& out) {
    const int type = status & 0xF0, ch = status & 15;
    const bool cc = type == 0xB0, noteOn = type == 0x90 && d2 > 0, noteOff = type == 0x80 || (type == 0x90 && d2 == 0);
    if (!cc && !noteOn && !noteOff) return;
    if (learnSeq >= 0 && learnMap >= 0 && (cc || noteOn)) {
        auto& m = proj.seqs[(size_t) learnSeq].maps[(size_t) learnMap];
        m.type = cc ? 1 : 2; m.num = d1; m.chan = (uint8_t) (ch + 2);
        learnSeq = learnMap = -1;
    }
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& q = proj.seqs[(size_t) s];
        for (int i = 0; i < kNumModMaps; ++i) {
            const auto& m = q.maps[(size_t) i];
            if (m.type == 0 || (m.type == 1) != cc || m.num != d1) continue;
            const int want = m.chan == 0 ? seqOutChannel(s) : m.chan == 1 ? -1 : m.chan - 2;
            if (want >= 0 && want != ch) continue;
            const int v = cc ? d2 : (noteOn ? 127 : 0);
            mapVal[(size_t) s][(size_t) i] = v;
            if (m.ccOut > 0) {
                const int o = std::clamp(v * m.amt1 / 100 + m.off1 * 127 / 100, 0, 127);
                out.push_back({0, (uint8_t) (0xB0 | seqOutChannel(s)), (uint8_t) (m.ccOut - 1), (uint8_t) o});
            }
            switch (m.par) { // destructive global parameters
                case MP_Mute: q.muted = v > 63; break;
                case MP_Enable: q.active = v > 63; break;
                case MP_Scale: q.scale = (uint8_t) std::min<int>((int) scaleLibrary().size() - 1, v * (int) scaleLibrary().size() / 128); break;
                case MP_Root: q.rootPc = (uint8_t) std::min(11, v * 12 / 128); break;
                case MP_Swing: q.pat().swing = (uint8_t) (10 + v * 80 / 127); break;
                case MP_Div: q.pat().tracks[0].div = (Division) std::min<int>((int) Division::Count - 1, v * (int) Division::Count / 128); break;
                case MP_Dir: q.pat().tracks[0].dir = (Dir) std::min<int>((int) Dir::Count - 1, v * (int) Dir::Count / 128); break;
                default: break;
            }
        }
    }
}

void Engine::computeMod(double ppq, double bpm, bool playing, std::vector<MidiOut>& out) {
    for (auto& a : modSum) a.fill(0);
    const double secs = (double) sampleClock / sr;
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& q = proj.seqs[(size_t) s];
        auto& sum = modSum[(size_t) s];
        // matrix values (also while stopped)
        for (int i = 0; i < kNumModMaps; ++i) {
            const int v = mapVal[(size_t) s][(size_t) i];
            if (v < 0) continue;
            const auto& m = q.maps[(size_t) i];
            const int bip = (v - 64) * 2;
            if (m.int1 && m.ccOut == 0) sum[m.int1] += bip * m.amt1 / 100 + m.off1 * 127 / 100;
            if (m.int2) sum[m.int2] += bip * m.amt2 / 100 + m.off2 * 127 / 100;
        }
        if (!playing || !q.active) { for (auto& lr : laneRt[(size_t) s]) lr.posUi.store(-1, std::memory_order_relaxed); continue; }
        // mod lanes
        auto& pat = q.pat();
        const auto& t0 = pat.tracks[0];
        for (int l = 0; l < kNumModLanes; ++l) {
            auto& ln = pat.lanes[(size_t) l];
            auto& lr = laneRt[(size_t) s][(size_t) l];
            const bool rec = latch.s == s && latch.lane == l && ppq < latch.until;
            if (ln.muted && !rec) { lr.posUi.store(-1, std::memory_order_relaxed); continue; }
            const int rs = ln.linked ? t0.start : ln.start, re = ln.linked ? t0.end : ln.end;
            const double L = divisionBeats(ln.linked ? t0.div : ln.div);
            const int len = std::max(1, re - rs + 1);
            const double steps = std::max(0.0, ppq) / L;
            const int64_t absStep = (int64_t) std::floor(steps);
            const int pos = rs + (int) (absStep % len);
            if (rec) { ln.val[(size_t) pos] = (int8_t) latch.value; ln.muted = false; }
            double v = ln.val[(size_t) pos];
            if (ln.smooth > 0) { // glide into the next step over the last part of this one
                const double w = ln.smooth / 100.0, f = steps - std::floor(steps);
                const int nxt = rs + (int) ((absStep + 1) % len);
                if (f > 1.0 - w) v += (ln.val[(size_t) nxt] - v) * (f - (1.0 - w)) / w;
            }
            if (ln.random != 0) {
                const double r = hashUnit((uint32_t) (s * 8 + l), (uint32_t) absStep, 11);
                v += (ln.random > 0 ? r : -std::fabs(r)) * std::abs(ln.random) * 1.27;
            }
            const int o = std::clamp((int) std::lround(v) + ln.offset, (int) ln.min, (int) ln.max);
            lr.posUi.store(pos, std::memory_order_relaxed);
            lr.outUi.store(o, std::memory_order_relaxed);
            sendModOut(s, ln.cc1, o, lr.last[0], out);
            sendModOut(s, ln.cc2, o, lr.last[1], out);
            if (ln.int1) sum[ln.int1] += o * ln.amt1 / 100 + ln.off1;
            if (ln.int2) sum[ln.int2] += o * ln.amt2 / 100 + ln.off2;
        }
        // LFOs (only while running, manual 7.8)
        for (int i = 0; i < kNumLfos; ++i) {
            const auto& lf = q.lfo[(size_t) i];
            const int amt = std::clamp(lf.amt + sum[i == 0 ? MD_Lfo1Amt : MD_Lfo2Amt] * 100 / 127, 0, 100);
            if (amt == 0 || (!lf.inDst && !lf.dst)) continue;
            const int rateMod = sum[i == 0 ? MD_Lfo1Rate : MD_Lfo2Rate] / 16;
            double ph;
            if (lf.rate < 0) {
                const int r = std::clamp(lf.rate - rateMod, -11, -1); // positive mod = faster
                ph = (ppq - (lf.retrig ? lastStepPpq[(size_t) s] : 0.0)) / lfoClockBeats(r);
            } else {
                const int r = std::clamp(lf.rate + rateMod * 4, 1, 80);
                ph = (secs - (lf.retrig ? lastStepSample[(size_t) s] / sr : 0.0)) * lfoHz(r);
            }
            ph += lf.phase / 100.0;
            const double w = wave(lf.wave, ph, (uint32_t) (s * 2 + i + 1));
            if (lf.inDst) sum[lf.inDst] += (int) std::lround(w * 127.0 * amt / 100.0);
            if (lf.dst) {
                const int v = lf.dst == kModOutPB ? (int) std::lround(w * 127.0 * amt / 100.0) + lf.offset
                                                  : (int) std::lround((w * 0.5 + 0.5) * 127.0 * amt / 100.0) + lf.offset;
                sendModOut(s, lf.dst, v, lfoRt[(size_t) s][(size_t) i].last, out);
            }
        }
        (void) bpm;
    }
    if (latch.s >= 0 && ppq >= latch.until) latch.s = -1;
    for (auto& a : modSum) for (auto& v : a) v = std::clamp(v, -127, 127);
}

// sequencer level destinations: applied for one block, restored afterwards (the project itself never changes)
void Engine::applySeqMods() {
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& q = proj.seqs[(size_t) s];
        auto& sv = seqSave[(size_t) s];
        const auto& m = modSum[(size_t) s];
        sv.valid = false;
        bool any = false;
        for (int d : {MD_GrvVel, MD_GrvTime, MD_ArpDiv, MD_ArpGate, MD_ArpOct, MD_ArpAccent, MD_RpVel, MD_RpOct, MD_RpGate, MD_RpNote, MD_RpTrig}) any |= m[(size_t) d] != 0;
        if (!any) continue;
        sv = {q.rpVel, q.rpOct, q.rpGate, q.rpNote, q.rpTrig, q.grooveAccent, q.grooveTime, q.arp.div, q.arp.gate, q.arp.octV, q.arp.accent, true};
        auto add8 = [](int8_t v, int d, int lo, int hi) { return (int8_t) std::clamp(v + d, lo, hi); };
        q.rpVel = add8(q.rpVel, m[MD_RpVel] * 100 / 127, -100, 100);
        q.rpOct = add8(q.rpOct, m[MD_RpOct] * 100 / 127, -100, 100);
        q.rpGate = add8(q.rpGate, m[MD_RpGate] * 100 / 127, -100, 100);
        q.rpNote = add8(q.rpNote, m[MD_RpNote] * 100 / 127, -100, 100);
        q.rpTrig = (uint8_t) std::clamp(q.rpTrig + m[MD_RpTrig] * 100 / 127, 0, 100);
        q.grooveAccent = (int16_t) std::clamp(q.grooveAccent + m[MD_GrvVel] * 120 / 127, -120, 120);
        q.grooveTime = (int16_t) std::clamp(q.grooveTime + m[MD_GrvTime] * 120 / 127, -120, 120);
        q.arp.div = (Division) std::clamp((int) q.arp.div + m[MD_ArpDiv] / 32, 0, (int) Division::Count - 1);
        q.arp.gate = (uint8_t) std::clamp(q.arp.gate + m[MD_ArpGate] * 100 / 254, 2, 100);
        q.arp.octV = add8(q.arp.octV, m[MD_ArpOct] / 32, -7, 7);
        q.arp.accent = add8(q.arp.accent, m[MD_ArpAccent] * 100 / 127, -100, 100);
    }
}

void Engine::restoreSeqMods() {
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& sv = seqSave[(size_t) s];
        if (!sv.valid) continue;
        auto& q = proj.seqs[(size_t) s];
        q.rpVel = sv.rpVel; q.rpOct = sv.rpOct; q.rpGate = sv.rpGate; q.rpNote = sv.rpNote; q.rpTrig = sv.rpTrig;
        q.grooveAccent = sv.grvA; q.grooveTime = sv.grvT;
        q.arp.div = sv.arpDiv; q.arp.gate = sv.arpGate; q.arp.octV = sv.arpOct; q.arp.accent = sv.arpAcc;
        sv.valid = false;
    }
}

} // namespace tek

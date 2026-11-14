#include "Engine.h"
#include "Chords.h"
#include <algorithm>
#include <cmath>

namespace tek {

Engine::Engine(Project& p) : proj(p) {
    queue.reserve(16384);
    liveQueue.reserve(512);
    previewOffs.reserve(64);
    due.reserve(4096);
    pendingPat.fill(-1);
    resetPending.fill(false);
    transposeOffset.fill(0);
}

void Engine::prepare(double sampleRate) { sr = sampleRate > 0 ? sampleRate : 44100.0; }

void Engine::setInternalPlaying(bool playing) {
    if (playing && !intPlaying) intPpq = 0.0;
    intPlaying = playing;
}

void Engine::requestPattern(int seq, int pattern) {
    if (seq < 0 || seq >= kNumSeqs || pattern < 0 || pattern >= kNumPatterns) return;
    if (!wasPlaying) { proj.seqs[(size_t) seq].curPattern = (uint8_t) pattern; pendingPat[(size_t) seq] = -1; }
    else pendingPat[(size_t) seq] = pattern;
}

void Engine::panic() { panicPending = true; }
void Engine::resetSeq(int seq) { if (seq >= 0 && seq < kNumSeqs) resetPending[(size_t) seq] = true; }

int Engine::positionFor(Dir dir, int start, int end, int64_t c) {
    const int len = end - start + 1;
    if (len <= 1) return start;
    switch (dir) {
        case Dir::Reverse: return end - (int) (c % len);
        case Dir::Pendulum: { // ends play twice
            int64_t p = c % (2 * len);
            return p < len ? start + (int) p : end - (int) (p - len);
        }
        case Dir::Pong: {     // ends play once
            int64_t period = 2 * len - 2;
            int64_t p = c % period;
            return p < len ? start + (int) p : end - (int) (p - len + 1);
        }
        default: return start + (int) (c % len);
    }
}

double Engine::stepLength(const Sequencer& q, const Track& tr) const {
    double L = divisionBeats(tr.hasLoop() && tr.loopRate != 255 ? (Division) tr.loopRate : tr.div);
    double bend = 1.0 + q.timeBend / 1000.0;
    return L / bend;
}

double Engine::trackStepLength(int s, int t) const {
    const auto& q = proj.seqs[(size_t) s];
    if (q.mode == Mode::Matr)
        return divisionBeats(q.pat().mat.tracks[(size_t) t].lanes[kLaneTrig].div) / (1.0 + q.timeBend / 1000.0);
    if (q.mode == Mode::Domino) return stepLength(q, q.pat().tracks[1]);
    if (q.mode == Mode::Player) return 0.25 / (1.0 + q.timeBend / 1000.0); // tick based: fixed 1/16 windows
    return stepLength(q, q.pat().tracks[(size_t) t]);
}

void Engine::initTrack(int s, int t, double ppq) {
    auto& q = proj.seqs[(size_t) s];
    auto& r = rt[(size_t) s][(size_t) t];
    double L = trackStepLength(s, t);
    r.matActive = 0.0; r.matHold = 0; r.matAccum = 0; r.matTicks = 0; r.lastNote = -1; r.domNote = -1; r.domNoteUi = -1;
    r.laneCnt.fill(-1); for (auto& lp : r.lanePos) lp.store(0);
    r.accVal.fill(0); r.accDir.fill(1);
    int64_t idx = (int64_t) std::ceil(ppq / L - 1e-7);
    r.nextIdx = idx;
    r.nextGrid = idx * L;
    r.sinceStart = 0;
    r.lastTrig = true;
    r.pos = -1;
    if (q.resetBars > 0) {
        double period = q.resetBars * 4.0;
        r.resetBlock = (int64_t) std::floor(r.nextGrid / period + 1e-9);
        r.counter = (int64_t) std::llround((r.nextGrid - r.resetBlock * period) / L);
    } else {
        r.resetBlock = -1;
        r.counter = idx; // timeline aligned: the pattern sits where it would be if played from the start
    }
    r.init = true;
}

void Engine::startAll(double ppq) {
    for (int s = 0; s < kNumSeqs; ++s)
        for (int t = 0; t < kNumTracks; ++t) initTrack(s, t, ppq);
    running = true;
}

void Engine::emitNoteOn(std::vector<MidiOut>& out, int off, int ch, int note, int vel) {
    auto& c = noteCount[(size_t) ch][(size_t) note];
    if (c > 0) out.push_back({off, (uint8_t) (0x80 | ch), (uint8_t) note, 0}); // re-attack
    if (c < 255) ++c;
    out.push_back({off, (uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) std::clamp(vel, 1, 127)});
}

void Engine::emitNoteOff(std::vector<MidiOut>& out, int off, int ch, int note) {
    auto& c = noteCount[(size_t) ch][(size_t) note];
    if (c == 0) return;
    --c;
    if (c == 0) out.push_back({off, (uint8_t) (0x80 | ch), (uint8_t) note, 0});
}

void Engine::stopAll(std::vector<MidiOut>& out, int offset) {
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (noteCount[(size_t) ch][(size_t) n] > 0) {
                out.push_back({offset, (uint8_t) (0x80 | ch), (uint8_t) n, 0});
                noteCount[(size_t) ch][(size_t) n] = 0;
            }
    queue.clear();
    for (auto& sq : rt) for (auto& r : sq) { r.init = false; r.playheadUi = -1; }
    running = false;
}

bool Engine::evalTrig(const Step& st, TrackRt& r, int len) {
    const auto& t = st.trig;
    int64_t cycle = len > 0 ? r.counter / len : 0;
    int64_t cycleSinceStart = len > 0 ? r.sinceStart / len : 0;
    bool res = true;
    switch (t.type) {
        case TrigType::Prob: res = t.a >= 100 ? true : (t.a == 0 ? false : (rand32() % 100) < t.a); break;
        case TrigType::AB: res = t.b > 0 && (cycle % t.b) + 1 == t.a; break;
        case TrigType::NotAB: res = !(t.b > 0 && (cycle % t.b) + 1 == t.a); break;
        case TrigType::Pre: res = r.lastTrig; break;
        case TrigType::NotPre: res = !r.lastTrig; break;
        case TrigType::First: res = cycleSinceStart == 0; break;
        case TrigType::NotFirst: res = cycleSinceStart != 0; break;
        case TrigType::Fixed: case TrigType::Locked: res = true; break;
        case TrigType::Flow: res = flowHeld.load(); break;
        case TrigType::NotFlow: res = !flowHeld.load(); break;
    }
    // PRE / !PRE look at the most recent condition result; plain 100% steps don't overwrite it
    bool isCondition = !(t.type == TrigType::Prob && t.a >= 100) && t.type != TrigType::Fixed && t.type != TrigType::Locked;
    if (isCondition) r.lastTrig = res;
    return res;
}

// ---------------------------------------------------------------- accumulator (manual 6.13)
// Returns the accumulated interval to play now; when `advance` is set the value moves on for next time.
int Engine::accNext(const Step& st, const Track& tr, TrackRt& r, int idx, bool advance) {
    if (st.accAmt == 0 || idx < 0 || idx >= kMaxSteps) return 0;
    const int cur = r.accVal[(size_t) idx];
    if (!advance) return cur;
    const bool pass = st.accProb >= 100 || (int) (rand32() % 100) < st.accProb;
    if (!pass) return st.accPctMode == 0 ? 0 : cur; // Ignore: original note, Skip: last value without advancing
    const int mx = tr.accTrackMode ? tr.accMax : st.accMax;
    const int mn = tr.accTrackMode ? tr.accMin : st.accMin;
    int v = cur, d = r.accDir[(size_t) idx] == 0 ? 1 : r.accDir[(size_t) idx];
    switch (st.accMode) {
        case 0: { v += st.accAmt; const int range = mx - mn + 1; while (v > mx) v -= range; while (v < mn) v += range; break; }
        case 1: v += st.accAmt; if (v > mx) v = mn; if (v < mn) v = mx; break;
        case 2: v += st.accAmt * d;
                if (v > mx) { v = mx - (v - mx); d = -d; }
                if (v < mn) { v = mn + (mn - v); d = -d; }
                v = std::clamp(v, mn, mx); break;
        default: v = std::clamp(v + st.accAmt, mn, mx); break;
    }
    r.accVal[(size_t) idx] = (int8_t) v;
    r.accDir[(size_t) idx] = (int8_t) d;
    return cur;
}

// ---------------------------------------------------------------- harmonizer (manual 6.15)
void Engine::recordHarm(int s, const int* notes, int n) {
    n = std::min(n, 8);
    for (int i = 0; i < n; ++i) harmNotes[(size_t) s][(size_t) i] = notes[i];
    harmCount[(size_t) s] = n;
}

// Pitch for a scale degree; followers of a harmonizer leader map degrees onto the leader's current chord.
int Engine::degPitch(const Sequencer& q, int s, int base, int degree) const {
    if (q.mode != Mode::Multi && q.mode != Mode::Matr) degree += q.perfTrans; // perform transpose (scale degrees)
    if (q.harm >= 0 && q.harm != s) {
        int pcs[8], n = 0;
        const int cnt = harmCount[(size_t) q.harm];
        for (int i = 0; i < cnt; ++i) {
            int pc = ((harmNotes[(size_t) q.harm][(size_t) i] % 12) + 12) % 12;
            if (std::find(pcs, pcs + n, pc) == pcs + n) pcs[n++] = pc;
        }
        if (n == 0) { pcs[0] = 0; pcs[1] = 4; pcs[2] = 7; n = 3; } // until the leader plays: C major
        std::sort(pcs, pcs + n);
        int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        int k = degree - oct * n;
        return (base / 12) * 12 + pcs[k] + 12 * oct;
    }
    return base + degreeToSemis(q.scale, degree);
}

// Channel multiplex: channels by pitch rank among voices [first, first + n)
template <typename V> void Engine::assignChMx(const Sequencer& q, V* voices, int first, int n) const {
    if (q.chMx == 0 || n <= 0) return;
    for (int i = first; i < first + n; ++i) {
        int rank = 0;
        for (int j = first; j < first + n; ++j) if (voices[j].note < voices[i].note || (voices[j].note == voices[i].note && j < i)) ++rank;
        voices[i].ch = chMxChannel(q.channel, q.chMx, rank, n);
    }
}

void Engine::scheduleStep(int s, int t, double grid, double L, double bpm) {
    auto& q = proj.seqs[(size_t) s];
    auto& pat = q.pat();
    auto& tr = pat.tracks[(size_t) t];
    auto& r = rt[(size_t) s][(size_t) t];

    // reset period handling
    if (q.resetBars > 0) {
        double period = q.resetBars * 4.0;
        int64_t blk = (int64_t) std::floor(grid / period + 1e-9);
        if (blk != r.resetBlock) { r.resetBlock = blk; r.counter = 0; r.pos = -1; }
    }

    // a loop (manual 5.2) replaces the pattern range while it is set
    const int rs = tr.hasLoop() ? tr.loopStart : tr.start, re = tr.hasLoop() ? tr.loopEnd : tr.end;
    const int len = re - rs + 1;
    int pos;
    if (tr.dir == Dir::Random) pos = rs + (int) (rand32() % (uint32_t) std::max(1, len));
    else if (tr.dir == Dir::Drunk) {
        if (r.pos < rs || r.pos > re) pos = rs;
        else {
            uint32_t x = rand32() % 4;
            pos = r.pos + (x < 2 ? 1 : (x == 2 ? 0 : -1));
            if (pos > re) pos = rs;
            if (pos < rs) pos = re;
        }
    } else pos = positionFor(tr.dir, rs, re, r.counter);
    r.pos = pos;
    if (q.perfRot != 0 && len > 0 && q.mode != Mode::Multi) pos = rs + (((pos - rs - q.perfRot) % len) + len) % len; // perform rotate

    { const uint32_t Lq = (uint32_t) std::clamp(std::lround(L * 960.0), 0L, 0xffffffL); // step length rides in ch/d1/d2
      push({grid, 2, (uint8_t) (Lq & 255), (uint8_t) ((Lq >> 8) & 255), (uint8_t) (Lq >> 16), (uint8_t) s, (uint8_t) t, (int16_t) pos}); }

    const Step& st = tr.steps[(size_t) pos];
    const bool parity = (r.nextIdx % 2) != 0; // nextIdx already refers to this step here
    const int64_t counter = r.counter;
    r.counter++;
    r.sinceStart++;
    if (q.mode == Mode::Stoch) { playStoch(s, pos, grid, L, bpm, parity, counter); return; }
    if (!st.active || st.numNotes == 0) return;

    bool fire = evalTrig(st, r, len);
    if (!seqAudible(s) || st.muted) fire = false;
    if (q.mode == Mode::Multi) {
        uint8_t solo = soloMask[(size_t) s];
        if (tr.muted) fire = false;
        if (solo != 0 && !(solo & (1u << t))) fire = false;
    }
    if (!fire) return;

    // ---- random perform (manual 5.6): temporary variations, the pattern itself is untouched
    const bool rpOn = (q.mode == Mode::Mono || q.mode == Mode::Poly || q.mode == Mode::Chord || q.mode == Mode::Multi)
                   && !(q.mode == Mode::Multi && ((q.rpMask >> t) & 1));
    auto chance = [&](int pct) { return pct > 0 && (int) (rand32() % 100) < pct; };
    auto bi = [&](int amt) { // amt > 0: -amt..+amt, amt < 0: amt..0 (percent)
        if (amt == 0) return 0.0;
        double x = rand01();
        return amt > 0 ? (x * 2.0 - 1.0) * amt : x * amt;
    };
    int rpDeg = 0, nRep = st.repeats, nRetrig = st.retrig;
    double velMul = 1.0, gateMul = 1.0;
    if (rpOn) {
        if (q.rpTrig < 100 && !chance(q.rpTrig)) return;
        if (q.rpRetri < 0 && chance(-q.rpRetri)) return; // skip
        velMul = 1.0 + bi(q.rpVel) / 100.0;
        gateMul = 1.0 + bi(q.rpGate) / 100.0;
        const int n = (int) scaleLibrary()[q.scale].intervals.size();
        if (chance(std::abs(q.rpOct))) rpDeg += n * (q.rpOct < 0 ? -1 : ((rand32() & 1) ? 1 : -1));
        if (chance(std::abs(q.rpNote))) { int d = 1 + (int) (rand32() % 3); rpDeg += q.rpNote < 0 ? -d : ((rand32() & 1) ? d : -d); }
        if (chance(q.rpRept)) nRep = 2 + (int) (rand32() % 3);
        if (q.rpRetri > 0 && chance(q.rpRetri)) nRetrig = 2 + (int) (rand32() % 3);
    }
    if (q.mode == Mode::Multi && multiFill[(size_t) s] > 0) nRetrig = std::max(nRetrig, multiFill[(size_t) s] + 1); // perform fill

    // ---- timing
    double time = grid;
    int swing = (q.mode == Mode::Multi && tr.swing != 0) ? tr.swing : pat.swing;
    if (parity) time += (swing - 50) / 50.0 * L;
    time += st.offset / 100.0 * L;
    double vel = st.velocity * velMul;
    if (q.groove > 0) {
        const auto& g = grooveTemplate(q.groove);
        int gl = std::max(1, (int) q.grooveLen);
        int gi = (int) (((counter + q.grooveOffset) % gl + gl) % gl) % 16;
        float rnd = 1.0f - rand01() * q.grooveRand / 100.0f;
        time += g.timing[(size_t) gi] * q.grooveTime / 100.0 * L * rnd;
        vel *= 1.0 + g.velocity[(size_t) gi] * q.grooveAccent / 100.0 * rnd;
    }
    time += q.nudgeMs / 1000.0 * bpm / 60.0;

    // ---- duration
    double dur;
    if (st.tie > 0) dur = (st.tie + 1) * L - 0.001;
    else if (st.gate >= kGateLegato) dur = L * 1.03;
    else dur = std::max(0.002, st.gate / 100.0 * L);
    dur = std::max(0.002, dur * gateMul);

    // ---- notes: build the voices of this step (pitch, channel, strum delay)
    struct Voice { int note; uint8_t ch; double delay; };
    const int nNotes = q.mode == Mode::Poly ? std::min<int>(st.numNotes, q.nVoices) : 1;
    const uint8_t ch = q.mode == Mode::Multi ? tr.channel : q.channel;
    auto buildVoices = [&](int degShift, Voice* out) -> int {
        int n = 0;
        if (q.mode == Mode::Chord) {
            int semis[kMaxChordNotes];
            int m = buildChord(q.scale, st.notes[0] + degShift + q.perfTrans, st.chordType, st.voicing, st.spread, semis);
            int base = q.baseMidi() + st.octave * 12 + transposeOffset[(size_t) s];
            if (q.bassMode > 0) {
                uint8_t bch = q.bassMode == 1 ? q.channel : (uint8_t) (q.bassMode - 2);
                out[n++] = {std::clamp(base + chordRootSemis(q.scale, st.notes[0] + degShift + q.perfTrans) - 12, 0, 127), bch, 0.0};
            }
            const double strumStep = std::abs(st.strum) / 100.0 * L / std::max(1, m);
            const int first = n;
            int rec[kMaxChordNotes];
            for (int k = 0; k < m; ++k) {
                double d = (st.strum >= 0 ? k : (m - 1 - k)) * strumStep;
                out[n++] = {std::clamp(base + semis[k], 0, 127), q.channel, d};
                rec[k] = base + semis[k];
            }
            assignChMx(q, out, first, m);
            if (degShift == 0) recordHarm(s, rec, m);
        } else {
            int basePitch = q.mode == Mode::Multi ? (int) tr.baseNote : q.baseMidi();
            int rec[kMaxPolyNotes];
            for (int k = 0; k < nNotes; ++k) {
                int deg = st.notes[(size_t) k] + degShift;
                int note = std::clamp(degPitch(q, s, basePitch, deg) + st.octave * 12 + transposeOffset[(size_t) s], 0, 127);
                out[n++] = {note, ch, 0.0};
                rec[k] = note;
            }
            if (q.mode == Mode::Poly) { assignChMx(q, out, 0, n); if (degShift == 0 && q.harm < 0) recordHarm(s, rec, n); }
        }
        return n;
    };

    auto addVoice = [&](double on, double length, const Voice& vc, double v) {
        int vv = std::clamp((int) std::lround(v), 1, 127);
        push({on + vc.delay, 0, vc.ch, (uint8_t) vc.note, (uint8_t) vv, (uint8_t) s, (uint8_t) t, 0});
        push({on + vc.delay + length, 1, vc.ch, (uint8_t) vc.note, 0, (uint8_t) s, (uint8_t) t, 0});
    };
    // accumulator (mono / poly): target All = step + every repeat, Step = once per trigger, Repeats = repeats only
    const bool accOn = st.accAmt != 0 && (q.mode == Mode::Mono || q.mode == Mode::Poly);
    const int stepAcc = (accOn ? accNext(st, tr, r, pos, st.accTarget != 2) : 0) + rpDeg;
    Voice voices[kMaxChordNotes + 2];
    const int nv = buildVoices(stepAcc, voices);

    if (nRep > 1) {
        const int n = nRep;
        const double total = st.repDur * L;
        const double f = 1.0 - st.accel / 100.0;
        double d0 = std::fabs(f - 1.0) < 1e-6 ? total / n : total * (1.0 - f) / (1.0 - std::pow(f, n));
        double tpos = time, d = d0;
        for (int i = 0; i < n; ++i) {
            if (i > 0 && st.repProb < 100 && (int) (rand32() % 100) >= st.repProb) { tpos += d; d *= f; continue; }
            double v = vel * (1.0 + st.ramp / 100.0 * (n > 1 ? (double) i / (n - 1) : 0.0));
            double length = std::max(0.002, std::min(dur, d * 0.9));
            const bool accRep = accOn && i > 0 && st.accTarget != 1;
            if (i > 0 && (st.rng > 0 || accRep)) {
                Voice rv[kMaxChordNotes + 2];
                int shift = st.rng > 0 ? (int) (rand32() % (uint32_t) (2 * st.rng + 1)) - st.rng : 0;
                shift += accRep ? accNext(st, tr, r, pos, true) : stepAcc;
                int rn = buildVoices(shift, rv);
                for (int k = 0; k < rn; ++k) addVoice(tpos, length, rv[k], v);
            } else
                for (int k = 0; k < nv; ++k) addVoice(tpos, length, voices[k], v);
            tpos += d; d *= f;
        }
    } else if (nRetrig > 1) {
        const int n = nRetrig;
        double span = st.tie > 0 ? dur : std::max(dur, L * 0.5);
        double d = span / n;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < nv; ++k) addVoice(time + i * d, std::max(0.002, d * 0.5), voices[k], vel);
    } else if ((q.mode == Mode::Poly || q.mode == Mode::Chord) && q.arp.seqType != kArpOff && nv > 0) {
        // sequencer arp: the step's notes are arpeggiated across the step (and its ties)
        int notes[kMaxChordNotes + 2];
        for (int k = 0; k < nv; ++k) notes[k] = voices[k].note;
        std::sort(notes, notes + nv);
        static thread_local int pat[512];
        const int pn = buildArpPattern(q.arp, q.arp.seqType, notes, nv, pat, s);
        const double La = divisionBeats(q.arp.div);
        const double span = (st.tie + 1) * L;
        int k = 0;
        for (double tt = 0.0; tt < span - 1e-6 && pn > 0; tt += La, ++k) {
            Voice vc = voices[0];
            vc.note = std::clamp(pat[k % pn], 0, 127); vc.delay = 0.0;
            addVoice(time + tt, std::max(0.002, q.arp.gate / 100.0 * La), vc, vel);
        }
    } else {
        for (int k = 0; k < nv; ++k) addVoice(time, dur, voices[k], vel);
    }
}

// ---------------------------------------------------------------- stochastic mode (manual 6.8)
void Engine::regenStoch(Stochastic& sc, const Track& tr, int i, bool pitch, bool rhythm) {
    if (pitch) {
        int total = 0;
        for (auto l : sc.noteLevel) total += l;
        int pick = 0;
        if (total > 0) {
            int x = (int) (rand32() % (uint32_t) total);
            for (int k = 0; k < 12; ++k) { x -= sc.noteLevel[(size_t) k]; if (x < 0) { pick = k; break; } }
        }
        int span = sc.octUp + sc.octDown + 1;
        int oct = (int) (rand32() % (uint32_t) span) - sc.octDown;
        sc.pitch[(size_t) i] = (int8_t) (pick + 12 * oct);
    }
    if (rhythm) {
        static const uint8_t codes[8] = {24, 22, 1, 2, 3, 4, 6, 8}; // long -> short
        int a = std::min<int>(sc.divRow, 2), b = std::max<int>(sc.divRow, 2);
        uint8_t code = 0;
        if (rand32() % 100 < 75) code = codes[a + (int) (rand32() % (uint32_t) (b - a + 1))];
        sc.rhythm[(size_t) i] = code;
        if (code >= 20) // a held note covers the following steps
            for (int k = 1; k < code - 20 && i + k <= tr.end; ++k) sc.rhythm[(size_t) (i + k)] = 99;
    }
}

void Engine::playStoch(int s, int pos, double grid, double L, double bpm, bool parity, int64_t counter) {
    auto& q = proj.seqs[(size_t) s];
    auto& pat = q.pat();
    auto& tr = pat.tracks[0];
    auto& sc = pat.stoch;
    const int len = tr.length();
    // mutate the phrase at the start of every cycle
    if (!sc.valid) {
        for (int i = 0; i < kMaxSteps; ++i) { sc.rhythm[(size_t) i] = 0; sc.pitch[(size_t) i] = 0; }
        for (int i = tr.start; i <= tr.end; ++i) regenStoch(sc, tr, i, true, true);
        sc.valid = true;
    } else if (len > 0 && counter % len == 0 && counter > 0) {
        for (int i = tr.start; i <= tr.end; ++i) {
            bool p = sc.pitchRand > 0 && (int) (rand32() % 7) < sc.pitchRand;
            bool rh = sc.rhythmRand > 0 && (int) (rand32() % 7) < sc.rhythmRand;
            if (p || rh) regenStoch(sc, tr, i, p, rh);
        }
    }
    int total = 0;
    for (auto l : sc.noteLevel) total += l;
    const uint8_t code = sc.rhythm[(size_t) pos];
    if (code == 0 || code == 99 || total == 0 || !seqAudible(s)) return;

    double time = grid;
    if (parity) time += (pat.swing - 50) / 50.0 * L;
    time += q.nudgeMs / 1000.0 * bpm / 60.0;
    const Step& prof = tr.steps[(size_t) pos]; // velocity / gate profiles drawn in the column editors
    const int vel = prof.velocity;
    const double gate = prof.gate >= kGateLegato ? 1.03 : prof.gate / 100.0;
    const int note = std::clamp(q.baseMidi() + sc.pitch[(size_t) pos] + degreeToSemis(q.scale, q.perfTrans) + transposeOffset[(size_t) s], 0, 127);
    auto add = [&](double on, double dur) {
        push({on, 0, q.channel, (uint8_t) note, (uint8_t) std::clamp(vel, 1, 127), (uint8_t) s, 0, 0});
        push({on + std::max(0.002, dur), 1, q.channel, (uint8_t) note, 0, (uint8_t) s, 0, 0});
    };
    if (code >= 20) add(time, (code - 20) * L * 0.98);
    else if (code == 1) add(time, gate * L);
    else { double d = L / code; for (int i = 0; i < code; ++i) add(time + i * d, d * 0.5); }
}

// ---------------------------------------------------------------- matriceal mode (manual 6.9)
void Engine::playMatriceal(int s, int t, double grid, double L, double bpm) {
    auto& q = proj.seqs[(size_t) s];
    auto& mt = q.pat().mat.tracks[(size_t) t];
    auto& r = rt[(size_t) s][(size_t) t];
    const bool silent = !seqAudible(s) || mt.muted;
    const bool parity = (r.nextIdx % 2) != 0;
    double time = grid + (parity ? (q.pat().swing - 50) / 50.0 * L : 0.0) + q.nudgeMs / 1000.0 * bpm / 60.0;

    r.matTicks++;
    if (mt.rlen > 0 && r.matTicks > mt.rlen) { // RLen: restart every lane
        r.matTicks = 1; r.matActive = 0.0; r.matAccum = 0; r.matHold = 0; r.laneCnt.fill(-1);
    }
    auto emit = [&](double on, double dur, int note, int vel, uint8_t ch) {
        push({on, 0, ch, (uint8_t) note, (uint8_t) std::clamp(vel, 1, 127), (uint8_t) s, (uint8_t) t, 0});
        push({on + std::max(0.002, dur), 1, ch, (uint8_t) note, 0, (uint8_t) s, (uint8_t) t, 0});
    };
    if (r.matHold > 0) { // pulses hold every lane on the current step
        r.matHold--;
        if (r.matHoldType == 1 && r.lastNote >= 0 && !silent) emit(time, r.lastDur, r.lastNote, r.lastVel, r.lastCh);
        return;
    }
    auto pos = [&](int l) {
        const auto& ln = mt.lanes[(size_t) l];
        double Ll = divisionBeats(ln.div) / (1.0 + q.timeBend / 1000.0);
        int64_t c = (int64_t) std::floor(r.matActive / Ll + 1e-9);
        if (c != r.laneCnt[(size_t) l]) {
            int prev = r.laneCnt[(size_t) l] < 0 ? -1 : r.lanePos[(size_t) l].load();
            r.laneCnt[(size_t) l] = c;
            int p;
            int len = ln.end - ln.start + 1;
            if (ln.dir == Dir::Random) p = ln.start + (int) (rand32() % (uint32_t) std::max(1, len));
            else if (ln.dir == Dir::Drunk) {
                if (prev < ln.start || prev > ln.end) p = ln.start;
                else { uint32_t x = rand32() % 4; p = prev + (x < 2 ? 1 : (x == 2 ? 0 : -1)); if (p > ln.end) p = ln.start; if (p < ln.start) p = ln.end; }
            } else p = positionFor(ln.dir, ln.start, ln.end, c);
            r.lanePos[(size_t) l].store(p);
        }
        return r.lanePos[(size_t) l].load();
    };
    int lp[kNumMatLanes];
    auto value = [&](int l, bool useProb) {
        const auto& ln = mt.lanes[(size_t) l];
        int p = lp[l];
        if (useProb && ln.prob[(size_t) p] < 100 && (int) (rand32() % 100) >= ln.prob[(size_t) p]) return matLaneDefault(l);
        return (int) ln.val[(size_t) p];
    };
    // sample every lane at the current (hold-free) time, then advance it
    for (int l = 0; l < kNumMatLanes; ++l) lp[l] = pos(l);
    r.matActive += L;
    push({grid, 2, 0, 0, 0, (uint8_t) s, (uint8_t) t, (int16_t) lp[kLaneTrig]});

    if (mt.lanes[kLaneTrig].val[(size_t) lp[kLaneTrig]] == 0) {
        const int fill = matFill[(size_t) s];
        if (fill == 0 || (int) (rand32() % 100) >= fill * 8) return; // perform: random trigger fill
    }
    if ((int) (rand32() % 100) >= mt.lanes[kLaneProb].val[(size_t) lp[kLaneProb]]) return;

    const int note = value(kLaneNote, true);
    const int intv = value(kLaneIntv, true);
    r.matAccum += intv;
    if (r.matAccum > mt.intMax) r.matAccum = mt.intMin;
    if (r.matAccum < mt.intMin) r.matAccum = mt.intMax;
    const int oct = value(kLaneOct, true);
    const int vel = mt.lanes[kLaneVel].val[(size_t) lp[kLaneVel]];
    const int gate = mt.lanes[kLaneGate].val[(size_t) lp[kLaneGate]];
    const int rep = value(kLaneRetrig, true);
    const int puls = mt.lanes[kLanePuls].val[(size_t) lp[kLanePuls]];
    const uint8_t ptype = mt.lanes[kLanePuls].aux[(size_t) lp[kLanePuls]];
    r.matHold = std::max(0, puls - 1);
    r.matHoldType = ptype;
    if (rep == 0) return; // "skip"

    const int midi = std::clamp(degPitch(q, s, (int) mt.baseNote, note + r.matAccum) + 12 * oct + transposeOffset[(size_t) s], 0, 127);
    double dur = gate >= 101 ? L * puls * 0.98 : gate == 100 ? L * 1.03 : gate / 100.0 * L;
    if (ptype == 2) dur = L * puls * 0.98;
    r.lastNote = midi; r.lastVel = vel; r.lastCh = mt.channel; r.lastDur = std::min(dur, L * 0.95);
    if (silent) return;
    if (rep > 1) { double d = L / rep; for (int i = 0; i < rep; ++i) emit(time + i * d, d * 0.5, midi, vel, mt.channel); }
    else emit(time, dur, midi, vel, mt.channel);
}

// ---------------------------------------------------------------- domino mode (manual 6.10)
// tracks[1] holds the trigger section (up to 64 steps), tracks[0] the note section (up to 32 steps).
// Every trigger and every trigger repeat advances the note section by one step.
void Engine::playDomino(int s, double grid, double L, double bpm) {
    auto& q = proj.seqs[(size_t) s];
    auto& pat = q.pat();
    auto& trigT = pat.tracks[1];
    auto& noteT = pat.tracks[0];
    auto& r = rt[(size_t) s][0];
    const int tlen = trigT.length();
    int pos;
    if (trigT.dir == Dir::Random) pos = trigT.start + (int) (rand32() % (uint32_t) std::max(1, tlen));
    else pos = positionFor(trigT.dir == Dir::Drunk ? Dir::Forward : trigT.dir, trigT.start, trigT.end, r.counter);
    const bool parity = (r.nextIdx % 2) != 0;
    push({grid, 2, 0, 0, 0, (uint8_t) s, 1, (int16_t) pos});
    r.counter++; r.sinceStart++;

    const Step& ts = trigT.steps[(size_t) pos];
    if (!ts.active) return;
    if (!evalTrig(ts, r, tlen)) return;

    double time = grid + (parity ? (pat.swing - 50) / 50.0 * L : 0.0) + q.nudgeMs / 1000.0 * bpm / 60.0;
    const int n = std::max<int>(1, ts.repeats);
    const double span = ts.repDur * L;
    const double d = n > 1 ? span / n : L;
    for (int i = 0; i < n; ++i) {
        if (i > 0 && ts.repProb < 100 && (int) (rand32() % 100) >= ts.repProb) continue;
        // advance the note section
        r.domNote++;
        const int npos = positionFor(noteT.dir == Dir::Random || noteT.dir == Dir::Drunk ? Dir::Forward : noteT.dir, noteT.start, noteT.end, r.domNote);
        push({time + i * d, 4, 0, 0, 0, (uint8_t) s, 0, (int16_t) npos});
        const Step& ns = noteT.steps[(size_t) npos];
        if (!ns.active || ns.numNotes == 0 || !seqAudible(s) || ns.muted) continue;
        if (ns.trig.type == TrigType::Prob && ns.trig.a < 100 && (int) (rand32() % 100) >= ns.trig.a) continue;
        double on = time + i * d + ns.offset / 100.0 * L;
        double dur;
        if (ns.tie > 0 || ts.tie > 0) dur = (std::max<int>(ns.tie, ts.tie) + 1) * L * 0.98;
        else if (ns.gate >= kGateLegato) dur = d * 1.03;
        else dur = std::max(0.002, ns.gate / 100.0 * std::min(d, L));
        const int voices = q.voice == 0 ? 1 : std::min<int>(ns.numNotes, 4);
        const int acc = accNext(ns, noteT, r, npos, true);
        struct V { int note; uint8_t ch; } vs[4];
        for (int k = 0; k < voices; ++k)
            vs[k] = {std::clamp(degPitch(q, s, q.baseMidi(), ns.notes[(size_t) k] + acc) + ns.octave * 12 + transposeOffset[(size_t) s], 0, 127), q.channel};
        assignChMx(q, vs, 0, voices);
        for (int k = 0; k < voices; ++k) {
            push({on, 0, vs[k].ch, (uint8_t) vs[k].note, (uint8_t) std::clamp((int) ns.velocity, 1, 127), (uint8_t) s, 0, 0});
            push({on + dur, 1, vs[k].ch, (uint8_t) vs[k].note, 0, (uint8_t) s, 0, 0});
        }
    }
}

// ---------------------------------------------------------------- saga mode (manual 6.11)
// Each stage plays for its own duration (in steps). The position is derived from the step counter so it
// stays locked to the timeline.
void Engine::playSaga(int s, double grid, double L, double bpm) {
    auto& q = proj.seqs[(size_t) s];
    auto& pat = q.pat();
    auto& tr = pat.tracks[0];
    auto& r = rt[(size_t) s][0];
    // stage order for one cycle
    int order[kMaxSteps * 2];
    int n = 0;
    const int a = tr.start, b = tr.end;
    switch (tr.dir) {
        case Dir::Reverse: for (int i = b; i >= a; --i) order[n++] = i; break;
        case Dir::Pendulum: for (int i = a; i <= b; ++i) order[n++] = i; for (int i = b; i >= a; --i) order[n++] = i; break;
        case Dir::Pong: for (int i = a; i <= b; ++i) order[n++] = i; for (int i = b - 1; i > a; --i) order[n++] = i; break;
        default: for (int i = a; i <= b; ++i) order[n++] = i; break;
    }
    int64_t total = 0;
    for (int k = 0; k < n; ++k) total += std::max<int>(1, tr.steps[(size_t) order[k]].dur);
    const int64_t c = r.counter;
    r.counter++; r.sinceStart++;
    if (total <= 0) return;
    int64_t t = c % total;
    int k = 0;
    while (k < n) { int d = std::max<int>(1, tr.steps[(size_t) order[k]].dur); if (t < d) break; t -= d; ++k; }
    if (k >= n || t != 0) return; // inside a stage: the playhead waits
    const int pos = order[k];
    push({grid, 2, 0, 0, 0, (uint8_t) s, 0, (int16_t) pos});
    const Step& st = tr.steps[(size_t) pos];
    if (!st.active || st.numNotes == 0 || !seqAudible(s) || st.muted) return;
    if (!evalTrig(st, r, n)) return;

    const bool parity = (c % 2) != 0;
    double time = grid + (parity ? (pat.swing - 50) / 50.0 * L : 0.0) + st.offset / 100.0 * L + q.nudgeMs / 1000.0 * bpm / 60.0;
    double span = std::max<int>(1, st.dur) * L;
    if (st.tie > 0) for (int i = 1; i <= st.tie && pos + i < kMaxSteps; ++i) span += std::max<int>(1, tr.steps[(size_t) (pos + i)].dur) * L;
    double dur = st.tie > 0 ? span * 0.99 : (st.gate >= kGateLegato ? span * 1.02 : std::max(0.002, st.gate / 100.0 * span));
    const int voices = q.voice == 0 ? 1 : std::min<int>(st.numNotes, kMaxPolyNotes);
    const int reps = std::max<int>(1, st.repeats);
    const double rd = std::max<int>(1, st.repDur) * L / reps;
    const int stepAcc = accNext(st, tr, r, pos, st.accTarget != 2);
    for (int i = 0; i < reps; ++i) {
        if (i > 0 && st.repProb < 100 && (int) (rand32() % 100) >= st.repProb) continue;
        int shift = (st.rng > 0 && i > 0) ? (int) (rand32() % (uint32_t) (2 * st.rng + 1)) - st.rng : 0;
        shift += (i > 0 && st.accTarget != 1 && st.accAmt != 0) ? accNext(st, tr, r, pos, true) : stepAcc;
        double on = time + i * rd;
        double len = reps > 1 ? std::min(dur, rd * 0.9) : dur;
        struct V { int note; uint8_t ch; } vs[kMaxPolyNotes];
        for (int v = 0; v < voices; ++v)
            vs[v] = {std::clamp(degPitch(q, s, q.baseMidi(), st.notes[(size_t) v] + shift) + st.octave * 12 + transposeOffset[(size_t) s], 0, 127), q.channel};
        assignChMx(q, vs, 0, voices);
        for (int v = 0; v < voices; ++v) {
            push({on, 0, vs[v].ch, (uint8_t) vs[v].note, (uint8_t) std::clamp((int) st.velocity, 1, 127), (uint8_t) s, 0, 0});
            push({on + len, 1, vs[v].ch, (uint8_t) vs[v].note, 0, (uint8_t) s, 0, 0});
        }
    }
}

// ---------------------------------------------------------------- MIDI player mode (manual 6.12)
void Engine::playClip(int s, double grid, double L, double bpm) {
    auto& q = proj.seqs[(size_t) s];
    const auto& clip = q.pat().clip;
    auto& r = rt[(size_t) s][0];
    const int64_t c = r.counter;
    r.counter++; r.sinceStart++;
    if (clip.notes.empty() || clip.lengthTicks == 0) return;
    const uint32_t win = kClipPpqn / 4; // 1/16 = 24 ticks
    const uint32_t from = (uint32_t) ((c * (int64_t) win) % clip.lengthTicks);
    const uint32_t to = from + win;
    r.clipProgressUi = (double) from / clip.lengthTicks;
    push({grid, 2, 0, 0, 0, (uint8_t) s, 0, (int16_t) (from * 16 / clip.lengthTicks)});
    if (!seqAudible(s)) return;
    const double beatsPerTick = L / win;
    const double nudge = q.nudgeMs / 1000.0 * bpm / 60.0;
    // notes are sorted by tick: binary search the window start
    auto it = std::lower_bound(clip.notes.begin(), clip.notes.end(), from, [](const ClipNote& n, uint32_t t) { return n.tick < t; });
    for (; it != clip.notes.end() && it->tick < to; ++it) {
        double on = grid + (it->tick - from) * beatsPerTick + nudge;
        int note = std::clamp((int) it->note + q.perfTrans + transposeOffset[(size_t) s], 0, 127);
        push({on, 0, q.channel, (uint8_t) note, it->vel, (uint8_t) s, 0, 0});
        push({on + std::max<uint32_t>(1, it->len) * beatsPerTick * 0.999, 1, q.channel, (uint8_t) note, 0, (uint8_t) s, 0, 0});
    }
}


// ---------------------------------------------------------------- live playing (keyboard view / MIDI input)
void Engine::liveNote(int ch, int note, int vel, bool on) {
    if (liveQueue.size() < 500) liveQueue.push_back({(uint8_t) ((on ? 0x90 : 0x80) | (ch & 15)), (uint8_t) std::clamp(note, 0, 127), (uint8_t) (on ? std::clamp(vel, 1, 127) : 0)});
}
void Engine::previewNote(int ch, int note, int vel) {
    liveNote(ch, note, vel, true);
    if (previewOffs.size() < 60) previewOffs.push_back({ch, std::clamp(note, 0, 127), (int64_t) (sr * 0.2)});
}
void Engine::liveRaw(uint8_t status, uint8_t d1, uint8_t d2) { if (liveQueue.size() < 500) liveQueue.push_back({status, d1, d2}); }

void Engine::arpKey(int seq, int note, bool down) {
    if (seq < 0 || seq >= kNumSeqs) return;
    auto& a = arpRt[(size_t) seq];
    const auto& set = proj.seqs[(size_t) seq].arp;
    const bool sticky = set.hold || set.latch;
    auto has = [&](int n) { for (int i = 0; i < a.nHeld; ++i) if (a.held[i] == n) return i; return -1; };
    if (down) {
        if (set.hold && !set.latch && a.down == 0) a.nHeld = 0; // hold: a new chord replaces the held one
        a.down++;
        int i = has(note);
        if (set.latch && i >= 0 && a.down == 1 && a.nHeld > 0) { // latch: tapping a latched note removes it
            for (int k = i; k < a.nHeld - 1; ++k) a.held[k] = a.held[k + 1];
            a.nHeld--;
        } else if (i < 0 && a.nHeld < 32) a.held[a.nHeld++] = note;
        a.changed = true;
    } else {
        if (a.down > 0) a.down--;
        if (!sticky) {
            int i = has(note);
            if (i >= 0) { for (int k = i; k < a.nHeld - 1; ++k) a.held[k] = a.held[k + 1]; a.nHeld--; a.changed = true; }
        }
    }
}
void Engine::arpClear(int seq) { if (seq >= 0 && seq < kNumSeqs) { arpRt[(size_t) seq].nHeld = 0; arpRt[(size_t) seq].down = 0; arpRt[(size_t) seq].changed = true; } }
int Engine::arpNotes(int seq, int* out, int max) const {
    const auto& a = arpRt[(size_t) seq];
    int n = std::min(a.nHeld, max);
    for (int i = 0; i < n; ++i) out[i] = a.held[i];
    return n;
}

// Builds one cycle of the arpeggio (MIDI notes) for `type` from `notes` (press order).
int Engine::buildArpPattern(const ArpSettings& a, int type, const int* notesIn, int n, int* out, int seqIdx) {
    if (n <= 0) return 0;
    int base[32];
    n = std::min(n, 32);
    for (int i = 0; i < n; ++i) base[i] = notesIn[i];
    if (type != kArpOrder) std::sort(base, base + n);
    int list[256], m = 0;
    const int octs = std::abs((int) a.octV);
    for (int o = 0; o <= octs && m < 240; ++o)
        for (int i = 0; i < n && m < 256; ++i) list[m++] = base[i] + (a.octV >= 0 ? 12 * o : -12 * o);
    if (a.octV < 0 && type != kArpOrder) std::sort(list, list + m);
    int k = 0;
    auto put = [&](int v) { if (k < 512) out[k++] = v; };
    switch (type) {
        case kArpDown: for (int i = m - 1; i >= 0; --i) put(list[i]); break;
        case kArpUpDown: for (int i = 0; i < m; ++i) put(list[i]); for (int i = m - 2; i > 0; --i) put(list[i]); break;
        case kArp2Up2Down: for (int i = 0; i < m; ++i) { put(list[i]); put(list[i]); } for (int i = m - 2; i > 0; --i) { put(list[i]); put(list[i]); } break;
        case kArpAltIn: for (int lo = 0, hi = m - 1; lo <= hi; ++lo, --hi) { put(list[hi]); if (lo != hi) put(list[lo]); } break;
        case kArpAltOut: for (int lo = 0, hi = m - 1; lo <= hi; ++lo, --hi) { put(list[lo]); if (lo != hi) put(list[hi]); } break;
        case kArpAltInOut: {
            int tmp[256], t = 0;
            for (int lo = 0, hi = m - 1; lo <= hi; ++lo, --hi) { tmp[t++] = list[hi]; if (lo != hi) tmp[t++] = list[lo]; }
            for (int i = 0; i < t; ++i) put(tmp[i]);
            for (int i = t - 2; i > 0; --i) put(tmp[i]);
            break;
        }
        case kArpPinky: if (m == 1) put(list[0]); for (int i = 0; i < m - 1; ++i) { put(list[i]); put(list[m - 1]); } break;
        case kArpThumb: if (m == 1) put(list[0]); for (int i = 1; i < m; ++i) { put(list[0]); put(list[i]); } break;
        case kArpRandom: for (int i = 0; i < m; ++i) put(list[rand32() % (uint32_t) m]); break;
        case kArpRandOnce: {
            auto& ar = arpRt[(size_t) seqIdx];
            uint32_t sig = (uint32_t) m * 2654435761u;
            for (int i = 0; i < m; ++i) sig = sig * 31u + (uint32_t) list[i];
            if (sig != ar.sig || ar.nRand != m) {
                ar.sig = sig; ar.nRand = m;
                for (int i = 0; i < m; ++i) ar.randOrder[i] = list[i];
                for (int i = m - 1; i > 0; --i) { int j = (int) (rand32() % (uint32_t) (i + 1)); std::swap(ar.randOrder[i], ar.randOrder[j]); }
            }
            for (int i = 0; i < m; ++i) put(ar.randOrder[i]);
            break;
        }
        default: for (int i = 0; i < m; ++i) put(list[i]); break; // Up and Order
    }
    return k;
}

// Keyboard arpeggiators: one clock per sequencer at the arp division, locked to the timeline.
void Engine::runArps(double from, double to, double bpm) {
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& q = proj.seqs[(size_t) s];
        auto& a = arpRt[(size_t) s];
        const auto& set = q.arp;
        const bool roll = rollDiv[(size_t) s] > 0.0;
        if ((set.type == kArpOff && !roll) || a.nHeld == 0) { a.nextGrid = -1.0; continue; }
        const double La = roll ? rollDiv[(size_t) s] : divisionBeats(set.div);
        if (a.nextGrid < 0.0) { a.nextGrid = std::ceil(from / La - 1e-7) * La; a.tick = 0; a.grooveBase = 0; a.pos = 0; a.repLeft = 0; }
        if (a.changed) {
            a.changed = false;
            if (set.reset == 1 || set.reset == 3) a.pos = 0;
            if (set.reset >= 2) a.grooveBase = a.tick;
        }
        static thread_local int pat[512];
        int guard = 0;
        while (a.nextGrid < to && guard++ < 256) {
            const double g = a.nextGrid;
            a.nextGrid += La;
            const int64_t t = a.tick++;
            if (roll) { // roller: every held note retriggers at the roll interval
                if (!seqAudible(s)) continue;
                const uint8_t rch = q.mode == Mode::Multi ? q.pat().tracks[0].channel : q.channel;
                for (int i = 0; i < a.nHeld; ++i) {
                    int nn = std::clamp(a.held[i] + transposeOffset[(size_t) s], 0, 127);
                    push({g, 0, rch, (uint8_t) nn, q.kbVel, (uint8_t) s, 0, 0});
                    push({g + La * 0.5, 1, rch, (uint8_t) nn, 0, (uint8_t) s, 0, 0});
                }
                continue;
            }
            // euclidean gate: pulses spread over len steps, plus repeats after each pulse
            const int len = std::max<int>(1, set.len), puls = std::clamp<int>(set.puls, 1, len);
            const int ep = (int) (t % len);
            const bool pulse = ((ep * puls) % len) < puls;
            bool fire = pulse;
            if (pulse) a.repLeft = set.rept;
            else if (a.repLeft > 0) { a.repLeft--; fire = true; }
            if (!fire || !seqAudible(s)) continue;
            const int pn = buildArpPattern(set, set.type, a.held, a.nHeld, pat, s);
            if (pn <= 0) continue;
            const int note = std::clamp(pat[a.pos % pn] + transposeOffset[(size_t) s], 0, 127);
            a.pos = (a.pos + 1) % std::max(1, pn);
            const auto& gr = grooveTemplate(set.groove);
            const int gi = (int) (((t - a.grooveBase) % 16 + 16) % 16);
            double vel = q.kbVel * (1.0 + set.accent / 100.0 * gr.velocity[(size_t) gi]);
            double on = g + set.gtime / 100.0 * gr.timing[(size_t) gi] * La + q.nudgeMs / 1000.0 * bpm / 60.0;
            const uint8_t ch = q.mode == Mode::Multi ? q.pat().tracks[0].channel : q.channel;
            push({std::max(from, on), 0, ch, (uint8_t) note, (uint8_t) std::clamp((int) std::lround(vel), 1, 127), (uint8_t) s, 0, 0});
            push({std::max(from, on) + std::max(0.002, set.gate / 100.0 * La), 1, ch, (uint8_t) note, 0, (uint8_t) s, 0, 0});
        }
    }
}

void Engine::process(const Transport& host, int numSamples, const std::vector<MidiIn>& in, std::vector<MidiOut>& out) {
    // ---- MIDI input: transposition (manual 3.15 "Trnsp"), otherwise the selected sequencer's keyboard
    for (auto& m : in) {
        const int type = m.status & 0xF0;
        const bool on = type == 0x90 && m.data2 > 0, off = type == 0x80 || (type == 0x90 && m.data2 == 0);
        if (!on && !off) continue;
        int ch = m.status & 0x0F;
        bool used = false;
        for (int s = 0; s < kNumSeqs; ++s) {
            auto& q = proj.seqs[(size_t) s];
            if (q.transpose == 0) continue;
            int want = q.transpose == 1 ? q.channel : q.transpose - 2;
            if (want != ch) continue;
            used = true;
            if (on) transposeOffset[(size_t) s] = (int) m.data1 - q.baseMidi();
        }
        if (used) continue;
        const int ks = std::clamp(kbTarget, 0, kNumSeqs - 1);
        auto& kq = proj.seqs[(size_t) ks];
        if (kq.arp.type != kArpOff) arpKey(ks, m.data1, on);
        else liveNote(kq.channel, m.data1, m.data2, on);
    }
    // ---- live notes and previews (also while stopped)
    for (auto& e : liveQueue) {
        int t = e.st & 0xF0, ch = e.st & 15;
        if (t == 0x90) emitNoteOn(out, 0, ch, e.d1, e.d2);
        else if (t == 0x80) emitNoteOff(out, 0, ch, e.d1);
        else out.push_back({0, e.st, e.d1, e.d2});
    }
    liveQueue.clear();
    for (size_t i = 0; i < previewOffs.size();) {
        auto& p = previewOffs[i];
        p.samplesLeft -= numSamples;
        if (p.samplesLeft <= 0) { emitNoteOff(out, std::max(0, numSamples - 1), p.ch, p.note); previewOffs[i] = previewOffs.back(); previewOffs.pop_back(); }
        else ++i;
    }

    if (panicPending) {
        panicPending = false;
        stopAll(out, 0);
        for (int ch = 0; ch < 16; ++ch) out.push_back({0, (uint8_t) (0xB0 | ch), 123, 0});
        wasPlaying = false;
        expectedPpq = -1.0;
    }

    Transport tp = host;
    if (proj.internalClock) {
        tp.playing = intPlaying;
        tp.bpm = proj.bpm;
        tp.ppq = intPpq;
    }
    if (tp.bpm <= 0) tp.bpm = 120.0;
    bpmUi = tp.bpm;
    ppqUi = tp.ppq;

    const double ppqPerSample = tp.bpm / 60.0 / sr;
    const double blockEnd = tp.ppq + numSamples * ppqPerSample;

    if (!tp.playing) {
        if (wasPlaying) { stopAll(out, 0); for (auto& a : arpRt) a.nextGrid = -1.0; }
        wasPlaying = false;
        expectedPpq = -1.0;
        for (int s = 0; s < kNumSeqs; ++s)
            if (pendingPat[(size_t) s] >= 0) { proj.seqs[(size_t) s].curPattern = (uint8_t) pendingPat[(size_t) s]; pendingPat[(size_t) s] = -1; }
        // keyboard arps keep running on a free clock at the host tempo
        const double fEnd = freePpq + numSamples * ppqPerSample;
        runArps(freePpq, fEnd, tp.bpm);
        emitDue(freePpq, fEnd, ppqPerSample, numSamples, out);
        freePpq = fEnd;
        return;
    }

    // start or timeline jump (loop / locate)
    bool jumped = expectedPpq >= 0.0 && std::fabs(tp.ppq - expectedPpq) > std::max(0.01, numSamples * ppqPerSample * 0.5);
    if (!wasPlaying || jumped) {
        if (jumped) stopAll(out, 0);
        else { // leaving the free arp clock: release its notes and restart the arps on the timeline
            for (auto& e : queue) if (e.kind == 1) emitNoteOff(out, 0, e.ch, e.d1);
            queue.clear();
            for (auto& a : arpRt) a.nextGrid = -1.0;
        }
        startAll(tp.ppq);
    }
    wasPlaying = true;
    expectedPpq = blockEnd;
    if (proj.internalClock) intPpq = blockEnd;

    // pattern switches at bar boundaries
    {
        double nextBar = std::floor(tp.ppq / 4.0 + 1e-9) * 4.0 + 4.0;
        bool crosses = std::floor(tp.ppq / 4.0 + 1e-9) != std::floor(blockEnd / 4.0 + 1e-9) || std::fabs(std::fmod(tp.ppq, 4.0)) < 1e-9;
        (void) nextBar;
        if (crosses)
            for (int s = 0; s < kNumSeqs; ++s)
                if (pendingPat[(size_t) s] >= 0) { proj.seqs[(size_t) s].curPattern = (uint8_t) pendingPat[(size_t) s]; pendingPat[(size_t) s] = -1; }
    }

    // ---- schedule steps one step ahead (allows negative offsets / swing)
    for (int s = 0; s < kNumSeqs; ++s) {
        auto& q = proj.seqs[(size_t) s];
        if (resetPending[(size_t) s]) {
            resetPending[(size_t) s] = false;
            for (int t = 0; t < kNumTracks; ++t) { initTrack(s, t, tp.ppq); rt[(size_t) s][(size_t) t].counter = 0; }
        }
        if (!q.active) continue;
        const int nt = tracksFor(q);
        for (int t = 0; t < nt; ++t) {
            auto& r = rt[(size_t) s][(size_t) t];
            if (!r.init) initTrack(s, t, tp.ppq);
            int guard = 0;
            while (guard++ < 512) {
                auto& tr = q.pat().tracks[(size_t) t];
                double L = trackStepLength(s, t);
                if (r.nextGrid - L > blockEnd) break;
                if (q.mode == Mode::Matr) playMatriceal(s, t, r.nextGrid, L, tp.bpm);
                else if (q.mode == Mode::Domino) playDomino(s, r.nextGrid, L, tp.bpm);
                else if (q.mode == Mode::Saga) playSaga(s, r.nextGrid, L, tp.bpm);
                else if (q.mode == Mode::Player) playClip(s, r.nextGrid, L, tp.bpm);
                else scheduleStep(s, t, r.nextGrid, L, tp.bpm);
                double nextL = L;
                if (q.mode != Mode::Matr && tr.rndDiv > 0 && (int) (rand32() % 100) < tr.rndDiv) nextL = L * ((rand32() & 1) ? 2.0 : 0.5);
                r.nextGrid += nextL;
                r.nextIdx++;
            }
        }
    }

    runArps(tp.ppq, blockEnd, tp.bpm);
    emitDue(tp.ppq, blockEnd, ppqPerSample, numSamples, out);
}

void Engine::emitDue(double blockStart, double blockEnd, double ppqPerSample, int numSamples, std::vector<MidiOut>& out) {
    // ---- emit due events
    due.clear();
    size_t w = 0;
    for (size_t i = 0; i < queue.size(); ++i) {
        if (queue[i].ppq < blockEnd) due.push_back(queue[i]);
        else queue[w++] = queue[i];
    }
    queue.resize(w);
    std::stable_sort(due.begin(), due.end(), [](const Ev& a, const Ev& b) {
        if (a.ppq != b.ppq) return a.ppq < b.ppq;
        return (a.kind == 4 ? 2 : a.kind) > (b.kind == 4 ? 2 : b.kind); // note offs before note ons at the same time
    });
    for (auto& e : due) {
        int off = (int) std::floor((e.ppq - blockStart) / ppqPerSample);
        off = std::clamp(off, 0, std::max(0, numSamples - 1));
        switch (e.kind) {
            case 0: emitNoteOn(out, off, e.ch, e.d1, e.d2); break;
            case 1: emitNoteOff(out, off, e.ch, e.d1); break;
            case 2: {
                auto& r = rt[e.seq][e.track];
                r.phPpq.store(e.ppq, std::memory_order_relaxed);
                r.phLen.store((double) (e.ch | (uint32_t) e.d1 << 8 | (uint32_t) e.d2 << 16) / 960.0, std::memory_order_relaxed);
                r.playheadUi.store(e.pos, std::memory_order_relaxed);
                break;
            }
            case 4: rt[e.seq][0].domNoteUi.store(e.pos, std::memory_order_relaxed); break;
            default: out.push_back({off, e.ch, e.d1, e.d2}); break;
        }
    }
}

} // namespace tek

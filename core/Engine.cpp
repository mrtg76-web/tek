#include "Engine.h"
#include "Chords.h"
#include <algorithm>
#include <cmath>

namespace tek {

Engine::Engine(Project& p) : proj(p) {
    queue.reserve(16384);
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
    double L = divisionBeats(tr.div);
    double bend = 1.0 + q.timeBend / 1000.0;
    return L / bend;
}

double Engine::trackStepLength(int s, int t) const {
    const auto& q = proj.seqs[(size_t) s];
    if (q.mode == Mode::Matr)
        return divisionBeats(q.pat().mat.tracks[(size_t) t].lanes[kLaneTrig].div) / (1.0 + q.timeBend / 1000.0);
    if (q.mode == Mode::Domino) return stepLength(q, q.pat().tracks[1]);
    return stepLength(q, q.pat().tracks[(size_t) t]);
}

void Engine::initTrack(int s, int t, double ppq) {
    auto& q = proj.seqs[(size_t) s];
    auto& r = rt[(size_t) s][(size_t) t];
    double L = trackStepLength(s, t);
    r.matActive = 0.0; r.matHold = 0; r.matAccum = 0; r.matTicks = 0; r.lastNote = -1; r.domNote = -1; r.domNoteUi = -1;
    r.laneCnt.fill(-1); for (auto& lp : r.lanePos) lp.store(0);
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

    const int len = tr.length();
    int pos;
    if (tr.dir == Dir::Random) pos = tr.start + (int) (rand32() % (uint32_t) std::max(1, len));
    else if (tr.dir == Dir::Drunk) {
        if (r.pos < tr.start || r.pos > tr.end) pos = tr.start;
        else {
            uint32_t x = rand32() % 4;
            pos = r.pos + (x < 2 ? 1 : (x == 2 ? 0 : -1));
            if (pos > tr.end) pos = tr.start;
            if (pos < tr.start) pos = tr.end;
        }
    } else pos = positionFor(tr.dir, tr.start, tr.end, r.counter);
    r.pos = pos;

    push({grid, 2, 0, 0, 0, (uint8_t) s, (uint8_t) t, (int16_t) pos});

    const Step& st = tr.steps[(size_t) pos];
    const bool parity = (r.nextIdx % 2) != 0; // nextIdx already refers to this step here
    const int64_t counter = r.counter;
    r.counter++;
    r.sinceStart++;
    if (q.mode == Mode::Stoch) { playStoch(s, pos, grid, L, bpm, parity, counter); return; }
    if (!st.active || st.numNotes == 0) return;

    bool fire = evalTrig(st, r, len);
    if (!q.active || q.muted || st.muted) fire = false;
    if (q.mode == Mode::Multi) {
        uint8_t solo = soloMask[(size_t) s];
        if (tr.muted) fire = false;
        if (solo != 0 && !(solo & (1u << t))) fire = false;
    }
    if (!fire) return;

    // ---- timing
    double time = grid;
    int swing = (q.mode == Mode::Multi && tr.swing != 0) ? tr.swing : pat.swing;
    if (parity) time += (swing - 50) / 50.0 * L;
    time += st.offset / 100.0 * L;
    double vel = st.velocity;
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

    // ---- notes: build the voices of this step (pitch, channel, strum delay)
    struct Voice { int note; uint8_t ch; double delay; };
    const int nNotes = q.mode == Mode::Poly ? std::min<int>(st.numNotes, q.nVoices) : 1;
    const uint8_t ch = q.mode == Mode::Multi ? tr.channel : q.channel;
    auto buildVoices = [&](int degShift, Voice* out) -> int {
        int n = 0;
        if (q.mode == Mode::Chord) {
            int semis[kMaxChordNotes];
            int m = buildChord(q.scale, st.notes[0] + degShift, st.chordType, st.voicing, st.spread, semis);
            int base = q.baseMidi() + st.octave * 12 + transposeOffset[(size_t) s];
            if (q.bassMode > 0) {
                uint8_t bch = q.bassMode == 1 ? q.channel : (uint8_t) (q.bassMode - 2);
                out[n++] = {std::clamp(base + chordRootSemis(q.scale, st.notes[0] + degShift) - 12, 0, 127), bch, 0.0};
            }
            const double strumStep = std::abs(st.strum) / 100.0 * L / std::max(1, m);
            for (int k = 0; k < m; ++k) {
                uint8_t c = q.chMx ? (uint8_t) ((q.channel + k) % 16) : q.channel;
                double d = (st.strum >= 0 ? k : (m - 1 - k)) * strumStep;
                out[n++] = {std::clamp(base + semis[k], 0, 127), c, d};
            }
        } else {
            int basePitch = q.mode == Mode::Multi ? (int) tr.baseNote : q.baseMidi();
            for (int k = 0; k < nNotes; ++k) {
                int deg = st.notes[(size_t) k] + degShift;
                int note = std::clamp(basePitch + degreeToSemis(q.scale, deg) + st.octave * 12 + transposeOffset[(size_t) s], 0, 127);
                uint8_t c = (q.mode == Mode::Poly && q.chMx) ? (uint8_t) ((q.channel + k) % 16) : ch;
                out[n++] = {note, c, 0.0};
            }
        }
        return n;
    };

    auto addVoice = [&](double on, double length, const Voice& vc, double v) {
        int vv = std::clamp((int) std::lround(v), 1, 127);
        push({on + vc.delay, 0, vc.ch, (uint8_t) vc.note, (uint8_t) vv, (uint8_t) s, (uint8_t) t, 0});
        push({on + vc.delay + length, 1, vc.ch, (uint8_t) vc.note, 0, (uint8_t) s, (uint8_t) t, 0});
    };
    Voice voices[kMaxChordNotes + 2];
    const int nv = buildVoices(0, voices);

    if (st.repeats > 1) {
        const int n = st.repeats;
        const double total = st.repDur * L;
        const double f = 1.0 - st.accel / 100.0;
        double d0 = std::fabs(f - 1.0) < 1e-6 ? total / n : total * (1.0 - f) / (1.0 - std::pow(f, n));
        double tpos = time, d = d0;
        for (int i = 0; i < n; ++i) {
            if (i > 0 && st.repProb < 100 && (int) (rand32() % 100) >= st.repProb) { tpos += d; d *= f; continue; }
            double v = vel * (1.0 + st.ramp / 100.0 * (n > 1 ? (double) i / (n - 1) : 0.0));
            double length = std::max(0.002, std::min(dur, d * 0.9));
            if (i > 0 && st.rng > 0) {
                Voice rv[kMaxChordNotes + 2];
                int shift = (int) (rand32() % (uint32_t) (2 * st.rng + 1)) - st.rng;
                int rn = buildVoices(shift, rv);
                for (int k = 0; k < rn; ++k) addVoice(tpos, length, rv[k], v);
            } else
                for (int k = 0; k < nv; ++k) addVoice(tpos, length, voices[k], v);
            tpos += d; d *= f;
        }
    } else if (st.retrig > 1) {
        const int n = st.retrig;
        double span = st.tie > 0 ? dur : std::max(dur, L * 0.5);
        double d = span / n;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < nv; ++k) addVoice(time + i * d, std::max(0.002, d * 0.5), voices[k], vel);
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
    if (code == 0 || code == 99 || total == 0 || !q.active || q.muted) return;

    double time = grid;
    if (parity) time += (pat.swing - 50) / 50.0 * L;
    time += q.nudgeMs / 1000.0 * bpm / 60.0;
    const Step& prof = tr.steps[(size_t) pos]; // velocity / gate profiles drawn in the column editors
    const int vel = prof.velocity;
    const double gate = prof.gate >= kGateLegato ? 1.03 : prof.gate / 100.0;
    const int note = std::clamp(q.baseMidi() + sc.pitch[(size_t) pos] + transposeOffset[(size_t) s], 0, 127);
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
    const bool silent = !q.active || q.muted || mt.muted;
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

    if (mt.lanes[kLaneTrig].val[(size_t) lp[kLaneTrig]] == 0) return;
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

    const int midi = std::clamp((int) mt.baseNote + degreeToSemis(q.scale, note + r.matAccum) + 12 * oct + transposeOffset[(size_t) s], 0, 127);
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
        if (!ns.active || ns.numNotes == 0 || !q.active || q.muted || ns.muted) continue;
        if (ns.trig.type == TrigType::Prob && ns.trig.a < 100 && (int) (rand32() % 100) >= ns.trig.a) continue;
        double on = time + i * d + ns.offset / 100.0 * L;
        double dur;
        if (ns.tie > 0 || ts.tie > 0) dur = (std::max<int>(ns.tie, ts.tie) + 1) * L * 0.98;
        else if (ns.gate >= kGateLegato) dur = d * 1.03;
        else dur = std::max(0.002, ns.gate / 100.0 * std::min(d, L));
        const int voices = q.voice == 0 ? 1 : std::min<int>(ns.numNotes, 4);
        for (int k = 0; k < voices; ++k) {
            int note = std::clamp(q.baseMidi() + degreeToSemis(q.scale, ns.notes[(size_t) k]) + ns.octave * 12 + transposeOffset[(size_t) s], 0, 127);
            uint8_t ch = q.chMx ? (uint8_t) ((q.channel + k) % 16) : q.channel;
            push({on, 0, ch, (uint8_t) note, (uint8_t) std::clamp((int) ns.velocity, 1, 127), (uint8_t) s, 0, 0});
            push({on + dur, 1, ch, (uint8_t) note, 0, (uint8_t) s, 0, 0});
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
    if (!st.active || st.numNotes == 0 || !q.active || q.muted || st.muted) return;
    if (!evalTrig(st, r, n)) return;

    const bool parity = (c % 2) != 0;
    double time = grid + (parity ? (pat.swing - 50) / 50.0 * L : 0.0) + st.offset / 100.0 * L + q.nudgeMs / 1000.0 * bpm / 60.0;
    double span = std::max<int>(1, st.dur) * L;
    if (st.tie > 0) for (int i = 1; i <= st.tie && pos + i < kMaxSteps; ++i) span += std::max<int>(1, tr.steps[(size_t) (pos + i)].dur) * L;
    double dur = st.tie > 0 ? span * 0.99 : (st.gate >= kGateLegato ? span * 1.02 : std::max(0.002, st.gate / 100.0 * span));
    const int voices = q.voice == 0 ? 1 : std::min<int>(st.numNotes, kMaxPolyNotes);
    const int reps = std::max<int>(1, st.repeats);
    const double rd = std::max<int>(1, st.repDur) * L / reps;
    for (int i = 0; i < reps; ++i) {
        if (i > 0 && st.repProb < 100 && (int) (rand32() % 100) >= st.repProb) continue;
        int shift = (st.rng > 0) ? (int) (rand32() % (uint32_t) (2 * st.rng + 1)) - st.rng : 0;
        double on = time + i * rd;
        double len = reps > 1 ? std::min(dur, rd * 0.9) : dur;
        for (int v = 0; v < voices; ++v) {
            int note = std::clamp(q.baseMidi() + degreeToSemis(q.scale, st.notes[(size_t) v] + shift) + st.octave * 12 + transposeOffset[(size_t) s], 0, 127);
            uint8_t ch = q.chMx ? (uint8_t) ((q.channel + v) % 16) : q.channel;
            push({on, 0, ch, (uint8_t) note, (uint8_t) std::clamp((int) st.velocity, 1, 127), (uint8_t) s, 0, 0});
            push({on + len, 1, ch, (uint8_t) note, 0, (uint8_t) s, 0, 0});
        }
    }
}

void Engine::process(const Transport& host, int numSamples, const std::vector<MidiIn>& in, std::vector<MidiOut>& out) {
    // ---- MIDI input: transposition (manual 3.15 "Trnsp")
    for (auto& m : in) {
        if ((m.status & 0xF0) != 0x90 || m.data2 == 0) continue;
        int ch = m.status & 0x0F;
        for (int s = 0; s < kNumSeqs; ++s) {
            auto& q = proj.seqs[(size_t) s];
            if (q.transpose == 0) continue;
            int want = q.transpose == 1 ? q.channel : q.transpose - 2;
            if (want != ch) continue;
            transposeOffset[(size_t) s] = (int) m.data1 - q.baseMidi();
        }
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
        if (wasPlaying) stopAll(out, 0);
        wasPlaying = false;
        expectedPpq = -1.0;
        for (int s = 0; s < kNumSeqs; ++s)
            if (pendingPat[(size_t) s] >= 0) { proj.seqs[(size_t) s].curPattern = (uint8_t) pendingPat[(size_t) s]; pendingPat[(size_t) s] = -1; }
        return;
    }

    // start or timeline jump (loop / locate)
    bool jumped = expectedPpq >= 0.0 && std::fabs(tp.ppq - expectedPpq) > std::max(0.01, numSamples * ppqPerSample * 0.5);
    if (!wasPlaying || jumped) {
        if (jumped) stopAll(out, 0);
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
                else scheduleStep(s, t, r.nextGrid, L, tp.bpm);
                double nextL = L;
                if (q.mode != Mode::Matr && tr.rndDiv > 0 && (int) (rand32() % 100) < tr.rndDiv) nextL = L * ((rand32() & 1) ? 2.0 : 0.5);
                r.nextGrid += nextL;
                r.nextIdx++;
            }
        }
    }

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
        int off = (int) std::floor((e.ppq - tp.ppq) / ppqPerSample);
        off = std::clamp(off, 0, std::max(0, numSamples - 1));
        switch (e.kind) {
            case 0: emitNoteOn(out, off, e.ch, e.d1, e.d2); break;
            case 1: emitNoteOff(out, off, e.ch, e.d1); break;
            case 2: rt[e.seq][e.track].playheadUi.store(e.pos, std::memory_order_relaxed); break;
            case 4: rt[e.seq][0].domNoteUi.store(e.pos, std::memory_order_relaxed); break;
            default: out.push_back({off, e.ch, e.d1, e.d2}); break;
        }
    }
}

} // namespace tek

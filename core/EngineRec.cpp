// Recording (manual 4.12): live recording into the playing step grid, step recording, the MIDI capture buffer,
// count-in + metronome. Also global per-block chores (scale master, program changes).
#include "Engine.h"
#include "Scales.h"
#include <algorithm>
#include <cmath>

namespace tek {

void Engine::recInput(int note, int vel, bool on) {
    if (recQueue.size() < 256) recQueue.push_back({std::clamp(note, 0, 127), std::clamp(vel, 1, 127), on});
}

void Engine::recArm(int seq, int track, bool waitForNote) {
    recSeq = std::clamp(seq, 0, kNumSeqs - 1); recTrack = std::clamp(track, 0, kNumTracks - 1);
    recWait = waitForNote;
    recHeld.clear();
    recWritten.fill(-1e9);
    if (wasPlaying && !waitForNote) { recOn = true; recArmed = false; recStartPpq = lastPpq; } // punch in
    else { recArmed = true; recOn = false; }
}

void Engine::recStop() { recOn = false; recArmed = false; recWait = false; recHeld.clear(); }

int Engine::recState() const {
    if (recOn) return lastPpq < recStartPpq ? 2 : 3;
    return recArmed ? 1 : 0;
}

double Engine::countInLeft() const { return recOn ? std::max(0.0, recStartPpq - lastPpq) : 0.0; }

// ---------------------------------------------------------------- step recording
void Engine::stepRecStart(int seq, int track) {
    stepRec = true; stepSeq = seq; stepTrack = track; stepHeld = 0; stepFresh = true;
    stepCursor = proj.seqs[(size_t) seq].pat().tracks[(size_t) track].start;
}
void Engine::stepRecMove(int delta) { stepCursor = std::clamp(stepCursor + delta, 0, kMaxSteps - 1); stepFresh = true; }
void Engine::stepRecReset() { stepCursor = proj.seqs[(size_t) stepSeq].pat().tracks[(size_t) stepTrack].start; stepFresh = true; }
void Engine::stepRecRest() {
    auto& tr = proj.seqs[(size_t) stepSeq].pat().tracks[(size_t) stepTrack];
    tr.steps[(size_t) stepCursor].clear();
    if (stepCursor < kMaxSteps - 1) ++stepCursor;
    if (stepCursor > tr.end) tr.end = (uint8_t) stepCursor;
    stepFresh = true;
}
void Engine::stepRecTie() { // extend the previous note over this step
    auto& tr = proj.seqs[(size_t) stepSeq].pat().tracks[(size_t) stepTrack];
    for (int i = stepCursor - 1; i >= 0; --i) {
        auto& st = tr.steps[(size_t) i];
        if (!st.active) continue;
        if (i + st.tie + 1 == stepCursor) { st.tie = (uint8_t) (st.tie + 1); st.gate = kGateLegato; }
        break;
    }
    tr.steps[(size_t) stepCursor].clear();
    if (stepCursor < kMaxSteps - 1) ++stepCursor;
    if (stepCursor > tr.end) tr.end = (uint8_t) stepCursor;
    stepFresh = true;
}

// ---------------------------------------------------------------- helpers
int Engine::recTrackFor(int s, int note) const {
    const auto& q = proj.seqs[(size_t) s];
    if (q.mode != Mode::Multi) return 0;
    for (int t = 0; t < kNumTracks; ++t) if (q.pat().tracks[(size_t) t].baseNote == note) return t;
    return -1;
}

void Engine::writeRecNote(int s, int t, int idx, int note, int vel, bool add) {
    auto& q = proj.seqs[(size_t) s];
    auto& tr = q.pat().tracks[(size_t) t];
    auto& st = tr.steps[(size_t) idx];
    const bool multi = q.mode == Mode::Multi;
    const int base = multi ? tr.baseNote : q.baseMidi();
    int rel = note - base, oct = 0;
    if (!multi) { oct = (int) std::floor(rel / 12.0); rel -= oct * 12; }
    const int deg = semisToDegree(q.scale, rel);
    if (!st.active || !add) {
        Step fresh; fresh.velocity = tr.defVel; fresh.gate = tr.defGate; fresh.repeats = tr.defRepeats;
        st = fresh; st.active = true; st.numNotes = 0;
        st.octave = (int8_t) std::clamp(oct, -9, 9);
    }
    st.velocity = (uint8_t) vel;
    const int degAbs = deg + (oct - st.octave) * (int) scaleLibrary()[q.scale].intervals.size();
    if (q.mode == Mode::Poly) {
        if (st.hasNote(degAbs)) return;
        if (st.numNotes >= std::max<int>(1, q.nVoices)) return;
        st.notes[st.numNotes++] = (int8_t) degAbs;
    } else if (st.numNotes == 0 || !add) { st.numNotes = 1; st.notes[0] = (int8_t) degAbs; }
    else if (degAbs < st.notes[0]) st.notes[0] = (int8_t) degAbs; // mono / chord: chords record their lowest note
}

int Engine::captureToPattern(int seq, int track) {
    if (capture.empty()) return 0;
    auto& q = proj.seqs[(size_t) seq];
    auto& tr0 = q.pat().tracks[(size_t) track];
    const int start = tr0.start;
    int idx = start - 1, lastGroup = -1, steps = 0;
    for (auto& t : q.pat().tracks) if (q.mode == Mode::Multi) for (int i = start; i < kMaxSteps; ++i) t.steps[(size_t) i].clear();
    for (auto& c : capture) {
        if (c.group != lastGroup) { if (idx >= kMaxSteps - 1) break; ++idx; ++steps; lastGroup = c.group; if (q.mode != Mode::Multi) tr0.steps[(size_t) idx].clear(); }
        int t = track;
        if (q.mode == Mode::Multi) { t = recTrackFor(seq, c.note); if (t < 0) continue; }
        writeRecNote(seq, t, idx, c.note, c.vel, true);
    }
    if (q.mode == Mode::Multi) for (auto& t : q.pat().tracks) { t.start = (uint8_t) start; t.end = (uint8_t) std::max(start, idx); }
    else tr0.end = (uint8_t) std::max(start, idx);
    capture.clear();
    return steps;
}

// ---------------------------------------------------------------- per block
void Engine::applyGlobals(std::vector<MidiOut>& out) {
    if (proj.scaleMaster >= 0) {
        const auto& m = proj.seqs[(size_t) proj.scaleMaster];
        for (int s = 0; s < kNumSeqs; ++s) {
            auto& q = proj.seqs[(size_t) s];
            if (s == proj.scaleMaster || q.harm >= 0 || q.mode == Mode::Multi) continue;
            q.scale = m.scale; q.rootPc = m.rootPc;
        }
    }
    for (int s = 0; s < kNumSeqs; ++s) { // bank + program change when it is set or changes (manual 3.15)
        const auto& q = proj.seqs[(size_t) s];
        if (q.programChange == 0) { sentPc[(size_t) s] = -1; continue; }
        const int key = q.programChange * 256 + q.bank;
        if (key == sentPc[(size_t) s]) continue;
        sentPc[(size_t) s] = key;
        const uint8_t ch = q.mode == Mode::Multi ? q.pat().tracks[0].channel : q.channel;
        if (q.bank > 0) out.push_back({0, (uint8_t) (0xB0 | ch), 0, (uint8_t) (q.bank - 1)});
        out.push_back({0, (uint8_t) (0xC0 | ch), (uint8_t) (q.programChange - 1), 0});
    }
}

void Engine::processRecording(double ppq, double blockEnd, double bpm, bool playing, int numSamples, double ppqPerSample, std::vector<MidiOut>& out) {
    lastPpq = ppq;
    if (!playing && recOn) recStop(); // stopping the transport ends a recording
    (void) bpm;
    for (auto& e : recQueue) {
        // MIDI capture buffer: always on, chords = notes within 40 ms
        if (e.on) {
            if (sampleClock - lastCapSample > (int64_t) (sr * 0.04)) ++capGroup;
            lastCapSample = sampleClock;
            capture.push_back({e.note, e.vel, capGroup});
            if (capture.size() > 128) capture.erase(capture.begin());
        }
        // step recording
        if (stepRec) {
            auto& sq = proj.seqs[(size_t) stepSeq];
            int t = stepTrack;
            if (sq.mode == Mode::Multi) { int mt = recTrackFor(stepSeq, e.note); if (mt >= 0) t = mt; }
            auto& tr = sq.pat().tracks[(size_t) t];
            if (e.on) {
                writeRecNote(stepSeq, t, stepCursor, e.note, e.vel, !stepFresh);
                stepFresh = false;
                ++stepHeld;
            } else if (stepHeld > 0 && --stepHeld == 0) { // all keys released: next step
                if (stepCursor < kMaxSteps - 1) ++stepCursor;
                if (stepCursor > tr.end) for (auto& tt : sq.pat().tracks) tt.end = (uint8_t) std::max<int>(tt.end, stepCursor);
                stepFresh = true;
            }
            continue;
        }
        // live recording
        if (recWait && recArmed && e.on) { recArmed = false; recWait = false; recOn = playing; recStartPpq = ppq; }
        if (!recOn || !playing || ppq < recStartPpq) continue;
        const int s = recSeq;
        auto& q = proj.seqs[(size_t) s];
        if (q.mode != Mode::Mono && q.mode != Mode::Poly && q.mode != Mode::Chord && q.mode != Mode::Multi) continue;
        int t = q.mode == Mode::Multi ? recTrackFor(s, e.note) : 0;
        if (q.mode == Mode::Multi && t < 0) t = recTrack;
        if (e.on) {
            auto& r = rt[(size_t) s][(size_t) t];
            const int pos = r.playheadUi.load(std::memory_order_relaxed);
            const double len = r.phLen.load(std::memory_order_relaxed), st0 = r.phPpq.load(std::memory_order_relaxed);
            if (pos < 0 || len <= 0) continue;
            auto& tr = q.pat().tracks[(size_t) t];
            const int rs = tr.hasLoop() ? tr.loopStart : tr.start, re = tr.hasLoop() ? tr.loopEnd : tr.end;
            double frac = (ppq - st0) / len;
            int idx = pos;
            if (frac >= 0.5) { idx = pos + 1 > re ? rs : pos + 1; frac -= 1.0; } // nearest step
            const double keep = proj.recQt ? 1.0 - proj.recQtPct / 100.0 : 1.0;
            const bool chord = recWritten[(size_t) idx] > ppq - len * 0.75; // notes of one chord land on one step
            writeRecNote(s, t, idx, e.note, e.vel, chord || (proj.recOvdb && q.mode == Mode::Poly));
            auto& stp = tr.steps[(size_t) idx];
            stp.offset = (int8_t) std::clamp((int) std::lround(frac * keep * 100.0), -45, 45);
            recWritten[(size_t) idx] = ppq;
            recHeld.push_back({e.note, s, t, idx, ppq, len});
        } else {
            for (size_t i = 0; i < recHeld.size(); ++i) {
                if (recHeld[i].note != e.note) continue;
                auto h = recHeld[i];
                recHeld.erase(recHeld.begin() + (long) i);
                auto& st = proj.seqs[(size_t) h.s].pat().tracks[(size_t) h.t].steps[(size_t) h.idx];
                const double steps = (ppq - h.ppq) / h.len;
                if (steps > 1.25) { st.tie = (uint8_t) std::clamp((int) std::lround(steps) - 1, 1, kMaxSteps - 1); st.gate = kGateLegato; }
                else st.gate = (uint8_t) std::clamp((int) std::lround(steps * 100.0), 5, 99);
                break;
            }
        }
    }
    recQueue.clear();
    sampleClock += numSamples;

    // metronome on a MIDI channel during the count-in / recording
    if (playing && proj.metroCh > 0 && recOn && (proj.metroMode == 0 || ppq < recStartPpq)) {
        const uint8_t ch = (uint8_t) (proj.metroCh - 1);
        for (double b = std::ceil(ppq - 1e-9); b < blockEnd; b += 1.0) {
            const bool bar = std::fmod(b, 4.0) < 1e-6;
            const int off = std::clamp((int) ((b - ppq) / ppqPerSample), 0, std::max(0, numSamples - 1));
            emitNoteOn(out, off, ch, bar ? 76 : 77, bar ? 120 : 85);
            push({b + 0.1, 1, ch, (uint8_t) (bar ? 76 : 77), 0, 0, 0, 0});
        }
    }
}

} // namespace tek

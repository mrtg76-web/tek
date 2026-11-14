// Arranger playback (manual 8.x): each lane walks its slots (pattern + repeats + next rule); one repeat cycle lasts
// the launch quantization (or the pattern length for P.End). A master lane drives every lane's column.
#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace tek {

double Engine::arrCycleBeats(int lane) const {
    const double q = arrQuantBeats(proj.arrQuant);
    if (q > 0.0) return q;
    const auto& sq = proj.seqs[(size_t) lane];
    const int slot = arrLanes[(size_t) lane].slot;
    if (slot < 0 || proj.song().lanes[(size_t) lane][(size_t) slot].pat < 0) return 4.0; // blank: one bar
    const auto& tr = sq.pat().tracks[0];
    return std::max(0.25, (tr.end - tr.start + 1) * stepLength(sq, tr));
}

double Engine::arrProgress() const {
    if (!arrRunning) return 0.0;
    const int l = proj.song().master >= 0 ? proj.song().master : 0;
    const auto& a = arrLanes[(size_t) l];
    if (a.cycleEnd <= a.cycleStart) return 0.0;
    return std::clamp((lastPpq - a.cycleStart) / (a.cycleEnd - a.cycleStart), 0.0, 1.0);
}

void Engine::arrCue(int lane, int slot) {
    if (lane < 0 || lane >= kNumSeqs || slot < 0 || slot >= kArrSlots) return;
    const int m = proj.song().master;
    if (m >= 0) { arrCueColumn(slot); return; } // the master lane moves every lane
    arrLanes[(size_t) lane].cue = slot;
}

void Engine::arrCueColumn(int slot) { for (auto& a : arrLanes) a.cue = slot; }

int Engine::arrNextSlot(int lane, int from, int rule) {
    const auto& cells = proj.song().lanes[(size_t) lane];
    std::vector<int> ok;
    for (int c = 0; c < kArrSlots; ++c) if (cells[(size_t) c].playable()) ok.push_back(c);
    if (ok.empty()) return -1;
    switch (rule) {
        case 1: { for (int c = from - 1; c >= 0; --c) if (cells[(size_t) c].playable()) return c; return ok.back(); }
        case 2: return ok.front();
        case 3: return ok[(size_t) (rand32() % ok.size())];
        case 4: { if (ok.size() == 1) return ok[0]; int c; do c = ok[(size_t) (rand32() % ok.size())]; while (c == from); return c; }
        default: { for (int c = from + 1; c < kArrSlots; ++c) if (cells[(size_t) c].playable()) return c; return ok.front(); } // song loops
    }
}

void Engine::arrGoto(int lane, int slot, std::vector<MidiOut>& out) {
    auto& a = arrLanes[(size_t) lane];
    auto& q = proj.seqs[(size_t) lane];
    a.slot = slot; a.cue = -1;
    if (slot < 0) { arrSilent |= (uint8_t) (1u << lane); return; }
    const auto& c = proj.song().lanes[(size_t) lane][(size_t) slot];
    a.repLeft = c.rept >= kArrInf ? 999999 : std::max<int>(1, c.rept);
    const uint8_t ch = q.mode == Mode::Multi ? q.pat().tracks[0].channel : q.channel;
    if (c.bank >= 2) out.push_back({0, (uint8_t) (0xB0 | ch), 0, (uint8_t) (c.bank - 2)});
    if (c.pc >= 2) out.push_back({0, (uint8_t) (0xC0 | ch), (uint8_t) (c.pc - 2), 0});
    if (c.pat >= 0) {
        q.curPattern = (uint8_t) c.pat;
        pendingPat[(size_t) lane] = -1;
        resetPending[(size_t) lane] = true;
        arrSilent &= (uint8_t) ~(1u << lane);
    } else arrSilent |= (uint8_t) (1u << lane); // blank: hold the position silently
}

void Engine::arrStart(double ppq, std::vector<MidiOut>& out) {
    arrRunning = true; arrStartPending = false; arrSilent = 0;
    const int m = proj.song().master;
    const int col = m >= 0 ? arrNextSlot(m, -1, 0) : -1;
    for (int l = 0; l < kNumSeqs; ++l) {
        auto& a = arrLanes[(size_t) l];
        int slot = m >= 0 ? (col >= 0 && proj.song().lanes[(size_t) l][(size_t) col].playable() ? col : -1) : arrNextSlot(l, -1, 0);
        if (m >= 0 && slot < 0) { a.slot = col; a.cue = -1; a.repLeft = 0; arrSilent |= (uint8_t) (1u << l); }
        else arrGoto(l, slot, out);
        a.cycleStart = ppq;
        a.cycleEnd = ppq + arrCycleBeats(l);
    }
}

void Engine::arrProcess(double ppq, double blockEnd, bool playing, std::vector<MidiOut>& out) {
    if (!playing) {
        if (arrRunning || arrStartPending) proj.arrOn = false; // the arranger disarms when stopped (manual 8.5)
        arrRunning = arrStartPending = false; arrSilent = 0;
        for (auto& a : arrLanes) a = ArrLane();
        return;
    }
    if (!proj.arrOn) { if (arrRunning) { arrRunning = false; arrSilent = 0; for (auto& a : arrLanes) a = ArrLane(); } return; }
    if (!arrRunning) { // armed while playing: start on the next bar
        const double b = std::ceil(ppq / 4.0 - 1e-9) * 4.0;
        if (b < blockEnd) arrStart(b, out);
        return;
    }
    const int m = proj.song().master;
    for (int l = 0; l < kNumSeqs; ++l) {
        if (m >= 0 && l != m) continue; // followers move with the master
        auto& a = arrLanes[(size_t) l];
        int guard = 0;
        while (a.cycleEnd < blockEnd && guard++ < 64) {
            const double at = a.cycleEnd;
            int target = a.slot;
            if (a.cue >= 0) target = a.cue;
            else if (a.slot >= 0 && --a.repLeft <= 0) {
                const auto& c = proj.song().lanes[(size_t) l][(size_t) a.slot];
                target = arrNextSlot(l, a.slot, c.next);
            }
            if (target != a.slot || a.cue >= 0 || (a.slot >= 0 && a.repLeft <= 0)) {
                if (m >= 0) { // the whole column follows
                    for (int k = 0; k < kNumSeqs; ++k) {
                        auto& b = arrLanes[(size_t) k];
                        if (target >= 0 && proj.song().lanes[(size_t) k][(size_t) target].playable()) arrGoto(k, target, out);
                        else { b.slot = target; b.cue = -1; arrSilent |= (uint8_t) (1u << k); }
                        b.cycleStart = at;
                    }
                    if (target >= 0 && !proj.song().lanes[(size_t) m][(size_t) target].playable()) a.repLeft = 1;
                } else arrGoto(l, target, out);
            }
            a.cycleStart = at;
            a.cycleEnd = at + arrCycleBeats(l);
            if (m >= 0) for (int k = 0; k < kNumSeqs; ++k) if (k != m) { arrLanes[(size_t) k].cycleStart = a.cycleStart; arrLanes[(size_t) k].cycleEnd = a.cycleEnd; }
        }
    }
}

} // namespace tek

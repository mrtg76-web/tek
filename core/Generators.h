#pragma once
// Pattern generators (manual 4.10, 4.11): destructive edits of one track, framework free and deterministic
// for a given seed so they can be unit tested.
#include "Model.h"

namespace tek {

// Euclidean generator: writes the track's start..end range from q.gen (two layers + logic, density, notes).
// Steps with a Fixed or Locked trig are kept. Multitrack tracks get triggers only.
void euclidGenerate(Sequencer& q, int track);
// The combined rhythm only (exposed for tests): out[i] for i in 0..n-1, accent[i] for the Accnt logic.
void euclidRhythm(const GenSettings& g, int n, bool* out, bool* accent);

// Random generator: den >= 1 generates a new pattern; 0 Human, -1 Order, -2 Pitch, -3 Flip modify the existing one.
void randomGenerate(Sequencer& q, int track, uint32_t seed);

} // namespace tek

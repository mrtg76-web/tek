#pragma once
// Diatonic chord construction for Chord mode (manual 6.7): each grid row is a scale degree and plays
// the chord built on that degree. Type, voicing (inversions / open), spread and strum are per step.
#include <string>
#include <vector>

namespace tek {

constexpr int kNumChordTypes = 12;
constexpr int kNumVoicings = 6;
constexpr int kMaxChordNotes = 8;

const char* chordTypeLabel(int type);
const char* voicingLabel(int voicing);

// Writes semitone offsets (relative to the sequencer root note) of the chord built on `rootDegree`,
// sorted low to high, into out[0..kMaxChordNotes-1] and returns the count (bass note excluded).
// Allocation free: safe to call on the audio thread.
int buildChord(int scale, int rootDegree, int type, int voicing, int spread, int* out);

// Semitone offset of the chord root (used for the optional bass note).
int chordRootSemis(int scale, int rootDegree);

// e.g. "G2 minor 7" (baseMidi = MIDI note of degree 0)
std::string chordName(int scale, int baseMidi, int rootDegree, int type);

} // namespace tek

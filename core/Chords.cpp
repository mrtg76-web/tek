#include "Chords.h"
#include "Scales.h"
#include <algorithm>

namespace tek {

namespace {
// Chord tones as scale-degree offsets from the root (diatonic stacking).
struct TypeDef { const char* label; const char* suffix; std::vector<int> degrees; bool sus; };
const TypeDef kTypes[kNumChordTypes] = {
    {"Triad", "",     {0, 2, 4},              false},
    {"7th",   " 7",   {0, 2, 4, 6},           false},
    {"9th",   " 9",   {0, 2, 4, 6, 8},        false},
    {"11th",  " 11",  {0, 2, 4, 6, 8, 10},    false},
    {"13th",  " 13",  {0, 2, 4, 6, 8, 10, 12},false},
    {"6th",   " 6",   {0, 2, 4, 5},           false},
    {"6/9",   " 6/9", {0, 2, 4, 5, 8},        false},
    {"add9",  " add9",{0, 2, 4, 8},           false},
    {"sus2",  "sus2", {0, 1, 4},              true},
    {"sus4",  "sus4", {0, 3, 4},              true},
    {"7sus4", "7sus4",{0, 3, 4, 6},           true},
    {"Power", "5",    {0, 4},                 true},
};
const char* kVoicings[kNumVoicings] = {"No Inv", "I Invr", "II Invr", "Open", "I Open", "II Open"};

bool diatonic(int scale) { return scaleLibrary()[(size_t) scale].intervals.size() == 7; }

// semitone offset of a chord tone: diatonic scales stack within the scale, others use a major shape
int toneSemis(int scale, int rootDegree, int degOffset) {
    if (diatonic(scale)) return degreeToSemis(scale, rootDegree + degOffset);
    return degreeToSemis(scale, rootDegree) + degreeToSemis(1 /* major */, degOffset);
}

void invert(int* v, int n) {
    if (n <= 0) return;
    std::sort(v, v + n);
    v[0] += 12;
    std::sort(v, v + n);
}
} // namespace

const char* chordTypeLabel(int t) { return kTypes[std::clamp(t, 0, kNumChordTypes - 1)].label; }
const char* voicingLabel(int v) { return kVoicings[std::clamp(v, 0, kNumVoicings - 1)]; }

int chordRootSemis(int scale, int rootDegree) { return degreeToSemis(scale, rootDegree); }

int buildChord(int scale, int rootDegree, int type, int voicing, int spread, int* out) {
    const auto& def = kTypes[std::clamp(type, 0, kNumChordTypes - 1)];
    int v[16];
    int n = 0;
    for (int d : def.degrees) v[n++] = toneSemis(scale, rootDegree, d);
    std::sort(v, v + n);
    n = (int) (std::unique(v, v + n) - v);

    voicing = std::clamp(voicing, 0, kNumVoicings - 1);
    int inversions = voicing == 1 || voicing == 4 ? 1 : (voicing == 2 || voicing == 5 ? 2 : 0);
    for (int i = 0; i < inversions; ++i) invert(v, n);
    if (voicing >= 3 && n >= 3) { v[1] += 12; std::sort(v, v + n); } // open: second tone up an octave

    // spread: add the upper octave of the lowest `spread` tones
    const int base = n;
    for (int i = 0; i < spread && i < base && n < kMaxChordNotes; ++i) {
        int x = v[i] + 12;
        if (std::find(v, v + n, x) == v + n) v[n++] = x;
    }
    std::sort(v, v + n);
    if (n > kMaxChordNotes) n = kMaxChordNotes;
    for (int i = 0; i < n; ++i) out[i] = v[i];
    return n;
}

std::string chordName(int scale, int baseMidi, int rootDegree, int type) {
    const auto& def = kTypes[std::clamp(type, 0, kNumChordTypes - 1)];
    int root = degreeToSemis(scale, rootDegree);
    std::string name = noteName(baseMidi + root);
    if (def.sus) return name + " " + def.suffix;
    int third = toneSemis(scale, rootDegree, 2) - root;
    int fifth = toneSemis(scale, rootDegree, 4) - root;
    const char* q = "major";
    if (third == 3 && fifth == 7) q = "minor";
    else if (third == 3 && fifth == 6) q = "dim";
    else if (third == 4 && fifth == 8) q = "aug";
    return name + " " + q + def.suffix;
}

} // namespace tek

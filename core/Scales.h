#pragma once
// Scale library (manual section 3.14). Intervals are semitones from the root.
#include <array>
#include <vector>
#include <string>
#include <cstdlib>

namespace tek {

struct ScaleDef {
    const char* label;          // short label shown on the display (max ~10 chars)
    std::vector<int> intervals; // ascending semitone offsets within one octave, first = 0
};

inline const std::vector<ScaleDef>& scaleLibrary() {
    static const std::vector<ScaleDef> lib = {
        {"CHROMATIC", {0,1,2,3,4,5,6,7,8,9,10,11}},
        {"MAJOR",     {0,2,4,5,7,9,11}},
        {"MINOR",     {0,2,3,5,7,8,10}},
        {"DORIAN",    {0,2,3,5,7,9,10}},
        {"PHRYGIAN",  {0,1,3,5,7,8,10}},
        {"LYDIAN",    {0,2,4,6,7,9,11}},
        {"MIXOLYDIAN",{0,2,4,5,7,9,10}},
        {"LOCRIAN",   {0,1,3,5,6,8,10}},
        {"PENTAMAJ",  {0,2,4,7,9}},
        {"PENTAMIN",  {0,3,5,7,10}},
        {"HARM MIN",  {0,2,3,5,7,8,11}},
        {"MELODIC",   {0,2,3,5,7,9,11}},
        {"BLUES",     {0,3,5,6,7,10}},
        {"ARABIAN",   {0,1,4,5,7,8,11}},
        {"ROMANIAN",  {0,2,3,6,7,9,10}},
        {"BALINESE",  {0,1,3,7,8}},
        {"HUNGAR 1",  {0,2,3,6,7,8,11}},
        {"HUNGAR 2",  {0,3,4,6,7,9,10}},
        {"ORIENTAL",  {0,1,4,5,6,9,10}},
        {"RAGATODI",  {0,1,3,6,7,8,11}},
        {"CHINESE",   {0,4,6,7,11}},
        {"JAPAN 1",   {0,1,5,7,8}},
        {"JAPAN 2",   {0,2,5,7,8}},
        {"PERSIAN",   {0,1,4,5,6,8,11}},
        {"DIMINISH",  {0,2,3,5,6,8,9,11}},
        {"PHRYGDOM",  {0,1,4,5,7,8,10}},
        {"BYZANTINE", {0,1,4,5,7,8,11}},
        {"SpainPHRY", {0,1,3,4,5,7,8,10}},
        {"NeapolMIN", {0,1,3,5,7,8,11}},
        {"NeapolMAJ", {0,1,3,5,7,9,11}},
        {"UKRANDORI", {0,2,3,6,7,9,10}},
    };
    return lib;
}

inline int numScales() { return (int) scaleLibrary().size(); }

// Converts a scale degree (can be negative / span octaves) to semitones from the root.
inline int degreeToSemis(int scaleIndex, int degree) {
    const auto& lib = scaleLibrary();
    if (scaleIndex < 0 || scaleIndex >= (int) lib.size()) scaleIndex = 0;
    const auto& iv = lib[(size_t) scaleIndex].intervals;
    const int n = (int) iv.size();
    int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    int idx = degree - oct * n;
    return oct * 12 + iv[(size_t) idx];
}

// Nearest scale degree for a semitone offset from root (used for quantising / transposing).
inline int semisToDegree(int scaleIndex, int semis) {
    // search a window of degrees
    int best = 0, bestDist = 1 << 30;
    const auto& iv = scaleLibrary()[(size_t) (scaleIndex < 0 || scaleIndex >= numScales() ? 0 : scaleIndex)].intervals;
    int n = (int) iv.size();
    int approx = semis * n / 12;
    for (int d = approx - n - 1; d <= approx + n + 1; ++d) {
        int dist = std::abs(degreeToSemis(scaleIndex, d) - semis);
        if (dist < bestDist) { bestDist = dist; best = d; }
    }
    return best;
}

inline std::string noteName(int midi) {
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    // Ableton convention: C3 = 60, so C-2 = 0
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 2);
}

inline const char* pitchClassName(int pc) {
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return names[((pc % 12) + 12) % 12];
}

} // namespace tek

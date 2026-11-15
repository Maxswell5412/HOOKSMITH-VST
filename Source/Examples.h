#pragma once

#include "Music.h"
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// LOAD EXAMPLE: turn a MIDI file or an audio loop (WAV/AIFF) into notes HOOKSMITH can copy
// and write new versions from.
//   - pitchTrack: one note at a time (an 808, a bass line, a sung or played melody)
//   - chordTrack: chords from a loop (piano, guitar, pads)
// No JUCE in here: plain sample arrays in, notes in beats out.
// -----------------------------------------------------------------------------
namespace hs
{
// BPM written in a file name ("Dark 808 140bpm F#m", "loop_92_BPM"). 0 if none.
double bpmFromName (const std::string& name);

// The loop's length in beats: whole bars, and a sensible tempo. Returns the tempo it settled on.
double settleTempo (double seconds, double bpmHint, double& lengthBeats);

// One note at a time. bassRange = look down to 25 Hz (808s), otherwise 70 Hz and up.
std::vector<NoteEv> pitchTrack (const float* mono, int numSamples, double sampleRate, double bpm, bool bassRange);

// Chords, one per beat at most, as simple voicings (root + chord tones) ready for the chord importer.
std::vector<NoteEv> chordTrack (const float* mono, int numSamples, double sampleRate, double bpm);

// Key of a set of notes (Krumhansl-Schmuckler on how long each pitch class sounds).
int estimateKey (const std::vector<NoteEv>& notes, bool& minor);
} // namespace hs

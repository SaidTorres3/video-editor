#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct AudioAlignment
{
    int streamIndex = -1;
    double offsetSeconds = 0.0; // Added to source timestamps; positive delays audio.
    double confidence = 0.0;
    bool matched = false;
};

using MediaProgress = std::function<void(int)>;

// Fixed-offset matching of shared audio. Does not estimate recording-clock drift.
AudioAlignment MatchAudio(const std::vector<float> &reference, const std::vector<float> &candidate,
                          int sampleRate, double maxOffsetSeconds = 30.0);
std::vector<AudioAlignment> AnalyzeAudioAlignment(const std::wstring &input, int referenceStream,
                                                  std::atomic<bool> &cancel,
                                                  const MediaProgress &progress);

class OpenFxEffect;

// Writes a new Matroska file, never overwrites an existing destination. Uses a
// sibling staging file and publishes only after the muxer has finished.
void ProcessMediaCopy(const std::wstring &input, const std::wstring &output,
                      const std::map<int, double> &audioOffsets, OpenFxEffect *effect,
                      std::atomic<bool> &cancel, const MediaProgress &progress);

#pragma once

#include "vwave/Audio.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Oscilloscope.h"
#include "vwave/Synth.h"
#include "vwave/Timeline.h"

#include <nlohmann/json.hpp>

#include <string>

namespace vwave {

nlohmann::json makeImageTrackRecipe(
    const std::string& imagePath, const ImageSpectrogramOptions& options);
nlohmann::json makeScopeTrackRecipe(const ScopePath& path,
                                    const ScopeRenderOptions& options);
nlohmann::json makeSynthTrackRecipe(const std::string& wavetablePath,
                                   double midiNote, double durationSeconds,
                                   const SynthNoteOptions& options);

bool renderTrackRecipe(const nlohmann::json& recipe, Audio& out,
                       std::string* error = nullptr);
bool regenerateTrack(const Timeline& timeline, int trackIndex, Audio& out,
                     std::string* error = nullptr);

} // namespace vwave

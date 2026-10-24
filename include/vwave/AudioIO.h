#pragma once

#include "vwave/Audio.h"

#include <cstdint>
#include <string>

namespace vwave {

enum class AudioFormat {
    Auto,
    Wav,
    Aiff,
    Flac,
    Ogg
};

enum class BitDepth {
    Int16,
    Int24,
    Float32
};

struct AudioExportOptions {
    AudioFormat format = AudioFormat::Auto;
    BitDepth bitDepth = BitDepth::Float32;
};

struct AudioFileInfo {
    int sampleRate = 0;
    int channels = 0;
    int64_t frames = 0;
    std::string format;
};

bool readAudio(const std::string& path, Audio& out,
               std::string* error = nullptr);
bool writeAudio(const std::string& path, const Audio& audio,
                const AudioExportOptions& options = {},
                std::string* error = nullptr);
bool probeAudio(const std::string& path, AudioFileInfo& info,
                std::string* error = nullptr);

const char* audioFormatName(AudioFormat format);

} // namespace vwave

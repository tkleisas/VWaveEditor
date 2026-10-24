#include "vwave/AudioIO.h"

#include <sndfile.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <vector>

namespace vwave {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

AudioFormat formatFromPath(const std::string& path) {
    const std::string extension = lowerExtension(path);
    if (extension == ".wav" || extension == ".wave") return AudioFormat::Wav;
    if (extension == ".aif" || extension == ".aiff") return AudioFormat::Aiff;
    if (extension == ".flac") return AudioFormat::Flac;
    if (extension == ".ogg" || extension == ".oga") return AudioFormat::Ogg;
    return AudioFormat::Auto;
}

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool isSupportedUpload(int format) {
    return (format & SF_FORMAT_TYPEMASK) != 0 &&
           (format & SF_FORMAT_SUBMASK) != 0;
}

int libsndfileFormat(AudioFormat format, BitDepth depth) {
    int major = 0;
    switch (format) {
    case AudioFormat::Wav: major = SF_FORMAT_WAV; break;
    case AudioFormat::Aiff: major = SF_FORMAT_AIFF; break;
    case AudioFormat::Flac: major = SF_FORMAT_FLAC; break;
    case AudioFormat::Ogg: return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    case AudioFormat::Auto: return 0;
    }

    if (format == AudioFormat::Flac) {
        return major | (depth == BitDepth::Int16 ? SF_FORMAT_PCM_16
                                                 : SF_FORMAT_PCM_24);
    }
    if (format == AudioFormat::Aiff) {
        if (depth == BitDepth::Float32) return major | SF_FORMAT_FLOAT;
        return major | (depth == BitDepth::Int16 ? SF_FORMAT_PCM_16
                                                 : SF_FORMAT_PCM_24);
    }
    switch (depth) {
    case BitDepth::Int16: return major | SF_FORMAT_PCM_16;
    case BitDepth::Int24: return major | SF_FORMAT_PCM_24;
    case BitDepth::Float32: return major | SF_FORMAT_FLOAT;
    }
    return major | SF_FORMAT_PCM_24;
}

} // namespace

const char* audioFormatName(AudioFormat format) {
    switch (format) {
    case AudioFormat::Auto: return "auto";
    case AudioFormat::Wav: return "wav";
    case AudioFormat::Aiff: return "aiff";
    case AudioFormat::Flac: return "flac";
    case AudioFormat::Ogg: return "ogg";
    }
    return "unknown";
}

bool readAudio(const std::string& path, Audio& out, std::string* error) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file) {
        setError(error, std::string("cannot open audio file: ") + sf_strerror(nullptr));
        return false;
    }
    if (info.channels <= 0 || info.samplerate <= 0 || info.frames < 0) {
        setError(error, "invalid audio file metadata");
        sf_close(file);
        return false;
    }

    const sf_count_t frames = info.frames;
    std::vector<float> interleaved(static_cast<size_t>(frames) *
                                       static_cast<size_t>(info.channels),
                                   0.0f);
    sf_count_t read = 0;
    if (frames > 0) {
        read = sf_readf_float(file, interleaved.data(), frames);
        if (read < 0) {
            setError(error, std::string("cannot read audio samples: ") + sf_strerror(file));
            sf_close(file);
            return false;
        }
    }
    sf_close(file);

    if (read != frames) {
        interleaved.resize(static_cast<size_t>(read) *
                           static_cast<size_t>(info.channels));
    }
    out = Audio::fromInterleaved(interleaved.data(), info.channels, read,
                                 info.samplerate);
    return true;
}

bool writeAudio(const std::string& path, const Audio& audio,
                const AudioExportOptions& options, std::string* error) {
    if (audio.empty()) {
        setError(error, "cannot write an empty audio buffer");
        return false;
    }
    AudioFormat format = options.format;
    if (format == AudioFormat::Auto) format = formatFromPath(path);
    if (format == AudioFormat::Auto) {
        setError(error, "cannot infer audio format from output path");
        return false;
    }
    if (format == AudioFormat::Ogg && audio.numChannels() > 2) {
        setError(error, "Ogg Vorbis export supports at most two channels");
        return false;
    }

    SF_INFO info{};
    info.samplerate = audio.sampleRate();
    info.channels = audio.numChannels();
    info.frames = audio.numFrames();
    info.format = libsndfileFormat(format, options.bitDepth);
    if (!isSupportedUpload(info.format)) {
        setError(error, "unsupported audio format");
        return false;
    }

    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!file) {
        setError(error, std::string("cannot create audio file: ") + sf_strerror(nullptr));
        return false;
    }

    const std::vector<float> interleaved = audio.toInterleaved();
    sf_count_t written = 0;
    if (!interleaved.empty()) {
        written = sf_writef_float(file, interleaved.data(), audio.numFrames());
    }
    const int closeStatus = sf_close(file);
    if (written != audio.numFrames()) {
        setError(error, "short write while exporting audio");
        return false;
    }
    if (closeStatus != 0) {
        setError(error, std::string("error closing audio file: ") +
                            sf_error_number(closeStatus));
        return false;
    }
    return true;
}

bool probeAudio(const std::string& path, AudioFileInfo& info,
                std::string* error) {
    SF_INFO sfInfo{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        setError(error, std::string("cannot open audio file: ") + sf_strerror(nullptr));
        return false;
    }
    info.sampleRate = sfInfo.samplerate;
    info.channels = sfInfo.channels;
    info.frames = sfInfo.frames;
    switch (sfInfo.format & SF_FORMAT_TYPEMASK) {
    case SF_FORMAT_WAV: info.format = "wav"; break;
    case SF_FORMAT_AIFF: info.format = "aiff"; break;
    case SF_FORMAT_FLAC: info.format = "flac"; break;
    case SF_FORMAT_OGG: info.format = "ogg"; break;
    default: info.format = "unknown"; break;
    }
    sf_close(file);
    return true;
}

} // namespace vwave

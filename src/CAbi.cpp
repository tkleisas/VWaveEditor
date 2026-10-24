#include "vwave/vwave_c.h"

#include "vwave/Document.h"
#include "vwave/Processing.h"
#include "vwave/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>

struct VWSession {
    vwave::Session session;
};

namespace {

void writeError(char* buffer, int32_t size, const std::string& message) {
    if (!buffer || size <= 0) return;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(size - 1), message.size());
    std::memcpy(buffer, message.data(), count);
    buffer[count] = '\0';
}

void writeResult(char* buffer, int32_t size, const std::string& text) {
    if (!buffer || size <= 0) return;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(size - 1), text.size());
    std::memcpy(buffer, text.data(), count);
    buffer[count] = '\0';
}

bool validAudio(const float* samples, int32_t channels, int64_t frames,
                int32_t sampleRate) {
    return samples != nullptr && channels > 0 && frames > 0 && sampleRate > 0;
}

int resultError(char* error, int32_t size, const std::string& message) {
    writeError(error, size, message);
    return 1;
}

} // namespace

extern "C" {

VWSession* vw_session_create(void) {
    try {
        return new VWSession();
    } catch (...) {
        return nullptr;
    }
}

VWSession* vw_session_create_from_interleaved(const float* samples,
                                              int32_t channels,
                                              int64_t frames,
                                              int32_t sampleRate) {
    if (!validAudio(samples, channels, frames, sampleRate)) return nullptr;
    try {
        vwave::Audio audio = vwave::Audio::fromInterleaved(
            samples, channels, frames, sampleRate);
        return new VWSession{vwave::Session(
            vwave::Document::fromAudio(std::move(audio)))};
    } catch (...) {
        return nullptr;
    }
}

void vw_session_destroy(VWSession* session) { delete session; }

int vw_session_load_audio(VWSession* session, const char* path, char* error,
                          int32_t error_size) {
    if (!session || !path) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.loadAudio(path, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_save_audio(VWSession* session, const char* path, char* error,
                          int32_t error_size) {
    if (!session || !path) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.saveAudio(path, {}, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_load_project(VWSession* session, const char* path, char* error,
                            int32_t error_size) {
    if (!session || !path) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.loadProject(path, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_save_project(VWSession* session, const char* path, char* error,
                            int32_t error_size) {
    if (!session || !path) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.saveProject(path, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_info(const VWSession* session, VWDocumentInfo* info) {
    if (!session || !info) return 1;
    try {
        const vwave::Document& document = session->session.document();
        info->abi_version = VWAVE_C_ABI_VERSION;
        info->sample_rate = document.sampleRate();
        info->channels = document.numChannels();
        info->frames = document.numFrames();
        info->undo_count = static_cast<int32_t>(document.undoCount());
        info->redo_count = static_cast<int32_t>(document.redoCount());
        info->selection_active = document.selection().active ? 1 : 0;
        info->selection_start = document.selection().start;
        info->selection_end = document.selection().end;
        return 0;
    } catch (...) {
        return 1;
    }
}

const float* vw_session_channel(const VWSession* session, int32_t channel) {
    if (!session || channel < 0) return nullptr;
    try {
        const vwave::Audio& audio = session->session.document().audio();
        if (channel >= audio.numChannels()) return nullptr;
        return audio.channel(channel);
    } catch (...) {
        return nullptr;
    }
}

int64_t vw_session_rendered_interleaved(const VWSession* session, float* dest,
                                        int64_t capacity_frames) {
    if (!session || !dest || capacity_frames <= 0) return 0;
    try {
        const vwave::Audio& audio = session->session.document().audio();
        const int64_t frames = std::min(capacity_frames, audio.numFrames());
        for (int64_t frame = 0; frame < frames; ++frame) {
            for (int ch = 0; ch < audio.numChannels(); ++ch) {
                dest[frame * audio.numChannels() + ch] =
                    audio.sample(ch, frame);
            }
        }
        return frames;
    } catch (...) {
        return 0;
    }
}

int vw_session_execute(VWSession* session, const char* command,
                       const char* parameters_json, char* result_json,
                       int32_t result_size, char* error, int32_t error_size) {
    if (!session || !command) return resultError(error, error_size, "invalid argument");
    try {
        nlohmann::json parameters = nlohmann::json::object();
        if (parameters_json && parameters_json[0] != '\0') {
            try {
                parameters = nlohmann::json::parse(parameters_json);
            } catch (const std::exception& e) {
                return resultError(error, error_size,
                                   std::string("invalid parameters JSON: ") +
                                       e.what());
            }
        }
        std::string message;
        const nlohmann::json result =
            session->session.execute(command, parameters, &message);
        if (result.is_null())
            return resultError(error, error_size,
                               message.empty() ? "command failed" : message);
        const std::string text = result.dump();
        if (result_json && result_size > 0 &&
            static_cast<int32_t>(text.size()) >= result_size) {
            return resultError(error, error_size, "result buffer is too small");
        }
        writeResult(result_json, result_size, text);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_undo(VWSession* session) {
    if (!session) return 1;
    try {
        return session->session.document().undo() ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int vw_session_redo(VWSession* session) {
    if (!session) return 1;
    try {
        return session->session.document().redo() ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int vw_session_select(VWSession* session, int64_t start_frame,
                      int64_t end_frame) {
    if (!session) return 1;
    try {
        session->session.document().setSelection(start_frame, end_frame);
        return 0;
    } catch (...) {
        return 1;
    }
}

int vw_session_clear_selection(VWSession* session) {
    if (!session) return 1;
    try {
        session->session.document().clearSelection();
        return 0;
    } catch (...) {
        return 1;
    }
}

int vw_session_gain_db(VWSession* session, double db, char* error,
                       int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().gainDb(db, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_normalize(VWSession* session, double peak_db, char* error,
                         int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().normalize(peak_db, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_reverse(VWSession* session, char* error, int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().reverseSelection(&message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_delete_selection(VWSession* session, char* error,
                                int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().deleteSelection(&message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_time_stretch(VWSession* session, double ratio,
                            const char* algorithm, char* error,
                            int32_t error_size) {
    if (!session || !algorithm) return resultError(error, error_size, "invalid argument");
    try {
        vwave::StretchAlgorithm selected = vwave::StretchAlgorithm::Wsola;
        if (!vwave::stretchAlgorithmFromName(algorithm, selected))
            return resultError(error, error_size, "unknown stretch algorithm");
        std::string message;
        if (!session->session.document().timeStretch(ratio, selected, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_pitch_shift(VWSession* session, double semitones,
                           const char* algorithm, char* error,
                           int32_t error_size) {
    if (!session || !algorithm) return resultError(error, error_size, "invalid argument");
    try {
        vwave::StretchAlgorithm selected = vwave::StretchAlgorithm::Wsola;
        if (!vwave::stretchAlgorithmFromName(algorithm, selected))
            return resultError(error, error_size, "unknown stretch algorithm");
        std::string message;
        if (!session->session.document().pitchShift(semitones, selected,
                                                    &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_spectral_gain(VWSession* session, double low_hz,
                             double high_hz, double db, char* error,
                             int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().spectralGain(low_hz, high_hz, db,
                                                      &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_resample(VWSession* session, int32_t sample_rate, char* error,
                        int32_t error_size) {
    if (!session) return resultError(error, error_size, "invalid argument");
    try {
        std::string message;
        if (!session->session.document().resample(sample_rate, &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

int vw_session_replace_audio(VWSession* session, const float* interleaved,
                             int32_t channels, int64_t frames,
                             int32_t sample_rate, char* error,
                             int32_t error_size) {
    if (!session || !validAudio(interleaved, channels, frames, sample_rate))
        return resultError(error, error_size, "invalid audio data");
    try {
        vwave::Audio audio = vwave::Audio::fromInterleaved(
            interleaved, channels, frames, sample_rate);
        std::string message;
        if (!session->session.document().apply(
                vwave::Operation::replaceAll(std::move(audio)), &message))
            return resultError(error, error_size, message);
        writeError(error, error_size, "");
        return 0;
    } catch (const std::exception& e) {
        return resultError(error, error_size, e.what());
    }
}

} // extern "C"

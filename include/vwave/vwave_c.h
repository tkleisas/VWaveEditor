#ifndef VWAVE_C_H
#define VWAVE_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VWAVE_C_ABI_VERSION 1

typedef struct VWSession VWSession;

typedef struct VWDocumentInfo {
    int32_t abi_version;
    int32_t sample_rate;
    int32_t channels;
    int64_t frames;
    int32_t undo_count;
    int32_t redo_count;
    int32_t selection_active;
    int64_t selection_start;
    int64_t selection_end;
} VWDocumentInfo;

VWSession* vw_session_create(void);
VWSession* vw_session_create_from_interleaved(const float* samples,
                                              int32_t channels,
                                              int64_t frames,
                                              int32_t sample_rate);
void vw_session_destroy(VWSession* session);

int vw_session_load_audio(VWSession* session, const char* path,
                          char* error, int32_t error_size);
int vw_session_save_audio(VWSession* session, const char* path,
                          char* error, int32_t error_size);
int vw_session_load_project(VWSession* session, const char* path,
                            char* error, int32_t error_size);
int vw_session_save_project(VWSession* session, const char* path,
                            char* error, int32_t error_size);

int vw_session_info(const VWSession* session, VWDocumentInfo* info);
const float* vw_session_channel(const VWSession* session, int32_t channel);
int64_t vw_session_rendered_interleaved(const VWSession* session, float* dest,
                                        int64_t capacity_frames);

int vw_session_execute(VWSession* session, const char* command,
                       const char* parameters_json, char* result_json,
                       int32_t result_size, char* error, int32_t error_size);
int vw_session_undo(VWSession* session);
int vw_session_redo(VWSession* session);
int vw_session_select(VWSession* session, int64_t start_frame,
                      int64_t end_frame);
int vw_session_clear_selection(VWSession* session);
int vw_session_gain_db(VWSession* session, double db, char* error,
                       int32_t error_size);
int vw_session_normalize(VWSession* session, double peak_db, char* error,
                         int32_t error_size);
int vw_session_reverse(VWSession* session, char* error, int32_t error_size);
int vw_session_delete_selection(VWSession* session, char* error,
                                int32_t error_size);
int vw_session_time_stretch(VWSession* session, double ratio,
                            const char* algorithm, char* error,
                            int32_t error_size);
int vw_session_pitch_shift(VWSession* session, double semitones,
                           const char* algorithm, char* error,
                           int32_t error_size);
int vw_session_spectral_gain(VWSession* session, double low_hz,
                             double high_hz, double db, char* error,
                             int32_t error_size);
int vw_session_resample(VWSession* session, int32_t sample_rate, char* error,
                        int32_t error_size);
int vw_session_replace_audio(VWSession* session, const float* interleaved,
                             int32_t channels, int64_t frames,
                             int32_t sample_rate, char* error,
                             int32_t error_size);

#ifdef __cplusplus
}
#endif

#endif

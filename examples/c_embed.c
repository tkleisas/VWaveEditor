#include "vwave/vwave_c.h"

#include <stdio.h>

int main(void) {
    float samples[48000];
    for (int i = 0; i < 48000; ++i) samples[i] = 0.25f;

    VWSession* session =
        vw_session_create_from_interleaved(samples, 1, 48000, 48000);
    if (!session) return 1;

    char error[512] = {0};
    if (vw_session_select(session, 0, 24000) != 0) return 2;
    if (vw_session_gain_db(session, -6.0, error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }

    VWDocumentInfo info;
    if (vw_session_info(session, &info) != 0) return 4;
    printf("%d Hz, %d channels, %lld frames, %d undo entries\n",
           info.sample_rate, info.channels, (long long)info.frames,
           info.undo_count);

    vw_session_destroy(session);
    return 0;
}

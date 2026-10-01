#ifndef QA_CINEMATIC_INTERNAL_H
#define QA_CINEMATIC_INTERNAL_H

#include "qa/cinematic.h"

struct qa_cinematic {
    qa_cinematic_options options;
    qa_cinematic_asset *asset;
    char *name;
    qa_cinematic_format format;
    union {
        qa_cin_playback *cin;
        qa_roq_playback *roq;
        qa_ogv_playback *ogv;
        const qa_scene_image *image;
    } movie;
    double start_ms, paused_at, paused_duration, offset_ms;
    bool paused, dirty, completed, focus_paused, busy, suppress_audio, faulted;
    bool restore_pending;
    qa_media_status status, decoder_status;
    qa_media_frame picture;
    bool has_picture;
    uint64_t revision, image_revision;
    qa_scene_image *image;
    const qa_scene_frame *image_frame;
    uint64_t image_sequence;
    bool raw_attached;
    uint64_t audio_loop;
    int16_t *pcm;
    size_t pcm_capacity;
};
bool cinematic_fail(qa_error *, const char *);
bool cinematic_elapsed(qa_cinematic *, double *, qa_error *);

#endif

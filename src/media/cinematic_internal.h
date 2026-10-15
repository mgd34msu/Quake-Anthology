#ifndef QA_CINEMATIC_INTERNAL_H
#define QA_CINEMATIC_INTERNAL_H

#include "qa/cinematic.h"
#include "qa/arena.h"

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
    double start_ms, paused_at, paused_duration, source_clock_offset;
    bool paused, dirty, completed, focus_paused, busy, faulted;
    qa_media_status status;
    qa_media_frame picture;
    bool has_picture;
    uint64_t revision, image_revision;
    qa_scene_image *image;
    const qa_scene_frame *image_frame;
    uint64_t image_sequence;
    bool raw_attached;
    qa_audio_raw_stream *prepared_raw;
    uint64_t audio_loop;
    qa_arena pcm_storage;
    bool restoring, restore_audio;
    const qa_cinematic_checkpoint *restore_checkpoint;
    double restore_source_ms;
    uint64_t restore_loop;
};
bool cinematic_fail(qa_error *, const char *);
bool cinematic_elapsed(qa_cinematic *, double *, qa_error *);
bool cinematic_cin_resume(qa_cin_playback *, const qa_cinematic_checkpoint *, double *, qa_media_status *, qa_error *);
bool cinematic_roq_resume(qa_roq_playback *, const qa_cinematic_checkpoint *, double *, qa_media_status *, qa_error *);
bool cinematic_ogv_resume(qa_ogv_playback *, const qa_cinematic_checkpoint *, double *, qa_media_status *, qa_error *);
void cinematic_cin_decoder_cursor(const qa_cin_decoder *, qa_cin_prefix_cursor *);
void cinematic_cin_position(const qa_cin_playback *, double, qa_cinematic_checkpoint *);
void cinematic_roq_decoder_cursor(const qa_roq_decoder *, qa_roq_prefix_cursor *);
void cinematic_roq_position(const qa_roq_playback *, double, qa_cinematic_checkpoint *);
void cinematic_ogv_position(const qa_ogv_playback *, double, qa_cinematic_checkpoint *);

#endif

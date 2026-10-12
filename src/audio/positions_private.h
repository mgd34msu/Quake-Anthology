#ifndef QA_AUDIO_POSITIONS_PRIVATE_H
#define QA_AUDIO_POSITIONS_PRIVATE_H

#include "qa/audio.h"
#include "qa/id_index.h"

typedef struct qa_audio_position {
    uint64_t actor, owner, projection, revision;
    qa_vec3 origin;
} qa_audio_position;

typedef struct qa_audio_position_projection {
    uint64_t id, reset_revision;
} qa_audio_position_projection;

typedef struct qa_audio_positions {
    qa_arena owned_storage;
    qa_arena *storage;
    qa_id_index actors;
    qa_audio_position *rows;
    size_t count, capacity;
    uint64_t revision, next_projection;
    bool fixed;
} qa_audio_positions;

bool qa_audio_positions_prepare(qa_audio_positions *, qa_arena *, size_t, bool, qa_error *);
void qa_audio_positions_project(qa_audio_positions *, qa_audio_position_projection *);
bool qa_audio_positions_set(qa_audio_positions *, const qa_audio_position_projection *,
    uint64_t, uint64_t, qa_vec3, bool, qa_error *);
qa_vec3 qa_audio_positions_read(const qa_audio_positions *, const qa_audio_position_projection *,
    uint64_t, uint64_t, bool);
void qa_audio_positions_projection_clear(qa_audio_positions *, qa_audio_position_projection *);
void qa_audio_positions_clear(qa_audio_positions *);
void qa_audio_positions_destroy(qa_audio_positions *);

#endif

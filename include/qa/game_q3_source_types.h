#ifndef QA_GAME_Q3_SOURCE_TYPES_H
#define QA_GAME_Q3_SOURCE_TYPES_H

#include "qa/actors.h"
#include "qa/math.h"
#include "qa/strings.h"

#define QA_Q3_SOURCE_ENTITIES 1024u
#define QA_Q3_SOURCE_CLIENTS 64u
#define QA_Q3_SOURCE_WORLD 1022u
#define QA_Q3_SOURCE_NONE 1023u
#define QA_Q3_SOURCE_MEMORY_BYTES 262144u
typedef struct qa_q3_source_memory {
    uint8_t pool[QA_Q3_SOURCE_MEMORY_BYTES];
    uint32_t allocated_bytes;
} qa_q3_source_memory;

typedef struct qa_q3_source_binding {
    qa_actor_id actor;
    qa_string_id classname;
    int32_t free_time_ms;
    int32_t number, owner_number, client_slot;
    uint32_t server_flags;
    bool in_use, never_free, body_attached;
} qa_q3_source_binding;

typedef struct qa_q3_source_team_state {
    int32_t team_scores[4], warmup_time_ms;
    float last_flag_capture_ms;
    int32_t last_capture_team, red_status, blue_status, neutral_status;
    int32_t red_taken_ms, blue_taken_ms;
    int32_t red_obelisk_attacked_ms, blue_obelisk_attacked_ms;
    bool initialized;
    qa_actor_id neutral_obelisk;
} qa_q3_source_team_state;
typedef struct qa_q3_source_match_state {
    int32_t intermission_time_ms, intermission_queued_ms, exit_time_ms;
    uint64_t warmup_modification_count;
    qa_string_id changemap;
    bool ready_to_exit, restarted;
    qa_vec3 intermission_origin, intermission_angles;
} qa_q3_source_match_state;

#endif

#ifndef QA_APPLICATION_GUEST_Q3_SCENE_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_SCENE_PROFILE_H

#include "guest_q3_body_profile.h"

typedef enum q3scene_argument_kind {
    Q3SCENE_LITERAL, Q3SCENE_CLIENT, Q3SCENE_TIME,
    Q3SCENE_SNAPSHOT, Q3SCENE_COMMAND_SEQUENCE, Q3SCENE_PLAYER_STATE,
    Q3SCENE_SNAPSHOT_ADDRESS, Q3SCENE_ENTITY_STATE, Q3SCENE_CENTITY,
    Q3SCENE_ORIGIN, Q3SCENE_EVENT, Q3SCENE_PARAMETER
} q3scene_argument_kind;
typedef struct q3scene_argument { q3scene_argument_kind kind; int32_t word; } q3scene_argument;
typedef struct q3scene_call {
    uint32_t entry;
    q3scene_argument *arguments;
    size_t count;
    bool weapon_presented;
} q3scene_call;
typedef struct q3scene_calls { q3scene_call *rows; size_t count; } q3scene_calls;
typedef struct q3scene_addresses { uint32_t *rows; size_t count; } q3scene_addresses;
typedef struct q3scene_cvar { char *name, *value; } q3scene_cvar;
typedef struct application_q3_scene_profile {
    qa_qvm_image *image;
    char *gameplay_path, *cgame_path;
    qa_qvm_abi abi;
    uint32_t game_state, command_sequence;
    uint32_t entities, stride, capacity, state, previous_event, snapshot_time;
    uint32_t event_type, event_entry, event_argument;
    uint32_t player_state, snapshot_address, entity_origin;
    q3scene_addresses snapshot_pointers;
    q3scene_addresses time, frame_time, origin, angles, axis;
    q3scene_calls initialize, refresh, snapshots, frame, hud;
    q3scene_calls project, event;
    q3scene_cvar *cvars;
    size_t cvar_count;
    bool has_hud, replace_status, player_events;
    application_q3_body_profile body;
} application_q3_scene_profile;

/* Both held executables are supplied by their real artifact owners. Reading
 * a scene never reopens a canonical module or guesses a stock layout. */
bool application_q3_scene_profile_create(qa_qvm_image *cgame, qa_qvm_abi,
    const char *cgame_path, const qa_qvm_image *gameplay,
    const char *gameplay_path, qa_bytes declaration,
    application_q3_scene_profile **, qa_error *);
void application_q3_scene_profile_destroy(application_q3_scene_profile *);

#endif

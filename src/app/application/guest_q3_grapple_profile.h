#ifndef QA_APPLICATION_GUEST_Q3_GRAPPLE_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_GRAPPLE_PROFILE_H

#include "qa/qvm.h"
#include "qa/math.h"

typedef struct application_q3_grapple_attachment {
    const char *path, *tag;
} application_q3_grapple_attachment;
typedef struct application_q3_grapple_cvar { const char *name, *value; } application_q3_grapple_cvar;
typedef struct application_q3_grapple_word { uint32_t offset; int32_t value; } application_q3_grapple_word;
typedef struct application_q3_grapple_definition {
    const char *id, *title;
    uint32_t entity_stride, client_stride;
    struct {
        uint32_t inuse, client, parent, target, mover, health, takedamage, hook;
        uint32_t event_time, free_after_event;
    } fields;
    struct { uint32_t time, frame, movement, forward, ground_plane; } globals;
    struct {
        uint32_t allocate, free, fire, release, force_release, missile, follow;
        uint32_t think, pull, move_mover_hooks, damage, same_team, player_move;
    } callbacks;
    const int32_t *fire_arguments;
    size_t fire_argument_count;
    uint32_t movement_bytes;
    const application_q3_grapple_word *movement_words;
    size_t movement_word_count;
    const application_q3_grapple_cvar *initial_cvars;
    size_t initial_cvar_count;
    uint32_t pulling_flag, event_lifetime_ms, damage_method;
    struct {
        const char *projectile_model, *view_model;
        int32_t weapon_index;
        const char *anchor_path, *anchor_tag;
        qa_vec3 anchor_offset;
        float fov_above, fov_scale;
        const application_q3_grapple_attachment *attachments;
        size_t attachment_count;
        bool cable_shader;
        const char *cable_path, *cable_flight, *cable_pull, *cable_hold;
        uint32_t cable_width, cable_segment_length;
        const char *fire_sound, *attach_sound, *release_sound, *pull_sound, *hang_sound;
    } presentation;
} application_q3_grapple_definition;

typedef struct application_q3_grapple_profile application_q3_grapple_profile;
/* The two genuine authored GAME profiles are admitted by executable digest,
 * original ABI/layout/global/function entries and reserved scratch capacity.
 * Unknown artifacts succeed with no profile. No source code or Init runs. */
bool application_q3_grapple_profile_create(qa_qvm_image *, qa_qvm_role, qa_qvm_abi,
    const char *actual_artifact_path, application_q3_grapple_profile **, qa_error *);
void application_q3_grapple_profile_destroy(application_q3_grapple_profile *);
const application_q3_grapple_definition *application_q3_grapple_profile_definition(
    const application_q3_grapple_profile *);
const qa_qvm_image *application_q3_grapple_profile_image(const application_q3_grapple_profile *);
const char *application_q3_grapple_profile_path(const application_q3_grapple_profile *);

#endif

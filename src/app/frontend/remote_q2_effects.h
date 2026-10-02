#ifndef QA_FRONTEND_REMOTE_Q2_EFFECTS_H
#define QA_FRONTEND_REMOTE_Q2_EFFECTS_H
#include "qa/network_q2_messages.h"
#include "qa/scene_effects.h"
#include "qa/source_save.h"
#include "qa/builtin.h"
#include "qa/collision.h"

typedef struct frontend_remote_q2_effects frontend_remote_q2_effects;
typedef struct frontend_remote_q2_effects_policy frontend_remote_q2_effects_policy;
typedef enum frontend_remote_q2_effects_profile {
    FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC=1, FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE
} frontend_remote_q2_effects_profile;
typedef struct frontend_remote_q2_effects_controls {
    int32_t muzzlelight_milliseconds;
    bool rerelease_effects, muzzleflashes;
    uint32_t dlight_hacks, disable_particles, disable_explosions;
} frontend_remote_q2_effects_controls;
typedef struct frontend_remote_q2_effects_pose {
    qa_actor_id actor;
    uint32_t number, event, model_index;
    uint64_t effects;
    int32_t frame;
    qa_vec3 origin, angles;
    qa_bounds bounds;
    float radius, scale;
    bool bounds_present, model_present;
} frontend_remote_q2_effects_pose;
typedef struct frontend_remote_q2_effects_source {
    qa_session *session;
    uint64_t identity, content_generation;
    frontend_remote_q2_effects_profile profile;
    /* Zero transport belongs to a genuine semantic Source whose rules/ABI
     * profile is declared independently from received raw TEMP records. */
    qa_net_protocol_id protocol;
    const qa_resource *map;
    qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_scene_world *world;
    const qa_scene_image *white;
    void *context;
    bool (*current)(void *, const struct frontend_remote_q2_effects_source *, qa_error *);
    bool (*actor)(void *, uint32_t received_number, frontend_remote_q2_effects_pose *, qa_error *);
    bool (*actor_pose)(void *, qa_actor_id, frontend_remote_q2_effects_pose *, qa_error *);
    bool (*viewer)(void *, qa_actor_id *, qa_error *);
    /* acquire=false reads the already retained model cache, including genuine
     * missing-resource receipts. Restore and readiness never register models. */
    bool (*model)(void *, const char *, bool acquire, qa_scene_model **, qa_error *);
    bool (*sound)(void *, const char *, qa_vec3, qa_actor_id, double milliseconds,
        int32_t channel, float volume, float attenuation, double delay_seconds, qa_error *);
    bool (*hit_marker)(void *, int32_t damage, qa_error *);
    bool (*controls)(void *, frontend_remote_q2_effects_controls *, qa_error *);
    bool (*footstep)(void *, const frontend_remote_q2_effects_pose *, uint32_t event,
        double milliseconds, qa_builtin_random *, qa_error *);
    bool (*trace)(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
} frontend_remote_q2_effects_source;
typedef struct frontend_remote_q2_effects_sample {
    double milliseconds, server_milliseconds;
    float fraction;
    uint64_t frame_sequence;
    const frontend_remote_q2_effects_pose *entities;
    size_t entity_count;
    qa_scene_view view;
    qa_actor_id viewer;
    qa_vec3 gun_offset;
    int32_t hand;
    bool hardware, per_pixel_lighting;
    float frame_seconds;
    float footsteps;
    const qa_scene_world_input *world_input;
} frontend_remote_q2_effects_sample;
typedef struct frontend_remote_q2_effects_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*actor_encode)(void *, qa_actor_id, qa_saved_actor_id *, qa_error *);
    bool (*actor_decode)(void *, qa_saved_actor_id, qa_actor_id *, qa_error *);
} frontend_remote_q2_effects_refs;

bool frontend_remote_q2_effects_create(const frontend_remote_q2_effects_source *,
    frontend_remote_q2_effects **, qa_error *);
bool frontend_remote_q2_effects_idle(const frontend_remote_q2_effects *);
bool frontend_remote_q2_effects_current(const frontend_remote_q2_effects *,
    const frontend_remote_q2_effects_source *, qa_error *);
bool frontend_remote_q2_effects_destroy(frontend_remote_q2_effects **, qa_error *);
const qa_scene_image *frontend_remote_q2_effects_particle_image(const frontend_remote_q2_effects *);
bool frontend_remote_q2_effects_policy_prepare(frontend_remote_q2_effects *, qa_scene_resource_policy *,
    frontend_remote_q2_effects_policy **, qa_error *);
bool frontend_remote_q2_effects_policy_ready(frontend_remote_q2_effects_policy *, qa_error *);
bool frontend_remote_q2_effects_policy_ready_is(const frontend_remote_q2_effects_policy *);
void frontend_remote_q2_effects_policy_publish(frontend_remote_q2_effects_policy *);
bool frontend_remote_q2_effects_policy_finish(frontend_remote_q2_effects_policy **, qa_error *);
bool frontend_remote_q2_effects_policy_abort(frontend_remote_q2_effects_policy **, qa_error *);
/* The decoder and normalized extension supply the same exact typed operands.
 * actors[i] is the real full identity attached to fields[i]; native records
 * instead resolve their received numbers through source.actor. */
bool frontend_remote_q2_effects_temporary(frontend_remote_q2_effects *,
    const qa_q2_temp_entity *, const qa_actor_id actors[7], double milliseconds,
    double server_milliseconds, qa_error *);
bool frontend_remote_q2_effects_muzzle(frontend_remote_q2_effects *,
    uint32_t received_entity, uint32_t flash, bool monster, bool silenced,
    double milliseconds, double server_milliseconds, qa_error *);
bool frontend_remote_q2_effects_actor_muzzle(frontend_remote_q2_effects *,
    qa_actor_id, uint32_t flash, bool monster, bool silenced,
    double milliseconds, double server_milliseconds, qa_error *);
/* The normalized producer already captured and offset this muzzle position;
 * its receiver qualifies the full actor against its actual replica registry. */
bool frontend_remote_q2_effects_monster_muzzle(frontend_remote_q2_effects *,
    qa_actor_id, uint32_t flash, qa_vec3 origin, qa_vec3 direction,
    double milliseconds, double server_milliseconds, qa_error *);
bool frontend_remote_q2_effects_monster_muzzle_pose(frontend_remote_q2_effects *,
    qa_actor_id, uint32_t flash, qa_vec3 origin, qa_vec3 angles, float scale,
    double milliseconds, double server_milliseconds, qa_error *);
bool frontend_remote_q2_effects_weapon_draw(frontend_remote_q2_effects *, qa_actor_id viewer,
    const qa_scene_model_input *weapon, qa_scene_frame *, qa_error *);
bool frontend_remote_q2_effects_named_effect(frontend_remote_q2_effects *,
    const char *recipe, qa_vec3 origin, qa_vec3 direction, int32_t count, int32_t color,
    double milliseconds, qa_error *);
bool frontend_remote_q2_effects_named_beam(frontend_remote_q2_effects *,
    const char *recipe, qa_actor_id, qa_vec3 start, qa_vec3 end, double duration_seconds,
    double milliseconds, qa_error *);
bool frontend_remote_q2_effects_frame(frontend_remote_q2_effects *,
    const frontend_remote_q2_effects_sample *, qa_error *);
bool frontend_remote_q2_effects_prepare(frontend_remote_q2_effects *,
    const frontend_remote_q2_effects_sample *, const qa_scene_light **, size_t *, qa_error *);
bool frontend_remote_q2_effects_draw(frontend_remote_q2_effects *,
    const frontend_remote_q2_effects_sample *, bool particles, bool entities,
    qa_scene_frame *, qa_error *);
bool frontend_remote_q2_effects_entity_beam(frontend_remote_q2_effects *,
    const qa_scene_view *, qa_vec3 start, qa_vec3 end, uint32_t packed_colors,
    int32_t width, qa_scene_frame *, qa_error *);
bool frontend_remote_q2_effects_checkpoint(const frontend_remote_q2_effects *,
    const frontend_remote_q2_effects_refs *, qa_buffer *, qa_error *);
bool frontend_remote_q2_effects_restore(const frontend_remote_q2_effects_source *,
    const frontend_remote_q2_effects_refs *, qa_bytes, frontend_remote_q2_effects **, qa_error *);
#endif

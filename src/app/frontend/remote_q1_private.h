#ifndef QA_FRONTEND_REMOTE_Q1_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q1_PRIVATE_H
#include "remote_q1_client.h"
#include "model_inventory.h"
#include "remote_q1_camera.h"
#include "q1_sky.h"
#include "qa/world.h"
#include "qa/event_ring.h"

typedef struct remote_q1_model {
    struct remote_q1_model *next;
    char *path;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_model decoded;
    const qa_model *source;
    frontend_model_lease *source_lease;
    qa_scene_model *scene;
    qa_scene_world *world;
    bool colors;
    uint8_t top, bottom;
} remote_q1_model;

typedef struct remote_q1_entities { qa_q1_entity *rows; size_t count, capacity; } remote_q1_entities;
typedef struct remote_q1_demo_seed {
    struct remote_q1_demo_seed *next;
    qa_buffer bytes;
    uint32_t sequence, acknowledged;
    uint8_t wire_prefix[8];
} remote_q1_demo_seed;
typedef struct remote_q1_client {
    char *name, *social, *player_info, *userinfo;
    int32_t frags, ping;
    uint8_t colors;
    bool present, has_ping, has_social, has_player_info;
} remote_q1_client;
struct frontend_remote_q1 {
    struct frontend_remote_q1 *next;
    struct frontend_remote_q1_effects *effects;
    struct frontend_remote_q1_hud_storage *hud;
    struct frontend_remote_q1_prediction *prediction;
    struct frontend_remote_q1_skins *skins;
    struct frontend_remote_q1_sky_policy *sky_policy;
    remote_q1_camera camera;
    qa_collision_geometry *collision;
    qa_world *collision_world;
    qa_actor_id *collision_actors;
    qa_entity_model_field *collision_models;
    size_t collision_count, collision_capacity;
    frontend_q1_motion_refs motion_refs;
    frontend_q1_sky_controls sky_controls;
    frontend_legacy_cvar_handles legacy_cvars;
    qa_cvar_handle hightrack, chasecam, noskins, baseskin, solid_players;
    frontend_q1_view_motion view_motion;
    frontend_q1_view_pose view_pose;
    qa_vec3 view_entity_origin, view_entity_angles;
    uint32_t view_entity_pose_number;
    bool view_pose_ready;
    qa_frontend *frontend;
    frontend_remote_q1_options options;
    qa_net_protocol_id protocol;
    frontend_remote_q1_content content;
    qa_resource *map;
    qa_vfs_acquisition map_opening;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_scene_world *world;
    qa_audio_bank *sound_bank;
    frontend_world_scratch world_scratch;
    remote_q1_model *model_cache;
    const qa_scene_image *sky_images[6];
    uint8_t sky_found;
    uint64_t revision, map_generation, received_ns, frame_number, next_event;
    double seconds, previous_seconds, fraction;
    uint32_t max_clients, view_entity;
    uint8_t pending_impulse;
    qa_vec3 view_angles;
    qa_q1_clientdata data;
    bool bound, loaded, retired, has_data, intermission, published;
    unsigned busy;
    remote_q1_entities current, previous, statics, qw_entities, qw_nails, qw_batch_players;
    char **models, **sounds;
    bool *sound_available;
    size_t model_count, sound_count;
    uint32_t spike_model;
    char *styles[256], *skybox;
    remote_q1_client clients[256];
    qa_qw_serverdata qw;
    char *qw_directory, *level_name;
    qa_qw_player qw_players[32];
    bool qw_player_valid[32], qw_ready, qw_frame;
    int32_t stats[256];
    int8_t qw_kick;
    bool qw_intermission;
    qa_vec3 qw_intermission_origin, qw_intermission_angles;
    qa_event_ring *events;
    uint64_t event_cursor;
    remote_q1_demo_seed *demo_seed, *demo_seed_last;
    size_t demo_seed_bytes;
    frontend_demo_sink demo_sink;
    uint64_t demo_record_start;
    bool demo_seed_complete;
};
bool remote_q1_fail(qa_error *, qa_status, const char *);
bool remote_q1_live(const frontend_remote_q1 *, qa_error *);
bool remote_q1_mutable(const frontend_remote_q1 *);
bool remote_q1_domain_equal(const frontend_remote_q1_domain *, const frontend_remote_q1_domain *);
bool remote_q1_string(char **, const char *, qa_error *);
bool remote_q1_entity_set(remote_q1_entities *, const qa_q1_entity *, qa_error *);
bool remote_q1_event_admit(frontend_remote_q1 *, const qa_nq_message *, uint64_t, qa_error *);
bool remote_q1_events_consume(frontend_remote_q1 *, uint64_t, qa_error *);
bool remote_q1_actor_read(frontend_remote_q1 *, uint32_t, qa_actor_id *, qa_error *);
void remote_q1_clear(frontend_remote_q1 *);
void remote_q1_media_clear(frontend_remote_q1 *);
bool remote_q1_view_sample(frontend_remote_q1 *,qa_error *);
bool remote_q1_view_damage(frontend_remote_q1 *,const qa_nq_message *,qa_error *);
void remote_q1_time_advance(frontend_remote_q1 *, double, uint64_t);
void remote_q1_publication_update(frontend_remote_q1 *);
bool remote_q1_media_prepare(frontend_remote_q1 *, qa_error *);
bool remote_q1_sky_load(frontend_remote_q1 *, qa_error *);
bool remote_q1_model_read(frontend_remote_q1 *, const frontend_remote_q1_entity_view *, remote_q1_model **, qa_error *);
bool remote_q1_event_present(frontend_remote_q1 *, const qa_nq_message *, qa_error *);
bool remote_q1_effects_clear(frontend_remote_q1 *, qa_error *);
bool remote_q1_effects_draw(frontend_remote_q1 *, const qa_scene_view *, const qa_scene_world_input *, qa_error *);
void remote_q1_demo_clear(frontend_remote_q1 *);
bool remote_q1_demo_batch(frontend_remote_q1 *, qa_bytes, qa_bytes, uint32_t, uint32_t, uint64_t, qa_error *);
bool remote_q1_effects_scene(frontend_remote_q1 *, double, const qa_scene_light **, size_t *, qa_error *);
#endif

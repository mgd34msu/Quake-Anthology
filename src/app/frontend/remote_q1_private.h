#ifndef QA_FRONTEND_REMOTE_Q1_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q1_PRIVATE_H
#include "remote_q1_client.h"
#include "model_inventory.h"

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
    uint64_t saved_model, saved_scene, saved_world;
    bool colors;
    uint8_t top, bottom;
} remote_q1_model;

typedef struct remote_q1_entities { qa_q1_entity *rows; size_t count, capacity; } remote_q1_entities;
typedef struct remote_q1_actor { uint32_t number; qa_actor_id id; } remote_q1_actor;
typedef struct remote_q1_pending { qa_nq_message message; char *text; } remote_q1_pending;
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
    struct frontend_remote_q1_sky_policy *sky_policy;
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
    remote_q1_model *model_cache;
    const qa_scene_image *sky_images[6];
    uint8_t sky_found;
    uint64_t revision, map_generation, received_ns, frame_number, next_event;
    double seconds, previous_seconds, fraction;
    uint32_t max_clients, view_entity;
    uint8_t pending_impulse;
    qa_vec3 view_angles;
    qa_q1_clientdata data;
    bool bound, loaded, retired, has_data, importing;
    uint64_t saved_world, saved_sky[6];
    unsigned busy;
    remote_q1_entities current, previous, statics, qw_entities, qw_nails, qw_batch_players;
    remote_q1_actor *actors;
    size_t actor_count, actor_capacity;
    char **models, **sounds;
    bool *sound_available;
    size_t model_count, sound_count;
    char *styles[256], *skybox;
    remote_q1_client clients[256];
    qa_qw_serverdata qw;
    char *qw_directory, *qw_level;
    qa_qw_player qw_players[32];
    bool qw_player_valid[32], qw_ready, qw_frame;
    int32_t qw_stats[256];
    int8_t qw_kick;
    uint8_t qw_pending_track;
    bool qw_has_pending_track;
    bool qw_intermission;
    qa_vec3 qw_intermission_origin, qw_intermission_angles;
    remote_q1_pending *qw_pending;
    size_t qw_pending_count, qw_pending_capacity, qw_pending_cursor;
};
bool remote_q1_fail(qa_error *, qa_status, const char *);
bool remote_q1_live(const frontend_remote_q1 *, qa_error *);
bool remote_q1_mutable(const frontend_remote_q1 *);
bool remote_q1_domain_equal(const frontend_remote_q1_domain *, const frontend_remote_q1_domain *);
bool remote_q1_string(char **, const char *, qa_error *);
bool remote_q1_entity_set(remote_q1_entities *, const qa_q1_entity *, qa_error *);
bool remote_q1_qw_queue(frontend_remote_q1 *, const qa_nq_message *, qa_error *);
void remote_q1_qw_queue_clear(frontend_remote_q1 *);
bool remote_q1_actor_read(frontend_remote_q1 *, uint32_t, qa_actor_id *, qa_error *);
void remote_q1_clear(frontend_remote_q1 *);
void remote_q1_media_clear(frontend_remote_q1 *);
void remote_q1_time_advance(frontend_remote_q1 *, double, uint64_t);
bool remote_q1_media_prepare(frontend_remote_q1 *, qa_error *);
bool remote_q1_sky_load(frontend_remote_q1 *, qa_error *);
bool remote_q1_model_read(frontend_remote_q1 *, const frontend_remote_q1_entity_view *, remote_q1_model **, qa_error *);
bool remote_q1_effects_service(frontend_remote_q1 *, const qa_nq_message *, qa_error *);
bool remote_q1_effects_clear(frontend_remote_q1 *, qa_error *);
bool remote_q1_effects_draw(frontend_remote_q1 *, const qa_scene_view *, const qa_scene_world_input *, qa_error *);
bool remote_q1_effects_scene(frontend_remote_q1 *, double, const qa_scene_light **, size_t *, qa_error *);
#endif

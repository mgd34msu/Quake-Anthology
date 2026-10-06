#ifndef QA_FRONTEND_REMOTE_Q2_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q2_PRIVATE_H
#include "internal.h"
#include "remote_q2_client.h"
#include "qa/hud_q2.h"
#include "qa/font.h"
#include "model_inventory.h"
#include "q2_animation.h"

typedef struct remote_q2_model {
    struct remote_q2_model *next;
    char *path;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_resource *scope, *palette;
    qa_vfs_acquisition scope_opening, palette_opening;
    qa_model decoded;
    const qa_model *source;
    frontend_model_lease *source_lease;
    uint64_t saved_model, saved_scene;
    qa_scene_model *scene;
} remote_q2_model;
typedef struct remote_q2_picture {
    struct remote_q2_picture *next;
    char *name;
    const qa_scene_image *image;
    uint64_t saved_image;
} remote_q2_picture;
typedef struct remote_q2_missing_model {
    struct remote_q2_missing_model *next;
    char *path;
} remote_q2_missing_model;
typedef struct remote_q2_layout {
    uint16_t models, sounds, images, lights, items, players, checksum, max_clients, air_accelerate;
    size_t max_models, max_sounds, max_images, max_configs;
} remote_q2_layout;
bool remote_q2_model_scope_required(const char *, qa_bytes);
bool remote_q2_model_scope_current(const frontend_remote_q2 *, const remote_q2_model *, qa_error *);
struct frontend_remote_q2_image_policy;
struct frontend_remote_q2_effects;
struct frontend_q2_entity_pose;
struct remote_q2_footsteps;
typedef struct remote_q2_entity_animation {
    frontend_q2_animation animation;
    int32_t server_frame;
} remote_q2_entity_animation;
typedef struct remote_q2_sent_command {
    bool valid;
    uint32_t packet_sequence;
    uint64_t sent_ns;
    uint64_t command_number;
    qa_q2_usercmd command;
    bool predicted;
    qa_vec3 origin;
} remote_q2_sent_command;
struct frontend_remote_q2 {
    frontend_remote_q2 *next;
    qa_frontend *frontend;
    frontend_remote_q2_options options;
    uint64_t identity, loading_generation, content_generation, received_ns, sample_ns;
    uint32_t acknowledged;
    remote_q2_sent_command sent[64];
    remote_q2_sent_command commands[64];
    uint64_t last_command, acknowledged_command;
    uint32_t last_sent;
    bool sent_set, input_set;
    bool predicted;
    qa_vec3 prediction_origin, prediction_angles, prediction_error, prediction_pml;
    float prediction_step;
    uint64_t prediction_step_ns;
    uint64_t prediction_command;
    int32_t prediction_frame;
    qa_movement_ground prediction_ground;
    qa_collision_plane prediction_plane;
    unsigned busy;
    bool bound, selected, content_admitted, media_ready, retired, retiring, importing, restore_media_ready;
    qa_q2_serverdata data;
    remote_q2_layout layout;
    frontend_remote_q2_content content;
    char **configs;
    qa_q2_entity *baselines;
    size_t baseline_count;
    qa_q2_wire_frame frame, previous;
    struct frontend_q2_entity_pose *effect_poses;
    size_t effect_pose_capacity;
    float fraction, frame_ms, height_previous, height_current;
    double sample_frame_seconds, demo_ms;
    double height_changed_ms;
    bool height_set;
    frontend_q2_animation gun_animation;
    remote_q2_entity_animation *entity_animations;
    size_t entity_animation_capacity;
    uint32_t hit_marker_count;
    int32_t hit_marker_frame;
    uint64_t hit_marker_ns;
    bool hit_marker_set;
    qa_scene_fog fog_start, fog_end;
    double fog_started_ms;
    uint16_t fog_duration_ms;
    bool fog_received;
    qa_resource *map;
    qa_vfs_acquisition map_opening;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_media_library *media;
    struct frontend_material_movies *shader_movies;
    qa_audio_bank *sounds;
    qa_font_library *fonts;
    const qa_font *classic;
    const qa_scene_image *white;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    uint64_t saved_world, saved_classic, saved_white;
    qa_buffer saved_effects;
    bool effects_imported;
    struct remote_q2_footsteps *footsteps;
    remote_q2_model *models;
    remote_q2_missing_model *missing_models;
    remote_q2_picture *pictures;
    struct frontend_remote_q2_image_policy *image_policy;
    struct frontend_remote_q2_effects *effects;
    qa_hud_q2_table hud_table;
    int32_t inventory[256];
    char *overlay;
    qa_fs_stage *download_stage;
    qa_fs_root *download_root;
    char *download_path;
    uint64_t download_bytes;
    uint64_t download_nonce;
    uint64_t download_logical_nonce;
    uint8_t download_percent;
    qa_buffer download_block;
    uint64_t download_block_offset, download_block_cursor;
    uint8_t download_block_percent;
    bool download_block_pending, download_block_committed, download_remembered;
    bool download_sealed, download_published, download_refresh_pending, download_refreshed;
    bool download_stage_sealed, download_stage_published;
    qa_fs_identity download_identity;
    char **download_attempted;
    size_t download_attempted_count;
    qa_input_command_builder input;
};
bool remote_q2_fail(qa_error *, qa_status, const char *);
bool remote_q2_live(const frontend_remote_q2 *, qa_error *);
bool remote_q2_capture_owned(const frontend_remote_q2 *);
bool remote_q2_retirement_current(const frontend_remote_q2 *, qa_error *);
bool remote_q2_domain_equal(const frontend_remote_q2_domain *, const frontend_remote_q2_domain *);
bool remote_q2_config_set(frontend_remote_q2 *, uint16_t, const char *, qa_error *);
bool remote_q2_media_clear(frontend_remote_q2 *, qa_error *);
bool remote_q2_media_prepare(frontend_remote_q2 *, qa_error *);
bool remote_q2_prediction_replay(frontend_remote_q2 *, qa_error *);
bool remote_q2_trace(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
qa_bounds remote_q2_solid_bounds(const frontend_remote_q2 *, uint32_t);
void remote_q2_prediction_receive(frontend_remote_q2 *);
bool remote_q2_map_validate(const frontend_remote_q2 *, const qa_resource *, qa_bsp_view *, qa_error *);
bool remote_q2_model_read(frontend_remote_q2 *, const char *, remote_q2_model **, qa_error *);
const qa_scene_image *remote_q2_picture_read(void *, const char *, qa_error *);
const qa_scene_image *remote_q2_sprite_read(frontend_remote_q2 *, const char *, qa_error *);
bool remote_q2_image_direct(const frontend_remote_q2 *, const char *);
bool remote_q2_download_prepare(frontend_remote_q2 *, qa_q2_preparation *, qa_error *);
bool remote_q2_download_receive(frontend_remote_q2 *, const qa_q2_server_event *, bool *, qa_error *);
bool remote_q2_download_path_valid(const char *);
qa_fs_root *remote_q2_download_destination(const frontend_remote_q2 *, const char *);
uint64_t remote_q2_download_extent(const frontend_remote_q2 *);
bool remote_q2_download_clear(frontend_remote_q2 *, qa_error *);
bool remote_q2_records(frontend_remote_q2 *, const qa_q2_server_record *, size_t, qa_error *);
bool remote_q2_player_fog_receive(frontend_remote_q2 *, qa_error *);
remote_q2_layout remote_q2_layout_read(qa_net_protocol_id);
bool remote_q2_layout_adopt(frontend_remote_q2 *, const qa_q2_serverdata *, qa_error *);
const qa_q2_entity *remote_q2_frame_entity(const qa_q2_wire_frame *, uint32_t);
bool remote_q2_float_movement(const frontend_remote_q2 *);
bool remote_q2_rerelease_presentation(const frontend_remote_q2 *);
#endif

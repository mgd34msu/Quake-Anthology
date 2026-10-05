#ifndef QA_FRONTEND_REMOTE_Q1_CLIENT_H
#define QA_FRONTEND_REMOTE_Q1_CLIENT_H
#include "qa/frontend.h"
#include "qa/application_client.h"
#include "qa/network_runtime.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q1_qw.h"
#include "qa/network_q1_client_runtime.h"
#include "qa/scene.h"
#include "qa/audio.h"
#include "demo_service.h"

typedef struct frontend_remote_q1 frontend_remote_q1;
struct frontend_remote_q1_skin_bindings;
struct frontend_remote_q1_skins;
typedef struct frontend_remote_q1_domain {
    qa_application *application;
    qa_network_runtime *runtime;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t epoch, configuration_generation;
    uint32_t physical_seat;
    qa_net_protocol_id protocol;
    qa_catalog *catalog;
    qa_product_id product;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command_context;
    qa_actor_registry *actors;
    qa_actor_owner actor_owner;
    qa_actor_definition actor_definition;
} frontend_remote_q1_domain;
typedef struct frontend_remote_q1_content {
    qa_catalog *catalog;
    qa_product_id product;
    qa_vfs *mounts;
} frontend_remote_q1_content;
typedef struct frontend_remote_q1_options {
    frontend_remote_q1_domain domain;
    void *context;
    bool (*current)(void *, const frontend_remote_q1_domain *, qa_error *);
    /* Real Source catalog selection, reached only on received serverinfo or
     * completed QW precaches. The returned catalog/view are borrowed. */
    bool (*load_content)(void *, const frontend_remote_q1_domain *,
        const qa_nq_serverinfo *, const qa_qw_serverdata *,
        frontend_remote_q1_content *, qa_error *);
    /* Synchronous, once per actually decoded service. Packet strings and list
     * storage are still alive here. No authoritative local GAME is consulted. */
    bool (*service)(void *, const frontend_remote_q1_domain *,
        qa_net_protocol_id decoded_protocol, const qa_nq_message *, double seconds, uint64_t sequence, qa_error *);
    bool (*disconnected)(void *, const frontend_remote_q1_domain *, const char *, qa_error *);
    bool (*application_read)(void *, const frontend_remote_q1_domain *, qa_application_client_source *, qa_error *);
    bool (*application_metadata_read)(void *, const frontend_remote_q1_domain *, qa_application_client_source *, qa_error *);
    bool (*sample_seconds)(void *, const frontend_remote_q1_domain *, double *, qa_error *);
    int32_t demo_forced_track;
    const struct frontend_remote_q1_skin_bindings *skin_bindings;
} frontend_remote_q1_options;
typedef struct frontend_remote_q1_client_row {
    const char *name, *social, *player_info;
    int32_t frags, ping;
    uint8_t colors;
    bool present, has_ping, has_social, has_player_info;
} frontend_remote_q1_client_row;
typedef struct frontend_remote_q1_view {
    const frontend_remote_q1 *owner;
    frontend_remote_q1_domain domain;
    frontend_remote_q1_content content;
    uint64_t revision, map_generation, received_ns, frame_number;
    double seconds, previous_seconds, sample_seconds, fraction;
    uint32_t view_entity, max_clients;
    qa_actor_id viewer;
    qa_vec3 view_angles;
    const qa_q1_clientdata *client_data;
    const char *skybox;
    bool bound, loaded, retired;
    const qa_resource *map;
    const qa_vfs_acquisition *map_opening;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_scene_world *world;
    qa_net_protocol_id protocol;
    bool published;
} frontend_remote_q1_view;
typedef struct frontend_remote_q1_entity_view {
    qa_actor_id actor;
    qa_q1_entity entity;
    const char *model;
    float alpha, scale;
    uint8_t top_color, bottom_color;
    bool has_colors, visible, view_weapon;
} frontend_remote_q1_entity_view;
typedef struct frontend_remote_q1_player_view {
    qa_actor_id actor;
    qa_vec3 origin, angles, kick_angles, velocity;
    float view_height, ideal_pitch;
    bool grounded, pitch_drift_disabled, intermission;
} frontend_remote_q1_player_view;
typedef struct frontend_remote_q1_media {
    qa_vfs *mounts;
    const qa_resource *map;
    const qa_vfs_acquisition *map_opening;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_audio_bank *sounds;
    const qa_scene_world *world;
} frontend_remote_q1_media;
typedef struct frontend_remote_q1_model_view {
    const char *path;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    const qa_model *model;
    const qa_scene_model *scene;
    const qa_scene_world *world;
    bool colors;
    uint8_t top, bottom;
} frontend_remote_q1_model_view;

bool frontend_remote_q1_create(qa_frontend *, const frontend_remote_q1_options *, frontend_remote_q1 **, qa_error *);
bool frontend_remote_q1_bind(frontend_remote_q1 *, const frontend_remote_q1_domain *, qa_error *);
bool frontend_remote_q1_receive_nq(frontend_remote_q1 *, const qa_nq_message *, uint64_t received_ns, qa_error *);
bool frontend_remote_q1_serverdata_qw(frontend_remote_q1 *, const qa_qw_serverdata *, qa_error *);
bool frontend_remote_q1_gamestate_qw(frontend_remote_q1 *, const char *const *models, size_t,
    const char *const *sounds, size_t, uint32_t *checksum, qa_error *);
bool frontend_remote_q1_receive_qw(frontend_remote_q1 *, const qa_qw_service *, uint64_t received_ns, qa_error *);
/* QW packet service order is arbitrary; commit after the entire actual
 * decoder batch so players/stats/nails following PACKETENTITIES are reached. */
bool frontend_remote_q1_receive_end(frontend_remote_q1 *, uint64_t received_ns, qa_error *);
bool frontend_remote_q1_disconnected(frontend_remote_q1 *, const char *reason, qa_error *);
bool frontend_remote_q1_sample(frontend_remote_q1 *, uint64_t now_ns, qa_error *);
bool frontend_remote_q1_metadata_read(const frontend_remote_q1 *, frontend_remote_q1_view *, qa_error *);
bool frontend_remote_q1_read(const frontend_remote_q1 *, frontend_remote_q1_view *, qa_error *);
bool frontend_remote_q1_current(const frontend_remote_q1_view *);
size_t frontend_remote_q1_entity_count(const frontend_remote_q1 *);
bool frontend_remote_q1_entity_at(frontend_remote_q1 *, size_t, frontend_remote_q1_entity_view *, qa_error *);
bool frontend_remote_q1_client_at(const frontend_remote_q1 *, uint32_t, frontend_remote_q1_client_row *, qa_error *);
const char *frontend_remote_q1_light_style(const frontend_remote_q1 *, uint32_t);
bool frontend_remote_q1_idle(const frontend_remote_q1 *);
bool frontend_remote_q1_media_read(const frontend_remote_q1 *, frontend_remote_q1_media *, qa_error *);
size_t frontend_remote_q1_model_count(const frontend_remote_q1 *);
bool frontend_remote_q1_model_at(const frontend_remote_q1 *, size_t, frontend_remote_q1_model_view *, qa_error *);
bool frontend_remote_q1_player_read(frontend_remote_q1 *, frontend_remote_q1_player_view *, bool *present, qa_error *);
/* Borrows the genuine enclosing physical CLIENT descriptor and namespace.
 * Reached content remains the independently retained row.content recipe. */
bool frontend_remote_q1_application_read(const frontend_remote_q1 *,qa_application_client_source *,qa_error *);
/* Existing retained topology only, including capture/import/retirement.
 * This does not admit a live command or prove Network currentness. */
bool frontend_remote_q1_application_metadata_read(const frontend_remote_q1 *,qa_application_client_source *,qa_error *);
bool frontend_remote_q1_draw(frontend_remote_q1 *, const qa_scene_view *,
    qa_audio_listener *, bool *rendered, qa_error *);
bool frontend_remote_q1_destroy(frontend_remote_q1 **, qa_error *);
size_t frontend_remote_q1_count(const qa_frontend *);
frontend_remote_q1 *frontend_remote_q1_at(const qa_frontend *, size_t);
bool frontend_remote_q1_sample_all(qa_frontend *, uint64_t, qa_error *);
bool frontend_remote_q1_destroy_all(qa_frontend *, qa_error *);
bool frontend_remote_q1_hooks(frontend_remote_q1 *, qa_network_q1_client_hooks *, qa_error *);
bool frontend_remote_q1_demo_seed(frontend_remote_q1 *, const frontend_demo_sink *, qa_nq_options, qa_error *);
bool frontend_remote_q1_demo_attach(frontend_remote_q1 *, const frontend_demo_sink *, bool *attached, qa_error *);
bool frontend_remote_q1_demo_detach(frontend_remote_q1 *, const frontend_demo_sink *, qa_error *);
bool frontend_remote_q1_demo_angles(frontend_remote_q1 *, const float angles[3], qa_error *);
struct frontend_remote_q1_skins *frontend_remote_q1_skins_owner(const frontend_remote_q1 *);
bool frontend_remote_q1_player_command(frontend_remote_q1 *, qa_actor_id, const char *,
    const char *const *, size_t, qa_error *);
#endif

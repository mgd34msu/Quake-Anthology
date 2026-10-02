#ifndef QA_FRONTEND_REMOTE_Q2_CLIENT_H
#define QA_FRONTEND_REMOTE_Q2_CLIENT_H
#include "qa/frontend.h"
#include "qa/network_q2_session.h"
#include "qa/audio.h"
#include "qa/scene.h"
#include "qa/input.h"
#include "qa/font.h"
#include "qa/collision.h"
#include "qa/persistence_content.h"
#include "qa/application_client.h"

typedef struct frontend_remote_q2 frontend_remote_q2;
/* The enclosing Source factory supplies its actual authenticated CLIENT
 * registry and full connection identity. These fields grant no GAME role. */
typedef struct frontend_remote_q2_domain {
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
} frontend_remote_q2_domain;
typedef struct frontend_remote_q2_content {
    qa_catalog *catalog;
    qa_product_id selected, base;
    qa_vfs *mounts;
    qa_fs_root *selected_write_root, *base_write_root;
} frontend_remote_q2_content;
typedef struct frontend_remote_q2_options {
    frontend_remote_q2_domain domain;
    bool material_scripts;
    void *context;
    bool (*current)(void *, const frontend_remote_q2_domain *, qa_error *);
    bool (*download_allowed)(void *, const char *path, bool *allowed, qa_error *);
    bool (*download_nonce)(void *, uint64_t *, qa_error *);
    bool (*entity_actor)(void *, const frontend_remote_q2_domain *, uint32_t received_number,
        qa_actor_id *, qa_error *);
    bool (*content_admit)(void *, uint64_t loading_generation,
        const frontend_remote_q2_content *, qa_error *);
    /* Returns borrowed actual fresh catalog roots/view after server directory
     * selection. READY means this retained operation completed, not media Init. */
    bool (*select_content)(void *, uint64_t loading_generation,
        const qa_q2_serverdata *, frontend_remote_q2_content *, qa_q2_preparation *, qa_error *);
    bool (*records)(void *, const frontend_remote_q2_domain *,
        const qa_q2_server_record *, size_t, qa_error *);
    bool (*disconnected)(void *, const frontend_remote_q2_domain *, const char *, qa_error *);
    bool (*entities_changed)(void *, const frontend_remote_q2_domain *, qa_error *);
} frontend_remote_q2_options;
typedef struct frontend_remote_q2_view {
    const frontend_remote_q2 *owner;
    frontend_remote_q2_domain domain;
    uint64_t identity, loading_generation, content_generation, received_ns;
    const qa_q2_serverdata *server_data;
    frontend_remote_q2_content content;
    const qa_resource *map;
    const qa_vfs_acquisition *map_opening;
    const qa_q2_wire_frame *frame, *previous;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_audio_bank *sounds;
    qa_scene_world *world;
    bool media_ready, retired;
    qa_collision_geometry *geometry;
} frontend_remote_q2_view;
typedef struct frontend_remote_q2_model_view {
    const char *path;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    const qa_model *model;
    qa_scene_model *scene;
} frontend_remote_q2_model_view;
size_t frontend_remote_q2_count(const qa_frontend *);
frontend_remote_q2 *frontend_remote_q2_at(const qa_frontend *, size_t);
size_t frontend_remote_q2_model_count(const frontend_remote_q2 *);
bool frontend_remote_q2_model_at(const frontend_remote_q2 *, size_t,
    frontend_remote_q2_model_view *, qa_error *);
qa_font_library *frontend_remote_q2_fonts(const frontend_remote_q2 *);
bool frontend_remote_q2_create(qa_frontend *, const frontend_remote_q2_options *,
    frontend_remote_q2 **, qa_error *);
/* Commit the real attach result. Pending constructors own no connection ID. */
bool frontend_remote_q2_bind(frontend_remote_q2 *, const frontend_remote_q2_domain *, qa_error *);
bool frontend_remote_q2_hooks(frontend_remote_q2 *, qa_network_q2_client_hooks *, qa_error *);
bool frontend_remote_q2_read(const frontend_remote_q2 *, frontend_remote_q2_view *, qa_error *);
bool frontend_remote_q2_current(const frontend_remote_q2_view *);
bool frontend_remote_q2_metadata_read(const frontend_remote_q2 *, frontend_remote_q2_view *, qa_error *);
bool frontend_remote_q2_idle(const qa_frontend *);
bool frontend_remote_q2_destroy(frontend_remote_q2 **, qa_error *);
bool frontend_remote_q2_destroy_all(qa_frontend *, qa_error *);
bool frontend_remote_q2_draw(qa_frontend *, uint32_t physical_seat, float stereo,
    qa_audio_listener *, bool *rendered, qa_error *);
bool frontend_remote_q2_sample(qa_frontend *, uint64_t now_ns, qa_error *);
bool frontend_remote_q2_input(qa_frontend *, uint32_t physical_seat,
    const qa_seat_input_sample *, uint64_t sequence, qa_q2_usercmd *, bool *handled,
    bool *command_ready, qa_error *);
bool frontend_remote_q2_content_visit(const qa_frontend *,
    const qa_application_content_visitor *, qa_error *);
const char *frontend_remote_q2_config(const frontend_remote_q2 *, uint16_t);
/* Pure observation of actual received entity/player rows. It grants no
 * canonical actor identity; the application observer owns that admission. */
bool frontend_remote_q2_entity_received(const frontend_remote_q2 *, uint32_t source_number);
/* Client number belongs to the negotiated player-state field or the genuine
 * serverdata split-seat receipt. It is not a canonical actor identifier. */
bool frontend_remote_q2_player_number(const frontend_remote_q2 *, const qa_q2_wire_frame *,
    size_t player_index, int32_t *client_number);
bool frontend_remote_q2_entity_generation(const frontend_remote_q2 *, uint32_t source_number,
    uint64_t *content_generation);
bool frontend_remote_q2_entity_publication_read(const frontend_remote_q2 *,
    qa_application_client_entity_publication *);
bool frontend_remote_q2_wire_seat(const frontend_remote_q2 *, uint32_t *remote_index, qa_error *);
/* Pure late attachment takes the exact candidate runtime/registry binding;
 * immutable content and media children are imported separately by graph IDs. */
bool frontend_remote_q2_rebind_ready(const frontend_remote_q2 *, qa_frontend *,
    const frontend_remote_q2_options *, qa_error *);
void frontend_remote_q2_rebind(frontend_remote_q2 *, qa_frontend *,
    const frontend_remote_q2_options *);
#endif

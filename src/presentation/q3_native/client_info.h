#ifndef QA_Q3_NATIVE_CLIENT_INFO_H
#define QA_Q3_NATIVE_CLIENT_INFO_H

#include "qa/application_native_q3_presentation.h"
#include "qa/application_native_q3_wire.h"
#include "qa/q3_presentation.h"

typedef struct q3n_clients q3n_clients;
typedef struct q3n_remote_source q3n_remote_source;
typedef struct q3n_remote_source_view q3n_remote_source_view;
typedef struct q3n_client_settings {
    bool force_model, defer_players, build_script, loading;
    char model[64], head_model[64], red_team_name[64], blue_team_name[64];
    size_t memory_remaining;
} q3n_client_settings;
typedef struct q3n_client_dynamic {
    int32_t score, location, health, armor, cur_weapon, powerups;
    int32_t medkit_usage_time, invulnerability_start_time, invulnerability_stop_time, breath_puff_time;
} q3n_client_dynamic;
typedef struct q3n_client_info {
    bool observed, info_valid, deferred, new_anims, team_leader;
    uint32_t physical_client;
    uint64_t configstring_revision, media_revision;
    char name[64], model_name[64], skin_name[64], head_model_name[64], head_skin_name[64];
    char red_team[32], blue_team[32];
    int32_t team, bot_skill, handicap, wins, losses, team_task;
    qa_vec3 color1, color2;
    int32_t models[3], skins[3], icon, sounds[32];
    qa_player_animation_config animations;
    q3n_client_dynamic dynamic;
} q3n_client_info;
typedef struct q3n_client_options {
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
    qa_native_q3_wire_reader *reader;
    q3n_remote_source *remote_source;
    void *context;
    void (*print)(void *, const char *);
} q3n_client_options;
bool q3n_clients_create(const q3n_client_options *, q3n_clients **, qa_error *);
bool q3n_clients_create_remote(const q3n_client_options *, q3n_clients **, qa_error *);
void q3n_clients_destroy(q3n_clients *);
bool q3n_clients_idle(const q3n_clients *);
qa_q3_presentation_assets *q3n_clients_assets(const q3n_clients *);
bool q3n_clients_remote_current(const q3n_clients *, const q3n_remote_source_view *, qa_error *);
const q3n_client_info *q3n_clients_get(const q3n_clients *, uint32_t physical_client);
/* Reads CS_PLAYERS through the borrowed wire client's reached gamestate.
 * The physical GAME observer still qualifies source and actor lifetime.
 * Successful media replacement increments media_revision to reset real poses. */
bool q3n_clients_sync(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, const q3n_client_settings *, qa_error *);
bool q3n_clients_register_one(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, const q3n_client_settings *,
    uint32_t physical_client, qa_error *);
/* Constructor registration begins with the observer's real local seat row. */
bool q3n_clients_initialize(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, const q3n_client_settings *,
    uint32_t seat, qa_error *);
/* The actual cvar owner calls this when cg_forceModel's modification count
 * changes, including a modification whose integer value remains unchanged. */
bool q3n_clients_reload(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, const q3n_client_settings *, qa_error *);
/* Console disposal clears the genuine donor cells and animation holders.
 * Reached row stamps survive so ordinary sync cannot replay unchanged CS;
 * a later explicit CS registration or force-model reload may load them again. */
bool q3n_clients_reset(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, qa_error *);
bool q3n_clients_load_deferred(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, const q3n_client_settings *, qa_error *);
bool q3n_clients_custom_sound(q3n_clients *, int32_t physical_client,
    const char *, int32_t *, qa_error *);
bool q3n_clients_dynamic_write(q3n_clients *, qa_application *,
    const qa_application_native_q3_presentation *, uint32_t physical_client,
    uint64_t configstring_revision, uint64_t media_revision, const q3n_client_dynamic *, qa_error *);
/* The held receipt qualifies the actual received CLIENT source and reached
 * rows through every media callback. Initialization uses its decoded viewer. */
bool q3n_clients_remote_register_one(q3n_clients *, const q3n_remote_source_view *,
    const q3n_client_settings *, uint32_t physical_client, qa_error *);
bool q3n_clients_remote_sync(q3n_clients *, const q3n_remote_source_view *,
    const q3n_client_settings *, qa_error *);
bool q3n_clients_remote_initialize(q3n_clients *, const q3n_remote_source_view *,
    const q3n_client_settings *, qa_error *);
bool q3n_clients_remote_reload(q3n_clients *, const q3n_remote_source_view *,
    const q3n_client_settings *, qa_error *);
bool q3n_clients_remote_reset(q3n_clients *, const q3n_remote_source_view *, qa_error *);
bool q3n_clients_remote_load_deferred(q3n_clients *, const q3n_remote_source_view *,
    const q3n_client_settings *, qa_error *);
bool q3n_clients_remote_dynamic_write(q3n_clients *, const q3n_remote_source_view *,
    uint32_t physical_client, uint64_t configstring_revision, uint64_t media_revision,
    const q3n_client_dynamic *, qa_error *);

/* The aggregate holds the backend registry through these codecs. Resource
 * resolution returns genuine imported immutable holders, without acquisition. */
typedef struct q3n_client_refs {
    void *context;
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, const qa_resource **, qa_error *);
} q3n_client_refs;
bool q3n_clients_checkpoint(const q3n_clients *, const q3n_client_refs *, qa_buffer *, qa_error *);
bool q3n_clients_restore(q3n_clients *, const q3n_client_refs *, qa_bytes, qa_error *);
/* Counted physical rows, including absent rows. Returned holder/receipt borrow
 * this idle owner. Each nonnull row owns exactly one immutable resource ref. */
bool q3n_clients_animation_holder(const q3n_clients *, uint32_t physical_client,
    const qa_resource **, const qa_vfs_acquisition **, qa_error *);

#endif

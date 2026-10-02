#ifndef QA_FRONTEND_UNIFIED_Q3_SOURCES_H
#define QA_FRONTEND_UNIFIED_Q3_SOURCES_H

#include "remote_unified_media.h"
#include "qa/network_q3.h"

typedef struct frontend_unified_q3_sources frontend_unified_q3_sources;
typedef struct frontend_unified_q3_source_frame frontend_unified_q3_source_frame;
typedef struct frontend_unified_q3_source_retirement frontend_unified_q3_source_retirement;
typedef struct frontend_unified_q3_client frontend_unified_q3_client;
typedef struct frontend_unified_q3_source_entity {
    qa_actor_id actor;
    qa_q3_entity state;
    qa_vec3 origin;
    qa_bounds link_bounds;
    uint32_t server_flags;
    int32_t single_client;
    bool present, linked;
} frontend_unified_q3_source_entity;
typedef struct frontend_unified_q3_source_player {
    qa_actor_id actor;
    qa_q3_player state;
    uint32_t source_number, client_slot;
} frontend_unified_q3_source_player;
typedef struct frontend_unified_q3_source_view {
    const frontend_unified_q3_sources *owner;
    const void *source;
    uint64_t revision, publication, map_revision;
    uint32_t epoch;
    const qa_recipe_provider *provider;
    const char *instance, *content;
    qa_vfs *files;
    const qa_product *content_product;
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
    qa_actor_id viewer;
    int32_t time, level_start, game_type;
    uint32_t max_clients, entity_count, client_number;
    uint8_t snapshot_bit;
    bool has_client;
    const frontend_unified_q3_source_entity *entities; /* physical 1024 rows */
    const frontend_unified_q3_source_player *players;
    size_t player_count;
    const qa_q3_gamestate *game_state; /* authoritative compiled Source copy */
    const uint64_t *configstring_revisions;
    const uint16_t *visible_entities;
    size_t visible_count;
    const uint8_t *area_mask; /* actual Source selection, 32 bytes */
} frontend_unified_q3_source_view;

/* Actual received Source observations, not an original Network session or
 * local GAME. The enclosing replica/media owns the content and actor aliases. */
bool frontend_unified_q3_sources_create(frontend_remote_unified *, frontend_unified_media *,
    frontend_unified_q3_sources **, qa_error *);
bool frontend_unified_q3_sources_prepare(frontend_unified_q3_sources *, const qa_unified_document *,
    frontend_unified_q3_source_frame **, qa_error *);
bool frontend_unified_q3_sources_ready(const frontend_unified_q3_source_frame *);
size_t frontend_unified_q3_source_frame_count(const frontend_unified_q3_source_frame *);
bool frontend_unified_q3_source_frame_read(const frontend_unified_q3_source_frame *, size_t,
    frontend_unified_q3_source_view *, qa_error *);
void frontend_unified_q3_sources_commit(frontend_unified_q3_source_frame **);
void frontend_unified_q3_sources_abort(frontend_unified_q3_source_frame **);
size_t frontend_unified_q3_sources_count(const frontend_unified_q3_sources *);
bool frontend_unified_q3_sources_read(const frontend_unified_q3_sources *, size_t,
    frontend_unified_q3_source_view *, qa_error *);
/* A held update does not retire the published rows until its nofail commit.
 * Round preparation uses these actual old receipts alongside its candidate. */
size_t frontend_unified_q3_sources_committed_count(const frontend_unified_q3_sources *);
bool frontend_unified_q3_sources_committed_read(const frontend_unified_q3_sources *, size_t,
    frontend_unified_q3_source_view *, qa_error *);
bool frontend_unified_q3_source_current(const frontend_unified_q3_source_view *);
bool frontend_unified_q3_sources_current(const frontend_unified_q3_sources *);
/* Pure cold-owner receipts. They require the replica's actual capture or
 * isolated restore admission and grant no ordinary frame/effect authority. */
bool frontend_unified_q3_sources_checkpoint_current(const frontend_unified_q3_sources *);
bool frontend_unified_q3_source_checkpoint_current(const frontend_unified_q3_source_view *);
bool frontend_unified_q3_source_staged_checkpoint_current(const frontend_unified_q3_source_view *);
/* Structural custody for checked cleanup and cold continuation. Removed rows
 * never gain ordinary Source/draw authority through this receipt. */
bool frontend_unified_q3_source_retirement_prepare(const frontend_unified_q3_source_view *,
    frontend_unified_q3_source_retirement **,qa_error *);
bool frontend_unified_q3_source_retirement_current(const frontend_unified_q3_source_retirement *);
bool frontend_unified_q3_source_retirement_departed(const frontend_unified_q3_source_retirement *);
bool frontend_unified_q3_source_retirement_checkpoint_current(const frontend_unified_q3_source_retirement *);
bool frontend_unified_q3_source_retirement_client_hold(frontend_unified_q3_source_retirement *,
    const frontend_unified_q3_client *,qa_error *);
bool frontend_unified_q3_source_retirement_client_drop(frontend_unified_q3_source_retirement *,
    const frontend_unified_q3_client *,qa_error *);
bool frontend_unified_q3_source_retirement_read(const frontend_unified_q3_source_retirement *,
    frontend_unified_q3_source_view *,qa_error *);
bool frontend_unified_q3_source_retirement_return(frontend_unified_q3_source_retirement **,qa_error *);
bool frontend_unified_q3_source_retirement_checkpoint(const frontend_unified_q3_source_retirement *,qa_buffer *,qa_error *);
bool frontend_unified_q3_source_retirement_restore(frontend_unified_q3_sources *,qa_bytes,
    frontend_unified_q3_source_retirement **,qa_error *);
bool frontend_unified_q3_sources_checkpoint_read(const frontend_unified_q3_sources *, size_t,
    frontend_unified_q3_source_view *, qa_error *);
bool frontend_unified_q3_sources_idle(const frontend_unified_q3_sources *);
bool frontend_unified_q3_sources_destroy(frontend_unified_q3_sources **, qa_error *);
bool frontend_unified_q3_sources_checkpoint(const frontend_unified_q3_sources *, qa_buffer *, qa_error *);
bool frontend_unified_q3_sources_checkpoint_stage(const frontend_unified_q3_sources *,
    const frontend_unified_q3_source_frame *, qa_buffer *, qa_error *);
bool frontend_unified_q3_source_frame_checkpoint(const frontend_unified_q3_source_frame *, qa_buffer *, qa_error *);
bool frontend_unified_q3_source_frame_checkpoint_ready(const frontend_unified_q3_source_frame *);
bool frontend_unified_q3_sources_restore_prepared(frontend_unified_q3_sources *, qa_bytes,
    const qa_unified_document *, frontend_unified_q3_source_frame **, qa_error *);
bool frontend_unified_q3_sources_checkpoint_stage_read(const frontend_unified_q3_sources *,
    const frontend_unified_q3_source_frame *, bool staged, size_t,
    frontend_unified_q3_source_view *, qa_error *);
bool frontend_unified_q3_sources_restore(frontend_remote_unified *, frontend_unified_media *,
    qa_bytes, frontend_unified_q3_sources **, qa_error *);

#endif

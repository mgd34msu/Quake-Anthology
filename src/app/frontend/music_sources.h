#ifndef QA_FRONTEND_MUSIC_SOURCES_H
#define QA_FRONTEND_MUSIC_SOURCES_H
#include "shared_music_policy.h"
#include "qa/executable_recipe.h"

typedef struct frontend_music_sources frontend_music_sources;
typedef enum frontend_music_slot { FRONTEND_MUSIC_MENU, FRONTEND_MUSIC_WORLD } frontend_music_slot;
typedef enum frontend_music_origin_kind {
    FRONTEND_MUSIC_SOURCE, FRONTEND_MUSIC_NATIVE, FRONTEND_MUSIC_REMOTE, FRONTEND_MUSIC_MODULE,
    FRONTEND_MUSIC_COMPONENT
} frontend_music_origin_kind;
/* The actual first-role declaration and player, supplied by the constructor
 * which owns this bus. Equal declarations may share a SOURCE group; different
 * declarations must retain their own real group. Callbacks are borrowed only
 * until that actual caller retires this origin. */
typedef struct frontend_music_origin {
    frontend_music_origin_kind kind;
    uint64_t bus;
    uint32_t physical_seat;
    qa_actor_owner receiver;
    const qa_launch_instance *descriptor;
    /* A remote COMPONENT instead borrows its actual executable recipe/provider.
     * Its caller retains that recipe until explicit_retire; no local launch
     * descriptor is synthesized for the remote declaration. */
    const qa_executable_recipe *recipe;
    const qa_recipe_provider *recipe_provider;
    const char *recipe_content;
    qa_catalog *catalog;
    qa_product_id product;
    const qa_vfs *files;
    qa_audio_music *music;
    void *context;
    bool (*current)(void *, const struct frontend_music_origin *);
    /* Optional retained caller proof for capture and isolated cold binding.
     * It proves this exact declaration/player after connection retirement;
     * ordinary playback continues to require current. */
    bool (*checkpoint_current)(void *, const struct frontend_music_origin *);
    bool (*stop)(void *, qa_error *);
} frontend_music_origin;
/* Fresh construction selects independent menu mounts from the actual initial
 * catalog/--game declaration. An empty catalog truthfully has no soundtrack.
 * WORLD selection reads the actual published ENTITIES binding and retained BSP.
 * It never derives a source product from prepared scene geometry. */
bool frontend_music_sources_create(qa_frontend *, frontend_music_sources **, qa_error *);
bool frontend_music_sources_world(frontend_music_sources *, qa_error *);
bool frontend_music_sources_world_retire(frontend_music_sources *, qa_error *);
/* Begin retires the prior soundtrack before actual cue admission. The policy
 * shares the source-owned player instead of manufacturing another soundtrack.
 * The returned boundary supplies actual intro/loop continuation after start. */
bool frontend_music_sources_explicit_begin(frontend_music_sources *, const frontend_music_origin *, qa_error *);
bool frontend_music_sources_explicit_play(frontend_music_sources *, const frontend_music_origin *, const char *, qa_error *);
bool frontend_music_sources_explicit_pause(frontend_music_sources *, const frontend_music_origin *, bool, qa_error *);
bool frontend_music_sources_received_pause(frontend_music_sources *,bool,qa_error *);
bool frontend_music_sources_explicit_selected(const frontend_music_sources *, const frontend_music_origin *);
bool frontend_music_sources_explicit(frontend_music_sources *, const frontend_music_origin *,
    const char *intro, const char *loop, bool looping, qa_error *);
bool frontend_music_sources_explicit_retire(frontend_music_sources *, const void *actual_context, qa_error *);
/* Late cold alias binding, after the actual source player and engine import.
 * Saved identities must resolve to a genuine constructed caller origin. */
bool frontend_music_sources_restore_origin(frontend_music_sources *, const frontend_music_origin *, qa_error *);
/* Pure saved namespace/declaration match for each genuine restored caller.
 * Finish rejects a saved origin which no actual caller has rebound. */
bool frontend_music_sources_restore_origin_matches(const frontend_music_sources *, const frontend_music_origin *);
bool frontend_music_sources_idle(const frontend_music_sources *);
/* Pure retained request inventory, including while a policy child is held.
 * Preparation forbids accepting another request until that child returns. */
bool frontend_music_sources_queued(const frontend_music_sources *, size_t *count);
/* Pure installed constructor/slot proof, including a retained prepared child.
 * Genuine empty catalogs/no-audio and no published WORLD have absent slots. */
bool frontend_music_sources_parent_is(const frontend_music_sources *, const qa_frontend *, const qa_audio_engine *);
/* Actual published canonical preferences, including a genuinely empty
 * soundtrack inventory. Menu text is borrowed until cvar mutation. */
bool frontend_music_sources_preferences_read(const frontend_music_sources *, bool *shuffle, const char **menu_track);
/* Actual application-owned controls; borrowed until checked source retirement.
 * Restoring access is only for late binding the imported external players. */
qa_audio_music_controls *frontend_music_sources_controls(const frontend_music_sources *);
bool frontend_music_sources_destroy(frontend_music_sources **, qa_error *);
frontend_music_policy *frontend_music_sources_policy(const frontend_music_sources *, frontend_music_slot);
bool frontend_music_sources_bus(const frontend_music_sources *, frontend_music_slot, uint64_t *);
bool frontend_music_sources_update(frontend_music_sources *, qa_error *);
/* Called by the real menu/GAME output publication, independently of whether a
 * client or map exists. This selection is retained across cold import. */
bool frontend_music_sources_output(frontend_music_sources *, frontend_music_slot, qa_error *);
/* Menu requests retain copied arguments and the actual captured origin. Flush
 * follows menu preference selection, before pending source publication. */
bool frontend_music_sources_flush(frontend_music_sources *, qa_error *);
bool frontend_music_sources_command(frontend_music_sources *, const qa_command_invocation *, qa_error *);
size_t frontend_music_sources_bank_count(const frontend_music_sources *);
qa_audio_bank *frontend_music_sources_bank_at(const frontend_music_sources *, size_t);
bool frontend_music_sources_content_visit(const frontend_music_sources *, const qa_application_content_visitor *, qa_error *);
void frontend_music_sources_rebind(frontend_music_sources *, qa_frontend *, frontend_music_sources **);
/* Metadata/banks are prepared before the genuine bank/engine import. Finish
 * binds the imported playback holders; neither stage opens or starts music. */
bool frontend_music_sources_checkpoint(const frontend_music_sources *, const qa_application_content_graph *,
    const qa_audio_checkpoint_refs *, qa_buffer *, qa_error *);
bool frontend_music_sources_restore_prepare(qa_frontend *, qa_application_content_graph *,
    const qa_audio_checkpoint_refs *, qa_bytes, frontend_music_sources **, qa_error *);
bool frontend_music_sources_restore_finish(frontend_music_sources *, qa_error *);
#endif

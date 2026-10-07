#ifndef QA_FRONTEND_SHARED_MUSIC_POLICY_H
#define QA_FRONTEND_SHARED_MUSIC_POLICY_H
#include "internal.h"
#include "qa/audio_music_prepare.h"
#include "qa/audio_music_engine.h"
#include "qa/audio_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/application_client_prepare.h"

typedef struct frontend_music_policy frontend_music_policy;
typedef struct frontend_shared_music frontend_shared_music;
typedef struct frontend_shared_audio frontend_shared_audio;
/* Receipts come from actual music constructor selection, independently of
 * typography. Files are cloned; catalog/product preserve the true declaration.
 * The optional fallback is the already selected official Q1 counterpart. */
typedef struct frontend_music_content {
    qa_catalog *catalog;
    qa_product_id product, fallback_product;
    const qa_vfs *files, *fallback_files;
} frontend_music_content;
typedef struct frontend_music_policy_options {
    qa_audio_music *music;
    uint64_t bus;
    uint32_t audience;
    float bus_gain;
    bool menu;
    /* Genuine explicit source player; its constructor/codec owns the player
     * independently of attachment. The policy retains only an alias reference. */
    bool external_player;
    /* Menu: theme first when present, selected family last. Gameplay: one
     * actual source row, with its exact authored WORLD cue below. */
    const frontend_music_content *sources;
    size_t source_count;
    const char *authored_cue;
    /* Independent constructor-owned music RNG; never the simulation RNG.
     * The producer supplies the actual fresh seed. Import restores its state. */
    uint64_t random_seed;
} frontend_music_policy_options;
/* Creation performs genuine mounted playlist enumeration but no playback or
 * source clock work. The caller passes its installed owner slot and retains
 * the attached music/engine until checked destruction has returned. */
bool frontend_music_policy_create(qa_frontend *, const frontend_music_policy_options *,
    frontend_music_policy **, qa_error *);
bool frontend_music_policy_idle(const frontend_music_policy *);
bool frontend_music_policy_binding_is(const frontend_music_policy *, const qa_frontend *,
    const qa_audio_engine *, uint64_t bus, bool menu);
bool frontend_music_policy_catalog_adopt(frontend_music_policy *,qa_catalog *previous,
    qa_catalog *published,qa_error *);
qa_audio_music *frontend_music_policy_player(const frontend_music_policy *);
/* Genuine output/start boundary. Reattaches a retained stopped player after
 * engine stop-all; it never opens tracks or advances the playlist/RNG. */
bool frontend_music_policy_attach(frontend_music_policy *, qa_error *);
bool frontend_music_policy_destroy(frontend_music_policy **, qa_error *);
/* Real pre-pump automatic completion boundary. Menu remains fixed. */
bool frontend_music_policy_update(frontend_music_policy *, qa_error *);
bool frontend_music_policy_random(const frontend_music_policy *, uint64_t *);
/* Actual explicit source music/CD commands retire automatic playback. */
bool frontend_music_policy_command(frontend_music_policy *, const qa_command_invocation *, qa_error *);
bool frontend_music_policy_manual_start(const frontend_music_policy *, const qa_command_invocation *, bool *);
bool frontend_music_policy_explicit(frontend_music_policy *, const char *intro, const char *loop, bool looping, qa_error *);
bool frontend_music_policy_source_play(frontend_music_policy *, const char *, qa_error *);
bool frontend_music_policy_world_cd(frontend_music_policy *, unsigned track, qa_error *);
/* Prepare before shared audio gains; all file admission, bag/RNG selection and
 * PCM allocation occurs offside. Ready binds the exact retained gains parent.
 * No parser, RNG, source/native query or decoder runs in ready_is/publish. */
bool frontend_shared_music_prepare(qa_frontend *, const qa_launch_snapshot *, const qa_cvars_edit *,
    frontend_music_policy *, frontend_shared_music **, qa_error *);
bool frontend_shared_music_prepare_client(qa_frontend *, const qa_application_client_preparation *,
    const qa_cvars_edit *, frontend_music_policy *, frontend_shared_music **, qa_error *);
bool frontend_shared_music_ready(frontend_shared_music *, const frontend_shared_audio *, qa_error *);
bool frontend_shared_music_ready_is(const frontend_shared_music *);
void frontend_shared_music_publish(frontend_shared_music **);
bool frontend_shared_music_abort(frontend_shared_music **, qa_error *);
size_t frontend_music_policy_bank_count(const frontend_music_policy *);
qa_audio_bank *frontend_music_policy_bank_at(const frontend_music_policy *, size_t);
size_t frontend_music_policy_track_count(const frontend_music_policy *);
const char *frontend_music_policy_track_at(const frontend_music_policy *, size_t);
bool frontend_music_policy_content_visit(const frontend_music_policy *,
    const qa_application_content_visitor *, qa_error *);
bool frontend_music_policy_restore_player(frontend_music_policy *, qa_audio_music *, qa_error *);
bool frontend_music_policy_source_is(const frontend_music_policy *, const frontend_music_content *);
/* Pure automatic WORLD binding to the actual retained declaration mounts and
 * official fallback product. Cold import never reopens that fallback plan. */
bool frontend_music_policy_world_is(const frontend_music_policy *, qa_catalog *, qa_product_id,
    const qa_vfs *, qa_product_id fallback_product);
void frontend_music_policy_rebind(frontend_music_policy *, qa_frontend *, frontend_music_policy **);
#endif

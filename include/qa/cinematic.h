#ifndef QA_CINEMATIC_H
#define QA_CINEMATIC_H

#include "qa/audio.h"
#include "qa/ogv.h"
#include "qa/scene.h"

typedef enum qa_cinematic_format {
    QA_CINEMATIC_CIN,
    QA_CINEMATIC_ROQ,
    QA_CINEMATIC_OGV,
    QA_CINEMATIC_IMAGE
} qa_cinematic_format;
typedef enum qa_cinematic_target_kind {
    QA_CINEMATIC_SEAT,
    QA_CINEMATIC_MATERIAL
} qa_cinematic_target_kind;
typedef struct qa_cinematic_target {
    qa_cinematic_target_kind kind;
    union {
        uint32_t seat;
        uint64_t material;
    } id;
} qa_cinematic_target;
typedef struct qa_cinematic_asset qa_cinematic_asset;
typedef struct qa_cinematic_source {
    const char *name;
    qa_cinematic_format format;
    /* Set by the shared library; preserves its cache lease during playback. */
    qa_cinematic_asset *asset;
    union {
        qa_cin_asset *cin;
        qa_media_input *roq;
        qa_ogv_asset *ogv;
        const qa_scene_image *image;
    } data;
} qa_cinematic_source;
typedef enum qa_cinematic_end {
    QA_CINEMATIC_FINISHED,
    QA_CINEMATIC_SKIPPED,
    QA_CINEMATIC_STOPPED
} qa_cinematic_end;
typedef enum qa_cinematic_audio_kind {
    QA_CINEMATIC_AUDIO_TARGET,
    QA_CINEMATIC_AUDIO_SEAT,
    QA_CINEMATIC_AUDIO_WORLD
} qa_cinematic_audio_kind;
typedef struct qa_cinematic_audio_audience {
    qa_cinematic_audio_kind kind;
    uint32_t seat;
} qa_cinematic_audio_audience;
typedef struct qa_cinematic_options {
    qa_media_clock clock;
    /* Explicit original Q3 shared decoder: its clock is the absolute Source
     * clock. NULL keeps ordinary movie-local elapsed playback. */
    qa_roq_scratch *roq_scratch;
    qa_cinematic_target target;
    bool loop, hold, silent;
    qa_audio_engine *audio;
    uint64_t audio_bus;
    float gain;
    /* Zero preserves target-derived routing. A material shown in one seat may
     * explicitly keep its audio private to that seat. Immutable during play. */
    qa_cinematic_audio_audience audio_audience;
    void *context;
    /* Callbacks may queue transitions; lifetime changes wait until the call
     * returns. The shared audio engine must outlive this cinematic. */
    void (*complete)(void *, qa_cinematic_target, qa_cinematic_end);
    void (*diagnostic)(void *, const char *);
} qa_cinematic_options;
typedef struct qa_cinematic_checkpoint {
    char *source;
    qa_cinematic_format format;
    qa_cinematic_target target;
    qa_cinematic_audio_audience audio_audience;
    double elapsed_ms;
    qa_media_status status, decoder_status;
    uint64_t revision, audio_loop;
    bool loop, hold, silent, paused, dirty, completed, focus_paused, audio_attached;
    qa_buffer audio;
    union {
        qa_cin_playback_checkpoint cin;
        qa_roq_playback_checkpoint roq;
        qa_ogv_checkpoint ogv;
        qa_sha256_digest image;
    } decoder;
} qa_cinematic_checkpoint;
typedef struct qa_cinematic qa_cinematic;
typedef struct qa_media_library qa_media_library;
/* One asset cache can serve multiple independently ordered VFS views. Resolve
 * a view first, then share by content digest, format and admission mode. */
qa_media_library *qa_media_library_create(qa_scene_resources *, qa_error *);
void qa_media_library_destroy(qa_media_library *);
void qa_media_library_trim(qa_media_library *);
bool qa_media_library_load(qa_media_library *, qa_vfs *, const char *path, qa_cinematic_asset **out,
                           qa_error *);
/* Shader registration opens its authored provider path before qualifying the
 * RoQ/CIN/OGV format, matching material directive evaluation order. */
bool qa_media_library_load_shader(qa_media_library *, qa_vfs *, const char *path,
    qa_cinematic_asset **out, qa_error *);
/* Original numeric CIN opens the exact requested path and admits RoQ by
 * header magic. An acquired empty file is an absent handle. */
bool qa_media_library_load_source_roq(qa_media_library *, qa_vfs *, const char *path,
    qa_cinematic_asset **out, qa_error *);
void qa_cinematic_asset_retain(qa_cinematic_asset *);
void qa_cinematic_asset_release(qa_cinematic_asset *);
qa_cinematic_source qa_cinematic_asset_source(const qa_cinematic_asset *);
void qa_cinematic_asset_dimensions(const qa_cinematic_asset *, uint32_t *width, uint32_t *height);
/* Sources are borrowed only during construction; private codec instances
 * retain shared assets. Restore builds a new movie without output callbacks.
 * Its bus ID must be reserved for this movie in the candidate audio engine. */
bool qa_cinematic_create(const qa_cinematic_source *, const qa_cinematic_options *,
                         const qa_cinematic_checkpoint *, qa_cinematic **out, qa_error *);
void qa_cinematic_destroy(qa_cinematic *);
bool qa_cinematic_tick(qa_cinematic *, qa_media_tick *, qa_error *);
bool qa_cinematic_pause(qa_cinematic *, bool, qa_error *);
bool qa_cinematic_end_playback(qa_cinematic *, qa_cinematic_end, qa_error *);
qa_media_status qa_cinematic_status(const qa_cinematic *);
qa_cinematic_target qa_cinematic_destination(const qa_cinematic *);
const qa_media_frame *qa_cinematic_frame(const qa_cinematic *);
/* Original RoQ shader upload exposes the decoder's actual physical 256-square
 * prefix. Other supported shader codecs retain their genuine decoded view. */
bool qa_cinematic_shader_frame(const qa_cinematic *, qa_media_frame *, qa_error *);
/* A reused numeric handle retains its creating SEAT/MATERIAL target. This
 * pure view also qualifies already decoded cold candidates before commit. */
bool qa_cinematic_upload_frame(const qa_cinematic *, bool shader, qa_media_frame *, qa_error *);
/* Original Source UI selects its actual limited draw dimensions. Dirty
 * resampling belongs to this reached call; clean draws borrow physical bytes. */
bool qa_cinematic_source_ui_frame(qa_cinematic *, uint32_t width, uint32_t height,
    bool dirty, qa_media_frame *, qa_error *);
/* Rewinds the retained original RoQ decoder and epoch without clearing its
 * shared buffers/codebooks or reopening its provider resource. */
bool qa_cinematic_roq_restart(qa_cinematic *, qa_error *);
bool qa_cinematic_roq_scratch_rebind_ready(const qa_cinematic *, const qa_roq_scratch *, qa_error *);
void qa_cinematic_roq_scratch_rebind(qa_cinematic *, qa_roq_scratch *);
uint64_t qa_cinematic_revision(const qa_cinematic *);
/* Retained decoder revision, including a qualified cold candidate whose live
 * playback operations remain unavailable until commit. */
bool qa_cinematic_checkpoint_revision_read(const qa_cinematic *, uint64_t *);
bool qa_cinematic_time(qa_cinematic *, double *elapsed_ms, double *source_ms, uint64_t *loop,
                       qa_error *);
bool qa_cinematic_capture(qa_cinematic *, qa_cinematic_checkpoint *, qa_error *);
void qa_cinematic_checkpoint_free(qa_cinematic_checkpoint *);
/* Reconnect a separately restored engine without replacing its retained raw
 * queue. Qualify all movies before applying any owner pointer exchanges. */
bool qa_cinematic_audio_rebind_ready(qa_cinematic *, qa_audio_engine *, uint64_t bus, qa_error *);
void qa_cinematic_audio_rebind(qa_cinematic *, qa_audio_engine *, uint64_t bus);
bool qa_cinematic_frame_rebind_ready(const qa_cinematic *, const qa_scene_frame *current, qa_error *);
void qa_cinematic_frame_rebind(qa_cinematic *, const qa_scene_frame *current, const qa_scene_frame *destination);
typedef struct qa_cinematic_publication {
    const qa_scene_image *image;
    const qa_scene_frame *frame;
    uint64_t revision, sequence;
} qa_cinematic_publication;
/* Pure retained owner metadata, available on a qualified cold candidate.
 * UINT64_MAX marks an image that has not uploaded a decoded picture. */
bool qa_cinematic_publication_read(const qa_cinematic *, qa_cinematic_publication *);
bool qa_cinematic_material_owner_is(const qa_cinematic *, const qa_cinematic_asset *,
    const char *path, uint64_t target, qa_media_clock);

/* Publication appends an immutable image revision barrier. The scene owns a
 * reference until reset, allowing decoding to continue after submission. */
bool qa_cinematic_image(qa_cinematic *, qa_scene_resources *, qa_scene_frame *,
                        const qa_scene_image **out, qa_error *);
typedef enum qa_cinematic_focus {
    QA_CINEMATIC_GAME,
    QA_CINEMATIC_CONSOLE,
    QA_CINEMATIC_MENU
} qa_cinematic_focus;
bool qa_cinematic_fullscreen(qa_cinematic *, qa_cinematic_focus, qa_scene_rect viewport,
                             qa_scene_resources *, qa_scene_frame *, bool *blank, qa_error *);
bool qa_cinematic_pixel_rect(qa_scene_rect_f, qa_scene_rect viewport, qa_scene_rect_f *out,
                             qa_error *);

typedef struct qa_material_movies qa_material_movies;
qa_material_movies *qa_material_movies_create(qa_scene_resources *, qa_error *);
void qa_material_movies_destroy(qa_material_movies *);
/* Takes ownership of a material-target movie on success. Its initial image is
 * transparent until reached publication, even if the decoder has a picture.
 * That identity remains the lookup key if later video dimensions change. */
bool qa_material_movies_add(qa_material_movies *, qa_cinematic *, qa_scene_frame *,
                            const qa_scene_image **initial_image, qa_error *);
bool qa_material_movies_remove(qa_material_movies *, uint64_t initial_image_identity, qa_error *);
bool qa_material_movies_enable(qa_material_movies *, uint64_t initial_image_identity, bool,
                               qa_error *);
/* Bind the actual submitted frame without advancing unseen movies.
 * Each reached video stage advances and publishes its own playback. */
bool qa_material_movies_prepare(qa_material_movies *, qa_scene_frame *, qa_error *);
const qa_scene_image *qa_material_movies_resolve(void *, uint64_t initial_image_identity,
                                                 double seconds, qa_error *);

typedef enum qa_cinematic_transition_kind {
    QA_CINEMATIC_RETURN,
    QA_CINEMATIC_Q2_NEXTSERVER,
    QA_CINEMATIC_Q3_NEXTMAP
} qa_cinematic_transition_kind;
typedef struct qa_cinematic_transition {
    qa_cinematic_transition_kind kind;
    uint32_t seat;
    int32_t server_count;
    const char *command;
} qa_cinematic_transition;
typedef struct qa_cinematic_transition_host {
    void *context;
    bool (*leave)(void *, uint32_t seat, qa_error *);
    bool (*send)(void *, uint32_t seat, const char *command, qa_error *);
    bool (*append)(void *, uint32_t seat, const char *command, qa_error *);
} qa_cinematic_transition_host;
bool qa_cinematic_transition_apply(const qa_cinematic_transition *, qa_cinematic_end,
                                   const qa_cinematic_transition_host *, qa_error *);

#endif

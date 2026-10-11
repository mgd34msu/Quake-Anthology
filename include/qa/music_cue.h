#ifndef QA_MUSIC_CUE_H
#define QA_MUSIC_CUE_H
#include "qa/audio.h"
#include "qa/launch.h"

typedef struct qa_executable_recipe qa_executable_recipe;
typedef struct qa_recipe_provider qa_recipe_provider;

typedef enum qa_music_cue_action { QA_MUSIC_CUE_START, QA_MUSIC_CUE_STOP } qa_music_cue_action;

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
    uint64_t receiver;
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
/* Borrowed state belonging to the module's existing music bus. */
typedef struct frontend_music_cue_state {
    qa_audio_music **player;
    char **intro, **loop;
    bool *looping, *attached, *pending;
} frontend_music_cue_state;
#endif

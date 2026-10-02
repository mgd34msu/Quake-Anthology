#ifndef QA_Q3_CINEMATIC_HANDLES_H
#define QA_Q3_CINEMATIC_HANDLES_H
#include "qa/q3_presentation.h"
#include "qa/q3_presentation_media_save.h"

typedef struct qa_q3_cinematic_handles qa_q3_cinematic_handles;
typedef struct qa_q3_cinematic_source qa_q3_cinematic_source;
typedef struct qa_q3_cinematic_handles_stage qa_q3_cinematic_handles_stage;
struct qa_media_library_stage;
typedef struct qa_q3_cinematic_handles_options {
    qa_scene_resources *images;
    void *context;
    bool (*ui_limits)(void *,bool *limited,qa_error *);
    /* Reached renderer upload: binds this physical scratch image, including
     * a clean frame. No constructor raw-unbind is performed here. */
    bool (*upload)(void *,const qa_scene_image *registered_slot,const qa_scene_image *version,
        bool redefine,bool dirty,qa_error *);
    bool (*fullscreen_draw)(void *,const qa_scene_image *,qa_scene_rect,uint32_t,qa_scene_frame *,qa_error *);
} qa_q3_cinematic_handles_options;
typedef struct qa_q3_cinematic_source_options {
    qa_vfs *files;
    qa_media_library *media;
    qa_media_clock clock;
    qa_audio_engine *audio;
    uint32_t seat;
    void *context;
    uint64_t (*audio_bus)(void *);
    void (*print)(void *,const char *);
    bool (*in_game_video)(void *,int32_t *,qa_error *);
    /* The retained physical source supplies its actual constructor/entered
     * lease. The callback must permit only that source's live operations. */
    bool (*current)(void *,const struct qa_q3_cinematic_source_options *);
} qa_q3_cinematic_source_options;
bool qa_q3_cinematic_handles_create(const qa_q3_cinematic_handles_options *,qa_q3_cinematic_handles **,qa_error *);
bool qa_q3_cinematic_handles_idle(const qa_q3_cinematic_handles *);
bool qa_q3_cinematic_handles_read(const qa_q3_cinematic_handles *,qa_q3_cinematic_handles_options *);
bool qa_q3_cinematic_handles_rebind_ready(const qa_q3_cinematic_handles *,const qa_q3_cinematic_handles_options *,qa_error *);
void qa_q3_cinematic_handles_rebind(qa_q3_cinematic_handles *,const qa_q3_cinematic_handles_options *);
bool qa_q3_cinematic_handles_image_is(const qa_q3_cinematic_handles *,const qa_scene_image *);
bool qa_q3_cinematic_handles_destroy(qa_q3_cinematic_handles **,qa_error *);
bool qa_q3_cinematic_source_create(qa_q3_cinematic_handles *,const qa_q3_cinematic_source_options *,qa_q3_cinematic_source **,qa_error *);
/* A real UI/CG role keeps its own declared bus while retaining the stable
 * provider source. Its callbacks never borrow the retiring presentation. */
bool qa_q3_cinematic_source_create_role(qa_q3_cinematic_source *,uint32_t seat,uint64_t bus,qa_q3_cinematic_source **,qa_error *);
const qa_q3_cinematic_source *qa_q3_cinematic_source_parent(const qa_q3_cinematic_source *);
bool qa_q3_cinematic_source_role_read(const qa_q3_cinematic_source *,const qa_q3_cinematic_source **parent,uint32_t *seat,uint64_t *bus);
/* Borrow the already constructed exact role source; performs no admission,
 * callbacks, allocation or replay during global handle import. */
bool qa_q3_cinematic_source_role_find(const qa_q3_cinematic_source *parent,uint32_t seat,uint64_t bus,qa_q3_cinematic_source **);
/* The actual role keeps this diagnostic owner alive until checked source
 * destruction; decoder output does not require an interpreter entry. */
bool qa_q3_cinematic_source_role_diagnostic_bind(qa_q3_cinematic_source *,void *context,
    void (*print)(void *,const char *),bool (*current)(void *,const qa_q3_cinematic_source *),qa_error *);
bool qa_q3_cinematic_source_role_diagnostic_read(const qa_q3_cinematic_source *,void **context,bool *bound);
bool qa_q3_cinematic_source_role_detach(qa_q3_cinematic_source *,void *expected_context,qa_error *);
bool qa_q3_cinematic_source_retain(qa_q3_cinematic_source *,qa_error *);
void qa_q3_cinematic_source_release(qa_q3_cinematic_source *);
bool qa_q3_cinematic_source_destroy(qa_q3_cinematic_source **,qa_error *);
bool qa_q3_cinematic_source_read(const qa_q3_cinematic_source *,qa_q3_cinematic_source_options *);
bool qa_q3_cinematic_source_retained(const qa_q3_cinematic_source *);
size_t qa_q3_cinematic_handles_source_count(const qa_q3_cinematic_handles *);
bool qa_q3_cinematic_handles_source_at(const qa_q3_cinematic_handles *,size_t,qa_q3_cinematic_source **);
bool qa_q3_cinematic_source_audio_rebind_ready(qa_q3_cinematic_source *,qa_audio_engine *,uint64_t,qa_error *);
void qa_q3_cinematic_source_audio_rebind(qa_q3_cinematic_source *,qa_audio_engine *,uint64_t);
qa_q3_cinematic_handles *qa_q3_cinematic_source_handles(const qa_q3_cinematic_source *);
qa_roq_scratch *qa_q3_cinematic_source_decoder_scratch(const qa_q3_cinematic_source *);
bool qa_q3_cinematic_system_select(qa_q3_cinematic_source *,int32_t,qa_cinematic *,qa_error *);
bool qa_q3_cinematic_system_fullscreen(qa_q3_cinematic_source *,int32_t,qa_scene_rect,qa_scene_frame *,bool *,qa_error *);
bool qa_q3_cinematic_play(qa_q3_cinematic_source *,const char *,qa_scene_rect_f,uint32_t,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *,int32_t *,qa_error *);
bool qa_q3_cinematic_run(qa_q3_cinematic_source *,int32_t,int32_t *,qa_error *);
bool qa_q3_cinematic_stop(qa_q3_cinematic_source *,int32_t,bool skip,qa_error *);
bool qa_q3_cinematic_extents(qa_q3_cinematic_source *,int32_t,qa_scene_rect_f,qa_error *);
bool qa_q3_cinematic_image(qa_q3_cinematic_source *,int32_t,qa_scene_frame *,const qa_scene_image **,qa_scene_rect_f *,qa_error *);
bool qa_q3_cinematic_draw_complete(qa_q3_cinematic_source *,int32_t,qa_error *);
/* Shader registration and reached stage use the same 0..15 handle pool as
 * guest traps. The returned initial image is scratchImage[actual handle]. */
bool qa_q3_cinematic_shader_play(qa_q3_cinematic_source *,const char *,int32_t *,const qa_scene_image **,qa_error *);
/* NULL with QA_OK is the genuine valid-handle/no-buffer upload no-op. Source
 * stages retain their reached texture binding in that case. */
const qa_scene_image *qa_q3_cinematic_shader_resolve(qa_q3_cinematic_source *,uint64_t initial_image_identity,qa_scene_frame *,qa_error *);
typedef struct qa_q3_cinematic_slot {
    int32_t handle;
    const qa_q3_cinematic_source *source;
    const char *path;
    const qa_cinematic_asset *asset;
    const qa_cinematic *playback;
    const qa_scene_image *scratch;
    const qa_q3_system_movie *system;
    uint32_t flags;
} qa_q3_cinematic_slot;
bool qa_q3_cinematic_handles_at(const qa_q3_cinematic_handles *,uint32_t,qa_q3_cinematic_slot *);
typedef struct qa_q3_cinematic_handles_refs {
    void *context;
    bool (*source_encode)(void *,const qa_q3_cinematic_source *,uint64_t *,qa_error *);
    /* Returns the already reconstructed real source, belonging to this exact
     * candidate pool. Decode never constructs or reruns its provider. */
    bool (*source_decode)(void *,uint64_t,qa_q3_cinematic_source **,qa_error *);
    bool (*source_clock_read)(void *,const qa_q3_cinematic_source *,double *,qa_error *);
    bool (*asset_encode)(void *,const qa_q3_cinematic_source *,const qa_cinematic_asset *,uint64_t *,qa_error *);
    bool (*asset_decode)(void *,const qa_q3_cinematic_source *,uint64_t,const char *,qa_cinematic_asset **,qa_error *);
    bool (*audio_bus_decode)(void *,const qa_q3_cinematic_source *,uint64_t saved,uint64_t *actual,qa_error *);
    /* System slots belong to their creating source's real fullscreen owner.
     * The source is already resolved before decoding its retained handle. */
    bool (*system_encode)(void *,const qa_q3_cinematic_source *,const qa_q3_system_movie *,uint32_t,qa_buffer *,qa_error *);
    bool (*system_decode)(void *,const qa_q3_cinematic_source *,qa_bytes,uint32_t,qa_q3_system_movie *,qa_error *);
    void (*system_discard)(void *,const qa_q3_cinematic_source *,qa_q3_system_movie *);
    qa_q3_movie_checkpoint_refs movies;
} qa_q3_cinematic_handles_refs;
bool qa_q3_cinematic_handles_checkpoint(qa_q3_cinematic_handles *,const qa_q3_cinematic_handles_refs *,qa_buffer *,qa_error *);
bool qa_q3_cinematic_handles_restore(qa_q3_cinematic_handles *,const qa_q3_cinematic_handles_refs *,double wall_milliseconds,qa_bytes,qa_error *);
/* One global slot claim is held while all actual Source material libraries
 * prepare. New movies and allocations stay private until the banks publish. */
bool qa_q3_cinematic_handles_stage_prepare(qa_q3_cinematic_handles *,qa_scene_resource_policy *,qa_q3_cinematic_handles_stage **,qa_error *);
bool qa_q3_cinematic_handles_stage_associated(const qa_q3_cinematic_handles *);
qa_q3_cinematic_handles *qa_q3_cinematic_handles_stage_owner(const qa_q3_cinematic_handles_stage *);
qa_scene_resource_policy *qa_q3_cinematic_handles_stage_bank(const qa_q3_cinematic_handles_stage *);
bool qa_q3_cinematic_handles_stage_shader(qa_q3_cinematic_handles_stage *,qa_q3_cinematic_source *,
    struct qa_media_library_stage *,const char *,int32_t *,const qa_scene_image **,qa_error *);
bool qa_q3_cinematic_handles_stage_ready(qa_q3_cinematic_handles_stage *,qa_error *);
bool qa_q3_cinematic_handles_stage_ready_is(const qa_q3_cinematic_handles_stage *);
void qa_q3_cinematic_handles_stage_publish(qa_q3_cinematic_handles_stage *);
bool qa_q3_cinematic_handles_stage_finish(qa_q3_cinematic_handles_stage **,qa_error *);
bool qa_q3_cinematic_handles_stage_abort(qa_q3_cinematic_handles_stage **,qa_error *);
#endif

#ifndef QA_Q3_PRESENTATION_H
#define QA_Q3_PRESENTATION_H

#include "qa/cinematic.h"
#include "qa/material.h"
#include "qa/world.h"
#include "qa/source_save.h"

typedef struct qa_q3_presentation qa_q3_presentation;
typedef struct qa_q3_presentation_assets qa_q3_presentation_assets;
typedef struct qa_q3_presentation_provider {
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_scene_family family;
} qa_q3_presentation_provider;
typedef enum qa_q3_asset_kind { QA_Q3_ASSET_MODEL, QA_Q3_ASSET_SKIN, QA_Q3_ASSET_SHADER } qa_q3_asset_kind;
typedef struct qa_q3_presentation_asset_options {
    qa_q3_presentation_provider provider;
    qa_audio_bank *sounds;
    qa_audio_asset *zero_sound;
    qa_media_library *movies;
    void *context;
    /* The composition owner resolves independently selected appearance and
     * arsenal content. Returned providers outlive this resource registry. */
    bool (*select)(void *, const char *, qa_q3_asset_kind,
                   qa_q3_presentation_provider *, qa_error *);
    void (*print)(void *, const char *);
} qa_q3_presentation_asset_options;
bool qa_q3_presentation_assets_create(const qa_q3_presentation_asset_options *,
                                       qa_q3_presentation_assets **, qa_error *);
void qa_q3_presentation_assets_destroy(qa_q3_presentation_assets *);
typedef struct qa_q3_asset_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, qa_buffer *, qa_error *);
    /* Returns an owned immutable candidate image version. */
    bool (*image_decode)(void *, qa_bytes, qa_scene_image **, qa_error *);
} qa_q3_asset_checkpoint_refs;
bool qa_q3_presentation_assets_checkpoint(qa_q3_presentation_assets *, qa_session *,
    const qa_q3_asset_checkpoint_refs *, qa_buffer *, qa_error *);
/* Existing candidate has qualified provider services and its map bindings,
 * but no registered handles. Failure retains partial candidate ownership;
 * the candidate must be discarded, never published or reused. */
bool qa_q3_presentation_assets_restore(qa_q3_presentation_assets *, qa_session *,
    const qa_q3_asset_checkpoint_refs *, qa_bytes, qa_error *);
typedef struct qa_q3_registered_model {
    int32_t handle;
    const char *name;
    qa_model_format format;
    bool world, inline_model;
} qa_q3_registered_model;
typedef struct qa_q3_registered_skin {
    int32_t handle;
    const char *name;
    const qa_model_skin_map *surfaces;
} qa_q3_registered_skin;
/* Arrays in scratch; names and maps borrow the source registry until its next
 * mutation. Includes live source handles only, and never acquires resources. */
bool qa_q3_registered_models(const qa_q3_presentation_assets *, qa_arena *,
                             const qa_q3_registered_model **, size_t *, qa_error *);
bool qa_q3_registered_skins(const qa_q3_presentation_assets *, qa_arena *,
                            const qa_q3_registered_skin **, size_t *, qa_error *);
bool qa_q3_register_model(qa_q3_presentation_assets *, const char *, int32_t *, qa_error *);
bool qa_q3_register_skin(qa_q3_presentation_assets *, const char *, int32_t *, qa_error *);
bool qa_q3_register_shader(qa_q3_presentation_assets *, const char *, bool mipmap, int32_t *, qa_error *);
bool qa_q3_register_picture_image(qa_q3_presentation_assets *, const qa_scene_image *, int32_t *, qa_error *);
bool qa_q3_register_sound(qa_q3_presentation_assets *, const char *, bool compressed, int32_t *, qa_error *);
bool qa_q3_presentation_model_bounds(const qa_q3_presentation_assets *, int32_t, qa_bounds *, qa_error *);
bool qa_q3_presentation_model_has_tags(const qa_q3_presentation_assets *, int32_t, bool *, qa_error *);
bool qa_q3_presentation_tag(const qa_q3_presentation_assets *, int32_t, const char *,
                            int32_t first, int32_t second, float fraction,
                            qa_model_tag *, bool *found, qa_error *);

typedef enum qa_q3_ref_kind {
    QA_Q3_REF_MODEL, QA_Q3_REF_POLY, QA_Q3_REF_SPRITE, QA_Q3_REF_BEAM,
    QA_Q3_REF_RAIL_CORE, QA_Q3_REF_RAIL_RINGS, QA_Q3_REF_LIGHTNING, QA_Q3_REF_PORTAL
} qa_q3_ref_kind;
typedef struct qa_q3_ref_entity {
    qa_q3_ref_kind kind;
    int32_t flags, model, frame, old_frame, skin, custom_skin, custom_shader;
    qa_vec3 lighting_origin, axis[3], origin, old_origin;
    float shadow_plane, back_lerp, shader_time, radius, rotation;
    qa_scene_vec2 shader_texcoord;
    uint8_t color[4];
    bool non_normalized_axes;
} qa_q3_ref_entity;
typedef struct qa_q3_refdef {
    int32_t x, y, width, height, time, flags;
    float fov_x, fov_y;
    qa_vec3 origin, axis[3];
    uint8_t area_mask[32];
    char text[8][32];
} qa_q3_refdef;
typedef struct qa_q3_poly_vertex {
    qa_vec3 position;
    qa_scene_vec2 texcoord;
    uint8_t color[4];
} qa_q3_poly_vertex;

typedef struct qa_q3_system_movie {
    void *context;
    qa_media_status (*status)(void *);
    bool (*end)(void *, qa_cinematic_end, qa_error *);
    void (*release)(void *);
} qa_q3_system_movie;
typedef struct qa_q3_movie_request { const char *path; bool loop, hold, silent; } qa_q3_movie_request;
typedef struct qa_q3_scene_options {
    qa_scene_world_input world;
    qa_scene_family world_family;
    qa_scene_state state;
    uint32_t first_entity;
    qa_vec3 weapon_offset;
    bool split_screen, supplemental_weapon;
} qa_q3_scene_options;
typedef struct qa_q3_presentation_options {
    qa_q3_presentation_assets *assets;
    qa_audio_engine *audio;
    qa_media_clock clock;
    uint32_t seat;
    uint64_t owner;
    qa_scene_rect viewport;
    float near_clip, far_clip, identity_light, lod_scale, lod_bias;
    uint32_t shadow_mode;
    int32_t rail_core_width, rail_ring_width;
    float rail_segment_length;
    void *context;
    /* Resolves through the application's canonical actor/audio identity map. */
    bool (*audio_actor)(void *, int32_t source_number, uint64_t *, qa_error *);
    bool (*listener)(void *, const qa_audio_listener *, qa_error *);
    bool (*music)(void *, const char *intro, const char *loop, qa_error *);
    int32_t (*frame_number)(void *);
    int32_t (*milliseconds)(void *);
    uint64_t (*audio_bus)(void *);
    bool (*system_movie)(void *, const qa_q3_movie_request *, qa_q3_system_movie *, qa_error *);
    /* The application supplies selected-map styles, translated camera, and
     * supplemental native presentation before this seat submits guest refs. */
    bool (*prepare_view)(void *, const qa_q3_refdef *, qa_q3_scene_options *, qa_error *);
    bool (*submit_view)(void *, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
    bool (*prepare_picture)(void *, qa_material_context *, qa_error *);
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    /* Renderer-wide remapping includes independently selected providers. */
    bool (*remap)(void *, const char *, const char *, float, qa_error *);
    void (*print)(void *, const char *);
} qa_q3_presentation_options;
/* One seat owner is shared by its native presentation and attached UI/cgame.
 * Resources may be shared by several seats. All calls are serialized; external
 * callbacks queue owner destruction rather than destroying an active call. */
bool qa_q3_presentation_create(const qa_q3_presentation_options *, qa_q3_presentation **, qa_error *);
bool qa_q3_presentation_destroy(qa_q3_presentation *, qa_error *);
bool qa_q3_presentation_frontend_rebind_ready(const qa_q3_presentation *, const qa_scene_frame *,
                                               qa_audio_engine *, uint64_t bus, qa_error *);
void qa_q3_presentation_frontend_rebind(qa_q3_presentation *, const qa_scene_frame *current,
                                         qa_scene_frame *destination, qa_audio_engine *, uint64_t bus);
qa_q3_presentation_assets *qa_q3_presentation_resources(qa_q3_presentation *);
bool qa_q3_presentation_frame(qa_q3_presentation *, qa_scene_frame *, qa_scene_rect, qa_error *);
bool qa_q3_presentation_world(qa_q3_presentation *, qa_scene_world *, qa_collision_geometry *,
                              qa_bytes entity_text, qa_error *);
/* Call for every seat sharing the registry after source calls finish and
 * before releasing a committed map. Inline handles retire without reuse;
 * independently registered preview models, shaders and skins remain live. */
bool qa_q3_presentation_retire_world(qa_q3_presentation *, qa_error *);
bool qa_q3_presentation_load_world(qa_q3_presentation *, const char *requested_path, qa_error *);
bool qa_q3_presentation_entity_token(qa_q3_presentation *, const char **token, bool *, qa_error *);
bool qa_q3_presentation_in_pvs(qa_q3_presentation *, qa_vec3, qa_vec3, bool *, qa_error *);
bool qa_q3_presentation_clear(qa_q3_presentation *, qa_error *);
bool qa_q3_presentation_entity(qa_q3_presentation *, const qa_q3_ref_entity *, qa_error *);
bool qa_q3_presentation_poly(qa_q3_presentation *, int32_t shader, const qa_q3_poly_vertex *, size_t,
                             qa_error *);
bool qa_q3_presentation_light(qa_q3_presentation *, qa_vec3, float radius, qa_vec3 color,
                              bool additive, qa_error *);
bool qa_q3_presentation_render(qa_q3_presentation *, const qa_q3_refdef *, qa_error *);
void qa_q3_presentation_color(qa_q3_presentation *, const qa_scene_vec4 *);
bool qa_q3_presentation_picture(qa_q3_presentation *, int32_t shader, qa_scene_rect_f,
                                qa_scene_vec4 uv, qa_error *);
bool qa_q3_presentation_remap(qa_q3_presentation *, const char *, const char *, float, qa_error *);

/* Sound validity is separate from playback so guest adapters can preserve
 * source admission before reading an optional origin or axis pointer. */
bool qa_q3_presentation_sound_valid(qa_q3_presentation *, int32_t);
bool qa_q3_presentation_sound(qa_q3_presentation *, int32_t sound, const qa_vec3 *origin,
                              int32_t entity, int32_t channel, bool local, qa_error *);
bool qa_q3_presentation_loop(qa_q3_presentation *, int32_t sound, int32_t entity,
                             qa_vec3 origin, qa_vec3 velocity, bool persistent, qa_error *);
bool qa_q3_presentation_clear_loops(qa_q3_presentation *, bool all, qa_error *);
bool qa_q3_presentation_stop_loop(qa_q3_presentation *, int32_t entity, qa_error *);
bool qa_q3_presentation_sound_position(qa_q3_presentation *, int32_t entity, qa_vec3, qa_error *);
bool qa_q3_presentation_listener(qa_q3_presentation *, int32_t entity, qa_vec3,
                                 const qa_vec3 axis[3], qa_error *);
bool qa_q3_presentation_music(qa_q3_presentation *, const char *intro, const char *loop, qa_error *);

bool qa_q3_presentation_movie_play(qa_q3_presentation *, const char *, qa_scene_rect_f,
                                   uint32_t flags, int32_t *handle, qa_error *);
bool qa_q3_presentation_movie_run(qa_q3_presentation *, int32_t handle, int32_t *status, qa_error *);
bool qa_q3_presentation_movie_stop(qa_q3_presentation *, int32_t handle, bool skip, qa_error *);
bool qa_q3_presentation_movie_draw(qa_q3_presentation *, int32_t handle, qa_error *);
void qa_q3_presentation_movie_extents(qa_q3_presentation *, int32_t handle, qa_scene_rect_f);

#endif

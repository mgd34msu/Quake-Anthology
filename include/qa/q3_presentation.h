#ifndef QA_Q3_PRESENTATION_H
#define QA_Q3_PRESENTATION_H

#include "qa/cinematic.h"
#include "qa/material.h"
#include "qa/world.h"
#include "qa/source_save.h"
#include "qa/scene_effects.h"

typedef struct qa_q3_presentation qa_q3_presentation;
typedef struct qa_q3_presentation_assets qa_q3_presentation_assets;
struct qa_q3_cinematic_source;
struct qa_q3_model_opening;
/* Pure parent-lifetime admission, including actual retained child captures. */
bool qa_q3_presentation_idle(const qa_q3_presentation *);
/* Bind the reconstructed source before importing its shared movie record.
 * This transfers one real source user; it never creates or replays movies. */
bool qa_q3_presentation_cinematics_bind(qa_q3_presentation *,struct qa_q3_cinematic_source *,qa_error *);
bool qa_q3_presentation_renderer_parameters_set(qa_q3_presentation *,float near_clip,float identity_light,qa_error *);
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
    /* Fresh admission only: the actual decoded registry parent and its first
     * opening are retained before the numeric handle is published. Imports
     * restore existing children without replaying this callback. */
    bool (*model_initialize)(void *, const struct qa_q3_model_opening *,
        const qa_model *, qa_scene_model *, qa_error *);
    void (*print)(void *, const char *);
} qa_q3_presentation_asset_options;
bool qa_q3_presentation_assets_create(const qa_q3_presentation_asset_options *,
                                       qa_q3_presentation_assets **, qa_error *);
void qa_q3_presentation_assets_destroy(qa_q3_presentation_assets *);
/* Actual owned fallback and numeric sound references, in that order. Array
 * entries borrow assets at the idle cut; NULL entries own no reference. Caller
 * frees only the returned array. No registration or reference change runs. */
bool qa_q3_presentation_audio_assets_read(const qa_q3_presentation_assets *,
    qa_audio_asset ***, size_t *, qa_error *);
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
/* One live source handle; the name borrows this registry until mutation.
 * Absent and failed registrations report present=false without resource work. */
bool qa_q3_registered_model_read(const qa_q3_presentation_assets *, int32_t,
    qa_q3_registered_model *, bool *present, qa_error *);
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
typedef struct qa_q3_scene_polygon {
    int32_t shader;
    size_t first, count;
    qa_scene_fog_volume fog;
} qa_q3_scene_polygon;

typedef struct qa_q3_system_movie {
    void *context;
    qa_media_status (*status)(void *);
    bool (*end)(void *, qa_cinematic_end, qa_error *);
    void (*release)(void *);
    qa_cinematic *(*playback)(void *);
} qa_q3_system_movie;
typedef struct qa_q3_movie_request {
    const char *path;
    bool loop, hold, silent;
    struct qa_q3_cinematic_source *numeric_source;
    int32_t numeric_handle;
} qa_q3_movie_request;
/* An actual entered UI/CGAME host supplies its own lifetime-qualified opener;
 * the shared presentation context cannot identify a requesting role. The
 * successful opener transfers one complete system handle to the numeric slot.
 * On failure it retains responsibility for any partially prepared owner. */
bool qa_q3_presentation_movie_play_system(qa_q3_presentation *,const char *path,uint32_t flags,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),
    void *context,int32_t *out,qa_error *);
typedef struct qa_q3_scene_options {
    qa_scene_world_input world;
    qa_scene_family world_family;
    qa_scene_state state;
    uint32_t first_entity, shadow_mode;
    float lod_scale, lod_bias, ambient_scale, directed_scale, near_clip;
    qa_scene_rail_options rail;
    qa_vec3 weapon_offset;
    const qa_q3_refdef *weapon_camera; /* Original source camera, borrowed during this submission only. */
    bool split_screen, supplemental_weapon;
    bool no_entities, no_portals, portal_only, no_refresh;
} qa_q3_scene_options;
typedef struct qa_q3_picture_receipt {
    bool source_raw;
    qa_q3_presentation_assets *assets;
    const qa_material *material;
    const qa_scene_image *image;
    int32_t shader, milliseconds;
    qa_scene_rect_f rect;
    qa_scene_vec4 uv, color;
    qa_scene_rect viewport;
    uint32_t seat;
    float identity_light;
} qa_q3_picture_receipt;
typedef struct qa_q3_presentation_options {
    qa_q3_presentation_assets *assets;
    qa_audio_engine *audio;
    /* Original Source UI/CG and videoMap share the frontend's actual numeric
     * cinematic source. Selected application movies retain their own owner. */
    struct qa_q3_cinematic_source *cinematics;
    qa_material_source_scratch *source_scratch;
    qa_scene_world_scratch *world_scratch, *world_child_scratch;
    qa_material_source_scratch *(*source_state)(void *, qa_error *);
    bool source_scene_membership;
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
    /* Borrowed exact command values before geometry or physical Source entry. */
    bool (*picture_capture)(void *, const qa_q3_picture_receipt *, qa_error *);
    /* Borrowed only during the successful RenderScene call, before its Source
     * membership advances. The receiver owns any retained snapshot. */
    bool (*scene_completed)(void *, const qa_q3_refdef *, const qa_q3_scene_options *,
        const qa_q3_ref_entity *, size_t, const qa_q3_scene_polygon *, size_t,
        const qa_scene_vertex *, size_t, const qa_scene_light *, size_t, qa_error *);
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    /* Renderer-wide remapping includes independently selected providers. */
    bool (*remap)(void *, const char *, const char *, float, qa_error *);
    void (*print)(void *, const char *);
    /* The installed owner discards only retained supplemental scene packets.
     * Called after the actual ClearScene resets its source collections. */
    void (*scene_cleared)(void *);
} qa_q3_presentation_options;
/* One seat owner is shared by its native presentation and attached UI/cgame.
 * Resources may be shared by several seats. All calls are serialized; external
 * callbacks queue owner destruction rather than destroying an active call. */
bool qa_q3_presentation_create(const qa_q3_presentation_options *, qa_q3_presentation **, qa_error *);
bool qa_q3_presentation_options_read(const qa_q3_presentation *, qa_q3_presentation_options *, qa_error *);
/* A failed retirement preserves the presentation for caller-owned retry. */
bool qa_q3_presentation_destroy(qa_q3_presentation *, qa_error *);
bool qa_q3_presentation_frontend_rebind_ready(const qa_q3_presentation *, const qa_scene_frame *,
                                               qa_audio_engine *, uint64_t bus, qa_error *);
/* A same-map round retains the real renderer, parser, media and numeric asset
 * registry. Only completed transient scene submissions may be cleared. */
bool qa_q3_presentation_round_ready(const qa_q3_presentation *, const qa_scene_frame *,
                                      const qa_scene_world *, const qa_collision_geometry *, qa_error *);
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
/* The actual pending entity index in the current Source scene. This does not
 * publish a ref; available=false retains the renderer's real capacity policy. */
bool qa_q3_presentation_entity_cursor(const qa_q3_presentation *, size_t *index,
    bool *available, qa_error *);
bool qa_q3_presentation_poly(qa_q3_presentation *, int32_t shader, const qa_q3_poly_vertex *, size_t,
                             qa_error *);
bool qa_q3_presentation_light(qa_q3_presentation *, qa_vec3, float radius, qa_vec3 color,
                              bool additive, qa_error *);
bool qa_q3_presentation_render(qa_q3_presentation *, const qa_q3_refdef *, qa_error *);
/* The caller already owns this unfinished view and its world submission.
 * Submit retained Q3 refs through their real registry without starting or
 * finishing another view, redrawing the world, or presenting a surface. */
struct qa_q3_source_scene_bank;
bool qa_q3_presentation_supplement(qa_q3_presentation *,struct qa_q3_source_scene_bank *,const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
typedef struct qa_q3_supplement qa_q3_supplement;
/* Admission captures genuine queued refs once. A failed prepare retains its
 * partial receipt; release it before destroying either parent or scene bank. */
bool qa_q3_presentation_supplement_prepare(qa_q3_presentation *,struct qa_q3_source_scene_bank *,
    qa_scene_frame *,qa_q3_supplement **,qa_error *);
bool qa_q3_presentation_supplement_draw(const qa_q3_supplement *,const qa_q3_scene_options *,
    qa_scene_frame *,qa_error *);
bool qa_q3_presentation_supplement_current(const qa_q3_supplement *,const qa_scene_frame *);
void qa_q3_presentation_supplement_release(qa_q3_supplement **);
bool qa_q3_presentation_lights_read(const qa_q3_presentation *, const qa_scene_light **, size_t *, qa_error *);
/* Only the actual submit_view callback may submit a retained selected model.
 * The scene, decoded source and transform remain owned by its content owner. */
bool qa_q3_presentation_selected_model(qa_q3_presentation *, qa_scene_model *,
    const qa_model *, const char *source_path, const qa_model_transform *, const qa_q3_ref_entity *,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
typedef struct qa_q3_foreign_view_lighting {
    qa_scene_family content;
    uint32_t flags;
} qa_q3_foreign_view_lighting;
/* Content flags retain their true family. Q3 authored parent flags remain on
 * the refEntity; this policy supplies only the foreign view lighting branch. */
bool qa_q3_presentation_selected_view_model(qa_q3_presentation *, qa_scene_model *,
    const qa_model *, const char *source_path, const qa_model_transform *,
    const qa_q3_ref_entity *, const qa_q3_scene_options *,
    const qa_q3_foreign_view_lighting *, uint32_t order, qa_scene_frame *, qa_error *);
/* Numeric model, skin and shader references resolve in the actual selected
 * registry. The primary renderer still owns the current world/view lease. */
bool qa_q3_presentation_selected_registered(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
/* The supplied model/skin and authored parent flags retain the selected
 * namespace. A captured held pass supplies shader/time/color from the actual
 * primary registry; no shader means the selected pass's white base color. */
bool qa_q3_presentation_selected_registered_pass(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *,
    const qa_q3_presentation_assets *source_assets, const qa_q3_ref_entity *source_pass,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
/* Retain the selected model's pose while applying every captured body material
 * field. Skin and shader handles both resolve in the actual primary registry. */
bool qa_q3_presentation_selected_body_pass(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *,
    const qa_q3_presentation_assets *source_assets, const qa_q3_ref_entity *source_pass,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
/* Primary Source geometry stays in its entered registry; a completed component
 * body pass uses its own retained material namespace and reached shader clock. */
bool qa_q3_presentation_source_body_pass(qa_q3_presentation *, const qa_q3_ref_entity *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *, int32_t source_time_ms,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_body_material_equal(qa_q3_presentation *,
    const qa_q3_presentation_assets *source_assets, const qa_q3_ref_entity *,
    const qa_q3_ref_entity *, bool *, qa_error *);
bool qa_q3_presentation_source_body_material_equal(qa_q3_presentation *,
    const qa_q3_presentation_assets *material_assets, const qa_q3_ref_entity *,
    const qa_q3_ref_entity *, bool *, qa_error *);
/* Completed component outputs resolve only in their actual idle registry and
 * enter the primary's active view without replaying a component RenderScene. */
bool qa_q3_presentation_source_component_entity(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *, int32_t source_time_ms,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
/* Returned companion references enter only their genuine idle presentation
 * registry and the caller's already prepared native view. */
bool qa_q3_presentation_completed_entity(qa_q3_presentation *,
    const qa_q3_presentation_assets *, qa_scene_world *recipient_world, const qa_q3_ref_entity *, int32_t source_time_ms,
    const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_completed_poly(qa_q3_presentation *,
    const qa_q3_presentation_assets *, qa_scene_world *recipient_world, int32_t shader, const qa_scene_vertex *, size_t count,
    const qa_scene_fog_volume *, int32_t source_time_ms, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_source_component_poly(qa_q3_presentation *,
    const qa_q3_presentation_assets *, int32_t shader, const qa_scene_vertex *, size_t count,
    const qa_scene_fog_volume *, int32_t source_time_ms, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_poly_cursor(const qa_q3_presentation *, size_t vertices,
    size_t *index, bool *available, qa_error *);
bool qa_q3_presentation_light_cursor(const qa_q3_presentation *,
    size_t *index, bool *available, qa_error *);
/* Captured selected output uses its own model/shader namespace and actual
 * source material clock during the primary submit_view lease. Lights must be
 * added before RenderScene. Default model zero retains selected materials. */
bool qa_q3_presentation_selected_effect(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *,
    int32_t source_time_ms, const qa_q3_scene_options *, uint32_t order, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_selected_poly(qa_q3_presentation *,
    const qa_q3_presentation_assets *, int32_t shader, const qa_scene_vertex *, size_t,
    int32_t source_time_ms, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
/* World supplements use compiled material order and insertion ordinals, outside
 * the packed source refEntity domain. A cable may contain 65537 model segments.
 * All model/skin/shader handles resolve in the supplied retained registry. */
bool qa_q3_presentation_selected_world_models(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_q3_ref_entity *, size_t count,
    const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
/* The authored shader cable is a translucent sequence, with the donor's
 * integer core width and endpoint convention (origin first, end second).
 * Its material is the supplied library's retained DYNAMIC/-1 registration,
 * including a named default material. Both world helpers use the input clock. */
bool qa_q3_presentation_selected_world_beam(qa_q3_presentation *,
    const qa_q3_presentation_assets *, const qa_material *, qa_vec3 origin, qa_vec3 end,
    double width, const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
void qa_q3_presentation_color(qa_q3_presentation *, const qa_scene_vec4 *);
bool qa_q3_presentation_picture(qa_q3_presentation *, int32_t shader, qa_scene_rect_f,
                                qa_scene_vec4 uv, qa_error *);
bool qa_q3_presentation_selected_picture(qa_q3_presentation *, const qa_q3_picture_receipt *,
    const qa_q3_scene_options *, qa_scene_frame *, qa_error *);
bool qa_q3_presentation_completed_picture(qa_q3_presentation *, const qa_q3_picture_receipt *,
    qa_scene_frame *, qa_error *);
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

#ifndef QA_SCENE_H
#define QA_SCENE_H

#include "qa/math.h"
#include "qa/ruleset.h"
#include "qa/arena.h"
#include "qa/bsp.h"
#include "qa/image.h"
#include "qa/q3_color.h"
#include "qa/model.h"
#include "qa/vfs.h"

typedef struct qa_material qa_material;
typedef struct qa_material_context qa_material_context;
struct qa_material_profile;
typedef struct qa_material_library qa_material_library;
typedef struct qa_material_order qa_material_order;
typedef struct qa_scene_resources qa_scene_resources;
typedef struct qa_scene_world qa_scene_world;
typedef struct qa_scene_source_world_view qa_scene_source_world_view;
typedef struct qa_scene_model qa_scene_model;
typedef struct qa_scene_geometry qa_scene_geometry;
typedef struct qa_material_source_scratch qa_material_source_scratch;
typedef struct qa_q3_presentation_assets qa_q3_presentation_assets;
typedef struct qa_q3_ref_entity qa_q3_ref_entity;
typedef struct qa_scene_source_diagnostics {
    int32_t debug_sort, stencil_bits, fast_sky, lightmap;
    int32_t rail_core_width, rail_width;
    float rail_segment_length;
    bool show_triangles, show_normals, show_sky, no_bind, vertex_lighting;
    float polygon_offset_factor, polygon_offset_units;
} qa_scene_source_diagnostics;
typedef bool (*qa_scene_source_diagnostics_read_fn)(void *, qa_scene_source_diagnostics *, qa_error *);

typedef struct qa_scene_matrix { float m[16]; } qa_scene_matrix;
typedef struct qa_scene_rect { int32_t x, y; uint32_t width, height; } qa_scene_rect;
typedef struct qa_scene_rect_f { float x, y, width, height; } qa_scene_rect_f;
typedef struct qa_scene_plane { qa_vec3 normal; float distance; } qa_scene_plane;
typedef enum qa_scene_wrap { QA_SCENE_REPEAT, QA_SCENE_CLAMP } qa_scene_wrap;
typedef enum qa_scene_filter {
    QA_SCENE_NEAREST, QA_SCENE_LINEAR, QA_SCENE_NEAREST_MIPMAP_NEAREST,
    QA_SCENE_LINEAR_MIPMAP_NEAREST, QA_SCENE_NEAREST_MIPMAP_LINEAR,
    QA_SCENE_LINEAR_MIPMAP_LINEAR
} qa_scene_filter;
typedef enum qa_scene_image_kind { QA_SCENE_RGBA8, QA_SCENE_RGB8, QA_SCENE_DEPTH32F } qa_scene_image_kind;
/* Color pixels occupy four bytes even for RGB8; depth pixels are float32. */
typedef struct qa_scene_image_level { uint32_t width, height; const void *pixels; size_t bytes; } qa_scene_image_level;
/* Versions own their pixels. Backends key residency by identity and revision.
 * Streamed images keep their storage and issue ordered, copied region updates;
 * their pixels describe current contents for cold backend admission. */
typedef struct qa_scene_image {
    uint64_t identity, revision;
    const char *name;
    qa_scene_image_kind kind;
    qa_scene_wrap wrap;
    qa_scene_filter filter;
    qa_vec4 border;
    const qa_scene_image_level *levels;
    size_t level_count;
    uint32_t logical_width, logical_height;
    /* GIF playback uses the donor's fixed 10 Hz clock. Element zero is NULL
     * and denotes this version; subsequent entries are owned immutable images. */
    const struct qa_scene_image *const *animation;
    size_t animation_count;
    size_t references;
    bool source_q3, source_mipmap;
    /* Ordinary mipmapped images follow the common texture mode.
     * Upload usage preserves independent picture, sky and sprite sampling. */
    bool texture_mode;
    qa_q3_texture_format source_format;
    uint32_t source_texture_unit;
    bool source_after_upload_border;
    bool source_dlight;
    qa_vec4 source_upload_border;
    /* Original decoded embedded model/BSP pixels may admit a recipient upload;
     * dynamic cinematic and generated control surfaces retain their identity. */
    bool recipient_upload_pixels, recipient_mipmap;
    bool streamed;
    uint64_t stream_writes; /* actual pixel writes, not a resource version */
} qa_scene_image;
/* The entered renderer recipient maps a reached immutable version into its
 * own upload domain. It preserves the original material and model identity. */
typedef bool (*qa_scene_recipient_image_fn)(void *, const qa_scene_image *,
    bool allow_picmip, bool mipmap, const qa_scene_image **, qa_error *);
typedef enum qa_scene_image_usage { QA_IMAGE_USAGE_DEFAULT, QA_IMAGE_USAGE_SKIN,
    QA_IMAGE_USAGE_SPRITE, QA_IMAGE_USAGE_WALL, QA_IMAGE_USAGE_PICTURE, QA_IMAGE_USAGE_SKY } qa_scene_image_usage;
typedef struct qa_scene_image_options {
    qa_game_family family;
    qa_scene_wrap wrap;
    qa_scene_filter filter;
    qa_scene_image_usage usage;
    bool mipmap, transparent, fullbright_only;
    int transparent_index;
    qa_bytes palette_rgb, translation;
    /* Set only by the actual Source renderer's admitted upload producer. */
    bool source_q3;
    qa_q3_image_upload_options source_upload;
} qa_scene_image_options;
typedef enum qa_scene_image_format { QA_SCENE_IMAGE_PNG, QA_SCENE_IMAGE_JPG,
    QA_SCENE_IMAGE_TGA, QA_SCENE_IMAGE_JPEG, QA_SCENE_IMAGE_BMP, QA_SCENE_IMAGE_GIF } qa_scene_image_format;
typedef struct qa_scene_image_policy {
    int32_t override_level;
    /* Source image-type bits: skin1, sprite2, wall4, picture8, sky16. */
    uint32_t override_usages;
    qa_scene_image_format formats[6];
    size_t format_count;
    bool source_formats;
} qa_scene_image_policy;

qa_scene_resources *qa_scene_resources_create(qa_vfs *, qa_error *);
void qa_scene_resources_destroy(qa_scene_resources *);
bool qa_scene_resources_retain(qa_scene_resources *, qa_error *);
qa_scene_resources *qa_scene_image_owner(const qa_scene_image *);
qa_scene_resources *qa_scene_image_resource_owner(const qa_scene_image *);
typedef bool (*qa_scene_source_image_admit_fn)(void *, const qa_scene_image *, uint32_t, qa_error *);
/* Bind the actual Source renderer before fresh registrations. Cold binding
 * preserves uploaded images and never dispatches their admission again. */
bool qa_scene_resources_set_source_image_admit(qa_scene_resources *, qa_scene_source_image_admit_fn, void *, qa_error *);
bool qa_scene_resources_source_image_admit_is(const qa_scene_resources *, qa_scene_source_image_admit_fn, const void *);
bool qa_scene_image_source_admit(qa_scene_resources *, qa_scene_image *, uint32_t, qa_error *);
/* Actual retained parent banks for cross-bank recipient image recipes.
 * Repeated parents are returned once per image edge; no admission or hold. */
size_t qa_scene_resources_parent_count(const qa_scene_resources *);
bool qa_scene_resources_parent_at(const qa_scene_resources *, size_t, qa_scene_resources **);
/* Live immutable image versions allocated by this resource owner. Array lives
 * in scratch; images borrow until the next owner/image mutation. No loading. */
bool qa_scene_resources_images(const qa_scene_resources *, qa_arena *,
                               const qa_scene_image *const **, size_t *, qa_error *);
/* Set image policy/fullbright range before content loads. Recreate resources
 * and dependent worlds/models when these registration settings change. */
bool qa_scene_image_policy_controls(int32_t override_level, uint32_t usage_mask,
                                    const char *formats, qa_scene_image_policy *, qa_error *);
bool qa_scene_resources_set_image_policy(qa_scene_resources *, qa_game_family,
                                         const qa_scene_image_policy *, qa_error *);
bool qa_scene_resources_set_fullbright_first(qa_scene_resources *, unsigned, qa_error *);
unsigned qa_scene_resources_fullbright_first(const qa_scene_resources *);
typedef struct qa_scene_resource_policy qa_scene_resource_policy;
/* Prepare a private loader against the actual retained lookup authority. All
 * dependent image owners prepare their bindings before ready seals this bank.
 * Publication keeps the resource service and existing image names alive. */
bool qa_scene_resource_policy_prepare(qa_scene_resources *,
    const qa_scene_image_policy policies[3], qa_scene_resource_policy **, qa_error *);
/* An actual renderer replacement reuploads admitted Source recipes using the
 * new physical renderer profile, retaining each request's mipmap/picmip flags. */
bool qa_scene_resource_policy_prepare_source_restart(qa_scene_resources *,
    const qa_q3_image_upload_options *, qa_scene_resource_policy **, qa_error *);
bool qa_scene_resource_policy_source_restart_read(const qa_scene_resource_policy *,
    qa_q3_image_upload_options *);
/* Completed new Source uploads. Constructor sequence merges true creation
 * order across banks even when animation or sampling shares image identities. */
bool qa_scene_resource_policy_source_image_count(const qa_scene_resource_policy *, size_t *, qa_error *);
bool qa_scene_resource_policy_source_image_at(const qa_scene_resource_policy *, size_t,
    const qa_scene_image **, uint64_t *creation_sequence, qa_error *);
qa_scene_resources *qa_scene_resource_policy_destination(const qa_scene_resource_policy *);
qa_scene_resources *qa_scene_resource_policy_source(const qa_scene_resource_policy *);
bool qa_scene_resource_policy_dependencies(qa_scene_resource_policy *,
    qa_scene_resource_policy *const *, size_t, qa_error *);
/* A cache-backed image is reacquired with its exact original request/options.
 * Procedural images retain their genuine original producer. NOT_FOUND is
 * reported to the binding owner, which owns its authored fallback policy. */
bool qa_scene_resource_policy_image(qa_scene_resource_policy *, const qa_scene_image *,
    qa_scene_image **, qa_error *);
bool qa_scene_resource_policy_dependency_image(qa_scene_resource_policy *, const qa_scene_image *,
    qa_scene_image **, qa_error *);
bool qa_scene_resource_policy_ready(qa_scene_resource_policy *, qa_error *);
bool qa_scene_resource_policy_ready_is(const qa_scene_resource_policy *);
void qa_scene_resource_policy_publish(qa_scene_resource_policy *);
bool qa_scene_resource_policy_finish(qa_scene_resource_policy **, qa_error *);
bool qa_scene_resource_policy_abort(qa_scene_resource_policy **, qa_error *);
typedef struct qa_scene_world_image_policy qa_scene_world_image_policy;
typedef struct qa_scene_material_image_policy qa_scene_material_image_policy;
typedef struct qa_material_order_image_policy qa_material_order_image_policy;
bool qa_material_order_image_policy_prepare(qa_material_order *, qa_material_order_image_policy **, qa_error *);
qa_material_order *qa_material_order_image_policy_source(const qa_material_order_image_policy *);
bool qa_material_order_image_policy_ready(qa_material_order_image_policy *, qa_error *);
bool qa_material_order_image_policy_ready_is(const qa_material_order_image_policy *);
void qa_material_order_image_policy_publish(qa_material_order_image_policy *);
bool qa_material_order_image_policy_finish(qa_material_order_image_policy **, qa_error *);
bool qa_material_order_image_policy_abort(qa_material_order_image_policy **, qa_error *);
bool qa_scene_world_image_policy_prepare(qa_scene_world *, qa_scene_resource_policy *,
    qa_scene_world_image_policy **, qa_error *);
bool qa_scene_world_image_policy_base(const qa_scene_world_image_policy *, uint64_t world,
    const char *name, const qa_scene_image *current, const qa_scene_image **destination);
bool qa_scene_world_image_policy_ready(qa_scene_world_image_policy *,
    const qa_scene_material_image_policy *, qa_error *);
bool qa_scene_world_image_policy_ready_is(const qa_scene_world_image_policy *);
void qa_scene_world_image_policy_publish(qa_scene_world_image_policy *);
bool qa_scene_world_image_policy_finish(qa_scene_world_image_policy **, qa_error *);
bool qa_scene_world_image_policy_abort(qa_scene_world_image_policy **, qa_error *);
bool qa_scene_material_image_policy_prepare(qa_material_library *, qa_scene_resource_policy *,
    qa_scene_world_image_policy *const *, size_t, qa_material_order_image_policy *,
    qa_scene_material_image_policy **, qa_error *);
/* Recompile the held records against an actual candidate Source profile.
 * The source profile stays installed until the prepared records publish. */
bool qa_scene_material_image_policy_prepare_profile(qa_material_library *, qa_scene_resource_policy *,
    qa_scene_world_image_policy *const *, size_t, qa_material_order_image_policy *,
    const struct qa_material_profile *, qa_scene_material_image_policy **, qa_error *);
qa_material_library *qa_scene_material_image_policy_source(const qa_scene_material_image_policy *);
qa_material_library *qa_scene_material_image_policy_destination(const qa_scene_material_image_policy *);
/* Prepared media admission targets only the held destination library. The
 * source retains its genuine live playback callback through publication. */
bool qa_scene_material_image_policy_video_start(qa_scene_material_image_policy *,
    const qa_scene_image *(*)(void *, const char *, qa_error *), void *, qa_error *);
bool qa_material_library_video_start_is(const qa_material_library *,
    const qa_scene_image *(*)(void *, const char *, qa_error *), const void *);
/* Borrow the exact installed producer without invoking it. The caller proves
 * the returned function's identity before interpreting its context. */
bool qa_material_library_video_start_read(const qa_material_library *,
    const qa_scene_image *(**)(void *, const char *, qa_error *), void **);
/* Every reached video callback stays recorded even if a later stage map
 * replaces its image. Source strings and initial images borrow the library. */
size_t qa_material_library_video_receipt_count(const qa_material_library *, size_t record);
bool qa_material_library_video_receipt_read(const qa_material_library *, size_t record,
    size_t receipt, const char **source, const qa_scene_image **initial_image);
bool qa_scene_material_image_policy_read(const qa_scene_material_image_policy *,
    const qa_material *current, const qa_material **destination);
bool qa_scene_material_image_policy_world(const qa_scene_material_image_policy *, uint64_t world,
    int32_t lightmap_index, bool has_lightmap, const char *name, const qa_scene_image_options *,
    const qa_material **current, const qa_material **destination, qa_error *);
bool qa_scene_material_image_policy_ready(qa_scene_material_image_policy *, qa_error *);
bool qa_scene_material_image_policy_ready_is(const qa_scene_material_image_policy *);
void qa_scene_material_image_policy_publish(qa_scene_material_image_policy *);
bool qa_scene_material_image_policy_finish(qa_scene_material_image_policy **, qa_error *);
bool qa_scene_material_image_policy_abort(qa_scene_material_image_policy **, qa_error *);
typedef struct qa_scene_model_image_policy qa_scene_model_image_policy;
struct qa_scene_model_content_lease;
bool qa_scene_model_image_policy_prepare(qa_scene_model *, qa_scene_resource_policy *,
    qa_scene_model_image_policy **, qa_error *);
bool qa_scene_model_image_policy_materials(qa_scene_model_image_policy *,
    qa_scene_material_image_policy *, qa_error *);
/* Prepare a genuine replacement child against the private image bank. On
 * success the child takes these owning content leases; failure leaves them
 * with the caller. Publication attaches the prepared child without loading. */
bool qa_scene_model_image_policy_replacement(qa_scene_model_image_policy *,
    const qa_model_replacement *, struct qa_scene_model_content_lease *mesh,
    struct qa_scene_model_content_lease *source, struct qa_scene_model_content_lease *animation,
    qa_error *);
/* The native source is the actual captured parent itself. Its replacement
 * child cannot outlive that parent; only the independently acquired MD5 mesh
 * and animation need transferred owning leases. */
bool qa_scene_model_image_policy_replacement_parent(qa_scene_model_image_policy *,
    const qa_model_replacement *, struct qa_scene_model_content_lease *mesh,
    struct qa_scene_model_content_lease *animation, qa_error *);
bool qa_scene_model_replacement_prepare(qa_scene_model *, const qa_model_replacement *,
    struct qa_scene_model_content_lease *mesh, struct qa_scene_model_content_lease *source,
    struct qa_scene_model_content_lease *animation, qa_error *);
/* A registered parent owns its native parsed source through destruction of
 * every child. Only the actual new mesh and animation leases transfer. */
bool qa_scene_model_replacement_prepare_parent(qa_scene_model *, const qa_model_replacement *,
    struct qa_scene_model_content_lease *mesh, struct qa_scene_model_content_lease *animation,
    qa_error *);
bool qa_scene_model_replacement_policy_bind(qa_scene_model *, bool enabled, double distance,
    const qa_model_replacement *, qa_error *);
bool qa_scene_model_replacement_policy_read(const qa_scene_model *, bool *configured,
    bool *enabled, double *distance, const qa_scene_model **selected);
/* Update an admitted replacement's use policy without changing its selected
 * child, parsed content, images or owning leases. */
bool qa_scene_model_replacement_policy_update(qa_scene_model *, bool enabled, double distance,
    qa_error *);
bool qa_scene_model_image_policy_select(qa_scene_model_image_policy *, bool enabled, double distance,
    const qa_model_replacement *, qa_error *);
bool qa_scene_model_source_bind(qa_scene_model *, struct qa_scene_model_content_lease *, qa_error *);
bool qa_scene_model_image_policy_ready(qa_scene_model_image_policy *, qa_error *);
bool qa_scene_model_image_policy_ready_is(const qa_scene_model_image_policy *);
void qa_scene_model_image_policy_publish(qa_scene_model_image_policy *);
bool qa_scene_model_image_policy_finish(qa_scene_model_image_policy **, qa_error *);
bool qa_scene_model_image_policy_abort(qa_scene_model_image_policy **, qa_error *);
uint64_t qa_scene_identity(void);
/* Returned palette borrows the resource service and is RGB, 256 entries. */
bool qa_scene_resources_palette(qa_scene_resources *, qa_game_family, qa_bytes *, qa_error *);
/* Read the already installed palette without acquisition or cache changes.
 * The borrowed span lasts until resource-owner restoration/destruction. */
bool qa_scene_resources_palette_read(const qa_scene_resources *, qa_game_family, qa_bytes *);
typedef struct qa_scene_palette_source {
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
} qa_scene_palette_source;
/* Exact palette admission retained by this bank; no historical-name lookup. */
bool qa_scene_resources_palette_source_read(const qa_scene_resources *, qa_game_family,
                                           qa_scene_palette_source *);
/* Borrowed immutable executable assets. The descriptor table, names and PNG
 * spans outlive the resource bank; no asset bytes are copied into checkpoints. */
typedef struct qa_scene_embedded_image { const char *name; qa_bytes png; } qa_scene_embedded_image;
bool qa_scene_resources_bind_embedded_images(qa_scene_resources *,
    const qa_scene_embedded_image *, size_t count, qa_error *);
bool qa_scene_image_load_embedded(qa_scene_resources *, const char *name, qa_scene_wrap,
    qa_scene_filter, qa_vec4 border, qa_scene_image **, qa_error *);
bool qa_scene_image_create(qa_scene_resources *, const char *, qa_scene_image_kind,
                          const qa_scene_image_level *, size_t, qa_scene_wrap,
                          qa_scene_filter, qa_vec4, qa_scene_image **, qa_error *);
bool qa_scene_image_stream_create(qa_scene_resources *, const char *, uint32_t, uint32_t,
                                  qa_scene_image **, qa_error *);
/* Construction writes need no frame. Live writes use frame_image_stream_write.
 * Consume that frame before producing the next frame from the same stream. */
bool qa_scene_image_stream_write(qa_scene_image *, qa_scene_rect, const uint8_t *, size_t stride,
                                 qa_error *);
bool qa_scene_image_load(qa_scene_resources *, const char *, const qa_scene_image_options *,
                        qa_scene_image **, qa_error *);
/* Decode this explicit file only. No extension or override search; the real
 * cache retains this admission rule through policy preparation and restore. */
bool qa_scene_image_load_exact(qa_scene_resources *, const char *, const qa_scene_image_options *,
                              qa_scene_image **, qa_error *);
/* Decode owned immutable input without filesystem lookup or cache admission.
 * Indexed formats that need an external palette require its explicit RGB span. */
bool qa_scene_image_decode_retained(qa_scene_resources *, const char *request, const char *source_path,
    qa_bytes source, const qa_scene_image_options *, qa_scene_image **, qa_error *);
typedef struct qa_scene_image_load_receipt {
    qa_resource *source, *logical_source;
    qa_mount_id source_mount, logical_mount;
    char *logical_path;
    qa_vfs_acquisition source_opening, logical_opening;
    qa_resource *palette_source;
    qa_vfs_acquisition palette_opening;
    bool palette_attempted;
    qa_status palette_error;
} qa_scene_image_load_receipt;
/* Observes the ordinary load winner even if its decode is rejected. Both real
 * resources remain retained until disposal; a missing search leaves it empty. */
bool qa_scene_image_load_observed(qa_scene_resources *, const char *, const qa_scene_image_options *,
    qa_scene_image **, qa_scene_image_load_receipt *empty_receipt, qa_error *);
void qa_scene_image_load_receipt_dispose(qa_scene_image_load_receipt *);
typedef struct qa_scene_image_alias_source {
    const qa_resource *source, *logical_source, *palette_source;
    const qa_vfs_acquisition *source_opening, *logical_opening, *palette_opening;
    const char *request, *source_path, *logical_path;
    qa_scene_image_options decode_options;
    qa_status source_error;
    bool palette_attempted;
    qa_status palette_error;
} qa_scene_image_alias_source;
/* Bind an actual transported immutable admission. The bank owns copied
 * receipts/options and retained resources. Sampling and recipient upload come
 * from the reached shader stage; original decode/palette and logical-size
 * decisions remain this source's. A NULL source is an explicit missing row. */
bool qa_scene_image_alias_bind(qa_scene_resources *, const char *alias,
                              const qa_scene_image_alias_source *, qa_error *);
/* Normalize an authored external model image path within its content root.
 * The caller owns the result. This is the same admission used by model images. */
char *qa_scene_model_image_path(const char *, qa_error *);
bool qa_scene_resources_source_q3_initialize(qa_scene_resources *, const qa_q3_image_upload_options *, qa_error *);
const qa_scene_image *qa_scene_source_q3_white(const qa_scene_resources *);
const qa_scene_image *qa_scene_source_q3_missing(const qa_scene_resources *);
const qa_scene_image *qa_scene_source_q3_dlight(const qa_scene_resources *);
const qa_scene_image *qa_scene_source_q3_fog(const qa_scene_resources *);
const qa_scene_image *qa_scene_source_q3_scratch(const qa_scene_resources *, size_t);
bool qa_scene_image_source_scratch_is(const qa_scene_resources *, size_t, const qa_scene_image *);
bool qa_scene_image_source_scratch_version(qa_scene_resources *, size_t,
    const qa_scene_image *previous, const qa_image *, qa_scene_image **, qa_error *);
typedef struct qa_scene_image_request {
    const char *name;
    qa_scene_image_options options;
    const qa_resource *source;
    qa_mount_id source_mount;
    bool exact_file;
} qa_scene_image_request;
/* Borrow the retained successful file admission for this exact image. */
bool qa_scene_image_request_read(const qa_scene_resources *, const qa_scene_image *, qa_scene_image_request *);
/* Re-decode the actual retained file using its original content rules, then
 * upload for a Source recipient. Raw playback/procedural images stay live. */
bool qa_scene_image_source_q3_variant(qa_scene_resources *, const qa_scene_image *,
    const qa_q3_image_upload_options *, qa_scene_image **, qa_error *);
/* Keep the first upload for the bank's exact installed Source receiver until
 * an actual Source restart replaces its recipient correspondence roots. */
bool qa_scene_image_source_q3_recipient_variant(qa_scene_resources *, const qa_scene_image *,
    const qa_q3_image_upload_options *, qa_scene_source_image_admit_fn, const void *,
    qa_scene_image **, qa_error *);
bool qa_scene_image_generic_variant(qa_scene_resources *, const qa_scene_image *,
    bool mipmap, qa_scene_image **, qa_error *);
bool qa_scene_image_replace(qa_scene_resources *, const qa_scene_image *, size_t level,
                           const qa_scene_image_level *, qa_scene_image **, qa_error *);
bool qa_scene_image_sample(qa_scene_resources *, const qa_scene_image *, bool mipmap,
                           qa_scene_wrap, qa_scene_image **, qa_error *);
void qa_scene_image_retain(const qa_scene_image *);
void qa_scene_image_release(const qa_scene_image *);
const qa_scene_image *qa_scene_image_at_time(const qa_scene_image *, double seconds);
const qa_scene_image *qa_scene_white(const qa_scene_resources *);
const qa_scene_image *qa_scene_missing(const qa_scene_resources *);

typedef struct qa_scene_vertex {
    qa_vec3 position, normal;
    qa_vec2 texcoord, lightmap;
    qa_vec4 color;
} qa_scene_vertex;
/* Takes both malloc-compatible arrays only on success, without copying. Finish
 * writing before publishing the first frame; replace rather than mutate a
 * published version. Producer/frame references own the arrays. Retain requires
 * an existing active reference; cached retirement records cannot be revived.
 * References are atomic, but callers must synchronize frame publication and never reset a
 * frame while a backend consumes it. */
/* Ordered source weights/ranges and render-to-source indices are copied on
 * successful adoption. Source ranges cover source_vertex_count; the map covers
 * the used render vertex prefix, which may be shorter than its allocation. */
typedef struct qa_scene_skeletal_input {
    const qa_model_weight *weights;
    const qa_model_weight_range *ranges;
    const uint32_t *sources;
    size_t vertex_count, source_vertex_count, weight_count, bone_count;
} qa_scene_skeletal_input;
typedef struct qa_scene_geometry_input {
    qa_scene_vertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count;
    const qa_scene_skeletal_input *skeletal;
} qa_scene_geometry_input;
qa_scene_geometry *qa_scene_geometry_adopt(const qa_scene_geometry_input *, qa_error *);
typedef struct qa_scene_skeletal_view {
    const qa_model_weight *weights;
    const qa_model_weight_range *ranges;
    const uint32_t *sources;
    size_t vertex_count, source_vertex_count, weight_count, bone_count;
} qa_scene_skeletal_view;
typedef struct qa_scene_geometry_view {
    const qa_scene_vertex *vertices;
    const uint32_t *indices;
    size_t vertex_count, index_count;
    qa_scene_skeletal_view skeletal;
} qa_scene_geometry_view;
/* Actual allocated extents, which may exceed a particular mesh's used prefix.
 * Read requires an active owner reference; retirement-only caches have none. */
bool qa_scene_geometry_read(const qa_scene_geometry *, qa_scene_geometry_view *);
void qa_scene_geometry_retain(const qa_scene_geometry *);
void qa_scene_geometry_release(const qa_scene_geometry *);
/* Backend residency keeps only the retirement record alive. It cannot prolong
 * active geometry indefinitely across multiple renderer caches. */
void qa_scene_geometry_cache_retain(const qa_scene_geometry *);
void qa_scene_geometry_cache_release(const qa_scene_geometry *);
bool qa_scene_geometry_active(const qa_scene_geometry *);
typedef enum qa_scene_primitive { QA_SCENE_TRIANGLES, QA_SCENE_LINES } qa_scene_primitive;
/* Nonzero identity/revision denotes immutable geometry owned by geometry.
 * Identity zero uses streaming storage. Such a mesh may still borrow retained
 * indices or vertices; preserve geometry when making that transient copy. */
typedef struct qa_scene_mesh {
    uint64_t identity, revision;
    const qa_scene_vertex *vertices;
    const uint32_t *indices;
    size_t vertex_count, index_count;
    qa_bounds bounds;
    qa_scene_primitive primitive;
    const qa_scene_geometry *geometry;
} qa_scene_mesh;
typedef enum qa_scene_blend {
    QA_BLEND_ZERO, QA_BLEND_ONE, QA_BLEND_SRC_COLOR, QA_BLEND_ONE_MINUS_SRC_COLOR,
    QA_BLEND_SRC_ALPHA, QA_BLEND_ONE_MINUS_SRC_ALPHA, QA_BLEND_DST_ALPHA,
    QA_BLEND_ONE_MINUS_DST_ALPHA, QA_BLEND_DST_COLOR, QA_BLEND_ONE_MINUS_DST_COLOR,
    QA_BLEND_SRC_ALPHA_SATURATE
} qa_scene_blend;
typedef enum qa_scene_depth { QA_DEPTH_ALWAYS, QA_DEPTH_LEQUAL, QA_DEPTH_EQUAL, QA_DEPTH_LESS,
    QA_DEPTH_GEQUAL, QA_DEPTH_DISABLED } qa_scene_depth;
typedef enum qa_scene_alpha { QA_ALPHA_NONE, QA_ALPHA_GT0, QA_ALPHA_LT128, QA_ALPHA_GE128, QA_ALPHA_GT666 } qa_scene_alpha;
typedef enum qa_scene_cull { QA_CULL_NONE, QA_CULL_FRONT, QA_CULL_BACK } qa_scene_cull;
typedef enum qa_scene_stencil_op { QA_STENCIL_KEEP, QA_STENCIL_ZERO, QA_STENCIL_REPLACE, QA_STENCIL_INCREMENT, QA_STENCIL_DECREMENT, QA_STENCIL_INVERT } qa_scene_stencil_op;
typedef enum qa_scene_stencil_test { QA_STENCIL_ALWAYS, QA_STENCIL_EQUAL, QA_STENCIL_NOTEQUAL } qa_scene_stencil_test;
typedef struct qa_scene_state {
    qa_scene_blend blend_source, blend_destination;
    qa_scene_depth depth_test;
    qa_scene_alpha alpha_test;
    qa_scene_cull cull;
    bool depth_write, color_write, polygon_offset, wireframe;
    float depth_near, depth_far, offset_factor, offset_units, line_width;
    bool stencil_enabled;
    qa_scene_stencil_test stencil_test;
    uint32_t stencil_reference, stencil_compare_mask, stencil_write_mask;
    qa_scene_stencil_op stencil_fail, stencil_depth_fail, stencil_depth_pass;
} qa_scene_state;
typedef enum qa_scene_texture_environment {
    QA_TEXTURE_MODULATE, QA_TEXTURE_ADD, QA_TEXTURE_REPLACE,
    /* Opaque base/lightmap combination. Native GL evaluates both texture units
     * before destination conversion; CPU retains its byte framebuffer rules. */
    QA_TEXTURE_LIGHTMAP_MODULATE, QA_TEXTURE_LIGHTMAP_INVERT_COLOR,
    QA_TEXTURE_LIGHTMAP_INVERT_ALPHA
} qa_scene_texture_environment;
typedef enum qa_scene_fog_kind { QA_FOG_NONE, QA_FOG_CONSTANT, QA_FOG_EXP2, QA_FOG_Q2 } qa_scene_fog_kind;
typedef enum qa_scene_fog_effect { QA_FOG_COLOR, QA_FOG_RGB, QA_FOG_ALPHA, QA_FOG_RGBA, QA_FOG_OVERLAY, QA_FOG_NO_EFFECT } qa_scene_fog_effect;
typedef struct qa_scene_fog {
    qa_scene_fog_kind kind;
    qa_scene_fog_effect effect;
    qa_vec3 color, height_color, height_end_color;
    float density, amount, sky_factor, height_density, height_start, height_end, height_falloff;
    float far_depth;
    bool sky_drawn;
} qa_scene_fog;
typedef struct qa_scene_light {
    qa_vec3 origin, color, direction;
    float radius, minimum, scale, cos_half_angle;
    bool additive, spot, casts_shadow;
    uint64_t identity, revision;
    uint32_t shadow_resolution;
    qa_game_family family;
} qa_scene_light;
typedef struct qa_scene_shadow_light {
    qa_scene_light light;
    qa_vec4 atlas_rect;
    qa_scene_matrix shadow_matrix;
    bool point_shadow, shadow_valid;
    qa_vec3 model_fraction;
} qa_scene_shadow_light;
typedef enum qa_scene_lighting_kind { QA_LIGHT_VERTEX, QA_LIGHT_Q2_WORLD, QA_LIGHT_Q2_MODEL_SHADOW } qa_scene_lighting_kind;
typedef enum qa_scene_light_pass { QA_LIGHT_PASS_TEXTURE, QA_LIGHT_PASS_LIGHTMAP,
    QA_LIGHT_PASS_MATERIAL_LIGHTMAP, QA_LIGHT_PASS_MODEL } qa_scene_light_pass;
typedef enum qa_scene_source_direct {
    QA_SOURCE_DIRECT_NONE, QA_SOURCE_DIRECT_BEAM, QA_SOURCE_DIRECT_AXIS, QA_SOURCE_DIRECT_SKY,
    QA_SOURCE_DIRECT_SHADOW_FINISH, QA_SOURCE_DIRECT_SHADOW_VOLUME_END, QA_SOURCE_DIRECT_RAW, QA_SOURCE_DIRECT_IMAGE_GRID
} qa_scene_source_direct;
typedef struct qa_scene_vertex_inputs {
    qa_vec4 color;
    bool constant_color, swap_uv;
} qa_scene_vertex_inputs;
typedef struct qa_scene_brush_surface {
    bool present;
    uint32_t polygon_vertices;
    qa_scene_plane plane;
    float texel_projection[2][4];
    float texture_mins[2];
    uint32_t texture_extents[2], texture_size[2];
    /* Absolute diffuse texels to the face's local light sample grid. */
    float lightmap_from_texel[2][3];
    qa_scene_rect lightmap_rect;
    uint64_t light_revision;
} qa_scene_brush_surface;
/* Final model-space joints are immutable frame storage shared by every draw of
 * one retained model pose slot. Ordinal is its first admission's command count. */
typedef struct qa_scene_skin_pose {
    const qa_model_pose *joints;
    size_t count, ordinal;
} qa_scene_skin_pose;
/* Borrow an existing model cache slot under the producer's frame lease. */
typedef struct qa_scene_skin_sample {
    qa_model_md5_view view;
    qa_model_vertex *vertices;
    size_t count;
    int rounding;
    qa_error error;
    bool ready;
} qa_scene_skin_sample;
typedef struct qa_scene_skinning {
    const qa_scene_skin_pose *pose;
    qa_vec3 shade_direction, light;
    qa_vec4 tint;
    float shell;
    bool shade;
    qa_scene_skin_sample *sample;
} qa_scene_skinning;
void qa_scene_skin_apply(const qa_scene_skinning *, const qa_model_vertex *,
                         const qa_scene_vertex *, qa_scene_vertex *);
typedef struct qa_scene_draw {
    qa_scene_mesh mesh;
    const qa_scene_skinning *skinning;
    qa_scene_brush_surface brush;
    qa_scene_vertex_inputs vertex_inputs;
    qa_scene_matrix model, mvp;
    const qa_scene_image *textures[2];
    /* Preserve the preceding binding for a source stage whose video/image
     * registration intentionally left that texture unit unchanged. */
    bool retain_texture[2];
    uint8_t texture_count;
    qa_scene_texture_environment environment;
    qa_scene_state state;
    qa_scene_fog fog;
    qa_scene_lighting_kind lighting;
    qa_scene_light_pass light_pass;
    const qa_scene_shadow_light *lights;
    size_t light_count;
    const qa_scene_image *shadow_atlas;
    float shadow_near, shade_scale;
    bool model_shade_scale;
    bool luminance_alpha;
    /* Reached Q3 Source stage submission, independent of queue grouping. */
    bool source_primitives;
    /* Producer-owned convex polygon: its triangles cover each sample once. */
    bool single_coverage;
    qa_scene_source_direct source_direct;
    bool source_retain_depth_range;
    bool source_retain_polygon_offset;
    bool source_stage_state;
    bool source_arrays;
    /* Source tess indices can address retained cells beyond the active count. */
    uint32_t source_vertex_storage;
    /* Packed Q3 shader/entity/fog/light order or caller's ordered sequence. */
    uint64_t sort_key;
    uint32_t entity, fog_index, light_mask;
} qa_scene_draw;
/* Recover the original pair for incompatible targets and final binding/state
 * publication after a fused draw. Returns false for ordinary environments. */
bool qa_scene_draw_lightmap_split(const qa_scene_draw *, qa_scene_draw *base,
                                 qa_scene_draw *lightmap);
typedef struct qa_scene_view {
    /* Every viewport, including a depth target, uses top-left pixel origin. */
    qa_scene_rect viewport;
    qa_vec3 origin, axis[3];
    qa_scene_matrix projection;
    bool clear_color, clear_depth, clear_stencil, clip_enabled, mirror;
    qa_vec4 color;
    float depth;
    qa_scene_plane clip_plane;
    uint32_t seat;
} qa_scene_view;
typedef struct qa_scene_particle_sample {
    qa_vec3 origin;
    qa_vec4 color;
} qa_scene_particle_sample;
typedef struct qa_scene_particle_batch {
    qa_scene_view view;
    qa_game_family family;
    const qa_scene_image *image;
    const qa_scene_particle_sample *samples; /* owned by the frame arena */
    size_t count;
} qa_scene_particle_batch;
typedef enum qa_scene_draw_buffer { QA_DRAW_FRONT, QA_DRAW_BACK, QA_DRAW_BACK_LEFT, QA_DRAW_BACK_RIGHT } qa_scene_draw_buffer;
typedef enum qa_scene_command_kind {
    QA_SCENE_COMMAND_VIEW, QA_SCENE_COMMAND_DRAW, QA_SCENE_COMMAND_TARGET,
    QA_SCENE_COMMAND_OPACITY_BEGIN, QA_SCENE_COMMAND_OPACITY_END,
    QA_SCENE_COMMAND_FOG, QA_SCENE_COMMAND_DRAW_BUFFER, QA_SCENE_COMMAND_SWAP,
    QA_SCENE_COMMAND_IMAGE, QA_SCENE_COMMAND_OUTPUT_DOMAIN,
    QA_SCENE_COMMAND_PREBLEND_GAMMA, QA_SCENE_COMMAND_IMAGE_REGION,
    QA_SCENE_COMMAND_IMAGE_STREAM, QA_SCENE_COMMAND_PARTICLES
} qa_scene_command_kind;
typedef struct qa_scene_image_region {
    const qa_scene_image *image;
    qa_scene_rect rect;
    const uint8_t *pixels; /* tightly packed RGBA rows, owned by the frame */
    const struct qa_scene_image_region *previous;
    uint64_t writes;
} qa_scene_image_region;
typedef struct qa_scene_image_stream {
    const qa_scene_image *image;
    const qa_scene_image_region *undo;
    uint64_t initial_writes;
    struct qa_scene_image_stream *next;
} qa_scene_image_stream;
typedef struct qa_scene_command {
    qa_scene_command_kind kind;
    union {
        qa_scene_view view;
        qa_scene_draw draw;
        struct { const qa_scene_image *image; } target; /* NULL restores display target. */
        struct { float value; } opacity;
        struct { qa_scene_fog fog; qa_scene_view view; } fog;
        struct { qa_scene_draw_buffer buffer; bool clear; } draw_buffer;
        /* Publish a new immutable version, including any retained binding with
         * the same identity. This preserves source update-image behavior. */
        const qa_scene_image *image;
        qa_scene_image_region image_region;
        const qa_scene_image_stream *image_stream;
        qa_scene_particle_batch particles;
        /* An actual renderer recipient chooses this region's upload/output
         * domain. Source RGB already owns software gamma in its image upload. */
        struct { qa_scene_rect rect; bool source; } output_domain;
        /* Generic overlay RGB enters a Source software-color viewport before
         * blending. Alpha and the viewport's output domain remain unchanged. */
        struct { bool enabled; } preblend_gamma;
    } data;
} qa_scene_command;
typedef enum qa_scene_group_kind { QA_SCENE_GROUP_COMPILED, QA_SCENE_GROUP_SOURCE,
    QA_SCENE_GROUP_SEQUENCE } qa_scene_group_kind;
typedef struct qa_scene_group {
    size_t first, count, ordinal;
    qa_scene_group_kind kind;
    const qa_material *material;
    float priority;
    uint32_t entity, fog, dlight, source_sort;
} qa_scene_group;
typedef struct qa_scene_model_pin {
    qa_scene_model *model;
    const qa_model_pose *pose;
    const qa_scene_skin_pose *prepared;
} qa_scene_model_pin;
/* Owns its arrays; do not shallow-copy a frame. Reset only once every consuming
 * backend has completed it. Commands are contiguous; transient geometry lives
 * in storage. Frame geometry pins survive command rollback until reset. */
typedef struct qa_scene_frame {
    qa_scene_image_stream *stream_images;
    qa_material_source_scratch *source_pending;
    uint64_t sequence, owner, image_epoch;
    bool source_backend, source_skip_backend, source_clear_draw_buffer;
    bool source_begin_frame;
    int32_t source_stereo_frame;
    bool source_front_buffer;
    /* Borrowed renderer registration owner. Libraries and their material
     * records outlive preparation of every pending source group. */
    qa_material_order *material_order;
    qa_arena storage;
    struct qa_scene_frame_storage *reserved;
    qa_scene_command *commands;
    size_t command_count, command_capacity;
    /* Derived picture run, invalidated by command publication and grouping.
     * An end unequal to command_count also rejects a producer rewind. */
    size_t picture_view_index, picture_view_end;
    const qa_scene_image **images;
    size_t image_count, image_capacity;
    const qa_scene_geometry **geometries;
    size_t geometry_count, geometry_capacity;
    qa_scene_model_pin *models;
    size_t model_count, model_capacity;
    qa_scene_group *groups;
    size_t group_count, group_capacity;
    qa_scene_group **sort_groups;
    size_t sort_group_capacity;
    qa_scene_command *sort_commands;
    size_t sort_command_capacity;
} qa_scene_frame;
/* Construct a frame at load. Transient geometry and persistent arrays use
 * the same reserved page pool, without heap fallback; zero selects 64 MiB. */
bool qa_scene_frame_init(qa_scene_frame *, uint64_t owner, size_t bytes, qa_error *);
bool qa_scene_frame_material_order(qa_scene_frame *, qa_material_order *, qa_error *);
void qa_scene_frame_reset(qa_scene_frame *, uint64_t sequence);
void qa_scene_frame_destroy(qa_scene_frame *);
bool qa_scene_frame_emit(qa_scene_frame *, const qa_scene_command *, qa_error *);
bool qa_scene_frame_output_domain(qa_scene_frame *, qa_scene_rect, bool source, qa_error *);
bool qa_scene_frame_preblend_gamma(qa_scene_frame *, bool enabled, qa_error *);
bool qa_scene_frame_draw(qa_scene_frame *, const qa_scene_draw *, qa_error *);
/* Pin borrowed retained geometry, including intermediate shadow-caster data,
 * until reset. No command is emitted. NULL geometry needs no reference. */
bool qa_scene_frame_geometry(qa_scene_frame *, const qa_scene_geometry *, qa_error *);
/* Retain and prepare one exact model pose slot until reset. Repeated admissions
 * in this frame return the same immutable joints and palette ordinal. */
bool qa_scene_frame_model(qa_scene_frame *, qa_scene_model *, const qa_model_pose *, size_t,
                           const qa_scene_skin_pose **, qa_error *);
bool qa_scene_frame_image(qa_scene_frame *, const qa_scene_image *, qa_error *);
bool qa_scene_frame_image_region(qa_scene_frame *, const qa_scene_image *, qa_scene_rect,
                                 qa_error *);
bool qa_scene_frame_image_stream(qa_scene_frame *, const qa_scene_image *, qa_error *);
bool qa_scene_frame_image_stream_write(qa_scene_frame *, qa_scene_image *, qa_scene_rect,
                                       const uint8_t *, size_t, qa_error *);
/* Publish GIF frame versions before any views, using the global render clock. */
bool qa_scene_resources_animate(qa_scene_resources *, double seconds, qa_scene_frame *, qa_error *);
/* Mark the just-appended commands as one indivisible surface/model group.
 * Ungrouped commands are barriers. Mark groups in append order; never nest.
 * Source shader ranks are resolved at finish, after all registrations exist. */
bool qa_scene_frame_group(qa_scene_frame *, size_t first, qa_scene_group_kind,
                          const qa_material *, float priority, uint32_t entity,
                          uint32_t fog, uint32_t dlight, qa_error *);
/* Finish a whole view only after world, models and effects are appended. It
 * sorts pending groups once and optionally appends the Q2 depth-fog pass. */
bool qa_scene_frame_finish(qa_scene_frame *, const qa_scene_view *, const qa_scene_fog *, qa_error *);
bool qa_scene_frame_picture(qa_scene_frame *, const qa_scene_image *, qa_scene_rect target,
                            qa_scene_rect rect, qa_vec4 uv, qa_vec4 color, qa_error *);
bool qa_scene_frame_picture_f(qa_scene_frame *, const qa_scene_image *, qa_scene_rect target,
                              qa_scene_rect_f rect, qa_vec4 uv, qa_vec4 color, qa_error *);
/* Clipped pixel-space quad owned by frame storage. Empty output means no overlap. */
bool qa_scene_picture_geometry(qa_scene_frame *, qa_scene_rect target, qa_scene_rect_f,
                               qa_vec4 uv, qa_vec4 color, qa_scene_mesh *, qa_error *);
void qa_scene_state_default(qa_scene_state *);
void qa_scene_matrix_identity(qa_scene_matrix *);
qa_scene_matrix qa_scene_matrix_multiply(qa_scene_matrix, qa_scene_matrix);
qa_scene_matrix qa_scene_view_matrix(const qa_scene_view *);
qa_scene_matrix qa_scene_projection(float fov_x, float fov_y, float near_clip, float far_clip);
qa_vec4 qa_scene_matrix_point(qa_scene_matrix, qa_vec3);
qa_scene_matrix qa_scene_model_matrix(const qa_model_transform *);
/* Four lateral planes plus the optional portal plane; near/far clipping belongs
 * to rasterization. Q3 source traversal deliberately selects only the first4. */
size_t qa_scene_frustum(const qa_scene_view *, qa_scene_plane planes[6]);
bool qa_scene_bounds_visible(qa_bounds, const qa_scene_plane *, size_t);

typedef enum qa_scene_q1_lightmap_encoding { QA_Q1_LIGHTMAP_RGB,
    QA_Q1_LIGHTMAP_INVERTED_LUMINANCE, QA_Q1_LIGHTMAP_INVERTED_ALPHA } qa_scene_q1_lightmap_encoding;
typedef struct qa_scene_world_options {
    qa_scene_image_options images;
    float subdivisions, q1_water_alpha, q2_light_modulate;
    uint32_t q3_overbright;
    bool source_fullbright;
    const char *q2_sky;
    qa_bytes external_lit;
    qa_bytes external_entities;
    bool has_external_entities;
    qa_scene_q1_lightmap_encoding q1_lightmap_encoding;
} qa_scene_world_options;
typedef struct qa_scene_world_entity {
    qa_vec3 ambient, directed, light_direction;
    qa_vec2 shader_texcoord;
    float shader_time, shadow_plane;
    bool non_normalized_axis, projection_shadow;
} qa_scene_world_entity;
typedef struct qa_scene_q1_mirror qa_scene_q1_mirror;
typedef struct qa_scene_q1_sky qa_scene_q1_sky;
typedef struct qa_scene_boxed_sky qa_scene_boxed_sky;
typedef struct qa_scene_q2_alpha qa_scene_q2_alpha;
typedef struct qa_scene_q1_sky_environment {
    bool boxed, fast;
    float quality, alpha, fog, far_clip;
    const qa_scene_image *images[6];
} qa_scene_q1_sky_environment;
typedef enum qa_scene_legacy_world_phase {
    QA_LEGACY_WORLD_ALL, QA_LEGACY_WORLD_OPAQUE, QA_LEGACY_WORLD_WATER,
    QA_LEGACY_WORLD_ALPHA
} qa_scene_legacy_world_phase;
typedef struct qa_scene_legacy_policy {
    qa_game_family source_family;
    bool present, fullbright, lightmap, dynamic, saturate, polyblend, cull, clear, flares;
    bool planar_shadows, double_eyes;
    float modulate;
    uint8_t monolightmap;
} qa_scene_legacy_policy;
typedef struct qa_scene_world_scratch qa_scene_world_scratch;

/* Each independent seat/subview owns load-sized traversal and visibility storage. */
bool qa_scene_world_scratch_create(const qa_scene_world *, qa_scene_world_scratch **, qa_error *);
void qa_scene_world_scratch_destroy(qa_scene_world_scratch *);

typedef struct qa_scene_world_input {
    qa_scene_world_scratch *scratch, *child_scratch;
    qa_scene_view view;
    double seconds;
    int64_t milliseconds;
    bool no_world, no_vis, no_cull, alternate_animation;
    bool skip_world, no_curves, disable_face_plane_cull, lock_pvs;
    bool source_hyperspace;
    int32_t fast_sky;
    bool source_show_cluster, source_show_cluster_modified;
    bool (*source_cluster_modified)(void *, bool *, qa_error *);
    bool (*source_cluster_clear)(void *, qa_error *);
    void *source_cluster_context;
    void (*source_cluster_print)(void *, const char *);
    void *source_cluster_print_context;
    const qa_scene_source_world_view *source_visibility;
    float source_far_clip;
    qa_vec3 pvs_origin;
    bool use_pvs_origin;
    int32_t secondary_cluster;
    bool use_secondary_cluster;
    const uint8_t *visible_areas;
    size_t visible_area_bytes;
    const float *q1_styles;
    const qa_vec3 *q2_styles;
    size_t style_count;
    const qa_scene_light *lights;
    size_t light_count;
    bool use_projected_lights, source_order;
    bool source_entity_cells;
    qa_material_source_scratch *source_scratch;
    const qa_scene_image *source_white;
    qa_scene_recipient_image_fn source_recipient_image;
    void *source_recipient_context;
    qa_scene_source_diagnostics source_diagnostics;
    qa_scene_source_diagnostics_read_fn source_diagnostics_read;
    void *source_diagnostics_context;
    const qa_scene_light *projected_lights;
    size_t projected_light_count;
    qa_scene_fog fog;
    float curve_error, identity_light;
    uint32_t animation_frame;
    bool use_animation_frame;
    const qa_scene_shadow_light *shadow_lights;
    size_t shadow_light_count;
    const qa_scene_image *shadow_atlas;
    const qa_scene_image *sky_images[6];
    bool override_sky, sky_auto_rotate;
    float sky_rotation;
    qa_vec3 sky_axis;
    const char *const *render_texts;
    size_t render_text_count;
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    /* Flare admission remains source-owned; this callback submits its visible
     * geometry into the same view without introducing another renderer. */
    bool (*flare)(void *, uint32_t surface, qa_vec3 origin, qa_vec3 color,
                  qa_vec3 normal, const qa_scene_view *, qa_scene_frame *, qa_error *);
    void *flare_context;
    /* Optional inline-model material context, borrowed for one submission. */
    const qa_scene_world_entity *entity_material;
    /* Reached legacy controls, independent of Q3 Source policies. */
    bool legacy_flashblend;
    bool legacy_texture_sort;
    qa_scene_legacy_policy legacy_policy;
    qa_scene_legacy_world_phase legacy_phase;
    const qa_scene_q1_mirror *q1_mirror;
    const qa_scene_q1_sky_environment *q1_sky_environment;
    qa_scene_q1_sky *q1_sky;
    qa_scene_boxed_sky *boxed_sky;
    qa_scene_q2_alpha *q2_alpha;
} qa_scene_world_input;
/* One actual world/brush traversal retains the visible sky footprint. Finish
 * places the flat and slow sky commands before that same view's solid draws. */
bool qa_scene_world_q1_sky_begin(qa_scene_world *, const qa_scene_world_input *,
    qa_scene_frame *, qa_scene_q1_sky **, qa_error *);
bool qa_scene_world_q1_sky_finish(qa_scene_q1_sky *, qa_scene_frame *, qa_error *);
bool qa_scene_world_boxed_sky_begin(qa_scene_world *, const qa_scene_world_input *,
    qa_scene_frame *, qa_scene_boxed_sky **, qa_error *);
bool qa_scene_world_boxed_sky_finish(qa_scene_boxed_sky *, qa_scene_frame *, qa_error *);
bool qa_scene_world_q2_alpha_begin(const qa_scene_world_input *, qa_scene_frame *,
    qa_scene_q2_alpha **, qa_error *);
/* World retains its immutable BSP bytes and the actual resource/material owners. */
bool qa_scene_world_create(const qa_bsp_view *, qa_scene_resources *, qa_material_library *,
                           const qa_scene_world_options *, qa_scene_world **, qa_error *);
void qa_scene_world_destroy(qa_scene_world *);
bool qa_scene_world_retain(qa_scene_world *, qa_error *);
void qa_scene_world_release(qa_scene_world *);
int32_t qa_scene_world_leaf(const qa_scene_world *, qa_vec3);
/* Original Q3 compares its area mask once for the parent scene. Portal views
 * share that result while each view can replace the retained PVS marks. */
bool qa_scene_world_source_begin_scene(qa_scene_world *, const qa_scene_world_input *, qa_error *);
/* Parent geometry retains this frame-owned visibility receipt while a portal
 * replaces the live Source PVS. Preparation selects the actual view zFar. */
bool qa_scene_world_source_prepare_view(qa_scene_world *, qa_scene_world_input *, qa_scene_frame *, qa_error *);
/* Borrowed actual world/surface mask. The owner outlives queued Source work;
 * the read does not change admission, PVS or the retained mask. */
bool qa_scene_world_source_light_mask_read(const qa_scene_world *, uint32_t, uint32_t *, qa_error *);
bool qa_scene_world_source_sky_context(const qa_scene_world *, qa_material_library *,
    const qa_scene_world_input *, qa_scene_frame *, qa_material_context *, qa_error *);
bool qa_scene_world_source_sky_submit(const qa_scene_world *, const qa_material *, const qa_material *,
    const qa_scene_mesh *, const qa_material_context *, float far_clip, qa_scene_frame *, qa_error *);
bool qa_scene_world_submit(qa_scene_world *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool qa_scene_world_submit_model(qa_scene_world *, uint32_t model, const qa_model_transform *,
                                 const qa_scene_world_input *, uint32_t entity,
                                 qa_vec4 color, qa_scene_frame *, qa_error *);
bool qa_scene_world_source_model_admission(const qa_scene_world *, uint32_t model,
    const qa_model_transform *, const qa_scene_world_input *, bool *visible, qa_error *);
bool qa_scene_world_sample_light(const qa_scene_world *, qa_vec3 point, qa_vec3 *ambient,
                                 qa_vec3 *directed, qa_vec3 *direction);
/* Samples the same static point light with the caller's entered lightstyles.
 * Dynamic entity lighting remains part of its actual model lighting policy. */
bool qa_scene_world_sample_light_input(const qa_scene_world *, const qa_scene_world_input *,
    qa_vec3 point, qa_vec3 *ambient, qa_vec3 *directed, qa_vec3 *direction, qa_error *);
/* The actual downward light-sampling BSP hit, including unlit surfaces. */
bool qa_scene_world_sample_floor(const qa_scene_world *, qa_vec3, qa_vec3 *point, bool *found);
/* A frame-owned visible NetQuake window02_1 chain. It remains valid until the
 * frame resets; reflected traversal never replaces the captured parent chain. */
bool qa_scene_world_q1_mirror(qa_scene_world *, const qa_scene_world_input *, qa_scene_frame *,
                              const qa_scene_q1_mirror **, qa_scene_view *, bool *, qa_error *);
bool qa_scene_world_q1_mirror_scope(const qa_scene_world *, const qa_scene_world_input *,
                                    const qa_scene_frame *);
bool qa_scene_world_q1_mirror_overlay(qa_scene_world *, const qa_scene_world_input *,
                                      float alpha, qa_scene_frame *, qa_error *);
qa_bounds qa_scene_world_bounds(const qa_scene_world *);
bool qa_scene_world_sky_drawn(const qa_scene_world *);
bool qa_scene_world_remap(qa_scene_world *, const char *original, const char *replacement,
                          float time_offset, qa_error *);
typedef struct qa_scene_fog_volume {
    uint32_t index;
    qa_scene_fog fog;
    float tc_scale;
    bool has_surface;
    qa_scene_plane surface;
} qa_scene_fog_volume;
/* Reads the reached, decomposed Original Q3 fog ordinal. Zero has no fog. */
bool qa_scene_world_source_fog_read(const qa_scene_world *, uint32_t,
                                  qa_scene_fog_volume *, qa_error *);
/* Returns the first source fog containing the sphere. A miss clears out. */
bool qa_scene_world_fog_for_sphere(const qa_scene_world *, qa_vec3, float,
                                  qa_scene_fog_volume *out);
/* Inclusive bounds overlap, in source fog order. Invalid bounds or a miss clear out. */
bool qa_scene_world_fog_for_bounds(const qa_scene_world *, qa_bounds,
                                  qa_scene_fog_volume *out);

typedef struct qa_scene_model_input qa_scene_model_input;
typedef enum qa_scene_alias_lighting {
    QA_ALIAS_CONTENT_LIGHTING, QA_ALIAS_Q3_DIFFUSE, QA_ALIAS_PREPARED_LIGHT
} qa_scene_alias_lighting;
typedef struct qa_scene_model_attachment {
    const char *tag;
    qa_scene_model *model;
    const qa_scene_model_input *input;
} qa_scene_model_attachment;
typedef struct qa_scene_model_indexed_skin {
    const char *name;
    uint32_t width, height;
    qa_bytes indices;
} qa_scene_model_indexed_skin;
struct qa_scene_model_input {
    qa_scene_view view;
    qa_model_transform transform;
    qa_vec3 previous_origin, ambient, directed, light_direction;
    qa_vec4 color;
    qa_game_family family;
    qa_scene_alias_lighting alias_lighting;
    qa_vec3 alias_light;
    uint32_t frame, old_frame, skin, flags, entity, lod;
    float back_lerp, radius, rotation, shadow_plane, identity_light;
    double seconds, sync_base;
    /* Optional exact source clock for material evaluation. */
    int64_t milliseconds;
    bool has_milliseconds;
    const qa_model_pose *pose;
    size_t pose_count;
    const qa_material *custom_material;
    const qa_model_skin_map *custom_skin;
    const qa_material *const *custom_skin_materials;
    size_t custom_skin_material_count;
    /* Per-input MDL skin; uploaded cache entries own the indexed pixels. */
    const qa_scene_model_indexed_skin *indexed_skin;
    /* Borrowed registration owner for Q3 surface, default and shadow materials.
     * Geometry may belong to another content owner. No library is retained. */
    qa_material_library *material_library;
    qa_scene_fog fog;
    const char *source_path;
    /* The actual registration namespace owns deferred Source model bytes. */
    qa_q3_presentation_assets *source_model_owner;
    bool (*source_model_retain)(void *, qa_error *);
    void (*source_model_release)(void *);
    const qa_model_replacement *replacement;
    const qa_model_animation *animation;
    const qa_scene_model_attachment *attachments;
    size_t attachment_count;
    bool view_model, player, infrared, monochrome, no_cull, planar_shadow, shadow_only;
    bool q1_double_eyes;
    bool non_normalized_axis;
    bool source_order, fog_has_surface;
    bool source_entity_cell;
    qa_scene_recipient_image_fn source_recipient_image;
    void *source_recipient_context;
    qa_material_source_scratch *source_scratch;
    qa_scene_source_diagnostics source_diagnostics;
    qa_scene_source_diagnostics_read_fn source_diagnostics_read;
    void *source_diagnostics_context;
    uint32_t fog_index;
    float fog_tc_scale;
    qa_scene_plane fog_surface;
    uint32_t shadow_mode;
    bool model_beam;
    float beam_segment_length;
    uint8_t left_hand;
    float shader_time, q1_overbright;
    qa_vec2 shader_texcoord;
    const char *const *render_texts;
    size_t render_text_count;
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    const qa_scene_shadow_light *shadow_lights;
    size_t shadow_light_count;
    const qa_scene_image *shadow_atlas;
};
/* Borrows immutable decoded model; caller keeps it alive until destruction. */
bool qa_scene_model_create(const qa_model *, qa_scene_resources *, qa_material_library *,
                           const qa_scene_image_options *, qa_scene_model **, qa_error *);
void qa_scene_model_destroy(qa_scene_model *);
uint32_t qa_scene_model_effect_flags(const qa_scene_model *);
bool qa_scene_model_submit(qa_scene_model *, const qa_scene_model_input *, qa_scene_frame *, qa_error *);
/* Pure Source whole-model admission before reached entity lighting setup. */
bool qa_scene_model_source_admission(const qa_scene_model *, const qa_scene_model_input *,
                                    bool *visible, qa_error *);
uint32_t qa_scene_model_select_lod(const qa_scene_model_input *, uint32_t count,
                                  float radius, float lod_scale, float lod_bias);

typedef struct qa_scene_portal {
    qa_vec3 origin, old_origin, axis[3];
    float rotation_speed, rotation_offset, range;
    bool mirror, oscillate;
} qa_scene_portal;
/* Source allows one child portal per unclipped seat view. A found child is
 * submitted before its parent with its returned PVS origin and no recursion. */
bool qa_scene_world_portal_view(qa_scene_world *, const qa_scene_world_input *,
                                const qa_scene_portal *, size_t, qa_scene_view *,
                                qa_vec3 *pvs_origin, bool *found, qa_error *);
bool qa_scene_portal_view(const qa_scene_view *, qa_scene_plane, const qa_scene_portal *,
                          double seconds, qa_scene_view *, qa_vec3 *pvs_origin);
bool qa_scene_beam(qa_scene_frame *, const qa_scene_view *, qa_vec3 start, qa_vec3 end,
                   float width, qa_vec4 color, const qa_scene_image *, qa_error *);
bool qa_scene_sky(qa_scene_frame *, const qa_scene_view *,
                  const qa_scene_image *const images[6], float radius, float rotation,
                  qa_vec3 axis, qa_vec4 color, qa_error *);
typedef struct qa_scene_shadow_caster {
    const qa_scene_mesh *meshes;
    size_t mesh_count;
    qa_scene_matrix transform;
    qa_bounds bounds;
    uint64_t identity, revision;
    bool world_geometry;
} qa_scene_shadow_caster;
bool qa_scene_world_shadow_caster(qa_scene_world *, uint32_t model,
                                  const qa_model_transform *, const qa_scene_world_input *,
                                  qa_scene_frame *, qa_scene_shadow_caster *, qa_error *);
bool qa_scene_model_shadow_caster(qa_scene_model *, const qa_scene_model_input *,
                                  qa_scene_frame *, qa_scene_shadow_caster *, qa_error *);
typedef struct qa_scene_shadows qa_scene_shadows;
qa_scene_shadows *qa_scene_shadows_create(qa_scene_resources *, qa_error *);
void qa_scene_shadows_destroy(qa_scene_shadows *);
bool qa_scene_shadows_prepare(qa_scene_shadows *, const qa_scene_light *, size_t,
                              const qa_scene_shadow_caster *, size_t, qa_scene_frame *,
                              const qa_scene_shadow_light **, size_t *,
                              const qa_scene_image **, qa_error *);

#endif

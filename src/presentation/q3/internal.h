#ifndef QA_Q3_PRESENTATION_INTERNAL_H
#define QA_Q3_PRESENTATION_INTERNAL_H

#include "qa/q3_presentation.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_model_opening.h"
#include "qa/common_parse.h"
#include "qa/scene_effects.h"
#include "qa/text.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum q3p_resource_kind { Q3P_MODEL, Q3P_SKIN, Q3P_SHADER, Q3P_SOUND } q3p_resource_kind;
typedef struct q3p_name {
    struct q3p_name *next;
    uint64_t hash;
    q3p_resource_kind kind;
    int32_t handle;
    bool option, generated;
    char name[];
} q3p_name;
typedef struct q3p_opening_order {
    qa_mount_id *mounts;
    size_t count;
    char *prefix;
    bool user_overlay;
} q3p_opening_order;
typedef struct q3p_model {
    qa_resource *resource;
    qa_resource *lod_resources[3];
    char *first_requested_path;
    qa_vfs_acquisition opening, lod_openings[3];
    int64_t opening_rank, lod_opening_ranks[3];
    q3p_opening_order opening_order, lod_opening_orders[3];
    const qa_model *sources[3];
    qa_q3_asset_model_lease source_leases[3];
    qa_q3_presentation_provider provider;
    qa_model model;
    qa_model_lod_set lods;
    qa_scene_model *scene[3];
    qa_scene_world *world;
    qa_bounds bounds;
    uint32_t inline_model;
    uint64_t scene_ordinals[3], world_ordinal;
    qa_model source_md4_model;
    const qa_model *source_md4;
    qa_q3_asset_model_lease source_md4_lease;
    qa_scene_model *source_md4_scene;
    qa_resource *source_md4_resource;
    qa_vfs_acquisition source_md4_opening;
    int64_t source_md4_rank;
    q3p_opening_order source_md4_order;
    uint64_t source_md4_scene_ordinal;
    qa_model_format source_kind;
    uint32_t source_num_lods;
    bool source_registration, registration_bad, source_md4_slots[3];
    bool has_lods, owns_world, borrowed_models, borrowed_scenes, borrowed_world;
} q3p_model;
typedef struct q3p_skin {
    qa_resource *resource;
    qa_q3_presentation_provider provider;
    qa_model_skin_map map;
    const qa_material **materials;
    bool source_registration;
} q3p_skin;
typedef struct q3p_provider_custody q3p_provider_custody;
typedef struct q3p_map_custody q3p_map_custody;
struct qa_q3_presentation_assets {
    qa_q3_presentation_asset_options options;
    qa_q3_presentation_assets *parent;
    q3p_provider_custody *providers;
    q3p_map_custody *maps;
    q3p_name **names;
    size_t name_count, name_capacity;
    q3p_model **models;
    size_t model_count, model_capacity;
    q3p_skin **skins;
    size_t skin_count, skin_capacity;
    const qa_material **shaders;
    size_t shader_count, shader_capacity;
    qa_audio_asset **sounds;
    size_t sound_count, sound_capacity;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    unsigned busy, users;
    bool capturing, codec_busy, retired;
};
typedef qa_q3_scene_polygon q3p_polygon;
typedef enum q3p_movie_kind { Q3P_MOVIE_EMPTY, Q3P_MOVIE_PENDING, Q3P_MOVIE_LOCAL, Q3P_MOVIE_SYSTEM } q3p_movie_kind;
typedef struct q3p_movie_source {
    struct q3p_movie_source *next;
    qa_cinematic_asset *asset;
    char *path;
    char request[];
} q3p_movie_source;
typedef struct q3p_movie {
    q3p_movie_kind kind;
    qa_cinematic *local;
    qa_cinematic_asset *asset;
    qa_q3_system_movie system;
    qa_scene_rect_f rect;
    char *path;
    uint32_t flags;
} q3p_movie;
struct qa_q3_presentation {
    qa_q3_presentation_options options;
    qa_scene_frame *frame;
    const qa_q3_scene_options *submission;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    qa_bytes entity_text;
    qa_common_parser parser;
    qa_common_cursor cursor;
    qa_q3_ref_entity *entities;
    size_t entity_count, entity_capacity;
    uint32_t source_entity_first;
    qa_q3_presentation_assets **source_entity_assets, **source_polygon_assets;
    q3p_polygon *polygons;
    size_t polygon_count, polygon_capacity;
    qa_scene_vertex *vertices;
    size_t vertex_count, vertex_capacity;
    qa_scene_light *lights;
    size_t light_count, light_capacity;
    qa_scene_portal *portals;
    size_t portal_capacity;
    qa_scene_vec4 color;
    int32_t render_milliseconds;
    qa_scene_view material_view;
    q3p_movie movies[16];
    q3p_movie_source *movie_sources;
    unsigned busy;
    bool world_loaded, material_view_valid;
};
bool q3p_fail(qa_error *, qa_status, const char *);
bool q3p_reserve(void **, size_t *capacity, size_t count, size_t width, qa_error *);
bool q3p_begin(qa_q3_presentation *, qa_error *);
bool q3p_end(qa_q3_presentation *, bool);
bool q3p_capture_begin(qa_q3_presentation *, bool *owned_assets, qa_error *);
void q3p_capture_end(qa_q3_presentation *, bool owned_assets);
q3p_name *q3p_find_name(qa_q3_presentation_assets *, q3p_resource_kind, const char *);
uint64_t q3p_name_hash(q3p_resource_kind, const char *);
bool q3p_add_name(qa_q3_presentation_assets *, q3p_resource_kind, const char *,
                   int32_t handle, bool option, qa_error *);
bool q3p_select(qa_q3_presentation_assets *, const char *, qa_q3_asset_kind,
                 qa_q3_presentation_provider *, qa_error *);
bool q3p_model_get(const qa_q3_presentation_assets *, int32_t, const q3p_model **, qa_error *);
bool q3p_shader_get(const qa_q3_presentation_assets *, int32_t, const qa_material **, qa_error *);
bool q3p_skin_get(const qa_q3_presentation_assets *, int32_t, const qa_model_skin_map **, qa_error *);
bool q3p_skin_materials(const qa_q3_presentation_assets *, int32_t, const qa_material *const **, size_t *, qa_error *);
qa_audio_asset *q3p_sound(const qa_q3_presentation_assets *, int32_t);
void q3p_model_free(q3p_model *);
const qa_model *q3p_model_source(const q3p_model *, uint32_t);
const qa_model *q3p_model_md4_source(const q3p_model *);
bool q3p_assets_children_idle(const qa_q3_presentation_assets *);
void q3p_provider_custody_release(qa_q3_presentation_assets *);
bool q3p_model_shared(const qa_q3_presentation_assets *, const q3p_model *);
bool q3p_skin_shared(const qa_q3_presentation_assets *, const q3p_skin *);
bool q3p_assets_fork(qa_q3_presentation_assets *, qa_q3_presentation_assets **, qa_error *);
char *q3p_movie_path(const char *, qa_error *);
bool q3p_movie_close(qa_q3_presentation *, uint32_t, qa_cinematic_end, qa_error *);
bool q3p_picture(qa_q3_presentation *, const qa_material *, qa_scene_rect_f, qa_scene_vec4, qa_error *);
qa_scene_vec4 q3p_color(const uint8_t[4]);
const qa_material *q3p_default_material(const qa_q3_presentation *);

#endif

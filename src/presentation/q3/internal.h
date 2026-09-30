#ifndef QA_Q3_PRESENTATION_INTERNAL_H
#define QA_Q3_PRESENTATION_INTERNAL_H

#include "qa/q3_presentation.h"
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
typedef struct q3p_model {
    qa_resource *resource;
    qa_q3_presentation_provider provider;
    qa_model model;
    qa_model_lod_set lods;
    qa_scene_model *scene[3];
    qa_scene_world *world;
    qa_bounds bounds;
    uint32_t inline_model;
    bool has_lods, owns_world;
} q3p_model;
typedef struct q3p_skin { qa_resource *resource; qa_model_skin_map map; } q3p_skin;
struct qa_q3_presentation_assets {
    qa_q3_presentation_asset_options options;
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
};
typedef struct q3p_polygon {
    int32_t shader;
    size_t first, count;
    qa_scene_fog_volume fog;
} q3p_polygon;
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
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    qa_bytes entity_text;
    qa_common_parser parser;
    qa_common_cursor cursor;
    qa_q3_ref_entity *entities;
    size_t entity_count, entity_capacity;
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
q3p_name *q3p_find_name(qa_q3_presentation_assets *, q3p_resource_kind, const char *);
bool q3p_add_name(qa_q3_presentation_assets *, q3p_resource_kind, const char *,
                   int32_t handle, bool option, qa_error *);
bool q3p_select(qa_q3_presentation_assets *, const char *, qa_q3_asset_kind,
                 qa_q3_presentation_provider *, qa_error *);
bool q3p_model_get(const qa_q3_presentation_assets *, int32_t, const q3p_model **, qa_error *);
bool q3p_shader_get(const qa_q3_presentation_assets *, int32_t, const qa_material **, qa_error *);
bool q3p_skin_get(const qa_q3_presentation_assets *, int32_t, const qa_model_skin_map **, qa_error *);
qa_audio_asset *q3p_sound(const qa_q3_presentation_assets *, int32_t);
void q3p_model_free(q3p_model *);
bool q3p_movie_close(qa_q3_presentation *, uint32_t, qa_cinematic_end, qa_error *);
bool q3p_picture(qa_q3_presentation *, const qa_material *, qa_scene_rect_f, qa_scene_vec4, qa_error *);
qa_scene_vec4 q3p_color(const uint8_t[4]);
const qa_material *q3p_default_material(const qa_q3_presentation *);

#endif

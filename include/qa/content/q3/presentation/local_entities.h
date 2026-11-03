/* Port of src/content/q3/presentation/local-entities.ts. GPL-2.0-or-later.
 * Local effect pool and frame processing from id Software
 * code/cgame/cg_localents.c. ImpactMarkSystem/ImpactMarkRequest
 * (marks.ts) and ClientEffects/SmokePuffOptions (effects.ts) are the
 * marks.h/effects.h types. */
#ifndef QA_CONTENT_Q3_PRESENTATION_LOCAL_ENTITIES_H
#define QA_CONTENT_Q3_PRESENTATION_LOCAL_ENTITIES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/mixer.h"
#include "qa/audio/wav.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/presentation/collision_host.h"
#include "qa/content/q3/presentation/effects.h"
#include "qa/content/q3/presentation/marks.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/contracts/math.h"
enum {
  QA_Q3_PRES_MAX_LOCAL_ENTITIES = 512,
  QA_Q3_PRES_LEF_PUFF_DONT_SCALE = 1,
  QA_Q3_PRES_LEF_TUMBLE = 2,
  QA_Q3_PRES_LEF_SOUND1 = 4,
  QA_Q3_PRES_LEF_SOUND2 = 8
};
typedef enum qa_q3_pres_local_entity_type {
  QA_Q3_PRES_LE_MOVE_SCALE_FADE, QA_Q3_PRES_LE_FALL_SCALE_FADE, QA_Q3_PRES_LE_SCALE_FADE,
  QA_Q3_PRES_LE_SCORE_PLUM, QA_Q3_PRES_LE_SPRITE_EXPLOSION,
  QA_Q3_PRES_LE_FRAGMENT, QA_Q3_PRES_LE_KAMIKAZE, QA_Q3_PRES_LE_INVUL_IMPACT, QA_Q3_PRES_LE_INVUL_JUICED,
  QA_Q3_PRES_LE_FADE_RGB, QA_Q3_PRES_LE_EXPLOSION,
  QA_Q3_PRES_LE_MARK, QA_Q3_PRES_LE_SHOW_REF_ENTITY
} qa_q3_pres_local_entity_type;
const char *qa_q3_pres_local_entity_type_name(qa_q3_pres_local_entity_type type);
typedef enum qa_q3_pres_le_mark_type { QA_Q3_PRES_LE_MARK_NONE, QA_Q3_PRES_LE_MARK_BURN, QA_Q3_PRES_LE_MARK_BLOOD } qa_q3_pres_le_mark_type;
typedef enum qa_q3_pres_le_bounce_sound_type { QA_Q3_PRES_LE_SOUND_NONE, QA_Q3_PRES_LE_SOUND_BLOOD, QA_Q3_PRES_LE_SOUND_BRASS } qa_q3_pres_le_bounce_sound_type;
/* TS LocalEntity union: category is implied by le_type (sprite types
 * hold a sprite ref entity, model types a model, etc.). */
typedef struct qa_q3_pres_local_entity {
  qa_q3_pres_local_entity_type le_type;
  qa_q3_pres_ref_entity ref_entity;
  double le_flags, start_time, end_time, fade_in_time, life_rate;
  qa_q3_base_trajectory pos, angles;
  double bounce_factor;
  qa_vec4 color;
  double radius, light;
  qa_vec3 light_color;
  qa_q3_pres_le_mark_type le_mark_type;
  qa_q3_pres_le_bounce_sound_type le_bounce_sound_type;
} qa_q3_pres_local_entity;
typedef struct qa_q3_pres_local_entity_media {
  const qa_q3_pres_scene_shader *blood_trail_shader, *blood_mark_shader, *burn_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *number_shaders[11]; /* nullable entries */
  qa_pcm_sound *gib_bounce_sounds[3]; /* borrowed, nullable */
} qa_q3_pres_local_entity_media;
typedef struct qa_q3_pres_mission_local_entity_media {
  qa_q3_pres_local_entity_media base;
  qa_q3_pres_scene_model kamikaze_shock_wave;
  qa_pcm_sound *kamikaze_explode_sound, *kamikaze_implode_sound; /* borrowed, nullable */
} qa_q3_pres_mission_local_entity_media;
/* TS ImpactMarkRequest / Pick<ImpactMarkSystem,"impactMark">
 * (marks.ts): the landed marks.h types. Returned polys are
 * caller-owned: destroy each with qa_q3_pres_ref_poly_destroy, then
 * free the array. */
/* TS SmokePuffOptions / ClientEffects (effects.ts): landed types. */
/* TS LocalEntityHost: mission_media non-NULL only for missionpack, and
 * media then points at mission_media->base. */
typedef struct qa_q3_pres_local_entity_host {
  qa_q3_base_product product;
  const qa_q3_pres_local_entity_media *media;
  const qa_q3_pres_mission_local_entity_media *mission_media;
  void *prediction_ctx;
  qa_q3_pres_movement_trace (*prediction_trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                                double skip_number, double mask);
  qa_q3_pres_collision_world collision; /* only point_contents is used */
  void *audio_ctx;
  void (*audio_start_sound)(void *ctx, qa_pcm_sound *sound, qa_start_sound_options options);
  double client_num;
  qa_q3_game_random *random;
  qa_q3_pres_impact_mark_system_view marks;
} qa_q3_pres_local_entity_host;
typedef struct qa_q3_pres_local_entity_frame { double time, frame_time; qa_vec3 view_origin; } qa_q3_pres_local_entity_frame;
typedef struct qa_q3_pres_local_entity_scene {
  qa_q3_pres_ref_entity *entities; size_t entity_count, entity_capacity;
  qa_q3_pres_dynamic_light *dynamic_lights; size_t light_count, light_capacity;
} qa_q3_pres_local_entity_scene;
void qa_q3_pres_local_entity_scene_destroy(qa_q3_pres_local_entity_scene *scene);
/* TS LocalEntitySceneSink. */
typedef struct qa_q3_pres_local_entity_scene_sink {
  void *ctx;
  void (*add_ref_entity)(void *ctx, const qa_q3_pres_ref_entity *entity);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
} qa_q3_pres_local_entity_scene_sink;
/* TS readCurrent closure passed to forEachOldestFirst visits. */
typedef qa_q3_pres_local_entity *(*qa_q3_pres_local_entity_read_current_fn)(void *ctx);
bool qa_q3_pres_local_entity_pool_create(qa_q3_pres_local_entity_pool **out, qa_q3_base_product product);
void qa_q3_pres_local_entity_pool_destroy(qa_q3_pres_local_entity_pool *pool);
qa_q3_base_product qa_q3_pres_local_entity_pool_product(const qa_q3_pres_local_entity_pool *pool);
double qa_q3_pres_local_entity_pool_active_count(const qa_q3_pres_local_entity_pool *pool);
void qa_q3_pres_local_entity_pool_initialize(qa_q3_pres_local_entity_pool *pool);
/* Caller frees the array of borrowed record pointers. False only OOM. */
bool qa_q3_pres_local_entity_pool_active_entities(qa_q3_pres_local_entity_pool *pool,
    qa_q3_pres_local_entity ***out, size_t *out_count);
/* Returned record is pool-owned; it expires at free/reset/eviction.
 * Missionpack-only types on baseq3 and ref-entity kind mismatches abort
 * with the exact TS error text. */
qa_q3_pres_local_entity *qa_q3_pres_local_entity_pool_allocate(qa_q3_pres_local_entity_pool *pool,
    qa_q3_pres_local_entity_type type, qa_q3_pres_ref_entity ref_entity);
bool qa_q3_pres_local_entity_pool_is_active(qa_q3_pres_local_entity_pool *pool, qa_q3_pres_local_entity *entity);
void qa_q3_pres_local_entity_pool_free(qa_q3_pres_local_entity_pool *pool, qa_q3_pres_local_entity *entity);
void qa_q3_pres_local_entity_pool_for_each_oldest_first(qa_q3_pres_local_entity_pool *pool,
    void (*visit)(void *ctx, qa_q3_pres_local_entity *entity,
                  qa_q3_pres_local_entity_read_current_fn read_current, void *read_current_ctx),
    void *visit_ctx);
typedef struct qa_q3_pres_local_entity_system {
  qa_q3_pres_client_effects_view effects;
  qa_q3_pres_local_entity_pool *pool;
  qa_q3_pres_local_entity_host host;
} qa_q3_pres_local_entity_system;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_local_entity_system_init(qa_q3_pres_local_entity_system *system,
    qa_q3_pres_client_effects_view effects, qa_q3_pres_local_entity_host host);
/* False only on OOM growing the diagnostic scene. */
bool qa_q3_pres_local_entity_system_collect_entities(qa_q3_pres_local_entity_system *system,
    qa_q3_pres_local_entity_frame frame, qa_q3_pres_local_entity_scene *out);
void qa_q3_pres_local_entity_system_add_entities(qa_q3_pres_local_entity_system *system,
    qa_q3_pres_local_entity_frame frame, qa_q3_pres_local_entity_scene_sink scene);
#endif

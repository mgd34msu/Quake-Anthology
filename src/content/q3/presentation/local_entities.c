/* Port of src/content/q3/presentation/local-entities.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static double f32(double value) { return (double)(float)value; }
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = qa_js_to_uint32(truncated);
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
static int32_t imul(double a, double b) {
  uint32_t product = (uint32_t)to_int32(a) * (uint32_t)to_int32(b);
  int32_t result;
  memcpy(&result, &product, sizeof result);
  return result;
}

const char *qa_q3_pres_local_entity_type_name(qa_q3_pres_local_entity_type type) {
  switch (type) {
    case QA_Q3_PRES_LE_MOVE_SCALE_FADE: return "move-scale-fade";
    case QA_Q3_PRES_LE_FALL_SCALE_FADE: return "fall-scale-fade";
    case QA_Q3_PRES_LE_SCALE_FADE: return "scale-fade";
    case QA_Q3_PRES_LE_SCORE_PLUM: return "score-plum";
    case QA_Q3_PRES_LE_SPRITE_EXPLOSION: return "sprite-explosion";
    case QA_Q3_PRES_LE_FRAGMENT: return "fragment";
    case QA_Q3_PRES_LE_KAMIKAZE: return "kamikaze";
    case QA_Q3_PRES_LE_INVUL_IMPACT: return "invul-impact";
    case QA_Q3_PRES_LE_INVUL_JUICED: return "invul-juiced";
    case QA_Q3_PRES_LE_FADE_RGB: return "fade-rgb";
    case QA_Q3_PRES_LE_EXPLOSION: return "explosion";
    case QA_Q3_PRES_LE_MARK: return "mark";
    case QA_Q3_PRES_LE_SHOW_REF_ENTITY: return "show-ref-entity";
  }
  return "mark";
}

typedef struct le_slot {
  int32_t prev, next;
  qa_q3_pres_local_entity entity;
  bool active;
} le_slot;
struct qa_q3_pres_local_entity_pool {
  qa_q3_base_product product;
  le_slot slots[QA_Q3_PRES_MAX_LOCAL_ENTITIES];
  int32_t head, tail, free_head;
  double count;
};
typedef qa_q3_pres_local_entity_pool pool_t;

static le_slot *slot_at(pool_t *pool, int32_t index) {
  if (index < 0 || index >= QA_Q3_PRES_MAX_LOCAL_ENTITIES) {
    qa_range_error("Invalid local entity slot %lld", (long long)index);
  }
  return &pool->slots[index];
}
static const le_slot *slot_at_const(const pool_t *pool, int32_t index) {
  if (index < 0 || index >= QA_Q3_PRES_MAX_LOCAL_ENTITIES) {
    qa_range_error("Invalid local entity slot %lld", (long long)index);
  }
  return &pool->slots[index];
}
static int32_t slot_index_of(pool_t *pool, qa_q3_pres_local_entity *entity) {
  uintptr_t base = (uintptr_t)&pool->slots[0].entity;
  uintptr_t at = (uintptr_t)entity;
  if (at < base) return -1;
  uintptr_t offset = at - base;
  if (offset % sizeof(le_slot) != 0) return -1;
  uintptr_t index = offset / sizeof(le_slot);
  if (index >= QA_Q3_PRES_MAX_LOCAL_ENTITIES) return -1;
  return (int32_t)index;
}
static void free_slot(pool_t *pool, int32_t index) {
  le_slot *slot = slot_at(pool, index);
  if (!slot->active) qa_range_error("CG_FreeLocalEntity: not active");
  if (slot->prev == -1) pool->head = slot->next; else slot_at(pool, slot->prev)->next = slot->next;
  if (slot->next == -1) pool->tail = slot->prev; else slot_at(pool, slot->next)->prev = slot->prev;
  slot->active = false;
  slot->next = pool->free_head;
  pool->free_head = index;
  pool->count--;
}

bool qa_q3_pres_local_entity_pool_create(pool_t **out, qa_q3_base_product product) {
  pool_t *pool = calloc(1, sizeof *pool);
  if (pool == NULL) return false;
  pool->product = product;
  qa_q3_pres_local_entity_pool_initialize(pool);
  *out = pool;
  return true;
}
void qa_q3_pres_local_entity_pool_destroy(pool_t *pool) { free(pool); }
qa_q3_base_product qa_q3_pres_local_entity_pool_product(const pool_t *pool) { return pool->product; }
double qa_q3_pres_local_entity_pool_active_count(const pool_t *pool) { return pool->count; }
void qa_q3_pres_local_entity_pool_initialize(pool_t *pool) {
  pool->head = -1;
  pool->tail = -1;
  pool->free_head = 0;
  pool->count = 0;
  for (int32_t index = 0; index < QA_Q3_PRES_MAX_LOCAL_ENTITIES; index++) {
    le_slot *slot = &pool->slots[index];
    slot->prev = -1;
    slot->next = index + 1 == QA_Q3_PRES_MAX_LOCAL_ENTITIES ? -1 : index + 1;
    slot->active = false;
    memset(&slot->entity, 0, sizeof slot->entity);
  }
}
bool qa_q3_pres_local_entity_pool_active_entities(pool_t *pool, qa_q3_pres_local_entity ***out,
    size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (pool->count == 0) return true;
  qa_q3_pres_local_entity **entities = malloc((size_t)pool->count * sizeof *entities);
  if (entities == NULL) return false;
  size_t count = 0;
  for (int32_t index = pool->head; index != -1; index = slot_at_const(pool, index)->next) {
    entities[count++] = &pool->slots[index].entity;
  }
  *out = entities;
  *out_count = count;
  return true;
}
static bool is_sprite_type(qa_q3_pres_local_entity_type type) {
  return type == QA_Q3_PRES_LE_MOVE_SCALE_FADE || type == QA_Q3_PRES_LE_FALL_SCALE_FADE ||
         type == QA_Q3_PRES_LE_SCALE_FADE || type == QA_Q3_PRES_LE_SCORE_PLUM ||
         type == QA_Q3_PRES_LE_SPRITE_EXPLOSION;
}
static bool is_model_type(qa_q3_pres_local_entity_type type) {
  return type == QA_Q3_PRES_LE_FRAGMENT || type == QA_Q3_PRES_LE_KAMIKAZE ||
         type == QA_Q3_PRES_LE_INVUL_IMPACT || type == QA_Q3_PRES_LE_INVUL_JUICED;
}
qa_q3_pres_local_entity *qa_q3_pres_local_entity_pool_allocate(pool_t *pool,
    qa_q3_pres_local_entity_type type, qa_q3_pres_ref_entity ref_entity) {
  const char *name = qa_q3_pres_local_entity_type_name(type);
  if (pool->product == QA_Q3_BASE_PRODUCT_BASEQ3 &&
      (type == QA_Q3_PRES_LE_KAMIKAZE || type == QA_Q3_PRES_LE_INVUL_IMPACT ||
       type == QA_Q3_PRES_LE_INVUL_JUICED || type == QA_Q3_PRES_LE_SHOW_REF_ENTITY)) {
    qa_range_error("%s requires missionpack local entities", name);
  }
  if (is_sprite_type(type) && ref_entity.kind != QA_Q3_PRES_REF_SPRITE) {
    qa_range_error("%s requires a sprite", name);
  }
  if (is_model_type(type) && ref_entity.kind != QA_Q3_PRES_REF_MODEL) {
    qa_range_error("%s requires a model", name);
  }
  if ((type == QA_Q3_PRES_LE_FADE_RGB || type == QA_Q3_PRES_LE_EXPLOSION) &&
      ref_entity.kind == QA_Q3_PRES_REF_PORTAL) {
    qa_range_error("%s requires a shaded entity", name);
  }
  if (pool->free_head == -1) free_slot(pool, pool->tail);
  int32_t index = pool->free_head;
  le_slot *slot = slot_at(pool, index);
  pool->free_head = slot->next;
  memset(&slot->entity, 0, sizeof slot->entity);
  slot->entity.le_type = type;
  slot->entity.ref_entity = ref_entity;
  slot->active = true;
  slot->prev = -1;
  slot->next = pool->head;
  if (pool->head != -1) slot_at(pool, pool->head)->prev = index; else pool->tail = index;
  pool->head = index;
  pool->count++;
  return &slot->entity;
}
bool qa_q3_pres_local_entity_pool_is_active(pool_t *pool, qa_q3_pres_local_entity *entity) {
  int32_t index = slot_index_of(pool, entity);
  return index != -1 && pool->slots[index].active && &pool->slots[index].entity == entity;
}
void qa_q3_pres_local_entity_pool_free(pool_t *pool, qa_q3_pres_local_entity *entity) {
  int32_t index = slot_index_of(pool, entity);
  if (index == -1 || !qa_q3_pres_local_entity_pool_is_active(pool, entity)) {
    qa_range_error("CG_FreeLocalEntity: not active");
  }
  free_slot(pool, index);
}
typedef struct read_current_ctx { pool_t *pool; int32_t index; } read_current_ctx;
static qa_q3_pres_local_entity *read_current(void *ctx) {
  const read_current_ctx *current = ctx;
  return &((pool_t *)current->pool)->slots[current->index].entity;
}
void qa_q3_pres_local_entity_pool_for_each_oldest_first(pool_t *pool,
    void (*visit)(void *ctx, qa_q3_pres_local_entity *entity,
                  qa_q3_pres_local_entity_read_current_fn read_current_fn, void *read_current_ctx),
    void *visit_ctx) {
  for (int32_t index = pool->tail; index != -1;) {
    int32_t current = index;
    int32_t next = slot_at(pool, current)->prev;
    read_current_ctx context = {pool, current};
    visit(visit_ctx, &pool->slots[current].entity, read_current, &context);
    index = next;
  }
}

static double byte_of(double value) { return qa_qvm_float_to_int(value) & 255; }
static double remaining(const qa_q3_pres_local_entity *entity, double time) {
  return f32(f32((double)to_int32(entity->end_time - time)) * entity->life_rate);
}
static qa_axis scaled_axis(qa_axis axis, double scale) {
  qa_axis result = {{qa_scale3(axis.vectors[0], scale), qa_scale3(axis.vectors[1], scale),
                     qa_scale3(axis.vectors[2], scale)}};
  return result;
}
static qa_vec4 rgba(qa_vec4 color, double scale) {
  return qa_math_vec4(byte_of(f32(color.x * scale)), byte_of(f32(color.y * scale)),
                      byte_of(f32(color.z * scale)), byte_of(f32(color.w * scale)));
}
static qa_q3_pres_shading *shading_of(qa_q3_pres_ref_entity *entity) {
  switch (entity->kind) {
    case QA_Q3_PRES_REF_MODEL: return &entity->as.model.shading;
    case QA_Q3_PRES_REF_SPRITE: return &entity->as.sprite.shading;
    case QA_Q3_PRES_REF_BEAM: return &entity->as.beam.shading;
    default: return &entity->as.rail.shading;
  }
}
static qa_vec3 origin_of(const qa_q3_pres_ref_entity *entity) {
  switch (entity->kind) {
    case QA_Q3_PRES_REF_MODEL: return entity->as.model.origin;
    case QA_Q3_PRES_REF_SPRITE: return entity->as.sprite.origin;
    case QA_Q3_PRES_REF_BEAM: return entity->as.beam.origin;
    case QA_Q3_PRES_REF_PORTAL: return entity->as.portal.origin;
    default: return entity->as.rail.origin;
  }
}

typedef qa_q3_pres_local_entity_system system_t;
typedef struct visit_state {
  system_t *system;
  qa_q3_pres_local_entity_frame frame;
  qa_q3_pres_local_entity_scene_sink scene;
} visit_state;

static void add_light(visit_state *visit, qa_vec3 origin, double radius, qa_vec3 color) {
  qa_q3_pres_dynamic_light light = {origin, radius, color, false};
  visit->scene.add_light(visit->scene.ctx, light);
}
static void explosion_light(system_t *system, qa_q3_pres_local_entity *entity, double time, visit_state *visit) {
  (void)system;
  if (!entity->light) return;
  double light = f32(f32((double)to_int32(time - entity->start_time)) /
                     f32((double)to_int32(entity->end_time - entity->start_time)));
  light = light < 0.5 ? 1 : f32(1 - f32(f32(light - 0.5) * 2));
  add_light(visit, origin_of(&entity->ref_entity), f32(entity->light * light), entity->light_color);
}

static void scale_fade(system_t *system, qa_q3_pres_local_entity *entity, visit_state *visit) {
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  qa_q3_pres_ref_sprite_entity *re = &entity->ref_entity.as.sprite;
  double c = remaining(entity, frame->time);
  if (entity->le_type == QA_Q3_PRES_LE_MOVE_SCALE_FADE && entity->fade_in_time > entity->start_time &&
      frame->time < entity->fade_in_time) {
    c = f32(1 - f32(f32((double)to_int32(entity->fade_in_time - frame->time)) /
                    f32((double)to_int32(entity->fade_in_time - entity->start_time))));
  }
  re->shading.shader_rgba.w = byte_of(f32(f32(255 * c) * entity->color.w));
  if (entity->le_type != QA_Q3_PRES_LE_MOVE_SCALE_FADE ||
      !(to_int32(entity->le_flags) & QA_Q3_PRES_LEF_PUFF_DONT_SCALE)) {
    re->radius = f32(f32(entity->radius * f32(1 - c)) +
                     (entity->le_type == QA_Q3_PRES_LE_FALL_SCALE_FADE ? 16 : 8));
  }
  if (entity->le_type == QA_Q3_PRES_LE_MOVE_SCALE_FADE) {
    re->origin = qa_q3_base_evaluate_trajectory(entity->pos, frame->time);
  } else if (entity->le_type == QA_Q3_PRES_LE_FALL_SCALE_FADE) {
    re->origin.z = f32(entity->pos.base.z - f32(f32(1 - c) * entity->pos.delta.z));
  }
  if (qa_length3(qa_sub3(re->origin, frame->view_origin)) < entity->radius) {
    qa_q3_pres_local_entity_pool_free(system->pool, entity);
    return;
  }
  visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
}

static void sprite_explosion(system_t *system, qa_q3_pres_local_entity *entity, visit_state *visit) {
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  double raw = f32(f32((double)to_int32(entity->end_time - frame->time)) /
                   f32((double)to_int32(entity->end_time - entity->start_time)));
  double c = raw < 1 ? raw : 1;
  qa_q3_pres_ref_entity re = entity->ref_entity;
  re.as.sprite.shading.shader_rgba =
      qa_math_vec4(255, 255, 255, byte_of(f32(f32(255 * c) * f32(0.33))));
  re.as.sprite.radius = f32(f32(42 * f32(1 - c)) + 30);
  visit->scene.add_ref_entity(visit->scene.ctx, &re);
  explosion_light(system, entity, frame->time, visit);
}

static void blood_trail(system_t *system, qa_q3_pres_local_entity_read_current_fn read_current_fn,
    void *read_current_ctx, qa_q3_pres_local_entity_frame frame) {
  const int32_t step = 150;
  int32_t start = imul(step, trunc((double)to_int32(frame.time - frame.frame_time + step) / step));
  int32_t end = imul(step, trunc(frame.time / step));
  for (int32_t time = start; time <= end; time = to_int32((double)time + step)) {
    /* A full pool can recycle this very slot while spawning a puff. */
    qa_vec3 origin = qa_q3_base_evaluate_trajectory(read_current_fn(read_current_ctx)->pos, time);
    qa_q3_pres_smoke_puff_options options;
    memset(&options, 0, sizeof options);
    options.origin = origin;
    options.radius = 20;
    options.color = qa_math_vec4(1, 1, 1, 1);
    options.duration = 2000;
    options.start_time = time;
    options.shader = system->host.media->blood_trail_shader;
    qa_q3_pres_local_entity *blood = system->effects.smoke_puff(system->effects.ctx, options);
    blood->le_type = QA_Q3_PRES_LE_FALL_SCALE_FADE;
    blood->pos.delta.z = 40;
  }
}

static void bounce_mark(system_t *system, qa_q3_pres_local_entity *entity, qa_vec3 origin, qa_vec3 normal) {
  if (entity->le_mark_type != QA_Q3_PRES_LE_MARK_NONE) {
    bool blood = entity->le_mark_type == QA_Q3_PRES_LE_MARK_BLOOD;
    double radius = (blood ? 16 : 8) + (qa_game_random_rand(system->host.random) & (blood ? 31 : 15));
    qa_q3_pres_impact_mark_request request;
    memset(&request, 0, sizeof request);
    request.shader = blood ? system->host.media->blood_mark_shader : system->host.media->burn_mark_shader;
    request.origin = origin;
    request.direction = normal;
    request.orientation = f32((double)qa_game_random_random(system->host.random) * 360);
    request.color = qa_math_vec4(1, 1, 1, 1);
    request.alpha_fade = true;
    request.radius = radius;
    request.temporary = false;
    qa_q3_pres_ref_poly *polys = NULL;
    size_t poly_count = 0;
    if (system->host.marks.impact_mark(system->host.marks.ctx, request, &polys, &poly_count)) {
      for (size_t i = 0; i < poly_count; i++) qa_q3_pres_ref_poly_destroy(&polys[i]);
      free(polys);
    }
  }
  entity->le_mark_type = QA_Q3_PRES_LE_MARK_NONE;
}
static void bounce_sound(system_t *system, qa_q3_pres_local_entity *entity, qa_vec3 origin) {
  if (entity->le_bounce_sound_type == QA_Q3_PRES_LE_SOUND_BLOOD &&
      (qa_game_random_rand(system->host.random) & 1)) {
    int32_t value = qa_game_random_rand(system->host.random) & 3;
    qa_pcm_sound *const *sounds = system->host.media->gib_bounce_sounds;
    qa_start_sound_options options;
    memset(&options, 0, sizeof options);
    options.entity = QA_Q3_BASE_ENTITYNUM_WORLD;
    options.channel = 0;
    options.origin.kind = QA_VOICE_ORIGIN_FIXED;
    options.origin.position = origin;
    options.volume = 127;
    system->host.audio_start_sound(system->host.audio_ctx,
        value == 0 ? sounds[0] : value == 1 ? sounds[1] : sounds[2], options);
  }
  entity->le_bounce_sound_type = QA_Q3_PRES_LE_SOUND_NONE;
}
static void reflect_velocity(qa_q3_pres_local_entity *entity, const qa_q3_trace_result *trace,
    qa_vec3 normal, qa_q3_pres_local_entity_frame frame) {
  int32_t hit_time = qa_qvm_float_to_int(f32(f32((double)to_int32(frame.time - frame.frame_time)) +
                                             f32(f32(frame.frame_time) * trace->fraction)));
  qa_vec3 velocity = qa_q3_base_evaluate_trajectory_delta(entity->pos, hit_time);
  double dot = qa_dot3(velocity, normal);
  qa_vec3 delta = qa_scale3(qa_add3(velocity, qa_scale3(normal, f32(-2 * dot))), entity->bounce_factor);
  bool stationary = strcmp(trace->solidity, "all-solid") == 0 ||
      (normal.z > 0 && (delta.z < 40 || delta.z < f32(f32(-frame.frame_time) * delta.z)));
  entity->pos.base = trace->end;
  entity->pos.time = frame.time;
  entity->pos.delta = delta;
  if (stationary) entity->pos.type = QA_Q3_BASE_TR_STATIONARY;
}

static void fragment(system_t *system, qa_q3_pres_local_entity *entity,
    qa_q3_pres_local_entity_read_current_fn read_current_fn, void *read_current_ctx, visit_state *visit) {
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  qa_q3_pres_ref_model_entity *re = &entity->ref_entity.as.model;
  if (entity->pos.type == QA_Q3_BASE_TR_STATIONARY) {
    int32_t t = to_int32(entity->end_time - frame->time);
    if (t < 1000) {
      re->lighting_origin = re->origin;
      re->shading.render_flags = to_int32(re->shading.render_flags) | QA_Q3_PRES_RF_LIGHTING_ORIGIN;
      qa_vec3 origin = re->origin;
      re->origin.z = f32(origin.z - f32(16 * f32(1 - f32(f32((double)t) / 1000))));
      visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
      re->origin = origin;
    } else {
      visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
    }
    return;
  }
  qa_vec3 new_origin = qa_q3_base_evaluate_trajectory(entity->pos, frame->time);
  qa_bounds zero_bounds = {{0, 0, 0}, {0, 0, 0}};
  qa_q3_pres_movement_trace trace = system->host.prediction_trace(system->host.prediction_ctx,
      re->origin, new_origin, zero_bounds, -1, 1);
  if (trace.result.fraction == 1) {
    re->origin = new_origin;
    if (to_int32(entity->le_flags) & QA_Q3_PRES_LEF_TUMBLE) {
      re->axis = qa_qvm_angles_to_axis(qa_q3_base_evaluate_trajectory(entity->angles, frame->time));
    }
    visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
    if (entity->le_bounce_sound_type == QA_Q3_PRES_LE_SOUND_BLOOD) {
      blood_trail(system, read_current_fn, read_current_ctx, *frame);
    }
    return;
  }
  double contents = system->host.collision.point_contents(system->host.collision.ctx, trace.result.end, 0);
  if ((to_int32(contents) & INT32_MIN) != 0) {
    qa_q3_pres_local_entity_pool_free(system->pool, entity);
    return;
  }
  /* CM allsolid leaves the zero-initialized source plane untouched. */
  qa_vec3 normal = trace.result.has_plane ? trace.result.plane.normal : qa_math_vec3(0, 0, 0);
  if (!trace.result.has_plane && strcmp(trace.result.solidity, "all-solid") != 0) {
    qa_range_error("Fragment impact has no trace plane");
  }
  bounce_mark(system, entity, trace.result.end, normal);
  bounce_sound(system, entity, trace.result.end);
  reflect_velocity(entity, &trace.result, normal, *frame);
  visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
}

static void score_plum(system_t *system, qa_q3_pres_local_entity *entity, visit_state *visit) {
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  qa_q3_pres_ref_sprite_entity *re = &entity->ref_entity.as.sprite;
  double c = remaining(entity, frame->time);
  int64_t score = qa_qvm_float_to_int(entity->radius);
  qa_vec4 color = score < 0 ? qa_math_vec4(255, 17, 17, 255)
      : score >= 50 ? qa_math_vec4(255, 0, 255, 255)
      : score >= 20 ? qa_math_vec4(0, 0, 255, 255)
      : score >= 10 ? qa_math_vec4(255, 255, 0, 255)
      : score >= 2 ? qa_math_vec4(0, 255, 0, 255) : qa_math_vec4(255, 255, 255, 255);
  if (c < 0.25) color.w = byte_of(f32(f32(255 * 4) * c));
  re->shading.shader_rgba = color;
  re->radius = 4;
  qa_vec3 origin = entity->pos.base;
  origin.z = f32(entity->pos.base.z + f32(110 - f32(c * 100)));
  qa_vec3 direction = qa_normalize3(qa_cross3(qa_sub3(frame->view_origin, origin), qa_math_vec3(0, 0, 1)));
  double phase = f32(f32(c * 2) * f32(acos(-1.0)));
  origin = qa_add3(origin, qa_scale3(direction, f32(-10 + f32(20 * f32(sin(phase))))));
  if (qa_length3(qa_sub3(origin, frame->view_origin)) < 20) {
    qa_q3_pres_local_entity_pool_free(system->pool, entity);
    return;
  }
  bool negative = score < 0;
  if (negative) score = -score;
  int64_t digits[24];
  size_t digit_count = 0;
  do {
    digits[digit_count++] = score % 10;
    score /= 10;
  } while (score != 0);
  if (negative) digits[digit_count++] = 10;
  for (size_t index = 0; index < digit_count; index++) {
    re->origin = qa_add3(origin, qa_scale3(direction,
        f32(f32(f32((double)digit_count / 2) - (double)index) * 8)));
    int64_t digit = digits[digit_count - 1 - index];
    re->shading.custom_shader = system->host.media->number_shaders[digit];
    visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
  }
}

static void shockwave(system_t *system, qa_q3_pres_local_entity *entity, qa_axis axis, double time,
    double start, double end, double fade, double radius, visit_state *visit) {
  if (system->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_range_error("Shockwave requires missionpack media");
  }
  qa_q3_pres_ref_model_entity model =
      qa_q3_pres_create_model_entity(system->host.mission_media->kamikaze_shock_wave);
  model.shading.shader_time = entity->ref_entity.as.model.shading.shader_time;
  model.origin = entity->ref_entity.as.model.origin;
  double c = f32(f32(time - start) / f32(end - start));
  model.axis = scaled_axis(axis, f32(f32(c * radius) / 88));
  model.non_normalized_axes = true;
  double alpha = time > fade ? f32(f32(time - fade) / f32(end - fade)) : 0;
  double channel = byte_of(f32(255 - f32(alpha * 255)));
  model.shading.shader_rgba = qa_math_vec4(channel, channel, channel, channel);
  qa_q3_pres_ref_entity tagged = qa_q3_pres_ref_entity_from_model(model);
  visit->scene.add_ref_entity(visit->scene.ctx, &tagged);
}
static void kamikaze(system_t *system, qa_q3_pres_local_entity *entity, visit_state *visit) {
  if (system->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_range_error("Kamikaze requires missionpack media");
  }
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  int32_t t = to_int32(frame->time - entity->start_time);
  qa_q3_pres_ref_model_entity *re = &entity->ref_entity.as.model;
  qa_axis axis = qa_qvm_angles_to_axis(qa_math_vec3(0, 0, 0));
  if (t > 0 && t < 2000) {
    if (!(to_int32(entity->le_flags) & QA_Q3_PRES_LEF_SOUND1)) {
      qa_start_sound_options options;
      memset(&options, 0, sizeof options);
      options.entity = system->host.client_num;
      options.channel = 0;
      options.origin.kind = QA_VOICE_ORIGIN_LOCAL;
      options.volume = 127;
      system->host.audio_start_sound(system->host.audio_ctx,
          system->host.mission_media->kamikaze_explode_sound, options);
      entity->le_flags = to_int32(entity->le_flags) | QA_Q3_PRES_LEF_SOUND1;
    }
    shockwave(system, entity, axis, t, 0, 2000, 1500, 1320, visit);
  }
  if (t > 250 && t < 2250) {
    re->shading.shader_rgba = rgba(entity->color, f32(remaining(entity, frame->time) * 255));
    double c;
    if (t < 2000) {
      c = f32(f32((double)t - 250) / 1750);
    } else {
      if (!(to_int32(entity->le_flags) & QA_Q3_PRES_LEF_SOUND2)) {
        qa_start_sound_options options;
        memset(&options, 0, sizeof options);
        options.entity = system->host.client_num;
        options.channel = 0;
        options.origin.kind = QA_VOICE_ORIGIN_LOCAL;
        options.volume = 127;
        system->host.audio_start_sound(system->host.audio_ctx,
            system->host.mission_media->kamikaze_implode_sound, options);
        entity->le_flags = to_int32(entity->le_flags) | QA_Q3_PRES_LEF_SOUND2;
      }
      c = f32(f32(2250 - (double)t) / 250);
    }
    re->axis = scaled_axis(axis, f32(f32(c * 720) / 72));
    re->non_normalized_axes = true;
    visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
    add_light(visit, re->origin, f32(c * 1000), qa_math_vec3(1, 1, c));
  }
  if (t > 2000 && t < 3000) {
    qa_vec3 angles = entity->angles.base;
    if (angles.x == 0 && angles.y == 0 && angles.z == 0) {
      double x2 = f32((double)qa_game_random_random(system->host.random) * 360);
      double y2 = f32((double)qa_game_random_random(system->host.random) * 360);
      double z2 = f32((double)qa_game_random_random(system->host.random) * 360);
      entity->angles.base = qa_math_vec3(
          x2, y2, z2);
    }
    shockwave(system, entity, qa_qvm_angles_to_axis(entity->angles.base), t, 2000, 3000, 2500, 704, visit);
  }
}
static void invulnerability_juiced(system_t *system, qa_q3_pres_local_entity *entity, visit_state *visit) {
  int32_t t = to_int32(visit->frame.time - entity->start_time);
  qa_q3_pres_ref_model_entity *re = &entity->ref_entity.as.model;
  if (t > 3000) {
    double xy = f32(1 + f32(f32(f32(0.3) * f32((double)t - 3000)) / 2000));
    double z = f32(f32(0.7) + f32(f32(f32(0.3) * f32(2000 - ((double)t - 3000))) / 2000));
    re->axis.vectors[0].x = xy;
    re->axis.vectors[1].y = xy;
    re->axis.vectors[2].z = z;
  }
  if (t > 5000) {
    entity->end_time = 0;
    system->effects.gib_player(system->effects.ctx, re->origin);
  } else {
    visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
  }
}

static void visit_entity(void *ctx, qa_q3_pres_local_entity *entity,
    qa_q3_pres_local_entity_read_current_fn read_current_fn, void *read_current_ctx) {
  visit_state *visit = ctx;
  system_t *system = visit->system;
  qa_q3_pres_local_entity_frame *frame = &visit->frame;
  if (frame->time >= entity->end_time) {
    qa_q3_pres_local_entity_pool_free(system->pool, entity);
    return;
  }
  switch (entity->le_type) {
    case QA_Q3_PRES_LE_MARK: break;
    case QA_Q3_PRES_LE_FRAGMENT: fragment(system, entity, read_current_fn, read_current_ctx, visit); break;
    case QA_Q3_PRES_LE_MOVE_SCALE_FADE:
    case QA_Q3_PRES_LE_FALL_SCALE_FADE:
    case QA_Q3_PRES_LE_SCALE_FADE: scale_fade(system, entity, visit); break;
    case QA_Q3_PRES_LE_FADE_RGB:
      shading_of(&entity->ref_entity)->shader_rgba =
          rgba(entity->color, f32(remaining(entity, frame->time) * 255));
      visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
      break;
    case QA_Q3_PRES_LE_EXPLOSION:
      visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
      explosion_light(system, entity, frame->time, visit);
      break;
    case QA_Q3_PRES_LE_SPRITE_EXPLOSION: sprite_explosion(system, entity, visit); break;
    case QA_Q3_PRES_LE_SCORE_PLUM: score_plum(system, entity, visit); break;
    case QA_Q3_PRES_LE_KAMIKAZE: kamikaze(system, entity, visit); break;
    case QA_Q3_PRES_LE_INVUL_IMPACT:
    case QA_Q3_PRES_LE_SHOW_REF_ENTITY:
      visit->scene.add_ref_entity(visit->scene.ctx, &entity->ref_entity);
      break;
    case QA_Q3_PRES_LE_INVUL_JUICED: invulnerability_juiced(system, entity, visit); break;
  }
}

void qa_q3_pres_local_entity_system_init(system_t *system, qa_q3_pres_client_effects_view effects,
    qa_q3_pres_local_entity_host host) {
  system->effects = effects;
  system->pool = effects.pool;
  system->host = host;
  if (qa_q3_pres_local_entity_pool_product(system->pool) != host.product) {
    qa_range_error("Local entity media product differs from its pool");
  }
}
void qa_q3_pres_local_entity_system_add_entities(system_t *system, qa_q3_pres_local_entity_frame frame,
    qa_q3_pres_local_entity_scene_sink scene) {
  visit_state visit = {system, frame, scene};
  qa_q3_pres_local_entity_pool_for_each_oldest_first(system->pool, visit_entity, &visit);
}
typedef struct collect_state { qa_q3_pres_local_entity_scene *scene; bool oom; } collect_state;
static void collect_ref_entity(void *ctx, const qa_q3_pres_ref_entity *entity) {
  collect_state *collect = ctx;
  qa_q3_pres_local_entity_scene *scene = collect->scene;
  if (scene->entity_count == scene->entity_capacity) {
    size_t capacity = scene->entity_capacity == 0 ? 16 : scene->entity_capacity * 2;
    qa_q3_pres_ref_entity *grown = realloc(scene->entities, capacity * sizeof *grown);
    if (grown == NULL) { collect->oom = true; return; }
    scene->entities = grown;
    scene->entity_capacity = capacity;
  }
  scene->entities[scene->entity_count++] = qa_q3_pres_copy_ref_entity(entity);
}
static void collect_light(void *ctx, qa_q3_pres_dynamic_light light) {
  collect_state *collect = ctx;
  qa_q3_pres_local_entity_scene *scene = collect->scene;
  if (scene->light_count == scene->light_capacity) {
    size_t capacity = scene->light_capacity == 0 ? 16 : scene->light_capacity * 2;
    qa_q3_pres_dynamic_light *grown = realloc(scene->dynamic_lights, capacity * sizeof *grown);
    if (grown == NULL) { collect->oom = true; return; }
    scene->dynamic_lights = grown;
    scene->light_capacity = capacity;
  }
  scene->dynamic_lights[scene->light_count++] = light;
}
bool qa_q3_pres_local_entity_system_collect_entities(system_t *system, qa_q3_pres_local_entity_frame frame,
    qa_q3_pres_local_entity_scene *out) {
  memset(out, 0, sizeof *out);
  collect_state collect = {out, false};
  qa_q3_pres_local_entity_scene_sink sink = {&collect, collect_ref_entity, collect_light};
  qa_q3_pres_local_entity_system_add_entities(system, frame, sink);
  return !collect.oom;
}
void qa_q3_pres_local_entity_scene_destroy(qa_q3_pres_local_entity_scene *scene) {
  free(scene->entities);
  free(scene->dynamic_lights);
  memset(scene, 0, sizeof *scene);
}

/* Port of src/render/scene/particles/q3-system.ts. GPL-2.0-or-later. */
#include "qa/render/scene/particles/q3_system.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/core/common_parse.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

#define ENTITYNUM_WORLD 1022

enum particle_type {
  PT_NONE, PT_WEATHER, PT_FLAT, PT_SMOKE, PT_ROTATE, PT_WEATHER_TURBULENT,
  PT_ANIMATED, PT_BAT, PT_BLEED, PT_FLAT_SCALE_UP, PT_FLAT_SCALE_UP_FADE,
  PT_WEATHER_FLURRY, PT_SMOKE_IMPACT, PT_BUBBLE, PT_BUBBLE_TURBULENT, PT_SPRITE
};
enum particle_color { PC_WHITE = 0, PC_BLOOD = 2, PC_EMISSIVE_FADE = 3, PC_GREY75 = 4 };

typedef struct particle {
  long next;
  double time, end_time;
  qa_vec3 origin, velocity, acceleration;
  int color;
  double color_velocity, alpha, alpha_velocity;
  int type;
  const qa_particle_shader *shader;
  double height, width, end_height, end_width;
  double start, end, start_fade;
  bool rotate;
  double snum;
  bool link;
  double shader_animation;
  double roll;
  int64_t accumulated_roll;
} particle;

typedef struct registered_animation {
  const char *name;
  double count, aspect_ratio;
  const qa_particle_shader **frames; /* owned array */
} registered_animation;

static const char *const ANIMATION_NAMES[6] = {
    "explode1", "blacksmokeanim", "twiltb2", "expblue", "blacksmokeanimb", "blood"};
static const double ANIMATION_COUNTS[6] = {23, 25, 45, 25, 23, 5};

static double f32(double v) { return (double)(float)v; }
static double f_add(double a, double b) { return f32(f32(a) + f32(b)); }
static double f_sub(double a, double b) { return f32(f32(a) - f32(b)); }
static double f_mul(double a, double b) { return f32(f32(a) * f32(b)); }
static double f_div(double a, double b) { return f32(f32(a) / f32(b)); }
static qa_vec3 ma(qa_vec3 origin, double scale, qa_vec3 direction) {
  return (qa_vec3){f_add(origin.x, f_mul(scale, direction.x)),
                   f_add(origin.y, f_mul(scale, direction.y)),
                   f_add(origin.z, f_mul(scale, direction.z))};
}
static double integer(double value, const char *label) {
  if (trunc(value) != value || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("%s must be int32", label);
  return value;
}
static double state_time(const qa_particle_system *s) { return *s->state.time; }
static registered_animation *animations_of(qa_particle_system *s) {
  return s->animations;
}
static particle *particles_of(qa_particle_system *s) { return s->particles; }

void qa_particle_animation_frames_destroy(qa_particle_animation_frames *frames) {
  free((void *)frames->frames);
  memset(frames, 0, sizeof *frames);
}
void qa_particle_animations_destroy(qa_particle_animations *a) {
  qa_particle_animation_frames_destroy(&a->explode1);
}
void qa_standalone_particle_animations_destroy(
    qa_standalone_particle_animations *a) {
  qa_particle_animation_frames_destroy(&a->explode1);
  qa_particle_animation_frames_destroy(&a->blacksmokeanim);
  qa_particle_animation_frames_destroy(&a->twiltb2);
  qa_particle_animation_frames_destroy(&a->expblue);
  qa_particle_animation_frames_destroy(&a->blacksmokeanimb);
  qa_particle_animation_frames_destroy(&a->blood);
}
void qa_ref_polys_destroy(qa_ref_poly *polys, size_t count) {
  for (size_t i = 0; i < count; i++) free(polys[i].vertices);
  free(polys);
}

static bool load_frames(const qa_particle_resources *resources, const char *name,
                        qa_particle_animation_frames *out) {
  double count = 0;
  for (int i = 0; i < 6; i++)
    if (strcmp(ANIMATION_NAMES[i], name) == 0) count = ANIMATION_COUNTS[i];
  out->frames = calloc((size_t)count, sizeof *out->frames);
  if (out->frames == NULL) return false;
  out->count = (size_t)count;
  char buffer[128];
  for (double frame = 1; frame <= count; frame++) {
    (void)snprintf(buffer, sizeof buffer, "%s%lld", name, (long long)frame);
    out->frames[(size_t)frame - 1] =
        resources->register_shader(resources->ctx, buffer);
  }
  return true;
}

bool qa_load_particle_animations(const qa_particle_resources *resources,
                                 const char *source,
                                 qa_particle_animations *out) {
  (void)source; /* cg_marks.c overload */
  memset(out, 0, sizeof *out);
  return load_frames(resources, "explode1", &out->explode1);
}
bool qa_load_standalone_particle_animations(
    const qa_particle_resources *resources,
    qa_standalone_particle_animations *out) {
  memset(out, 0, sizeof *out);
  return load_frames(resources, "explode1", &out->explode1) &&
         load_frames(resources, "blacksmokeanim", &out->blacksmokeanim) &&
         load_frames(resources, "twiltb2", &out->twiltb2) &&
         load_frames(resources, "expblue", &out->expblue) &&
         load_frames(resources, "blacksmokeanimb", &out->blacksmokeanimb) &&
         load_frames(resources, "blood", &out->blood);
}

static registered_animation make_registered(const char *name,
                                            qa_particle_animation_frames frames,
                                            double aspect_ratio) {
  double count = 0;
  for (int i = 0; i < 6; i++)
    if (strcmp(ANIMATION_NAMES[i], name) == 0) count = ANIMATION_COUNTS[i];
  if ((double)frames.count != count)
    qa_range_error("%s requires its %g registered source frames", name, count);
  registered_animation out = {name, count, aspect_ratio, NULL};
  out.frames = calloc(frames.count ? frames.count : 1, sizeof *out.frames);
  if (out.frames != NULL)
    memcpy((void *)out.frames, frames.frames, frames.count * sizeof *out.frames);
  return out;
}

static particle zero_particle(long next) {
  particle p = {0};
  p.next = next;
  p.type = PT_NONE;
  p.color = PC_WHITE;
  return p;
}

static void reset_pool(qa_particle_system *s) {
  particle *particles = particles_of(s);
  size_t capacity = (size_t)s->capacity;
  for (size_t i = 0; i < capacity; i++)
    particles[i] = zero_particle(i + 1 == capacity ? -1 : (long)i + 1);
  s->active = -1;
  s->free_list = 0;
  s->count = 0;
  s->old_time = f32(state_time(s));
}

void qa_particle_system_init(qa_particle_system *system,
                             qa_particle_client_state state,
                             qa_particle_host host, const char *source) {
  memset(system, 0, sizeof *system);
  system->state = state;
  system->host = host;
  bool standalone = source != NULL && strcmp(source, "cg_particles.c") == 0;
  system->capacity = standalone ? 8192 : QA_MAX_PARTICLES;
  system->explosion_alpha = standalone ? 1 : 0.5;
  registered_animation *animations;
  if (standalone) {
    animations = calloc(6, sizeof *animations);
    qa_particle_animation_frames rows[6] = {
        host.standalone_animations.explode1, host.standalone_animations.blacksmokeanim,
        host.standalone_animations.twiltb2, host.standalone_animations.expblue,
        host.standalone_animations.blacksmokeanimb, host.standalone_animations.blood};
    double aspects[6] = {f32(1.405), 1, 1, 1, 1, 1};
    if (animations != NULL)
      for (int i = 0; i < 6; i++)
        animations[i] = make_registered(ANIMATION_NAMES[i], rows[i], aspects[i]);
    system->animation_count = 6;
  } else {
    animations = calloc(1, sizeof *animations);
    if (animations != NULL)
      animations[0] = make_registered("explode1", host.animations.explode1, 1);
    system->animation_count = 1;
  }
  system->animations = animations;
  system->particles = calloc((size_t)system->capacity, sizeof(particle));
  reset_pool(system);
}

void qa_particle_system_destroy(qa_particle_system *system) {
  registered_animation *animations = animations_of(system);
  for (size_t i = 0; i < system->animation_count; i++)
    free((void *)animations[i].frames);
  free(animations);
  free(system->particles);
  memset(system, 0, sizeof *system);
}

double qa_particle_system_active_count(const qa_particle_system *system) {
  return system->count;
}
void qa_particle_system_reset_round(qa_particle_system *system) {
  reset_pool(system);
}
void qa_particle_system_clear(qa_particle_system *system,
                              const qa_particle_resources *resources) {
  qa_particle_system_reset_round(system);
  registered_animation *animations = animations_of(system);
  char buffer[128];
  for (size_t a = 0; a < system->animation_count; a++)
    for (double frame = 0; frame < animations[a].count; frame++) {
      (void)snprintf(buffer, sizeof buffer, "%s%lld", animations[a].name,
                     (long long)frame + 1);
      animations[a].frames[(size_t)frame] =
          resources->register_shader(resources->ctx, buffer);
    }
}

static particle *at_particle(qa_particle_system *s, long index) {
  if (index < 0 || (size_t)index >= (size_t)s->capacity)
    qa_range_error("invalid particle slot %ld", index);
  return &particles_of(s)[index];
}
static particle *allocate(qa_particle_system *s) {
  if (s->free_list == -1) return NULL;
  long index = s->free_list;
  particle *p = at_particle(s, index);
  s->free_list = p->next;
  p->next = s->active;
  s->active = index;
  s->count++;
  p->time = f32(state_time(s));
  return p;
}
static void release(qa_particle_system *s, long index, particle *p) {
  p->next = s->free_list;
  s->free_list = index;
  p->type = PT_NONE;
  p->color = PC_WHITE;
  p->alpha = 0;
  s->count--;
}
static void warn_shader(qa_particle_system *s, const qa_particle_shader *shader,
                        const char *name) {
  if (shader == NULL) {
    char buffer[256];
    (void)snprintf(buffer, sizeof buffer, "%s pshader == ZERO!\n", name);
    s->host.print(s->host.print_ctx, buffer);
  }
}
static double after(qa_particle_system *s, double duration) {
  return f32((double)qa_int32(state_time(s) + integer(duration, "particle duration")));
}
static qa_game_random *rng(qa_particle_system *s) { return s->host.random; }

void qa_particle_system_explosion(qa_particle_system *s,
                                  qa_particle_explosion_request request) {
  registered_animation *animations = animations_of(s);
  char lowered[128];
  size_t li = 0;
  for (; request.animation[li] != '\0' && li + 1 < sizeof lowered; li++) {
    char c = request.animation[li];
    lowered[li] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  lowered[li] = '\0';
  long animation_index = -1;
  for (size_t i = 0; i < s->animation_count; i++)
    if (strcmp(animations[i].name, lowered) == 0) { animation_index = (long)i; break; }
  if (animation_index < 0) {
    char buffer[512];
    (void)snprintf(buffer, sizeof buffer,
                   "CG_ParticleExplosion: unknown animation string: %s\n",
                   request.animation);
    qa_common_error(QA_COMMON_ERROR_DROP, buffer);
  }
  double duration = integer(request.duration, "particle duration");
  (void)integer(request.size_start, "particle start size");
  (void)integer(request.size_end, "particle end size");
  particle *p = allocate(s);
  if (p == NULL) return;
  registered_animation *animation = &animations[animation_index];
  p->alpha = s->explosion_alpha;
  p->alpha_velocity = 0;
  if (duration < 0) {
    duration = (double)qa_int32(-duration);
    p->roll = 0;
  } else {
    p->roll = qa_qvm_float_to_int(f_mul(qa_game_random_crandom(rng(s)), 179));
  }
  p->shader_animation = (double)animation_index;
  p->width = f32(request.size_start);
  p->height = f_mul(request.size_start, animation->aspect_ratio);
  p->end_height = f32(request.size_end);
  p->end_width = f_mul(request.size_end, animation->aspect_ratio);
  p->end_time = after(s, duration);
  p->type = PT_ANIMATED;
  p->origin = qa_math_vec3(request.origin.x, request.origin.y, request.origin.z);
  p->velocity = qa_math_vec3(request.velocity.x, request.velocity.y, request.velocity.z);
  p->acceleration = qa_math_vec3(0, 0, 0);
}

void qa_particle_system_snow_flurry(qa_particle_system *s,
                                    const qa_particle_shader *shader,
                                    qa_particle_client_entity entity) {
  warn_shader(s, shader, "CG_ParticleSnowFlurry");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->color = PC_WHITE;
  p->alpha = f32(0.9);
  p->alpha_velocity = 0;
  p->start = entity.origin2.x;
  p->end = entity.origin2.y;
  p->end_time = after(s, entity.time);
  p->start_fade = after(s, entity.time2);
  p->shader = shader;
  if ((int64_t)qa_game_random_rand(rng(s)) % 100 > 90) {
    p->height = 32; p->width = 32; p->alpha = f32(0.1);
  } else { p->height = 1; p->width = 1; }
  p->type = PT_WEATHER_FLURRY;
  p->origin = entity.origin;
  p->velocity = (qa_vec3){
      f_add(f_mul(entity.angles.x, 32), f_mul(qa_game_random_crandom(rng(s)), 16)),
      f_add(f_mul(entity.angles.y, 32), f_mul(qa_game_random_crandom(rng(s)), 16)),
      f_add(-10, entity.angles.z)};
  /* vec3() stages each component */
  p->velocity = qa_math_vec3(p->velocity.x, p->velocity.y, p->velocity.z);
  p->acceleration = qa_math_vec3(f_mul(qa_game_random_crandom(rng(s)), 16),
                                 f_mul(qa_game_random_crandom(rng(s)), 16), 0);
}

void qa_particle_system_snow(qa_particle_system *s,
                             const qa_particle_shader *shader, qa_vec3 origin,
                             qa_vec3 end, bool turbulent, double range,
                             double snum) {
  if (!(origin.z > end.z))
    qa_range_error("snow start must exceed end to keep the source wrap loop finite");
  warn_shader(s, shader, "CG_ParticleSnow");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->color = PC_WHITE;
  p->alpha = f32(0.4);
  p->alpha_velocity = 0;
  p->start = origin.z;
  p->end = end.z;
  p->shader = shader;
  p->height = 1; p->width = 1;
  p->type = turbulent ? PT_WEATHER_TURBULENT : PT_WEATHER;
  p->origin = qa_math_vec3(
      f_add(origin.x, f_mul(qa_game_random_crandom(rng(s)), range)),
      f_add(origin.y, f_mul(qa_game_random_crandom(rng(s)), range)),
      f_add(origin.z, f_mul(qa_game_random_crandom(rng(s)), f_sub(p->start, p->end))));
  p->velocity = qa_math_vec3(
      turbulent ? f_mul(qa_game_random_crandom(rng(s)), 16) : 0,
      turbulent ? f_mul(qa_game_random_crandom(rng(s)), 16) : 0,
      turbulent ? f_mul(-50, 1.3) : -50);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->snum = snum;
  p->link = true;
}

void qa_particle_system_bubble(qa_particle_system *s,
                               const qa_particle_shader *shader,
                               qa_vec3 origin, qa_vec3 end, bool turbulent,
                               double range, double snum) {
  warn_shader(s, shader, "CG_ParticleSnow");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->color = PC_WHITE;
  p->alpha = f32(0.4);
  p->alpha_velocity = 0;
  p->start = origin.z;
  p->end = end.z;
  p->shader = shader;
  p->height = p->width = f_add(1, f_mul(qa_game_random_crandom(rng(s)), 0.5));
  double velocity_z = f_add(50, f_mul(qa_game_random_crandom(rng(s)), 10));
  if (turbulent) velocity_z = f_mul(50, 1.3);
  p->type = turbulent ? PT_BUBBLE_TURBULENT : PT_BUBBLE;
  p->origin = qa_math_vec3(
      f_add(origin.x, f_mul(qa_game_random_crandom(rng(s)), range)),
      f_add(origin.y, f_mul(qa_game_random_crandom(rng(s)), range)),
      f_add(origin.z, f_mul(qa_game_random_crandom(rng(s)), f_sub(p->start, p->end))));
  p->velocity = qa_math_vec3(
      turbulent ? f_mul(qa_game_random_crandom(rng(s)), 4) : 0,
      turbulent ? f_mul(qa_game_random_crandom(rng(s)), 4) : 0, velocity_z);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->snum = snum;
  p->link = true;
}

void qa_particle_system_smoke(qa_particle_system *s,
                              const qa_particle_shader *shader,
                              qa_particle_client_entity entity) {
  if (shader == NULL) s->host.print(s->host.print_ctx, "CG_ParticleSmoke == ZERO!\n");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->end_time = after(s, entity.time);
  p->start_fade = after(s, entity.time2);
  p->color = PC_WHITE;
  p->alpha = 1;
  p->alpha_velocity = 0;
  p->start = entity.origin.z;
  p->end = entity.origin2.z;
  p->shader = shader;
  p->rotate = false;
  p->height = 8; p->width = 8; p->end_height = 32; p->end_width = 32;
  p->type = PT_SMOKE;
  p->origin = entity.origin;
  p->velocity = qa_math_vec3(0, 0, entity.frame == 1 ? -5 : 5);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->roll = qa_qvm_float_to_int(f_add(8, f_mul(qa_game_random_crandom(rng(s)), 4)));
}

void qa_particle_system_bullet_debris(qa_particle_system *s,
                                      qa_vec3 origin, qa_vec3 velocity,
                                      double duration) {
  particle *p = allocate(s);
  if (p == NULL) return;
  p->end_time = after(s, duration);
  p->start_fade = after(s, trunc(duration / 2));
  p->color = PC_EMISSIVE_FADE;
  p->alpha = 1;
  p->alpha_velocity = 0;
  p->height = p->width = p->end_height = p->end_width = 0.5;
  p->shader = s->host.media.tracer_shader;
  p->type = PT_SMOKE;
  p->origin = origin;
  p->velocity = qa_math_vec3(velocity.x, velocity.y, f_add(velocity.z, -20));
  p->acceleration = qa_math_vec3(0, 0, -60);
}

void qa_particle_system_add_particle_shrapnel(qa_particle_system *s) {
  (void)s;
}

static char *next_token(qa_tokenizer *tokens) {
  qa_text_token token = {0};
  if (!qa_tokenizer_next(tokens, false, &token)) return NULL;
  return token.value;
}

bool qa_particle_system_new_particle_area(qa_particle_system *s, double index) {
  const char *text = s->host.config_string(s->host.config_ctx, index);
  if (text == NULL || text[0] == '\0') return false;
  char name[128];
  (void)snprintf(name, sizeof name, "particle configstring %lld", (long long)index);
  qa_tokenizer tokens;
  if (!qa_tokenizer_init(&tokens, text, strlen(text), name)) return false;
  char *raw[10] = {0};
  for (int i = 0; i < 10; i++) raw[i] = next_token(&tokens);
  qa_tokenizer_destroy(&tokens);
  int32_t type = qa_game_atoi(raw[0] != NULL ? raw[0] : "");
  double range = type == 0 ? 256 : type == 1 ? 128 : (type == 2 || type == 7) ? 64
                 : (type == 3 || type == 6) ? 32 : type == 4 ? 8 : type == 5 ? 16 : 0;
  qa_vec3 origin = qa_math_vec3(qa_game_atof(raw[1] != NULL ? raw[1] : ""),
                                qa_game_atof(raw[2] != NULL ? raw[2] : ""),
                                qa_game_atof(raw[3] != NULL ? raw[3] : ""));
  qa_vec3 end = qa_math_vec3(qa_game_atof(raw[4] != NULL ? raw[4] : ""),
                             qa_game_atof(raw[5] != NULL ? raw[5] : ""),
                             qa_game_atof(raw[6] != NULL ? raw[6] : ""));
  int32_t count = qa_game_atoi(raw[7] != NULL ? raw[7] : "");
  bool turbulent = qa_game_atoi(raw[8] != NULL ? raw[8] : "") != 0;
  int32_t snum = qa_game_atoi(raw[9] != NULL ? raw[9] : "");
  for (int i = 0; i < 10; i++) free(raw[i]);
  for (int32_t item = 0; item < count; item++) {
    if (type >= 4)
      qa_particle_system_bubble(s, s->host.media.water_bubble_shader, origin, end, turbulent, range, snum);
    else
      qa_particle_system_snow(s, s->host.media.water_bubble_shader, origin, end, turbulent, range, snum);
  }
  return true;
}

void qa_particle_system_snow_link(qa_particle_system *s,
                                  qa_particle_client_entity entity,
                                  bool enabled) {
  for (long index = s->active; index != -1;) {
    particle *p = at_particle(s, index);
    index = p->next;
    if ((p->type == PT_WEATHER || p->type == PT_WEATHER_TURBULENT) &&
        p->snum == entity.frame)
      p->link = enabled;
  }
}

void qa_particle_system_impact_smoke_puff(qa_particle_system *s,
                                          const qa_particle_shader *shader,
                                          qa_vec3 origin) {
  warn_shader(s, shader, "CG_ParticleImpactSmokePuff");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->alpha = 0.25;
  p->alpha_velocity = 0;
  p->roll = qa_qvm_float_to_int(f_mul(qa_game_random_crandom(rng(s)), 179));
  p->shader = shader;
  p->end_time = after(s, 500);
  p->start_fade = after(s, 100);
  p->width = (double)((int64_t)qa_game_random_rand(rng(s)) % 4) + 8;
  p->height = (double)((int64_t)qa_game_random_rand(rng(s)) % 4) + 8;
  p->end_height = f_mul(p->height, 2);
  p->end_width = f_mul(p->width, 2);
  p->type = PT_SMOKE_IMPACT;
  p->origin = origin;
  p->velocity = qa_math_vec3(0, 0, 20);
  p->acceleration = qa_math_vec3(0, 0, 20);
  p->rotate = true;
}

void qa_particle_system_bleed(qa_particle_system *s,
                              const qa_particle_shader *shader, qa_vec3 start,
                              double flesh_entity_num, double duration) {
  warn_shader(s, shader, "CG_Particle_Bleed");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->alpha = 0.75;
  p->alpha_velocity = 0;
  p->shader = shader;
  p->end_time = after(s, duration);
  p->start_fade = after(s, flesh_entity_num != 0 ? 0 : 100);
  p->width = p->height = 4;
  p->end_height = 4 + (double)((int64_t)qa_game_random_rand(rng(s)) % 3);
  p->end_width = p->end_height;
  p->type = PT_SMOKE;
  p->origin = start;
  p->velocity = qa_math_vec3(0, 0, -20);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->rotate = false;
  p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
  p->color = PC_BLOOD;
}

void qa_particle_system_oil_particle(qa_particle_system *s,
                                     const qa_particle_shader *shader,
                                     qa_particle_client_entity entity) {
  double ratio = f_sub(1, f_div(state_time(s),
                              (double)qa_int32(state_time(s) + entity.time)));
  if (shader == NULL)
    s->host.print(s->host.print_ctx, "CG_Particle_OilParticle == ZERO!\n");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->alpha = 0.75;
  p->alpha_velocity = 0;
  p->shader = shader;
  p->end_time = f_add(state_time(s), 1500);
  p->start_fade = p->end_time;
  p->width = p->end_width = 1;
  p->height = p->end_height = 3;
  p->type = PT_SMOKE;
  p->origin = entity.origin;
  p->velocity = qa_math_vec3(f_mul(entity.origin2.x, f_mul(16, ratio)),
                             f_mul(entity.origin2.y, f_mul(16, ratio)),
                             entity.origin2.z);
  p->snum = 1;
  p->acceleration = qa_math_vec3(0, 0, -20);
  p->rotate = false;
  p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
}

void qa_particle_system_oil_slick(qa_particle_system *s,
                                  const qa_particle_shader *shader,
                                  qa_particle_client_entity entity) {
  if (shader == NULL)
    s->host.print(s->host.print_ctx, "CG_Particle_OilSlick == ZERO!\n");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->end_time = entity.angles2.z != 0 ? f_add(state_time(s), entity.angles2.z)
                                      : after(s, 60000);
  p->start_fade = p->end_time;
  p->alpha = 0.75;
  p->alpha_velocity = 0;
  p->shader = shader;
  bool sizes = entity.angles2.x != 0 || entity.angles2.y != 0;
  p->width = p->height = sizes ? entity.angles2.x : 8;
  p->end_height = p->end_width = sizes ? entity.angles2.y : 16;
  p->type = PT_FLAT_SCALE_UP;
  p->snum = 1;
  p->origin = qa_math_vec3(entity.origin.x, entity.origin.y,
                           f_add(entity.origin.z,
                                f_add(0.55, f_mul(qa_game_random_crandom(rng(s)), 0.5))));
  p->velocity = qa_math_vec3(0, 0, 0);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->rotate = false;
  p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
}

void qa_particle_system_oil_slick_remove(qa_particle_system *s) {
  for (long index = s->active; index != -1;) {
    particle *p = at_particle(s, index);
    index = p->next;
    if (p->type == PT_FLAT_SCALE_UP && p->snum == 1) {
      p->end_time = after(s, 100);
      p->start_fade = p->end_time;
      p->type = PT_FLAT_SCALE_UP_FADE;
    }
  }
}

bool qa_particle_system_valid_blood_pool(qa_particle_system *s, qa_vec3 start) {
  qa_angle_vectors axes = qa_qvm_angle_vectors(
      qa_vector_to_angles(qa_math_vec3(0, 0, 1)));
  qa_vec3 center = ma(start, 0.5, qa_math_vec3(0, 0, 1));
  qa_bounds zero_bounds = {{0, 0, 0}, {0, 0, 0}};
  for (double x = -8; x < 16; x += 16)
    for (double y = -8; y < 16; y += 16) {
      qa_vec3 point = ma(ma(center, x, axes.right), y, axes.up);
      qa_vec3 end = ma(point, -1, qa_math_vec3(0, 0, 1));
      qa_particle_trace trace = s->host.prediction.trace(
          s->host.prediction.ctx, point, end, zero_bounds, -1, 1);
      if (trace.entity_num < ENTITYNUM_WORLD ||
          strcmp(trace.solidity, "clear") != 0 || trace.fraction >= 1)
        return false;
    }
  return true;
}

void qa_particle_system_blood_pool(qa_particle_system *s,
                                   const qa_particle_shader *shader,
                                   qa_vec3 trace_end) {
  warn_shader(s, shader, "CG_BloodPool");
  if (s->free_list == -1 || !qa_particle_system_valid_blood_pool(s, trace_end))
    return;
  particle *p = allocate(s);
  if (p == NULL) return;
  p->end_time = after(s, 3000);
  p->start_fade = p->end_time;
  p->alpha = 0.75;
  p->alpha_velocity = 0;
  p->shader = shader;
  double size = f_add(0.4, f_mul(qa_game_random_random(rng(s)), 0.6));
  p->width = p->height = f_mul(8, size);
  p->end_height = p->end_width = f_mul(16, size);
  p->type = PT_FLAT_SCALE_UP;
  p->origin = trace_end;
  p->velocity = qa_math_vec3(0, 0, 0);
  p->acceleration = qa_math_vec3(0, 0, 0);
  p->rotate = false;
  p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
  p->color = PC_BLOOD;
}

void qa_particle_system_blood_cloud(qa_particle_system *s,
                                    qa_vec3 origin, qa_vec3 direction) {
  double count = fmax(1, f_div(qa_length3(direction), 32));
  for (double index = 0; index < count; index++) {
    particle *p = allocate(s);
    if (p == NULL) return;
    p->alpha = 0.75;
    p->alpha_velocity = 0;
    p->shader = s->host.media.smoke_puff_shader;
    p->end_time = f_add((double)qa_int32(state_time(s) + 350),
                       f_mul(qa_game_random_crandom(rng(s)), 100));
    p->start_fade = f32(state_time(s));
    p->width = p->height = p->end_height = p->end_width = 32;
    p->type = PT_SMOKE;
    p->origin = origin;
    p->velocity = qa_math_vec3(0, 0, -1);
    p->acceleration = qa_math_vec3(0, 0, 0);
    p->rotate = false;
    p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
    p->color = PC_BLOOD;
  }
}

void qa_particle_system_sparks(qa_particle_system *s, qa_vec3 origin,
                               qa_vec3 velocity, double duration, double x,
                               double y, double speed) {
  particle *p = allocate(s);
  if (p == NULL) return;
  p->end_time = after(s, duration);
  p->start_fade = after(s, trunc(duration / 2));
  p->color = PC_EMISSIVE_FADE;
  p->alpha = f32(0.4);
  p->alpha_velocity = 0;
  p->height = p->width = p->end_height = p->end_width = 0.5;
  p->shader = s->host.media.tracer_shader;
  p->type = PT_SMOKE;
  p->origin = qa_math_vec3(f_add(origin.x, f_mul(qa_game_random_crandom(rng(s)), x)),
                           f_add(origin.y, f_mul(qa_game_random_crandom(rng(s)), y)),
                           origin.z);
  p->velocity = qa_math_vec3(
      f_add(velocity.x, f_mul(qa_game_random_crandom(rng(s)), 4)),
      f_add(velocity.y, f_mul(qa_game_random_crandom(rng(s)), 4)),
      f_add(velocity.z, f_mul(f_add(20, f_mul(qa_game_random_crandom(rng(s)), 10)), speed)));
  p->acceleration = qa_math_vec3(f_mul(qa_game_random_crandom(rng(s)), 4),
                                 f_mul(qa_game_random_crandom(rng(s)), 4), 0);
}

qa_vec3 qa_particle_system_dust(qa_particle_system *s, qa_vec3 origin,
                                qa_vec3 direction) {
  qa_vec3 negated = qa_math_vec3(-direction.x, -direction.y, -direction.z);
  double length = qa_length3(negated);
  qa_vec3 forward = qa_qvm_angle_vectors(qa_vector_to_angles(negated)).forward;
  double count = fmax(1, f_div(length, 32));
  qa_vec3 point = origin;
  for (double index = 0; index < count; index++) {
    point = ma(point, 32, forward);
    particle *p = allocate(s);
    if (p == NULL) break;
    p->alpha = 0.75;
    p->alpha_velocity = 0;
    p->shader = s->host.media.smoke_puff_shader;
    p->end_time = f_add((double)qa_int32(state_time(s) + (length != 0 ? 4500 : 750)),
                       f_mul(qa_game_random_crandom(rng(s)), length != 0 ? 3500 : 500));
    p->start_fade = f32(state_time(s));
    p->width = p->height = length != 0 ? 32 : f_mul(32, 0.2);
    p->end_height = p->end_width = length != 0 ? 96 : 16;
    p->type = PT_SMOKE;
    p->origin = point;
    p->velocity = qa_math_vec3(f_mul(qa_game_random_crandom(rng(s)), 6),
                               f_mul(qa_game_random_crandom(rng(s)), 6),
                               f_mul(qa_game_random_random(rng(s)), 20));
    (void)qa_game_random_crandom(rng(s));
    (void)qa_game_random_crandom(rng(s));
    p->acceleration = qa_math_vec3(0, 0, 0);
    p->rotate = false;
    p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
  }
  return negated;
}

void qa_particle_system_misc(qa_particle_system *s,
                             const qa_particle_shader *shader, qa_vec3 origin,
                             double size, double duration, double alpha) {
  (void)alpha;
  warn_shader(s, shader, "CG_ParticleImpactSmokePuff");
  particle *p = allocate(s);
  if (p == NULL) return;
  p->alpha = 1;
  p->alpha_velocity = 0;
  p->roll = (double)((int64_t)qa_game_random_rand(rng(s)) % 179);
  p->shader = shader;
  p->end_time = duration > 0 ? after(s, duration) : f32(duration);
  p->start_fade = f32(state_time(s));
  p->width = p->height = p->end_height = p->end_width = f32(size);
  p->type = PT_SPRITE;
  p->origin = origin;
  p->rotate = false;
}

typedef struct poly_list {
  qa_ref_poly *polys; size_t count, capacity;
} poly_list;
static void push_poly(poly_list *list, const qa_particle_shader *shader,
                      const qa_ref_poly_vertex *vertices, size_t count) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    list->polys = realloc(list->polys, list->capacity * sizeof *list->polys);
    if (list->polys == NULL) return;
  }
  qa_ref_poly *poly = &list->polys[list->count];
  poly->shader = shader;
  poly->vertices = calloc(count ? count : 1, sizeof *poly->vertices);
  if (poly->vertices == NULL) { poly->vertex_count = 0; return; }
  memcpy(poly->vertices, vertices, count * sizeof *vertices);
  poly->vertex_count = count;
  list->count++;
}

static double distance_to(qa_particle_system *s, qa_vec3 origin,
                          const qa_vec3 *view_origin) {
  if (view_origin != NULL) return qa_length3(qa_sub3(*view_origin, origin));
  if (s->state.snap_origin == NULL)
    qa_range_error("particle distance culling requires the current cgame snapshot");
  return qa_length3(qa_sub3(*s->state.snap_origin, origin));
}

static void rolled_axes(qa_particle_system *s, double roll, qa_vec3 *right,
                        qa_vec3 *up) {
  if (roll == 0) {
    *right = s->view_axes.vectors[1];
    *up = s->view_axes.vectors[2];
    return;
  }
  qa_vec3 angles = qa_vector_to_angles(s->state.view_axis->vectors[0]);
  qa_angle_vectors axes = qa_qvm_angle_vectors(
      qa_math_vec3(angles.x, angles.y, f_add(angles.z, roll)));
  *right = axes.right;
  *up = axes.up;
}

static qa_ref_poly_vertex make_vertex(qa_vec3 position, double st_s, double st_t,
                                      qa_vec4 color) {
  return (qa_ref_poly_vertex){position, qa_math_vec2(st_s, st_t), color};
}
static double to_byte(double value) {
  return (double)(qa_qvm_float_to_int(f_mul(255, value)) & 255);
}
static qa_vec3 scene_point(qa_vec3 origin, double height, double width,
                           qa_vec3 right, qa_vec3 up) {
  return ma(ma(origin, height, up), width, right);
}
static void quad_vertices(qa_vec3 origin, double width, double height,
                          qa_vec3 right, qa_vec3 up, qa_vec4 color,
                          qa_ref_poly_vertex out[4]) {
  out[0] = make_vertex(scene_point(origin, -height, -width, right, up), 0, 0, color);
  out[1] = make_vertex(scene_point(origin, -height, width, right, up), 0, 1, color);
  out[2] = make_vertex(scene_point(origin, height, width, right, up), 1, 1, color);
  out[3] = make_vertex(scene_point(origin, height, -width, right, up), 1, 0, color);
}

static void add_to_scene(qa_particle_system *s, particle *p, qa_vec3 origin,
                         poly_list *output, const qa_vec3 *view_origin) {
  const qa_vec4 white = {255, 255, 255, 255};
  qa_ref_poly_vertex vertices[4];
  size_t vertex_count = 0;
  double ratio = f_div(f_sub(state_time(s), p->time), f_sub(p->end_time, p->time));
  switch (p->type) {
    case PT_WEATHER: case PT_WEATHER_TURBULENT: case PT_WEATHER_FLURRY:
    case PT_BUBBLE: case PT_BUBBLE_TURBULENT: {
      bool bubble = p->type == PT_BUBBLE || p->type == PT_BUBBLE_TURBULENT;
      if (p->type != PT_WEATHER_FLURRY) {
        if (bubble && origin.z > p->end) {
          p->time = f32(state_time(s));
          p->origin = qa_math_vec3(origin.x, origin.y,
                                   f_add(p->start, f_mul(qa_game_random_crandom(rng(s)), 4)));
          if (p->type == PT_BUBBLE_TURBULENT)
            p->velocity = qa_math_vec3(f_mul(qa_game_random_crandom(rng(s)), 4),
                                       f_mul(qa_game_random_crandom(rng(s)), 4),
                                       p->velocity.z);
        } else if (!bubble && origin.z < p->end) {
          p->time = f32(state_time(s));
          double z = origin.z;
          while (z < p->end) {
            double next = f_add(z, f_sub(p->start, p->end));
            if (!(next > z))
              qa_range_error("snow wrap cannot advance at source float32 precision");
            z = next;
          }
          p->origin = qa_math_vec3(origin.x, origin.y, z);
          if (p->type == PT_WEATHER_TURBULENT)
            p->velocity = qa_math_vec3(f_mul(qa_game_random_crandom(rng(s)), 16),
                                       f_mul(qa_game_random_crandom(rng(s)), 16),
                                       p->velocity.z);
        }
        if (!p->link) return;
        p->alpha = 1;
      }
      if (distance_to(s, origin, view_origin) > 1024) return;
      qa_vec4 color = {255, 255, 255, to_byte(p->alpha)};
      qa_vec3 right = s->view_axes.vectors[1], up = s->view_axes.vectors[2];
      if (bubble) {
        quad_vertices(origin, p->width, p->height, right, up, color, vertices);
        vertex_count = 4;
      } else {
        vertices[0] = make_vertex(scene_point(origin, -p->height, -p->width, right, up), 1, 0, color);
        vertices[1] = make_vertex(scene_point(origin, p->height, -p->width, right, up), 0, 0, color);
        vertices[2] = make_vertex(scene_point(origin, p->height, p->width, right, up), 0, 1, color);
        vertex_count = 3;
      }
      break;
    }
    case PT_SPRITE: {
      double width = f_add(p->width, f_mul(ratio, f_sub(p->end_width, p->width)));
      double height = f_add(p->height, f_mul(ratio, f_sub(p->end_height, p->height)));
      qa_vec3 right, up;
      rolled_axes(s, p->roll, &right, &up);
      qa_vec3 a = scene_point(origin, -height, -width, right, up);
      qa_vec3 b = ma(a, f_mul(2, height), up);
      qa_vec3 c = ma(b, f_mul(2, width), right);
      qa_vec3 d = ma(c, f_mul(-2, height), up);
      vertices[0] = make_vertex(a, 0, 0, white);
      vertices[1] = make_vertex(b, 0, 1, white);
      vertices[2] = make_vertex(c, 1, 1, white);
      vertices[3] = make_vertex(d, 1, 0, white);
      vertex_count = 4;
      break;
    }
    case PT_SMOKE: case PT_SMOKE_IMPACT: {
      if (p->type == PT_SMOKE_IMPACT && distance_to(s, origin, view_origin) > 1024)
        return;
      qa_vec3 color = {1, 1, 1};
      if (p->color == PC_BLOOD) color = (qa_vec3){0.22, 0, 0};
      else if (p->color == PC_GREY75) {
        double d = distance_to(s, origin, view_origin);
        double grey = fmin(f_mul(0.25, f_div(4096, d == 0 ? 1 : d)), 0.5);
        color = (qa_vec3){grey, grey, grey};
      }
      double inverse;
      if (f32(state_time(s)) > p->start_fade) {
        inverse = f_sub(1, f_div(f_sub(state_time(s), p->start_fade),
                               f_sub(p->end_time, p->start_fade)));
        if (p->color == PC_EMISSIVE_FADE) {
          double fade = fmax(0, f_mul(inverse, inverse));
          color = (qa_vec3){fade, fade, fade};
        }
        inverse = f_mul(inverse, p->alpha);
      } else inverse = p->alpha;
      bool rage = s->host.hardware_type != NULL &&
                  strcmp(s->host.hardware_type, "rage-pro") == 0;
      if (rage) inverse = 1;
      if (inverse > 1) inverse = 1;
      qa_vec3 right, up;
      if (p->type != PT_SMOKE_IMPACT) {
        qa_vec3 angles = qa_vector_to_angles(s->rotated_axes.vectors[0]);
        p->accumulated_roll = qa_int32((double)p->accumulated_roll + p->roll);
        qa_angle_vectors axes = qa_qvm_angle_vectors(qa_math_vec3(
            angles.x, angles.y,
            f_add(angles.z, f_mul((double)p->accumulated_roll, 0.1))));
        right = axes.right;
        up = axes.up;
      } else {
        right = s->rotated_axes.vectors[1];
        up = s->rotated_axes.vectors[2];
      }
      double width = p->rotate
                         ? f_add(p->width, f_mul(ratio, f_sub(p->end_width, p->width)))
                         : p->width;
      double height = p->rotate
                          ? f_add(p->height, f_mul(ratio, f_sub(p->end_height, p->height)))
                          : p->height;
      quad_vertices(origin, width, height,
                    p->rotate ? right : s->view_axes.vectors[1],
                    p->rotate ? up : s->view_axes.vectors[2],
                    (qa_vec4){to_byte(color.x), to_byte(color.y), to_byte(color.z),
                              to_byte(inverse)},
                    vertices);
      vertex_count = 4;
      break;
    }
    case PT_BLEED: {
      qa_vec3 right, up;
      rolled_axes(s, p->roll, &right, &up);
      bool rage = s->host.hardware_type != NULL &&
                  strcmp(s->host.hardware_type, "rage-pro") == 0;
      quad_vertices(origin, p->width, p->height, right, up,
                    (qa_vec4){111, 19, 9, to_byte(rage ? 1 : p->alpha)}, vertices);
      vertex_count = 4;
      break;
    }
    case PT_FLAT_SCALE_UP: {
      double width = fmin(f_add(p->width, f_mul(ratio, f_sub(p->end_width, p->width))), p->end_width);
      double height = fmin(f_add(p->height, f_mul(ratio, f_sub(p->end_height, p->height))), p->end_height);
      double radians = f_div(f_mul(p->roll, f32(acos(-1.0))), 180);
      double root2 = f32(sqrt(2));
      double sine = f_mul(f_mul(height, f32(sin(radians))), root2);
      double cosine = f_mul(f_mul(width, f32(cos(radians))), root2);
      double channel = p->color == PC_BLOOD ? 255 : to_byte(0.5);
      qa_vec4 color = {channel, channel, channel, 255};
      vertices[0] = make_vertex(qa_math_vec3(f_sub(origin.x, sine), f_sub(origin.y, cosine), origin.z), 0, 0, color);
      vertices[1] = make_vertex(qa_math_vec3(f_sub(origin.x, cosine), f_add(origin.y, sine), origin.z), 0, 1, color);
      vertices[2] = make_vertex(qa_math_vec3(f_add(origin.x, sine), f_add(origin.y, cosine), origin.z), 1, 1, color);
      vertices[3] = make_vertex(qa_math_vec3(f_add(origin.x, cosine), f_sub(origin.y, sine), origin.z), 1, 0, color);
      vertex_count = 4;
      break;
    }
    case PT_FLAT:
      vertices[0] = make_vertex(qa_math_vec3(f_sub(origin.x, p->height), f_sub(origin.y, p->width), origin.z), 0, 0, white);
      vertices[1] = make_vertex(qa_math_vec3(f_sub(origin.x, p->height), f_add(origin.y, p->width), origin.z), 0, 1, white);
      vertices[2] = make_vertex(qa_math_vec3(f_add(origin.x, p->height), f_add(origin.y, p->width), origin.z), 1, 1, white);
      vertices[3] = make_vertex(qa_math_vec3(f_add(origin.x, p->height), f_sub(origin.y, p->width), origin.z), 1, 0, white);
      vertex_count = 4;
      break;
    case PT_ANIMATED: {
      double amount = ratio;
      if (amount >= 1) amount = f32(0.9999);
      double width = f_add(p->width, f_mul(amount, f_sub(p->end_width, p->width)));
      double height = f_add(p->height, f_mul(amount, f_sub(p->end_height, p->height)));
      if (distance_to(s, origin, view_origin) < f_div(width, 1.5)) return;
      registered_animation *animations = animations_of(s);
      if (!(p->shader_animation >= 0) || trunc(p->shader_animation) != p->shader_animation ||
          (size_t)p->shader_animation >= s->animation_count)
        qa_range_error("invalid particle animation %g", p->shader_animation);
      registered_animation *animation = &animations[(size_t)p->shader_animation];
      int32_t frame = qa_qvm_float_to_int(floor(f_mul(amount, animation->count)));
      if (frame < 0 || (double)frame >= animation->count)
        qa_range_error("particle animation frame %d outside source range", (int)frame);
      p->shader = animation->frames[frame];
      qa_vec3 right, up;
      rolled_axes(s, p->roll, &right, &up);
      qa_vec3 a = scene_point(origin, -height, -width, right, up);
      qa_vec3 b = ma(a, f_mul(2, height), up);
      qa_vec3 c = ma(b, f_mul(2, width), right);
      qa_vec3 d = ma(c, f_mul(-2, height), up);
      vertices[0] = make_vertex(a, 0, 0, white);
      vertices[1] = make_vertex(b, 0, 1, white);
      vertices[2] = make_vertex(c, 1, 1, white);
      vertices[3] = make_vertex(d, 1, 0, white);
      vertex_count = 4;
      break;
    }
    case PT_NONE: case PT_ROTATE: case PT_BAT: case PT_FLAT_SCALE_UP_FADE:
      if (p->shader == NULL) return;
      qa_range_error("source particle type %d has no initialized polygon geometry", p->type);
    default:
      if (p->shader == NULL) return;
      qa_range_error("source particle type %d has no initialized polygon geometry", p->type);
  }
  if (p->shader != NULL) push_poly(output, p->shader, vertices, vertex_count);
}

qa_ref_poly *qa_particle_system_add_particles(qa_particle_system *s,
                                              const qa_vec3 *view_origin,
                                              size_t *out_count) {
  poly_list output = {0};
  s->view_axes = *s->state.view_axis;
  qa_vec3 angles = qa_vector_to_angles(s->view_axes.vectors[0]);
  s->view_roll = f_add(s->view_roll, f_mul(f_sub(state_time(s), s->old_time), 0.1));
  qa_angle_vectors rotated = qa_qvm_angle_vectors(qa_math_vec3(
      angles.x, angles.y, f_add(angles.z, f_mul(s->view_roll, 0.9))));
  s->rotated_axes = (qa_axis){{rotated.forward, rotated.right, rotated.up}};
  s->old_time = f32(state_time(s));
  long head = -1, tail = -1;
  for (long index = s->active; index != -1;) {
    long current = index;
    particle *p = at_particle(s, index);
    index = p->next;
    double elapsed = f_mul(f_sub(state_time(s), p->time), 0.001);
    double alpha = f_add(p->alpha, f_mul(elapsed, p->alpha_velocity));
    bool timed = p->type == PT_SMOKE || p->type == PT_ANIMATED ||
                 p->type == PT_BLEED || p->type == PT_SMOKE_IMPACT ||
                 p->type == PT_WEATHER_FLURRY || p->type == PT_FLAT_SCALE_UP_FADE;
    if (alpha <= 0 || (timed && f32(state_time(s)) > p->end_time)) {
      release(s, current, p);
      continue;
    }
    if ((p->type == PT_BAT || p->type == PT_SPRITE) && p->end_time < 0) {
      add_to_scene(s, p, p->origin, &output, view_origin);
      release(s, current, p);
      continue;
    }
    p->next = -1;
    if (tail == -1) head = current;
    else at_particle(s, tail)->next = current;
    tail = current;
    double squared = f_mul(elapsed, elapsed);
    qa_vec3 origin = qa_math_vec3(
        f_add(f_add(p->origin.x, f_mul(p->velocity.x, elapsed)), f_mul(p->acceleration.x, squared)),
        f_add(f_add(p->origin.y, f_mul(p->velocity.y, elapsed)), f_mul(p->acceleration.y, squared)),
        f_add(f_add(p->origin.z, f_mul(p->velocity.z, elapsed)), f_mul(p->acceleration.z, squared)));
    add_to_scene(s, p, origin, &output, view_origin);
  }
  s->active = head;
  *out_count = output.count;
  return output.polys;
}

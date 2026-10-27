/* Port of src/content/q2/base/monsters/common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_COMMON_H
#define QA_CONTENT_Q2_BASE_MONSTERS_COMMON_H
#include "qa/content/q2/foundation/monsters/types.h"

/* Mirrored unported surface (TS owner: src/content/q2/foundation/host.ts
 * Q2SpawnModule; the qa_q2f mirrors carry no spawn-module type). The TS
 * callbacks getter becomes a function rebuilt per call. */
typedef struct qa_q2bm_spawn_module {
  void *ctx;
  qa_q2f_callback_definitions (*callbacks)(void *ctx); /* NULL = TS undefined */
  bool (*spawn)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
} qa_q2bm_spawn_module;

/* Mirrored unported surface (TS owner: src/content/q2/foundation/host.ts
 * Q2PresentationEvent "monster-beam" and "print" variants; qa_q2f_
 * presentation events carry only the foundation slice's kinds). The host
 * wires a sink with qa_q2bm_set_event_sink; emitting without one aborts. */
typedef struct qa_q2bm_extended_event {
  const char *kind; /* "monster-beam" | "print" */
  qa_actor_id actor;
  const char *effect; qa_vec3 start, end; /* monster-beam */
  const char *level; const char *text;    /* print */
} qa_q2bm_extended_event;
typedef struct qa_q2bm_event_sink {
  void *ctx;
  void (*emit)(void *ctx, qa_q2bm_extended_event event);
} qa_q2bm_event_sink;
void qa_q2bm_set_event_sink(qa_q2bm_event_sink sink);
void qa_q2bm_emit_extended(qa_q2bm_extended_event event);

extern const qa_bounds qa_q2bm_humanoid_bounds;
/* TS move(name) closure: ctx is the borrowed move name. */
qa_q2m_handler qa_q2bm_move_handler(const char *name);
void qa_q2bm_move_handler_fn(void *ctx, qa_q2m_context *context);
/* TS sound(path, channel, attenuation) closure over a static spec. */
typedef struct qa_q2bm_sound_spec { const char *path; double channel, attenuation; } qa_q2bm_sound_spec;
qa_q2m_handler qa_q2bm_sound_handler(const qa_q2bm_sound_spec *spec);
void qa_q2bm_sound_handler_fn(void *ctx, qa_q2m_context *context);
bool qa_q2bm_alive_enemy(qa_q2m_context *context);
void qa_q2bm_damaged_skin(qa_q2m_context *context);
void qa_q2bm_muzzle(qa_q2m_context *context, double flash, qa_vec3 direction, qa_vec3 origin);
typedef struct qa_q2bm_shot_result { qa_vec3 start, direction; } qa_q2bm_shot_result;
/* False is TS null. */
bool qa_q2bm_shot(qa_q2m_context *context, double flash, double lead, qa_q2bm_shot_result *out);
bool qa_q2bm_shot_default(qa_q2m_context *context, double flash, qa_q2bm_shot_result *out);
qa_q2bm_shot_result qa_q2bm_forward_shot(qa_q2m_context *context, double flash);
bool qa_q2bm_standard_gib(qa_q2m_context *context, qa_death_reaction reaction, double bones, double meats,
                          const char *head, double attenuation);
bool qa_q2bm_standard_gib_default(qa_q2m_context *context, qa_death_reaction reaction);
void qa_q2bm_explode(qa_q2m_context *context, const char *path);
void qa_q2bm_loop_sound(qa_q2m_context *context, const char *path);
void qa_q2bm_begin_death(qa_q2m_context *context, qa_death_reaction reaction, const char *path,
                         const char *animation, double bones, double meats);
void qa_q2bm_begin_death_default(qa_q2m_context *context, qa_death_reaction reaction, const char *path,
                                 const char *animation);
void qa_q2bm_finish_corpse(qa_q2m_context *context);
void qa_q2bm_finish_corpse_bounds(qa_q2m_context *context, qa_bounds bounds);
/* qa_q2m_handler form of finishCorpse (default bounds). */
void qa_q2bm_finish_corpse_handler(void *ctx, qa_q2m_context *context);
#endif

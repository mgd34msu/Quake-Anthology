/* Port of src/content/q2/base/monsters/common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_COMMON_H
#define QA_CONTENT_Q2_BASE_MONSTERS_COMMON_H
#include "qa/content/q2/foundation/monsters/types.h"

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

/* Port of src/app/bootstrap/audio/q2-events.ts (Q2 cl_fx.c
 * CL_EntityEvent / CL_ParseMuzzleFlash sound calls).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_Q2_EVENTS_H
#define QA_APP_BOOTSTRAP_AUDIO_Q2_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS Q2EventSound. path is formatted inline. */
typedef struct qa_app_q2_event_sound {
  char path[64];
  double channel;
  double attenuation;
  double volume;
  double delay_seconds;
} qa_app_q2_event_sound;
/* TS random: () => number, consumed with & and % (integer source). */
typedef int64_t (*qa_app_q2_random_fn)(void *ctx);
/* TS q2EntitySound: false is the TS null. */
bool qa_app_q2_entity_sound(double event, qa_app_q2_random_fn random, void *random_ctx,
                            qa_app_q2_event_sound *out);
/* TS q2MuzzleSounds: writes at most 3, returns the count. */
size_t qa_app_q2_muzzle_sounds(double flash, bool silenced, qa_app_q2_random_fn random,
                               void *random_ctx, bool rerelease, qa_app_q2_event_sound out[3]);
/* TS q2MonsterMuzzleSounds: -1 is the TS null (uncovered flash),
 * 0 is the empty list, 1 writes *out. */
int qa_app_q2_monster_muzzle_sounds(double flash, qa_app_q2_random_fn random, void *random_ctx,
                                    bool rerelease, qa_app_q2_event_sound *out);
#endif

/*
 * Port of src/content/q1/addons/context.ts. GPL-2.0-or-later.
 * Q1AddonContext runs over the one Q1 game (qa_q1_services, TS
 * Q1EntityServices) and its Q1Base: addon files call the foundation and
 * base functions and the world tables on game->host directly.
 * qa_q1ad_services is TS Q1AddonServices (a host interface in TS too).
 * Lifetime: the context is registered with the game (state extension,
 * sourceItemStart/sourceBloodyNightmare, every named callback and spawn
 * handler the addon files register carry it as ctx), as TS closures keep
 * it alive. Destroy it after the last use of the game and before the
 * actor registry (game->host.actors) is destroyed.
 */
#ifndef QA_CONTENT_Q1_ADDONS_CONTEXT_H
#define QA_CONTENT_Q1_ADDONS_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/entity_services.h"

/* TS Q1AddonProgram. */
typedef enum qa_q1ad_program {
  QA_Q1AD_DOPA, QA_Q1AD_MG1, QA_Q1AD_MG3, QA_Q1AD_CTF
} qa_q1ad_program;
const char *qa_q1ad_program_name(qa_q1ad_program program);

/* TS Q1AddonEvent union on kind; unused fields zero. */
typedef enum qa_q1ad_event_kind {
  QA_Q1AD_EVENT_MUSIC, QA_Q1AD_EVENT_SELL_SCREEN, QA_Q1AD_EVENT_ALPHA,
  QA_Q1AD_EVENT_RUNE_COLLECTED, QA_Q1AD_EVENT_CUTSCENE, QA_Q1AD_EVENT_FOG,
  QA_Q1AD_EVENT_PUNCH_ANGLE, QA_Q1AD_EVENT_VIEW_ROLL, QA_Q1AD_EVENT_LIGHTNING,
  QA_Q1AD_EVENT_COLORED_EXPLOSION, QA_Q1AD_EVENT_MONSTER_COUNT,
  QA_Q1AD_EVENT_DEVELOPER_MESSAGE, QA_Q1AD_EVENT_ACTOR_EFFECTS,
  QA_Q1AD_EVENT_DEBUG_BOUNDS
} qa_q1ad_event_kind;

typedef struct qa_q1ad_event {
  qa_q1ad_event_kind kind;
  double track, loop_track, alpha, bits, density, sky_factor, duration, roll;
  double color_start, color_length, count, effects, color, lifetime;
  qa_actor_id actor;
  bool has_actor, has_player, depth_test; /* fog: has_player false = TS null */
  qa_actor_id player;
  qa_q1ad_program program;
  int style;
  qa_vec3 camera, angles, color_vec, start, end, origin, min, max;
  const char *text;
} qa_q1ad_event;

/* TS Q1AddonServices. cheat_arsenal NULL = TS optional member absent. */
typedef struct qa_q1ad_services {
  void *ctx;
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  void (*emit)(void *ctx, const qa_q1ad_event *event);
  bool (*is_monster)(void *ctx, qa_actor_id actor);
  double (*cvar)(void *ctx, const char *name);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
} qa_q1ad_services;

/* TS Q1AddonContext. */
typedef struct qa_q1ad_context {
  qa_q1_base *base;
  qa_q1_services *game; /* base.game */
  qa_q1ad_program program;
  qa_q1ad_services services;
  double frame_time;
  /* private playerWords: Map<OwnedActor, Map<string, number>> flattened in
   * insertion order; name NULL marks an admitted owner with no words yet. */
  struct qa_q1ad_word { qa_owned_actor owner; char *name; double value; } *words;
  size_t word_count, word_capacity;
  /* private playerReferences: Map<OwnedActor, Map<string, ActorId | null>>. */
  struct qa_q1ad_ref { qa_owned_actor owner; char *name; bool has_target; qa_actor_id target; } *refs;
  size_t ref_count, ref_capacity;
  /* private frameTicks. */
  qa_q1_actor **ticks;
  size_t tick_count, tick_capacity;
  int64_t release_subscription; /* game.host.actors.onRelease */
  char extension_id[24];        /* `q1:${program}:addons` */
  qa_q1_state_extension extension;
  void *spawn_use_rows; /* triggers.c spawnUse closures (owned) */
  qa_q1_state_extension world_words_extension; /* targets.c "mg3:world-words" */
  /* Registrations made by qa_q1ad_register_q1_campaign_addons, released by
   * qa_q1ad_context_destroy (TS GC): the MG3 item tables and addon monsters. */
  struct qa_q1ad_mg3_items *mg3_items;
  bool addon_monsters;
} qa_q1ad_context;

/* new Q1AddonContext(base, program, services). NULL only on OOM. */
qa_q1ad_context *qa_q1ad_context_create(qa_q1_base *base, qa_q1ad_program program, qa_q1ad_services services);
void qa_q1ad_context_destroy(qa_q1ad_context *context);

double qa_q1ad_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name);
/* TS playerWord: false = undefined. */
bool qa_q1ad_player_word(qa_q1ad_context *context, qa_actor_id actor, const char *name, double *out);
void qa_q1ad_set_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name, double value);
/* TS playerReference: false = null. */
bool qa_q1ad_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name, qa_actor_id *out);
/* target NULL = TS null. */
void qa_q1ad_set_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name,
                                  const qa_actor_id *target);
void qa_q1ad_set_number(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double value);
void qa_q1ad_set_vector(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, qa_vec3 value);
void qa_q1ad_alpha(qa_q1ad_context *context, qa_q1_actor *entity, double alpha);
void qa_q1ad_broadcast(qa_q1ad_context *context, const char *text);
void qa_q1ad_init_trigger(qa_q1ad_context *context, qa_q1_actor *entity);
/* TS removedOutsideCoop(entity, inhibitCoop = true). */
bool qa_q1ad_removed_outside_coop(qa_q1ad_context *context, qa_q1_actor *entity, bool inhibit_coop);
bool qa_q1ad_removed_for_runes(qa_q1ad_context *context, qa_q1_actor *entity);
void qa_q1ad_add_frame_tick(qa_q1ad_context *context, qa_q1_actor *entity, const char *name);
void qa_q1ad_remove_frame_tick(qa_q1ad_context *context, qa_q1_actor *entity);
/* Called by the existing source frame phase; this does not advance simulation time. */
void qa_q1ad_frame(qa_q1ad_context *context, double seconds);

/* C string assignment for the owned char * fields of qa_q1_actor (TS
 * `entity.model = "..."`). Keeps the old value on OOM. */
void qa_q1ad_set_text(char **slot, const char *text);

#endif

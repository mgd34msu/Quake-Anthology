/* Port of src/content/q2/missionpacks/modes/tag.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MODES_TAG_H
#define QA_CONTENT_Q2_MISSIONPACKS_MODES_TAG_H
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q2/missionpacks/players.h"
#include "qa/contracts/session.h"

/* TS Q2TagHooks. */
typedef struct qa_q2mp_tag_hooks {
  qa_q2_item_module *items; /* TS Q2ItemModule (borrowed) */
  void *ctx;
  qa_q2mp_spawn_point (*select_spawn)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  qa_q2_entity *(*farthest_spawn)(void *ctx, qa_q2_game_services *game); /* NULL = TS null */
  void (*add_score)(void *ctx, qa_actor_id actor, double amount);
} qa_q2mp_tag_hooks;
/* TS Q2TagCheckpoint. has_* false = TS null. */
typedef struct qa_q2mp_tag_checkpoint {
  bool has_token; qa_saved_actor_id token;
  bool has_owner; qa_saved_actor_id owner;
  double count;
} qa_q2mp_tag_checkpoint;

/* TS Q2Tag. */
typedef struct qa_q2mp_tag {
  qa_q2mp_tag_hooks hooks;
  bool has_token; qa_actor_id token;
  bool has_owner; qa_actor_id owner;
  double count;
  qa_q2_named_think cb_think[2];
  qa_q2_named_touch cb_touch[1];
  qa_q2_think respawn, make_touchable;
  qa_q2_touch touch;
} qa_q2mp_tag;

void qa_q2mp_tag_init(qa_q2mp_tag *tag, qa_q2mp_tag_hooks hooks);
qa_q2_callback_definitions qa_q2mp_tag_callbacks(qa_q2mp_tag *tag);
qa_q2mp_tag_checkpoint qa_q2mp_tag_capture(qa_q2mp_tag *tag);
void qa_q2mp_tag_restore(qa_q2mp_tag *tag, qa_q2_game_services *game, qa_q2mp_tag_checkpoint saved);
/* False return of qa_q2mp_tag_owner_actor is TS null. */
bool qa_q2mp_tag_owner_actor(qa_q2mp_tag *tag, qa_actor_id *out);
double qa_q2mp_tag_effects(qa_q2mp_tag *tag, qa_actor_id actor);
/* NULL return is TS null. */
const char *qa_q2mp_tag_dog_tag(qa_q2mp_tag *tag, qa_actor_id actor);
double qa_q2mp_tag_change_damage(qa_q2mp_tag *tag, qa_actor_id target, bool has_attacker, qa_actor_id attacker,
                                 double damage);
void qa_q2mp_tag_player_death(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2mp_tag_disconnect(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2mp_tag_score(qa_q2mp_tag *tag, qa_q2_entity *attacker, qa_q2_entity *victim, qa_q2_game_services *game,
                       double change, double means_of_death);
void qa_q2mp_tag_drop(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2mp_tag_post_spawn(qa_q2mp_tag *tag, qa_q2_game_services *game);
bool qa_q2mp_tag_spawn(qa_q2mp_tag *tag, qa_q2_entity *entity, qa_q2_game_services *game);
#endif

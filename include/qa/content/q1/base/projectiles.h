/* Port of src/content/q1/base/projectiles.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_PROJECTILES_H
#define QA_CONTENT_Q1_BASE_PROJECTILES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/base/monsters.h"

/* TS BackpackContents. weapon NULL = TS null; has_* = TS optional. */
typedef struct qa_q1_backpack_extra { qa_item_id item; double count; } qa_q1_backpack_extra;
typedef struct qa_q1_backpack_contents {
  const char *weapon;
  double shells; double nails; double rockets; double cells;
  const qa_q1_backpack_extra *extra; size_t extra_count; bool has_extra;
  const char *selection; bool has_selection; /* "source-default"|"rank" */
  bool avoid_underwater_lightning; bool has_avoid_underwater_lightning;
  double owner_pickup_delay; bool has_owner_pickup_delay;
} qa_q1_backpack_contents;

/* TS backpackMessage result. text is a literal ("$qc_backpack_got") or the
 * owned classic text; parts and their args are owned. Unbounded like TS:
 * release with qa_q1_backpack_message_free. NULL members only on OOM. */
typedef struct qa_q1_backpack_message {
  const char *text;
  const qa_q1_message_part *parts; size_t part_count;
  char *owned_text;
  qa_q1_message_part *owned_parts;
  qa_q1_msg_arg *owned_args;
} qa_q1_backpack_message;
qa_q1_backpack_message qa_q1_backpack_message_make(const qa_q1_backpack_contents *contents,
                                                   const char *edition, bool new_weapon);
void qa_q1_backpack_message_free(qa_q1_backpack_message *message);

qa_q1_actor *qa_q1_throw_gib(qa_q1_services *game, qa_vec3 origin, const char *model, double damage);
void qa_q1_throw_head(qa_q1_services *game, qa_q1_actor *entity, const char *model);
void qa_q1_throw_head_damage(qa_q1_services *game, qa_q1_actor *entity, const char *model, double damage);
qa_q1_actor *qa_q1_spawn_meat_spray(qa_q1_services *game, qa_q1_actor *owner, qa_vec3 origin, qa_vec3 velocity);
/* launch NULL = TS undefined. movement is "bounce"|"toss". */
typedef struct qa_q1_backpack_launch { qa_vec3 origin; qa_vec3 velocity; const char *movement; } qa_q1_backpack_launch;
qa_q1_actor *qa_q1_drop_backpack(qa_q1_services *game, qa_vec3 origin,
                                  const qa_q1_backpack_contents *contents,
                                  const qa_q1_backpack_launch *launch);
qa_q1_actor *qa_q1_create_missile(qa_q1_services *game, const qa_actor_id *owner, const char *classname,
                                   const char *model, qa_vec3 origin, qa_vec3 velocity);
qa_q1_actor *qa_q1_create_missile_lifetime(qa_q1_services *game, const qa_actor_id *owner, const char *classname,
                                            const char *model, qa_vec3 origin, qa_vec3 velocity, double lifetime);
/* kind is "spike"|"superspike"|"wizard"|"wizard2"|"knight". */
qa_q1_actor *qa_q1_launch_spike(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 velocity, const char *kind);
qa_q1_actor *qa_q1_launch_laser(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 direction);
/* TS launchLaserAlt (mg3 LaunchLaserAlt): no glow, no impact sound. */
qa_q1_actor *qa_q1_launch_laser_alt(qa_q1_services *game, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 direction);
void qa_q1_sprite_explosion(qa_q1_services *game, qa_q1_actor *missile);
/* TS spriteExplosion(game, missile, plainEffect): the explosion effect only when plain_effect. */
void qa_q1_sprite_explosion_plain_effect(qa_q1_services *game, qa_q1_actor *missile, bool plain_effect);
void qa_q1_launch_ogre_grenade(qa_q1_base_monster *monster);
void qa_q1_launch_zombie_grenade(qa_q1_base_monster *monster, qa_vec3 offset);
void qa_q1_launch_vore_ball(qa_q1_base_monster *monster);
void qa_q1_register_projectile_callbacks(qa_q1_services *game);
void qa_q1_cast_lightning(qa_q1_base_monster *monster);

#endif

/* Port of src/content/q1/addons/monsters/ordinary/chthon.ts.
 * quakec_mg1/monsters/boss.qc BOSS_KILLABLE and quakec_mg1/combat.qc: the 4000 HP Chthon
 * that only the lightning gun hurts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/chthon.h"

#include <stdlib.h>
#include <string.h>

#include "../support.h"

#define PREFIX "mg1:ordinary"

bool qa_q1am_killable_chthon(const qa_q1_base_monster *monster) {
  return strcmp(monster->spec->species, "boss") == 0 && (QA_Q1AM_FLAGS(monster->entity->spawnflags) & QA_Q1AM_BOSS_KILLABLE) != 0;
}
static void boss_freeze(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!qa_q1am_killable_chthon(monster)) return;
  qa_q1am_field_number(entity, "storednextthink", entity->next_think);
  const char *think = qa_q1_action_name(entity->think);
  qa_q1am_field_text(entity, "addon.storedthink", think == NULL ? "" : think);
  qa_q1am_field_text(entity, "is_frozen", "1");
  qa_q1_cancel(game, entity);
  entity->think = qa_q1_callback_registry_action(game->named, entity, "SUB_Null");
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":boss_unfreeze");
}
const qa_q1_monster_action_entry qa_q1am_chthon_actions[] = {{"boss_freeze", boss_freeze, NULL}};
const size_t qa_q1am_chthon_action_count = sizeof qa_q1am_chthon_actions / sizeof qa_q1am_chthon_actions[0];

void qa_q1am_awake_killable_chthon(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_set_text(&entity->model, "progs/boss.mdl");
  entity->solid = "slidebox";
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 4000);
  entity->max_health = 4000;
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  qa_q1_base_monster_set_enemy(monster, activator);
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":monster_use");
  qa_q1_effect(game, "lava-splash", qa_q1_base_monster_origin(monster), NULL, 1);
  qa_q1_link(game, entity);
  qa_q1_base_monster_play(monster, "boss_rise1");
}
void qa_q1am_chthon_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double health = qa_q1_health(game, entity->actor.id), max = entity->max_health, prior = health + damage;
  if (prior == max && health < max) {
    qa_q1_base_monster_play(monster, "boss_shockz1");
    qa_q1_use_targets(game, entity, attacker);
  }
  if (health < max / 3 && prior > max / 3) {
    qa_q1_base_monster_play(monster, "boss_shockb1");
    qa_q1_use_targets(game, entity, attacker);
    return;
  }
  if (health < max * 2 / 3 && prior > max * 2 / 3) {
    qa_q1_base_monster_play(monster, "boss_shocka1");
    qa_q1_use_targets(game, entity, attacker);
  }
}
/* The TS registerChthon closure environment. */
typedef struct chthon_registration {
  qa_q1ad_context *context;
  qa_q1am_chthon_lookup_fn chthon;
  void *chthon_ctx;
  struct chthon_registration *next;
} chthon_registration;
static chthon_registration *all_registrations;
static void boss_unfreeze(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; /* TS (_game, entity) */
  char name[128];
  qa_q1am_copy_name(name, sizeof name, qa_q1_actor_text(entity, "addon.storedthink"));
  const double due = qa_q1_actor_number(entity, "storednextthink");
  if (name[0] == '\0') memset(&entity->think, 0, sizeof entity->think);
  else entity->think = qa_q1_callback_registry_action(game->named, entity, name);
  if (entity->think.fn != NULL || entity->think.name != NULL) qa_q1_schedule(game, entity, due - game->time, entity->think);
  qa_q1am_field_text(entity, "storednextthink", "-1");
  qa_q1am_field_text(entity, "is_frozen", "0");
}
static bool chthon_before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                 const qa_combat_state *attacker) {
  (void)damage; (void)target; (void)attacker; /* TS beforeHealth(request) */
  const chthon_registration *state = ctx;
  qa_q1_services *game = state->context->game;
  qa_q1_actor *entity = qa_q1_get_entity(game, &request.target);
  qa_q1_base_monster *monster = entity == NULL ? NULL : state->chthon(state->chthon_ctx, entity);
  if (monster == NULL || !qa_q1am_killable_chthon(monster)) return true;
  const qa_q1_player_state *player = request.attack.attacker == NULL ? NULL : qa_q1_player(game, *request.attack.attacker);
  return player != NULL && player->weapon != NULL && strcmp(player->weapon, "lightning") == 0;
}
void qa_q1am_register_chthon(qa_q1ad_context *context, qa_q1am_chthon_lookup_fn chthon, void *chthon_ctx) {
  qa_q1_services *game = context->game;
  chthon_registration *slot = calloc(1, sizeof *slot);
  if (slot == NULL) qa_range_error("out of memory");
  slot->context = context;
  slot->chthon = chthon;
  slot->chthon_ctx = chthon_ctx;
  slot->next = all_registrations;
  all_registrations = slot;
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.use = boss_unfreeze;
  qa_q1_callback_registry_register(game->named, PREFIX ":boss_unfreeze", handlers);
  qa_q1_damage_source_effects effects;
  memset(&effects, 0, sizeof effects);
  effects.ctx = slot;
  effects.before_health = chthon_before_health;
  qa_q1_register_damage_source_effects(game, "mg1:chthon", effects);
}
void qa_q1am_release_chthon(qa_q1_services *game) {
  for (chthon_registration **link = &all_registrations; *link != NULL;) {
    chthon_registration *slot = *link;
    if (slot->context->game != game) { link = &slot->next; continue; }
    *link = slot->next;
    free(slot);
  }
}

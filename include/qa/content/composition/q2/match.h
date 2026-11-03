/*
 * Port of src/content/composition/q2/match.ts: Q2ProductMatch, the
 * selected source match mode (CTF, LMCTF, Tag, DeathBall, Horde, or none)
 * as one Q2SpawnModule. The TS `instanceof` checks read source_kind.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_MATCH_H
#define QA_CONTENT_COMPOSITION_Q2_MATCH_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/composition/q2/types.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/missionpacks/modes/deathball.h"
#include "qa/content/q2/missionpacks/modes/tag.h"
#include "qa/content/q2/multiplayer/ctf/index.h"
#include "qa/content/q2/multiplayer/horde.h"
#include "qa/content/q2/multiplayer/lmctf/runtime.h"
#include "qa/world/gameplay/policies.h"

/** TS Q2ProductMatch. players/world are the constructor's lazy accessors
 * (TS `() => this.players`, `() => this.game`), called with accessor_ctx.
 * Exactly one of ctf/lmctf/horde/tag/ball is live per source_kind;
 * QA_Q2_MATCH_STANDARD is the TS null source. */
typedef struct qa_q2_product_match {
  qa_q2_match_selection selection;
  qa_q2_match_kind source_kind;
  qa_q2_ctf *ctf;
  qa_q2_lmctf *lmctf;
  qa_q2_horde *horde;
  qa_q2mp_tag tag;
  qa_q2mp_deathball ball;
  void *accessor_ctx;
  qa_q2_players *(*players)(void *ctx);
  qa_q2_game_services *(*world)(void *ctx);
  /* TS Omit<Q2CtfHooks, "emit"> as given by the composition (forwarded
   * with its own ctx) and the services whose emit wraps mode events. */
  qa_q2_ctf_hooks hooks;
  const qa_q2_composition_services *services;
  /* Strings the last DeathBall settings() read handed out. */
  char *settings_skins[2];
} qa_q2_product_match;

/** TS constructor(selection, players, items, world, hooks, services).
 * selection is copied (its borrowed travel/cvars must outlive the match);
 * hooks.emit is ignored. Aborts on OOM. */
void qa_q2_product_match_init(qa_q2_product_match *match, const qa_q2_match_selection *selection,
                              void *accessor_ctx, qa_q2_players *(*players)(void *ctx),
                              qa_q2_item_module *items, qa_q2_game_services *(*world)(void *ctx),
                              qa_q2_ctf_hooks hooks, const qa_q2_composition_services *services);
void qa_q2_product_match_destroy(qa_q2_product_match *match);

/* TS Q2SpawnModule view (callbacks + spawn). The callbacks pointer stays
 * valid for the match's life. */
qa_q2_spawn_module qa_q2_product_match_as_spawn(qa_q2_product_match *match);
qa_q2_callback_definitions qa_q2_product_match_callbacks(qa_q2_product_match *match);
bool qa_q2_product_match_spawn(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_product_match_after_spawn(qa_q2_product_match *match, qa_q2_game_services *game);
void qa_q2_product_match_admitted(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
/* False = TS null. */
bool qa_q2_product_match_select_spawn(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game,
                                      qa_vec3 *out_origin, qa_vec3 *out_angles);
/* has_attacker false = TS omitted argument (defaults to recipient);
 * otherwise attacker NULL is TS null. */
void qa_q2_product_match_score(qa_q2_product_match *match, qa_q2_entity *victim, qa_q2_game_services *game,
                               double change, double means, qa_q2_entity *recipient, bool has_attacker,
                               qa_q2_entity *attacker);
void qa_q2_product_match_death(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_product_match_disconnect(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
/* attacker NULL = TS null. */
double qa_q2_product_match_damage(qa_q2_product_match *match, qa_actor_id target, const qa_actor_id *attacker,
                                  double amount);
double qa_q2_product_match_knockback(qa_q2_product_match *match, qa_actor_id target, double amount, double means,
                                     qa_q2_game_services *game);
bool qa_q2_product_match_check_rules(qa_q2_product_match *match, qa_q2_game_services *game);
void qa_q2_product_match_drop_inventory(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_product_match_command(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game,
                                 const char *name, const char *const *args, size_t arg_count);
void qa_q2_product_match_before_player(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_product_match_player_spawned(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
double qa_q2_product_match_gravity_scale(qa_q2_product_match *match, qa_actor_id actor);

/** TS sourceEffects(game): the returned closures capture match and game
 * in caller storage, which must outlive the effects. */
typedef struct qa_q2_product_match_effects_binding {
  qa_q2_product_match *match;
  qa_q2_game_services *game;
} qa_q2_product_match_effects_binding;
qa_q2_damage_source_effects qa_q2_product_match_source_effects(qa_q2_product_match_effects_binding *storage,
                                                               qa_q2_product_match *match,
                                                               qa_q2_game_services *game);
void qa_q2_product_match_effects(qa_q2_product_match *match, qa_q2_entity *entity, qa_q2_game_services *game);

#endif

/* Port of src/content/q1/base/provider.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_PROVIDER_H
#define QA_CONTENT_Q1_BASE_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/base/monsters.h"

typedef struct qa_q1_level_rules qa_q1_level_rules;
typedef struct qa_q1_spawn_selector qa_q1_spawn_selector;

/* TS Q1CampaignBinding (fn+ctx). */
typedef struct qa_q1_campaign_binding {
  void *ctx;
  double (*read_flags)(void *ctx);
  void (*write_flags)(void *ctx, double flags);
  void (*set_skill)(void *ctx, int skill);
} qa_q1_campaign_binding;

/* TS Q1CampaignState. */
typedef struct qa_q1_campaign_state {
  double flags; int skill;
  qa_q1_campaign_binding binding;
} qa_q1_campaign_state;
void qa_q1_campaign_state_init(qa_q1_campaign_state *state, double flags, int skill);

/* TS Q1BaseOptions. campaign NULL = default state. */
typedef struct qa_q1_base_options {
  const qa_q1_campaign_binding *campaign;
  bool has_registered; bool registered;
  void *ctx;
  bool (*finale_finished)(void *ctx);
  void (*finish_campaign)(void *ctx);
  bool (*same_level)(void *ctx);
  void (*player_exited)(void *ctx, qa_actor_id actor);
  bool has_official_campaign; bool official_campaign;
} qa_q1_base_options;

/* TS Q1_BASE_CLASSNAMES (computed; count via out param). */
size_t qa_q1_base_classnames(const char *const **out);

typedef struct qa_q1_base qa_q1_base;
/* TS q1Base(game): aborts when base content was not registered. */
qa_q1_base *qa_q1_base_for(qa_q1_services *game);
qa_q1_base *qa_q1_base_create(qa_q1_services *game, const qa_q1_base_options *options);
qa_q1_base *qa_q1_register_base(qa_q1_services *game, const qa_q1_base_options *options);
void qa_q1_base_destroy(qa_q1_base *base);
qa_q1_services *qa_q1_base_game(qa_q1_base *base);
const qa_q1_base_options *qa_q1_base_options_of(qa_q1_base *base);
qa_q1_creatures *qa_q1_base_creatures(qa_q1_base *base);
qa_q1_level_rules *qa_q1_base_level_rules(qa_q1_base *base);
qa_q1_spawn_selector *qa_q1_base_spawn_selector(qa_q1_base *base);
qa_q1_campaign_binding qa_q1_base_campaign(qa_q1_base *base);
bool qa_q1_base_registered(qa_q1_base *base);
typedef bool (*qa_q1_kill_count_rule_fn)(void *ctx, qa_q1_base_monster *monster);
void qa_q1_base_register_kill_count_rule(qa_q1_base *base, const char *id,
                                         qa_q1_kill_count_rule_fn rule, void *ctx);
bool qa_q1_base_count_monster_kill(qa_q1_base *base, qa_q1_base_monster *monster);
const char *qa_q1_base_next_hell_knight_melee(qa_q1_base *base);
void qa_q1_base_spawn_lightning(qa_q1_base *base, qa_q1_actor *entity);
void qa_q1_base_finale(qa_q1_base *base, qa_q1_base_monster *monster);
void qa_q1_base_finish_finale(qa_q1_base *base, qa_q1_base_monster *monster);
/* This object as TS MonsterServices (Q1Base implements it), e.g. for
 * `new BaseMonster(game, entity, spec, context.base, source)`. */
qa_q1_monster_services qa_q1_base_as_monster_services(qa_q1_base *base);
void qa_q1_base_dismiss_finale(qa_q1_base *base);
void qa_q1_base_reset_finale(qa_q1_base *base);
bool qa_q1_base_has_finished_finale(qa_q1_base *base);

#endif

/* Port of src/content/q2/multiplayer/ctf/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/index.h"
#include "../multiplayer_internal.h"
#include <ctype.h>

struct qa_q2_ctf {
  qa_q2_ctf_context context;
  qa_q2_ctf_flags *flags;
  qa_q2_ctf_match *match;
  qa_q2_ctf_presentation *presentation;
  qa_q2_ctf_grapple *grapple;
  qa_q2_ctf_techs *techs;
  qa_q2_named_think *thinks;
  qa_q2_named_touch *touchs;
  qa_q2_named_use *uses;
  qa_q2_named_pain *pains;
  qa_q2_named_die *dies;
  qa_q2_named_blocked *blockeds;
  qa_q2_callback_definitions callbacks;
};

static void banner_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g);
static void teleport_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact);
static void spawn_player_impl(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g);
static void set_observer(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g);

static bool rules_haste(void *ctx, qa_q2mp_weapon_context *context) {
  qa_q2_ctf *ctf = ctx;
  return qa_q2_ctf_techs_haste(ctf->techs, context->actor.id, context->game);
}
static bool rules_strength_sound(void *ctx, qa_q2mp_weapon_context *context) {
  qa_q2_ctf *ctf = ctx;
  return qa_q2_ctf_techs_strength_sound(ctf->techs, context->actor, context->game);
}
static void rules_haste_sound(void *ctx, qa_q2mp_weapon_context *context) {
  qa_q2_ctf *ctf = ctx;
  qa_q2_ctf_techs_haste_sound(ctf->techs, context->actor, context->game);
}

static void actions_reset_players(void *ctx, qa_q2_game_services *g) { qa_q2_ctf_reset_players(ctx, g); }
static void actions_reset_grapple(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf *ctf = ctx;
  qa_q2_ctf_grapple_module_reset(ctf->grapple, entity->actor, g);
}
static bool actions_join(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_q2_ctf_playing_team team, bool ghost) {
  return qa_q2_ctf_join(ctx, entity, g, team, ghost);
}

#define MERGE(field, type, extra_name, extra_fn)                                       \
  do {                                                                                 \
    size_t total = flag.field##_count + grap.field##_count + tech.field##_count + 1;   \
    ctf->field##s = qa_q2mp_xalloc(malloc(total * sizeof *ctf->field##s));             \
    size_t at = 0;                                                                     \
    for (size_t i = 0; i < flag.field##_count; i++) ctf->field##s[at++] = flag.field[i]; \
    for (size_t i = 0; i < grap.field##_count; i++) ctf->field##s[at++] = grap.field[i]; \
    for (size_t i = 0; i < tech.field##_count; i++) ctf->field##s[at++] = tech.field[i]; \
    ctf->field##s[at++] = (type){extra_name, {ctf, extra_fn}};                         \
    ctf->callbacks.field = ctf->field##s;                                              \
    ctf->callbacks.field##_count = at;                                                 \
  } while (0)
#define MERGE_PLAIN(field, type)                                                       \
  do {                                                                                 \
    size_t total = flag.field##_count + grap.field##_count + tech.field##_count;       \
    if (total > 0) {                                                                   \
      ctf->field##s = qa_q2mp_xalloc(malloc(total * sizeof *ctf->field##s));           \
      size_t at = 0;                                                                   \
      for (size_t i = 0; i < flag.field##_count; i++) ctf->field##s[at++] = flag.field[i]; \
      for (size_t i = 0; i < grap.field##_count; i++) ctf->field##s[at++] = grap.field[i]; \
      for (size_t i = 0; i < tech.field##_count; i++) ctf->field##s[at++] = tech.field[i]; \
      ctf->callbacks.field = ctf->field##s;                                            \
      ctf->callbacks.field##_count = at;                                               \
    }                                                                                  \
  } while (0)

qa_q2_ctf *qa_q2_ctf_create(qa_q2_ctf_hooks hooks, const qa_q2_ctf_rules *changes, unsigned mask, qa_shared_grapple_control *shared_grapple) {
  qa_q2_ctf *ctf = calloc(1, sizeof *ctf);
  if (ctf == NULL) return NULL;
  ctf->context.hooks = hooks;
  if (changes != NULL) qa_create_q2_ctf_rules_with(&ctf->context.rules, changes, mask);
  else qa_create_q2_ctf_rules(&ctf->context.rules);
  qa_q2_ctf_match_state_init(&ctf->context.match);
  ctf->flags = qa_q2_ctf_flags_create(&ctf->context);
  ctf->grapple = qa_q2_ctf_grapple_module_create(&ctf->context, shared_grapple);
  ctf->techs = qa_q2_ctf_techs_create(&ctf->context);
  qa_q2_ctf_match_actions actions = {0};
  actions.ctx = ctf;
  actions.reset_players = actions_reset_players;
  actions.reset_grapple = actions_reset_grapple;
  actions.join = actions_join;
  ctf->match = qa_q2_ctf_match_create(&ctf->context, actions);
  ctf->presentation = qa_q2_ctf_presentation_create(&ctf->context, ctf->flags);
  if (ctf->flags == NULL || ctf->grapple == NULL || ctf->techs == NULL || ctf->match == NULL || ctf->presentation == NULL) {
    qa_q2_ctf_destroy(ctf);
    return NULL;
  }
  qa_q2_callback_definitions flag = qa_q2_ctf_flags_callbacks(ctf->flags);
  qa_q2_callback_definitions grap = qa_q2_ctf_grapple_module_callbacks(ctf->grapple);
  qa_q2_callback_definitions tech = qa_q2_ctf_techs_callbacks(ctf->techs);
  MERGE(think, qa_q2_named_think, "misc_ctf_banner_think", banner_think_fn);
  MERGE(touch, qa_q2_named_touch, "old_teleporter_touch", teleport_touch_fn);
  MERGE_PLAIN(use, qa_q2_named_use);
  MERGE_PLAIN(pain, qa_q2_named_pain);
  MERGE_PLAIN(die, qa_q2_named_die);
  MERGE_PLAIN(blocked, qa_q2_named_blocked);
  qa_q2_ctf_flags_register(ctf->flags);
  qa_q2_ctf_grapple_module_register(ctf->grapple);
  qa_q2_ctf_techs_register(ctf->techs);
  qa_q2mp_source_rules rules = {0};
  rules.kind = "ctf";
  rules.ctx = ctf;
  rules.haste = rules_haste;
  rules.strength_sound = rules_strength_sound;
  rules.haste_sound = rules_haste_sound;
  hooks.weapons->set_source_rules(hooks.weapons->ctx, rules);
  return ctf;
}

void qa_q2_ctf_destroy(qa_q2_ctf *ctf) {
  if (ctf == NULL) return;
  qa_q2_ctf_flags_destroy(ctf->flags);
  qa_q2_ctf_match_destroy(ctf->match);
  qa_q2_ctf_presentation_destroy(ctf->presentation);
  qa_q2_ctf_grapple_module_destroy(ctf->grapple);
  qa_q2_ctf_techs_destroy(ctf->techs);
  free(ctf->thinks);
  free(ctf->touchs);
  free(ctf->uses);
  free(ctf->pains);
  free(ctf->dies);
  free(ctf->blockeds);
  free(ctf->context.states);
  qa_q2_ctf_match_state_destroy(&ctf->context.match);
  qa_q2_ctf_rules_destroy(&ctf->context.rules);
  free(ctf);
}

static bool spawn_cb(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) { return qa_q2_ctf_spawn(ctx, entity, g); }
static const char *item_name_cb(void *ctx, const char *classname) { return qa_q2_ctf_item_name(ctx, classname); }

qa_q2_spawn_module qa_q2_ctf_as_spawn(qa_q2_ctf *ctf) {
  qa_q2_spawn_module m = {0};
  m.ctx = ctf;
  m.callbacks = &ctf->callbacks;
  m.item_name = item_name_cb;
  m.spawn = spawn_cb;
  return m;
}

qa_q2_ctf_context *qa_q2_ctf_get_context(qa_q2_ctf *ctf) { return &ctf->context; }
qa_q2_ctf_flags *qa_q2_ctf_get_flags(qa_q2_ctf *ctf) { return ctf->flags; }
qa_q2_ctf_match *qa_q2_ctf_get_match(qa_q2_ctf *ctf) { return ctf->match; }
qa_q2_ctf_presentation *qa_q2_ctf_get_presentation(qa_q2_ctf *ctf) { return ctf->presentation; }
qa_q2_ctf_grapple *qa_q2_ctf_get_grapple(qa_q2_ctf *ctf) { return ctf->grapple; }
qa_q2_ctf_techs *qa_q2_ctf_get_techs(qa_q2_ctf *ctf) { return ctf->techs; }
qa_q2_callback_definitions qa_q2_ctf_callbacks(qa_q2_ctf *ctf) { return ctf->callbacks; }
const char *qa_q2_ctf_item_name(qa_q2_ctf *ctf, const char *classname) {
  return qa_q2_item_module_item_name(ctf->context.hooks.items, classname);
}

static void banner_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  entity->frame = fmod(entity->frame + 1, 16);
  g->show(g->ctx, entity);
  qa_q2_think self = {ctx, banner_think_fn};
  g->schedule(g->ctx, entity, 0.1, self);
}

static void teleport_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_ctf *ctf = ctx;
  qa_q2_entity *player = qa_q2_game_entity_of(g, &contact.other);
  if (player == NULL || !g->host->is_player(g->host->ctx, contact.other)) return;
  size_t n = 0;
  qa_q2_entity *const *targets = g->targets(g->ctx, entity->target, &n);
  if (n == 0) {
    g->host->diagnostic(g->host->ctx, "Couldn't find CTF teleporter destination");
    return;
  }
  qa_q2_entity *destination = targets[0];
  qa_q2_ctf_grapple_module_reset(ctf->grapple, player->actor, g);
  qa_body_state body = qa_q2_game_body(g, destination);
  qa_vec3 velocity = qa_q2_scale(qa_q2_movedir(body.angles), 200);
  qa_shared_body_table_unlink(g->host->bodies, player->actor);
  qa_q2_body_changes move = {0};
  move.has_origin = true; move.origin = body.origin;
  move.has_velocity = true; move.velocity = velocity;
  move.has_angles = true; move.angles = (qa_vec3){0, body.angles.y, 0};
  qa_q2_game_move(g, player, move, false);
  ctf->context.hooks.teleport(ctf->context.hooks.ctx, player, g, body.origin, body.angles, velocity);
  if (entity->has_enemy) {
    qa_q2_presentation_event event = {0};
    event.kind = "entity-event";
    event.has_actor = true;
    event.actor = entity->enemy;
    event.event = 6;
    g->host->emit(g->host->ctx, event);
  }
  qa_q2_presentation_event event = {0};
  event.kind = "entity-event";
  event.has_actor = true;
  event.actor = player->actor.id;
  event.event = 6;
  g->host->emit(g->host->ctx, event);
  qa_q2mp_mirror_kill_box(player, g);
  g->link(g->ctx, player);
}

bool qa_q2_ctf_spawn(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (qa_q2_ctf_flags_spawn(ctf->flags, entity, g) || qa_q2_ctf_techs_spawn(ctf->techs, entity, g)) return true;
  const char *classname = entity->classname;
  if (strcmp(classname, "info_player_team1") == 0 || strcmp(classname, "info_player_team2") == 0) return true;
  if (strcmp(classname, "misc_ctf_banner") == 0 || strcmp(classname, "misc_ctf_small_banner") == 0) {
    entity->model = strcmp(classname, "misc_ctf_banner") == 0 ? "models/ctf/banner/tris.md2" : "models/ctf/banner/small.md2";
    entity->skin = ((int64_t)entity->spawnflags & 1) != 0 ? 1 : 0;
    entity->frame = floor(g->host->random(g->host->ctx) * 16);
    g->solid(g->ctx, entity, "none");
    g->motion(g->ctx, entity, "stationary");
    g->show(g->ctx, entity);
    qa_q2_think think = {ctf, banner_think_fn};
    g->schedule(g->ctx, entity, 0.1, think);
    return true;
  }
  if (strcmp(classname, "info_teleport_destination") == 0) {
    qa_q2_body_changes move = {0};
    move.has_origin = true;
    move.origin = qa_q2_add(qa_q2_game_body(g, entity).origin, (qa_vec3){0, 0, 16});
    qa_q2_game_move(g, entity, move, true);
    return true;
  }
  if (strcmp(classname, "trigger_teleport") == 0) {
    if (entity->target == NULL || entity->target[0] == '\0') {
      g->host->diagnostic(g->host->ctx, "teleporter without a target");
      g->remove(g->ctx, entity);
      return true;
    }
    entity->visible = false;
    entity->server_flags = (double)((int64_t)entity->server_flags | 1);
    entity->touch = (qa_q2_touch){ctf, teleport_touch_fn};
    if (entity->model != NULL && entity->model[0] == '*') {
      qa_q2_body_changes move = {0};
      move.has_bounds = true;
      move.bounds = g->host->inline_model_bounds(g->host->ctx, strtod(entity->model + 1, NULL));
      qa_q2_game_move(g, entity, move, true);
    }
    g->solid(g->ctx, entity, "trigger");
    g->show(g->ctx, entity);
    qa_q2_entity *sound = g->create(g->ctx, "ctf_teleport_sound", NULL, 0);
    qa_body_state body = qa_q2_game_body(g, entity);
    entity->has_enemy = true;
    entity->enemy = sound->actor.id;
    qa_q2_body_changes move = {0};
    move.has_origin = true;
    move.origin = qa_q2_add(body.origin, qa_q2_scale(qa_q2_add(body.bounds.min, body.bounds.max), 0.5));
    qa_q2_game_move(g, sound, move, true);
    qa_q2_presentation_event event = {0};
    event.kind = "sound";
    event.has_actor = true;
    event.actor = sound->actor.id;
    event.origin = qa_q2_game_body(g, sound).origin;
    event.path = "world/hum1.wav";
    event.channel = 0;
    event.volume = 1;
    event.attenuation = 1;
    event.reliable = false;
    event.loop = "start";
    g->host->emit(g->host->ctx, event);
    return true;
  }
  return false;
}

void qa_q2_ctf_after_spawn(qa_q2_ctf *ctf, qa_q2_game_services *g) {
  qa_q2_ctf_match_after_spawn(ctf->match, g);
  qa_q2_ctf_techs_setup(ctf->techs, g);
}

void qa_q2_ctf_admitted(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_grapple_equipment *equipment = qa_q2_ctf_grapple_module_equipment(ctf->grapple);
  if (equipment != NULL) qa_q2_ctf_grapple_bind(equipment, g);
  qa_shared_grapple_control *shared = qa_q2_ctf_grapple_module_shared(ctf->grapple);
  if (shared != NULL && !shared->selection.is_enabled) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:weapon_grapple"; entry.count = 0; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, entity->actor, entry, NULL, NULL);
  }
  if (qa_q2_ctf_context_get(&ctf->context, entity->actor.id) != NULL) return;
  qa_q2mp_player_state *common = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  if (common == NULL) qa_q2f_error("CTF admission requires the shared player state");
  qa_q2_ctf_player_state fresh;
  qa_q2_ctf_player_state_init(&fresh);
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_set(&ctf->context, entity->actor.id, fresh);
  if (((int64_t)g->options.deathmatch_flags & 131072) != 0 && strcmp(ctf->context.match.phase, "none") == 0 && !common->requested_spectator) {
    double one = 0, two = 0;
    for (size_t i = 0; i < ctf->context.state_count; i++) {
      if (ctf->context.states[i].state.team == 1) one++;
      else if (ctf->context.states[i].state.team == 2) two++;
    }
    state->team = one < two ? 1 : two < one ? 2 : g->host->random(g->host->ctx) < 0.5 ? 1 : 2;
    qa_q2_ctf_assign_skin(ctf, entity);
    spawn_player_impl(ctf, entity, g);
    return;
  }
  set_observer(ctf, entity, g);
  qa_q2_ctf_presentation_join_menu(ctf->presentation, entity);
}

void qa_q2_ctf_assign_skin(qa_q2_ctf *ctf, qa_q2_entity *entity) {
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  qa_q2_ctf_player_state *state = qa_ctf_player(&ctf->context, entity->actor.id);
  if (player == NULL) qa_q2f_error("Missing shared CTF player");
  char skin_buf[512];
  const char *from_info = qa_q2mp_userinfo_get(player->userinfo, "skin", skin_buf, sizeof skin_buf);
  const char *original = from_info == NULL ? player->skin : from_info;
  if (state->team == 0) {
    ctf->context.hooks.set_skin(ctf->context.hooks.ctx, entity->actor.id, original);
    return;
  }
  const char *slash = strrchr(original, '/');
  char skin[560];
  if (slash == NULL) snprintf(skin, sizeof skin, "male/%s", state->team == 1 ? "ctf_r" : "ctf_b");
  else snprintf(skin, sizeof skin, "%.*s%s", (int)(slash - original) + 1, original, state->team == 1 ? "ctf_r" : "ctf_b");
  ctf->context.hooks.set_skin(ctf->context.hooks.ctx, entity->actor.id, skin);
}

static void spawn_player_impl(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  if (player == NULL) qa_q2f_error("Missing shared CTF player");
  player->spectator = false;
  player->requested_spectator = false;
  player->dead = false;
  player->noclip = false;
  player->god = false;
  entity->server_flags = (double)((int64_t)entity->server_flags & ~1);
  entity->visible = true;
  ctf->context.hooks.spawn_player(ctf->context.hooks.ctx, entity, g);
  qa_combat_trait_changes traits = {0};
  traits.has_team = true;
  traits.team = qa_ctf_team_name(qa_ctf_player(&ctf->context, entity->actor.id)->team);
  qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
  if (qa_q2_ctf_grapple_module_native_enabled(ctf->grapple) && player->use_q2_weapons) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:weapon_grapple"; entry.count = 1; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, entity->actor, entry, NULL, NULL);
  }
  qa_q2_ctf_assign_skin(ctf, entity);
}

bool qa_q2_ctf_join(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_q2_ctf_playing_team team, bool ghost) {
  qa_q2_ctf_player_state *state = qa_ctf_player(&ctf->context, entity->actor.id);
  const char *phase = ctf->context.match.phase;
  if (!ghost && ((ctf->context.rules.match_lock && (strcmp(phase, "pregame") == 0 || strcmp(phase, "game") == 0)) ||
                 (strcmp(ctf->context.rules.force_join, "red") == 0 && team != 1) ||
                 (strcmp(ctf->context.rules.force_join, "blue") == 0 && team != 2)))
    return false;
  qa_q2_ctf_drop_inventory(ctf, entity, g);
  state->team = team;
  state->spawn_state = 0;
  state->ready = false;
  if (strcmp(phase, "game") == 0 && !ghost) qa_q2_ctf_match_assign_ghost(ctf->match, entity, g);
  spawn_player_impl(ctf, entity, g);
  qa_q2_presentation_event event = {0};
  event.kind = "entity-event";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.event = 6;
  g->host->emit(g->host->ctx, event);
  qa_ctf_print(g, NULL, NULL, "%s joined the %s team.\n", qa_ctf_name(&ctf->context, entity->actor.id), qa_ctf_team_name(team));
  if (strcmp(phase, "setup") == 0) {
    qa_q2_presentation_event center = {0};
    center.kind = "centerprint";
    center.has_actor = true;
    center.actor = entity->actor.id;
    center.text = "Type \"ready\" in console to ready up.";
    g->host->emit(g->host->ctx, center);
  }
  return true;
}

static void lower_into(char *out, size_t size, const char *text) {
  size_t i = 0;
  for (; text[i] && i + 1 < size; i++) out[i] = (char)tolower((unsigned char)text[i]);
  out[i] = '\0';
}

void qa_q2_ctf_team_command(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, const char *name) {
  qa_q2_ctf_player_state *state = qa_ctf_player(&ctf->context, entity->actor.id);
  char lowered[256];
  lower_into(lowered, sizeof lowered, name);
  bool has_team = strcmp(lowered, "red") == 0 || strcmp(lowered, "blue") == 0;
  qa_q2_ctf_playing_team team = strcmp(lowered, "red") == 0 ? 1 : 2;
  if (name[0] == '\0') {
    qa_ctf_print(g, &entity->actor.id, NULL, "You are on the %s team.\n", qa_ctf_team_name(state->team));
    return;
  }
  const char *phase = ctf->context.match.phase;
  if (strcmp(phase, "none") != 0 && strcmp(phase, "setup") != 0) {
    qa_ctf_print(g, &entity->actor.id, NULL, "Can't change teams in a match.\n");
    return;
  }
  if (!has_team) {
    qa_ctf_print(g, &entity->actor.id, NULL, "Unknown team %s.\n", name);
    return;
  }
  if (state->team == team) {
    qa_ctf_print(g, &entity->actor.id, NULL, "You are already on the %s team.\n", qa_ctf_team_name(team));
    return;
  }
  qa_q2_ctf_team previous = state->team;
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  qa_q2_ctf_drop_inventory(ctf, entity, g);
  if (previous != 0 && player != NULL) {
    player->god = false;
    qa_combat_trait_changes traits = {0};
    traits.has_invulnerable = true;
    traits.invulnerable = false;
    qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
    g->damage(g->ctx, entity->actor.id, entity, entity->actor.id, &entity->actor.id, 100000, 0, QA_Q2_ZERO,
              qa_q2_game_body(g, entity).origin, QA_Q2_ZERO, 23, 32, NULL);
    player->score = 0;
  }
  qa_q2_ctf_join(ctf, entity, g, team, false);
}

static void set_observer(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  if (player == NULL) qa_q2f_error("Missing shared CTF player");
  player->spectator = true;
  player->requested_spectator = true;
  player->noclip = true;
  player->god = false;
  qa_combat_trait_changes traits = {0};
  traits.has_team = true;
  traits.team = NULL;
  traits.has_can_take_damage = true;
  traits.can_take_damage = false;
  traits.has_invulnerable = true;
  traits.invulnerable = false;
  qa_gameplay_authority_set_traits(g->host->combat, entity->actor, traits);
  entity->visible = false;
  entity->server_flags = (double)((int64_t)entity->server_flags | 1);
  g->solid(g->ctx, entity, "none");
  g->show(g->ctx, entity);
  ctf->context.hooks.observer(ctf->context.hooks.ctx, entity, g);
}

void qa_q2_ctf_observer(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_drop_inventory(ctf, entity, g);
  qa_q2_ctf_player_state *state = qa_ctf_player(&ctf->context, entity->actor.id);
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  state->team = 0;
  state->ready = false;
  state->has_match_respawn_at = false;
  if (player != NULL) player->score = 0;
  set_observer(ctf, entity, g);
  qa_q2_ctf_assign_skin(ctf, entity);
  qa_q2_ctf_presentation_join_menu(ctf->presentation, entity);
}

bool qa_q2_ctf_select_spawn(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(&ctf->context, entity->actor.id);
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  if (state == NULL || state->team == 0 || player == NULL) return false;
  qa_q2_entity *spot = NULL;
  if (state->spawn_state == 0) {
    state->spawn_state++;
    char classname[32];
    snprintf(classname, sizeof classname, "info_player_team%.0f", state->team);
    size_t n = 0;
    qa_q2_entity **starts = qa_q2mp_entities_named(g, classname, &n);
    /* sorted copy by range (stable). */
    qa_q2_entity **sorted = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *sorted));
    double *ranges = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *ranges));
    for (size_t i = 0; i < n; i++) {
      sorted[i] = starts[i];
      ranges[i] = qa_q2mp_players_range(g, starts[i]);
    }
    for (size_t i = 1; i < n; i++) {
      qa_q2_entity *v = sorted[i];
      double r = ranges[i];
      size_t j = i;
      while (j > 0 && ranges[j - 1] > r) {
        sorted[j] = sorted[j - 1];
        ranges[j] = ranges[j - 1];
        j--;
      }
      sorted[j] = v;
      ranges[j] = r;
    }
    size_t candidate_count = 0;
    qa_q2_entity **candidates = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *candidates));
    for (size_t i = 0; i < n; i++) {
      if (n > 2 && (starts[i] == sorted[0] || starts[i] == sorted[1])) continue;
      candidates[candidate_count++] = starts[i];
    }
    if (candidate_count > 0) {
      double pick = floor(g->host->random(g->host->ctx) * (double)candidate_count);
      spot = candidates[(size_t)pick];
    }
    free(starts);
    free(sorted);
    free(ranges);
    free(candidates);
  }
  if (spot == NULL) spot = qa_q2mp_select_spawn(g, player->slot, "");
  *out_origin = qa_q2mp_spawn_origin(spot, g);
  *out_angles = qa_q2_game_body(g, spot).angles;
  return true;
}

void qa_q2_ctf_score_event(qa_q2_ctf *ctf, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g, double change, double means, qa_q2_entity *recipient) {
  (void)means;
  qa_ctf_score(&ctf->context, recipient->actor.id, change);
  qa_q2_ctf_flags_frag(ctf->flags, victim, attacker, g);
}

bool qa_q2_ctf_same_team(qa_q2_ctf *ctf, const qa_actor_id *one, qa_actor_id two) {
  qa_q2_ctf_player_state *first = one == NULL ? NULL : qa_q2_ctf_context_get(&ctf->context, *one);
  qa_q2_ctf_player_state *second = qa_q2_ctf_context_get(&ctf->context, two);
  return first != NULL && first->team != 0 && second != NULL && first->team == second->team;
}

void qa_q2_ctf_drop_inventory(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_grapple_module_reset(ctf->grapple, entity->actor, g);
  qa_q2_ctf_flags_drop(ctf->flags, entity, g);
  qa_q2_ctf_techs_drop(ctf->techs, entity, g, true);
}

void qa_q2_ctf_death(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_grapple_module_reset(ctf->grapple, entity->actor, g);
}

void qa_q2_ctf_disconnect(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_match_sync_ghost(ctf->match, entity->actor.id);
  qa_q2_ctf_drop_inventory(ctf, entity, g);
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(&ctf->context, entity->actor.id);
  qa_q2_ctf_ghost *ghost = state != NULL && state->has_ghost_code ? qa_q2_ctf_match_state_ghost(&ctf->context.match, state->ghost_code) : NULL;
  if (ghost != NULL) ghost->has_actor = false;
  if (ctf->context.match.election != NULL && qa_same_actor(ctf->context.match.election->target, entity->actor.id))
    qa_q2_ctf_match_state_set_election(&ctf->context.match, NULL);
  qa_q2_ctf_context_delete(&ctf->context, entity->actor.id);
  qa_q2_ctf_grapple_module_state_delete(ctf->grapple, entity->actor.id);
}

void qa_q2_ctf_before_player(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(&ctf->context, entity->actor.id);
  if (state == NULL) return;
  if (state->has_match_respawn_at && state->match_respawn_at <= qa_q2mp_now(g)) {
    state->has_match_respawn_at = false;
    spawn_player_impl(ctf, entity, g);
  } else {
    qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
    if (state->team != 0 && strcmp(ctf->context.match.phase, "game") == 0 && player != NULL && player->dead &&
        player->respawn_time < qa_q2mp_now(g))
      spawn_player_impl(ctf, entity, g);
  }
}

qa_q2mp_weapon_input qa_q2_ctf_weapon_input(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, qa_q2mp_weapon_input input) {
  input.haste = input.haste || qa_q2_ctf_techs_haste(ctf->techs, entity->actor.id, g);
  input.instant_switch = input.instant_switch || ctf->context.rules.instant_weapons;
  return input;
}

void qa_q2_ctf_after_player(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(&ctf->context, entity->actor.id);
  if (state == NULL) return;
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  if (qa_q2_ctf_grapple_module_native_enabled(ctf->grapple) && state->team != 0 && player != NULL && player->use_q2_weapons &&
      !player->dead && qa_q2mp_inv_count(g, entity->actor.id, "q2:weapon_grapple") == 0) {
    qa_inventory_entry entry = {0};
    entry.item = "q2:weapon_grapple"; entry.count = 1; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, entity->actor, entry, NULL, NULL);
  }
  qa_q2_ctf_grapple_module_player_frame(ctf->grapple, entity, g, "weapon-slot");
  qa_q2_ctf_techs_regenerate(ctf->techs, entity, g);
  qa_q2_ctf_flags_effects(ctf->flags, entity, g);
  qa_q2_ctf_match_sync_ghost(ctf->match, entity->actor.id);
  char *status = qa_q2_ctf_match_status(ctf->match, g);
  qa_q2_ctf_presentation_hud(ctf->presentation, entity, g, status);
  free(status);
}

void qa_q2_ctf_after_player_frames(qa_q2_ctf *ctf, qa_q2_game_services *g) {
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) qa_q2_ctf_match_sync_ghost(ctf->match, players[i]);
}

bool qa_q2_ctf_check_rules(qa_q2_ctf *ctf, qa_q2_game_services *g) { return qa_q2_ctf_match_check_rules(ctf->match, g); }

bool qa_q2_ctf_match_setup_phase(qa_q2_ctf *ctf) {
  return strcmp(ctf->context.match.phase, "setup") == 0 || strcmp(ctf->context.match.phase, "pregame") == 0;
}
bool qa_q2_ctf_pickups_allowed(qa_q2_ctf *ctf) { return !qa_q2_ctf_match_setup_phase(ctf); }

void qa_q2_ctf_reset_players(qa_q2_ctf *ctf, qa_q2_game_services *g) {
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_entity *entity = qa_q2_game_entity_of(g, &players[i]);
    if (entity != NULL && qa_q2_ctf_context_get(&ctf->context, players[i]) != NULL) qa_q2_ctf_observer(ctf, entity, g);
  }
  qa_q2_ctf_techs_reset(ctf->techs, g);
  qa_q2_ctf_flags_reset_all(ctf->flags, g);
  qa_q2_callback_definitions callbacks = qa_q2_item_module_callbacks(ctf->context.hooks.items);
  qa_q2_think respawn = {0};
  bool has_respawn = false;
  for (size_t i = 0; i < callbacks.think_count; i++)
    if (strcmp(callbacks.think[i].name, "q2_items_respawn") == 0) { respawn = callbacks.think[i].cb; has_respawn = true; }
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  qa_q2_entity **copy = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *copy));
  for (size_t i = 0; i < n; i++) copy[i] = all[i];
  for (size_t i = 0; i < n; i++) {
    qa_q2_entity *entity = copy[i];
    if (has_respawn && strcmp(entity->solid, "none") == 0 && qa_q2_think_eq(entity->think, respawn) &&
        entity->has_next_think && entity->next_think >= qa_q2mp_now(g)) {
      g->cancel(g->ctx, entity);
      respawn.fn(respawn.ctx, entity, g);
    }
  }
  free(copy);
}

static char *join_args(const char *const *args, size_t count) {
  size_t len = 1;
  for (size_t i = 0; i < count; i++) len += strlen(args[i]) + 1;
  char *out = qa_q2mp_xalloc(malloc(len));
  out[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i > 0) strcat(out, " ");
    strcat(out, args[i]);
  }
  return out;
}

static bool js_number(const char *text, double *out) {
  const char *p = text;
  while (isspace((unsigned char)*p)) p++;
  if (*p == '\0') return false;
  char *end = NULL;
  double value = strtod(p, &end);
  if (end == p) return false;
  while (isspace((unsigned char)*end)) end++;
  if (*end != '\0') return false;
  *out = value;
  return true;
}

static bool digits_only(const char *text) {
  if (text[0] == '\0') return false;
  for (const char *p = text; *p; p++)
    if (!isdigit((unsigned char)*p)) return false;
  return true;
}

bool qa_q2_ctf_command(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, const char *command, const char *const *args, size_t arg_count) {
  if (strcmp(command, "hook") == 0 || strcmp(command, "+hook") == 0 || strcmp(command, "unhook") == 0 || strcmp(command, "-hook") == 0) {
    qa_q2_ctf_grapple_module_command(ctf->grapple, entity, strcmp(command, "hook") == 0 || strcmp(command, "+hook") == 0);
    return true;
  }
  if (qa_q2_ctf_context_get(&ctf->context, entity->actor.id) == NULL) return false;
  qa_q2_ctf_player_state *state = qa_ctf_player(&ctf->context, entity->actor.id);
  qa_q2mp_player_state *player = ctf->context.hooks.player(ctf->context.hooks.ctx, entity->actor.id);
  char *words = join_args(args, arg_count);
  char lowered[256];
  lower_into(lowered, sizeof lowered, command);
  bool handled = true;
  if (strcmp(lowered, "ctf-menu") == 0) {
    if (arg_count != 1) handled = false;
    else {
      const char *action = args[0];
      static const char *const actions[] = {"join-red", "join-blue", "observer", "chase", "credits", "match", "ready", "notready", "admin-settings", "admin-start", "admin-cancel", "close"};
      bool known = false;
      for (size_t i = 0; i < sizeof actions / sizeof *actions; i++)
        if (strcmp(action, actions[i]) == 0) known = true;
      if (!known) handled = false;
      else qa_q2_ctf_menu_action(ctf, entity, g, action);
    }
  } else if (strcmp(lowered, "ctf-settings") == 0) {
    if (arg_count != 2 || !state->admin) handled = false;
    else {
      const char *key = args[0];
      const char *value = args[1];
      qa_q2_ctf_rules *rules = &ctf->context.rules;
      int64_t flags = (int64_t)g->options.deathmatch_flags;
      qa_q2_ctf_admin_settings settings = {rules->match_minutes, rules->setup_minutes, rules->start_seconds,
          (flags & 4) != 0, (flags & 16) != 0, (flags & 16384) != 0, rules->instant_weapons, rules->match_lock};
      if (strcmp(key, "matchMinutes") == 0 || strcmp(key, "setupMinutes") == 0 || strcmp(key, "startSeconds") == 0) {
        double number;
        bool blank = true;
        for (const char *p = value; *p; p++)
          if (!isspace((unsigned char)*p)) blank = false;
        if (blank || !js_number(value, &number) || !isfinite(number) || number <= 0) {
          handled = false;
        } else {
          if (strcmp(key, "matchMinutes") == 0) settings.match_minutes = number;
          else if (strcmp(key, "setupMinutes") == 0) settings.setup_minutes = number;
          else settings.start_seconds = number;
          qa_q2_ctf_match_configure(ctf->match, entity, g, settings);
          qa_q2_ctf_match_settings_menu(ctf->match, entity, g);
        }
      } else if (strcmp(key, "weaponsStay") == 0 || strcmp(key, "instantItems") == 0 || strcmp(key, "quadDrop") == 0 ||
                 strcmp(key, "instantWeapons") == 0 || strcmp(key, "matchLock") == 0) {
        if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0 && strcmp(value, "1") != 0 && strcmp(value, "0") != 0) {
          handled = false;
        } else {
          bool boolean = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
          if (strcmp(key, "weaponsStay") == 0) settings.weapons_stay = boolean;
          else if (strcmp(key, "instantItems") == 0) settings.instant_items = boolean;
          else if (strcmp(key, "quadDrop") == 0) settings.quad_drop = boolean;
          else if (strcmp(key, "instantWeapons") == 0) settings.instant_weapons = boolean;
          else settings.match_lock = boolean;
          qa_q2_ctf_match_configure(ctf->match, entity, g, settings);
          qa_q2_ctf_match_settings_menu(ctf->match, entity, g);
        }
      } else {
        handled = false;
      }
    }
  } else if (strcmp(lowered, "team") == 0) {
    qa_q2_ctf_team_command(ctf, entity, g, words);
  } else if (strcmp(lowered, "observer") == 0) {
    qa_q2_ctf_observer(ctf, entity, g);
  } else if (strcmp(lowered, "id") == 0) {
    state->id_view = !state->id_view;
    qa_ctf_print(g, &entity->actor.id, NULL, "Disabling player identification %s.\n", state->id_view ? "off" : "on");
  } else if (strcmp(lowered, "say_team") == 0) {
    qa_q2_ctf_presentation_say_team(ctf->presentation, entity, g, words);
  } else if (strcmp(lowered, "score") == 0 || strcmp(lowered, "help") == 0) {
    if (player != NULL) {
      player->show_scores = !player->show_scores;
      if (player->show_scores) qa_q2_ctf_presentation_scoreboard(ctf->presentation, entity, g);
    }
  } else if (strcmp(lowered, "inven") == 0) {
    if (state->team != 0) handled = false;
    else qa_q2_ctf_presentation_join_menu(ctf->presentation, entity);
  } else if (strcmp(lowered, "ready") == 0) {
    qa_q2_ctf_match_ready(ctf->match, entity, g, true);
  } else if (strcmp(lowered, "notready") == 0) {
    qa_q2_ctf_match_ready(ctf->match, entity, g, false);
  } else if (strcmp(lowered, "yes") == 0) {
    qa_q2_ctf_match_vote(ctf->match, entity, g, true);
  } else if (strcmp(lowered, "no") == 0) {
    qa_q2_ctf_match_vote(ctf->match, entity, g, false);
  } else if (strcmp(lowered, "ghost") == 0) {
    if (digits_only(words)) qa_q2_ctf_match_restore_ghost(ctf->match, entity, g, strtod(words, NULL));
  } else if (strcmp(lowered, "admin") == 0) {
    qa_q2_ctf_match_admin(ctf->match, entity, g, words);
  } else if (strcmp(lowered, "stats") == 0) {
    qa_q2_ctf_presentation_stats(ctf->presentation, entity, g);
  } else if (strcmp(lowered, "warp") == 0) {
    qa_q2_ctf_match_warp(ctf->match, entity, g, words);
  } else if (strcmp(lowered, "boot") == 0) {
    qa_q2_ctf_match_boot(ctf->match, entity, g, words);
  } else if (strcmp(lowered, "playerlist") == 0) {
    size_t pn = 0;
    const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
    char *lines = qa_q2mp_xalloc(malloc(pn * 256 + 1));
    lines[0] = '\0';
    size_t used = 0;
    for (size_t i = 0; i < pn; i++) {
      qa_q2mp_player_state *common = ctf->context.hooks.player(ctf->context.hooks.ctx, players[i]);
      qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(&ctf->context, players[i]);
      if (common == NULL || member == NULL) continue;
      double elapsed = qa_q2mp_now(g) - common->entered_at;
      char slot[32], minutes[32], seconds[32], ping[32], score[32];
      qa_q2mp_format_number(slot, sizeof slot, common->slot + 1);
      qa_q2mp_format_number(minutes, sizeof minutes, floor(elapsed / 60));
      qa_q2mp_format_number(seconds, sizeof seconds, fmod(trunc(elapsed), 60));
      qa_q2mp_format_number(ping, sizeof ping, common->ping);
      qa_q2mp_format_number(score, sizeof score, common->score);
      used += (size_t)snprintf(lines + used, pn * 256 + 1 - used, "%s %s:%s %s %s %s%s%s\n", slot, minutes, seconds, ping, score,
                             common->name, member->team == 0 ? " (spectator)" : "", member->admin ? " (admin)" : "");
    }
    if (strlen(lines) > 1399) lines[1399] = '\0';
    qa_ctf_print(g, &entity->actor.id, NULL, "%s", lines);
    free(lines);
  } else if (strcmp(lowered, "drop") == 0) {
    char lowered_words[512];
    lower_into(lowered_words, sizeof lowered_words, words);
    if (strcmp(lowered_words, "tech") != 0) handled = false;
    else if (qa_ctf_tech(g, entity->actor.id) != NULL) qa_q2_ctf_techs_drop(ctf->techs, entity, g, false);
  } else {
    handled = false;
  }
  free(words);
  return handled;
}

void qa_q2_ctf_menu_action(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *g, const char *action) {
  if (strcmp(action, "join-red") == 0) qa_q2_ctf_join(ctf, entity, g, 1, false);
  else if (strcmp(action, "join-blue") == 0) qa_q2_ctf_join(ctf, entity, g, 2, false);
  else if (strcmp(action, "observer") == 0) qa_q2_ctf_observer(ctf, entity, g);
  else if (strcmp(action, "chase") == 0) ctf->context.hooks.chase(ctf->context.hooks.ctx, entity->actor.id);
  else if (strcmp(action, "match") == 0) qa_q2_ctf_match_begin_election(ctf->match, entity, g, "match", "");
  else if (strcmp(action, "ready") == 0) qa_q2_ctf_match_ready(ctf->match, entity, g, true);
  else if (strcmp(action, "notready") == 0) qa_q2_ctf_match_ready(ctf->match, entity, g, false);
  else if (strcmp(action, "admin-start") == 0) {
    if (qa_ctf_player(&ctf->context, entity->actor.id)->admin) {
      if (strcmp(ctf->context.match.phase, "setup") == 0) {
        ctf->context.match.phase = "pregame";
        ctf->context.match.match_time = qa_q2mp_now(g) + ctf->context.rules.start_seconds;
      } else {
        qa_q2_ctf_match_setup(ctf->match, g);
      }
    }
  } else if (strcmp(action, "admin-cancel") == 0) {
    if (qa_ctf_player(&ctf->context, entity->actor.id)->admin) {
      ctf->context.match.phase = "none";
      qa_q2_ctf_reset_players(ctf, g);
    }
  } else if (strcmp(action, "admin-settings") == 0) {
    qa_q2_ctf_match_settings_menu(ctf->match, entity, g);
  } else if (strcmp(action, "credits") == 0) {
    char *labels[3] = {qa_q2mp_dup("Design and code: David 'Zoid' Kirsch"), qa_q2mp_dup("id Software Quake II"), qa_q2mp_dup("Close")};
    qa_q2_ctf_menu_entry entries[3] = {{labels[0], NULL}, {labels[1], NULL}, {labels[2], "close"}};
    qa_q2_ctf_event event = {0};
    event.kind = "menu";
    event.actor = entity->actor.id;
    event.title = "ThreeWave CTF credits";
    event.entries = entries;
    event.entry_count = 3;
    ctf->context.hooks.emit(ctf->context.hooks.ctx, event);
    for (int i = 0; i < 3; i++) free(labels[i]);
  } else if (strcmp(action, "close") == 0) {
    qa_q2_ctf_event event = {0};
    event.kind = "menu";
    event.actor = entity->actor.id;
    event.title = "";
    event.entries = NULL;
    event.entry_count = 0;
    ctf->context.hooks.emit(ctf->context.hooks.ctx, event);
  }
}

void qa_q2_ctf_capture(qa_q2_ctf *ctf, qa_q2_ctf_checkpoint *out) {
  qa_capture_q2_ctf(&ctf->context, qa_q2_ctf_grapple_module_equipment(ctf->grapple), out);
}

void qa_q2_ctf_restore(qa_q2_ctf *ctf, const qa_q2_ctf_checkpoint *checkpoint, qa_q2_game_services *g) {
  qa_restore_q2_ctf(&ctf->context, g, checkpoint, qa_q2_ctf_grapple_module_equipment(ctf->grapple));
}

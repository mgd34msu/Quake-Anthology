/* Port of src/content/q1/missionpacks/world/rogue-runes.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_runes.h"
#include "qa/foundation/support.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/persistence/value.h"

static int32_t bits(double value) { return qa_js_to_int32(value); }
static double random_of(qa_q1_services *game) { return game->host.random(game->host.ctx); }

/* TS this.players.get(owner) ?? insert, keyed by the owned actor. */
static qa_q1mp_rune_state *upsert(qa_q1mp_rogue_runes *runes, qa_owned_actor owner) {
  for (size_t i = 0; i < runes->player_count; i++)
    if (qa_same_owned_actor(runes->players[i].actor, owner)) return &runes->players[i];
  if (runes->player_count == runes->player_capacity) {
    const size_t next = runes->player_capacity != 0 ? runes->player_capacity * 2 : 8;
    qa_q1mp_rune_state *grown = realloc(runes->players, next * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    runes->players = grown; runes->player_capacity = next;
  }
  qa_q1mp_rune_state *state = &runes->players[runes->player_count++];
  memset(state, 0, sizeof *state);
  state->actor = owner;
  return state;
}
static qa_q1mp_rune_state *state_of(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(runes->game->host.actors, actor, &owner))
    qa_range_error("Rune operation needs a live actor");
  return upsert(runes, owner);
}
static qa_vec3 spawn_point(qa_q1mp_rogue_runes *runes) {
  qa_q1_services *game = runes->game;
  qa_q1_actor *world = game->world;
  if (world == NULL) qa_range_error("Rune spawn requires worldspawn");
  size_t count;
  qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  size_t points = 0;
  for (size_t i = 0; i < count; i++)
    if (qa_q1mp_streq(entities[i]->classname, "info_player_deathmatch") && qa_q1_live(game, entities[i])) entities[points++] = entities[i];
  if (points == 0) { free(entities); qa_range_error("Rogue runes require an info_player_deathmatch spawn"); }
  qa_actor_id spot;
  qa_q1_actor *previous = qa_q1_actor_reference_get(world, "rogue:rune_spawn_spot", &spot) ? qa_q1_get_entity(game, &spot) : NULL;
  size_t index = points; /* TS indexOf: -1 when absent; (index + 1) % length is then 0 */
  for (size_t i = 0; i < points && index == points; i++) if (entities[i] == previous) index = i;
  qa_q1_actor *next = entities[index == points ? 0 : (index + 1) % points];
  free(entities);
  if (!qa_q1_actor_reference_set(world, "rogue:rune_spawn_spot", &next->actor.id)) qa_range_error("Out of memory");
  return qa_q1_get_body(game, next).origin;
}
static qa_vec3 rune_velocity(qa_q1mp_rogue_runes *runes) {
  qa_vec3 velocity;
  velocity.x = -300 + random_of(runes->game) * 600;
  velocity.y = -300 + random_of(runes->game) * 600;
  velocity.z = 300;
  return velocity;
}
static qa_q1_actor *spawn_rune(qa_q1mp_rogue_runes *runes, int32_t rune, qa_vec3 origin) {
  qa_q1_services *game = runes->game;
  qa_q1_actor *item = qa_q1_create(game, "rogue_rune", NULL, NULL);
  qa_q1mp_world_number(item, "rune", rune);
  item->movement_flags = 256; item->solid = "trigger"; item->movement = "toss";
  char model[32];
  (void)snprintf(model, sizeof model, "progs/end%d.mdl", (rune & 1) != 0 ? 1 : (rune & 2) != 0 ? 2 : (rune & 4) != 0 ? 3 : 4);
  qa_q1mp_set_string(&item->model, model);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = rune_velocity(runes);
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
  qa_q1_set_body(game, item, &patch);
  item->touch = qa_q1_callback_registry_touch(game->named, item, "rogue:rune_touch");
  qa_q1mp_world_later(game, item, 120, "rogue:rune_respawn");
  qa_q1_link(game, item);
  return item;
}
static void noise(qa_q1mp_rogue_runes *runes, qa_actor_id actor, int rune) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(runes->game->host.actors, actor, &owner)) return;
  char path[32];
  (void)snprintf(path, sizeof path, "runes/end%d.wav", rune);
  qa_q1_sound(runes->game, owner.id, path, "item", 1, 1);
}
static void emit_message(qa_q1_services *game, qa_actor_id player, const char *text) {
  qa_q1_event event = {0};
  event.kind = "message"; event.has_player = true; event.player = player; event.text = text; event.center = true;
  qa_q1_emit(game, event);
}

void qa_q1mp_runes_frame(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1_services *game = runes->game;
  qa_q1_actor *world = game->world;
  const double gamecfg = runes->gamecfg != NULL ? runes->gamecfg(runes->gamecfg_ctx) : 0;
  if (game->options.deathmatch != 0 && (bits(gamecfg) & 1) != 0 && world != NULL && qa_q1_actor_number(world, "rogue:runes_spawned") == 0) {
    qa_q1mp_world_number(world, "rogue:runes_spawned", 1);
    qa_q1mp_world_later(game, qa_q1_create(game, "rogue_rune_spawner", NULL, NULL), 0.1, "rogue:rune_spawn");
  }
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if ((bits(state->rune) & 8) == 0 || state->regeneration >= game->time || qa_q1_health(game, actor) >= 100) return;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner)) return;
  qa_q1_sound(game, owner.id, "runes/end4.wav", "item", 1, 1);
  qa_gameplay_authority_set_health(game->host.combat, owner, qa_js_min(100, qa_q1_health(game, actor) + 5));
  state->regeneration = game->time + 1;
}
void qa_q1mp_runes_drop(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  const qa_body_state_result body = qa_shared_body_table_read(runes->game->host.bodies, actor);
  if (!body.has_value) return;
  static const int32_t RUNES[] = {1, 2, 4, 8};
  for (size_t i = 0; i < 4; i++) if ((bits(state->rune) & RUNES[i]) != 0) (void)spawn_rune(runes, RUNES[i], body.value.origin);
  state->rune = 0;
}
double qa_q1mp_runes_damage(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount) {
  qa_owned_actor owner;
  /* A projectile can outlive its owner. */
  if (!qa_session_actor_registry_resolve_owned(runes->game->host.actors, actor, &owner)) return amount;
  return (bits(state_of(runes, actor)->rune) & 2) != 0 ? qa_q1mp_fround(amount * 2) : amount;
}
double qa_q1mp_runes_resistance(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double amount) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if ((bits(state->rune) & 1) == 0) return amount;
  if (state->earth_noise < runes->game->time) { noise(runes, actor, 1); state->earth_noise = runes->game->time + 1; }
  return qa_q1mp_fround(amount / 2);
}
void qa_q1mp_runes_attack_sound(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if ((bits(state->rune) & 2) != 0 && state->black_noise < runes->game->time) { noise(runes, actor, 2); state->black_noise = runes->game->time + 1; }
}
double qa_q1mp_runes_attack_delay(qa_q1mp_rogue_runes *runes, qa_actor_id actor, double delay) {
  qa_q1mp_rune_state *state = state_of(runes, actor);
  if ((bits(state->rune) & 4) == 0) return delay;
  if (state->hell_noise < runes->game->time) { noise(runes, actor, 3); state->hell_noise = runes->game->time + 1; }
  return qa_q1mp_fround(qa_q1mp_fround(delay * 2) / 3);
}
bool qa_q1mp_runes_has_regeneration(qa_q1mp_rogue_runes *runes, qa_actor_id actor) {
  return (bits(state_of(runes, actor)->rune) & 8) != 0;
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1mp_rogue_runes *runes = ctx;
  for (size_t i = 0; i < runes->player_count; i++) if (qa_same_owned_actor(runes->players[i].actor, actor)) {
    memmove(&runes->players[i], &runes->players[i + 1], (runes->player_count - i - 1) * sizeof *runes->players);
    runes->player_count--;
    return;
  }
}
static bool capture(void *ctx, uint8_t **out, size_t *out_len) {
  qa_q1mp_rogue_runes *runes = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < runes->player_count; i++) {
    const qa_q1mp_rune_state *state = &runes->players[i];
    qa_save_value *entry = qa_save_value_object(), *actor = qa_save_value_object();
    bool ok = entry != NULL && actor != NULL &&
              qa_save_value_set(actor, "slot", qa_save_value_number((double)state->actor.id.slot)) &&
              qa_save_value_set(actor, "generation", qa_save_value_number((double)state->actor.id.generation));
    if (ok) ok = qa_save_value_set(entry, "actor", actor); else qa_save_value_free(actor);
    ok = ok && qa_save_value_set(entry, "rune", qa_save_value_number(state->rune)) &&
         qa_save_value_set(entry, "notice", qa_save_value_number(state->notice)) &&
         qa_save_value_set(entry, "earthNoise", qa_save_value_number(state->earth_noise)) &&
         qa_save_value_set(entry, "blackNoise", qa_save_value_number(state->black_noise)) &&
         qa_save_value_set(entry, "hellNoise", qa_save_value_number(state->hell_noise)) &&
         qa_save_value_set(entry, "regeneration", qa_save_value_number(state->regeneration)) &&
         qa_save_value_push(list, entry);
    if (!ok) { qa_save_value_free(entry); qa_save_value_free(list); return false; }
  }
  *out = qa_encode_checkpoint_value(list, out_len);
  qa_save_value_free(list);
  return *out != NULL;
}
static double read_field(const qa_save_reader *value, const char *name, bool integer) {
  qa_save_reader field;
  if (!qa_save_reader_field(value, name, &field)) qa_range_error("Out of memory");
  const double result = integer ? qa_save_reader_integer_min(&field, 0) : qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return result;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore(void *ctx, const uint8_t *bytes, size_t len) {
  qa_q1mp_rogue_runes *runes = ctx;
  runes->player_count = 0;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, len);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  if (decoded == NULL) qa_range_error("Out of memory");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "rogue:runes")) qa_range_error("Out of memory");
  const size_t count = qa_save_reader_list_count(&reader);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value, ref;
    if (!qa_save_reader_item(&reader, i, &value) || !qa_save_reader_field(&value, "actor", &ref)) qa_range_error("Out of memory");
    qa_saved_actor_id saved;
    saved.slot = read_field(&ref, "slot", true);
    saved.generation = read_field(&ref, "generation", true);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(runes->game->host.actors, saved, &actor)) qa_save_reader_fail(&ref, "missing rune carrier");
    qa_q1mp_rune_state state;
    state.actor = actor;
    state.rune = read_field(&value, "rune", true);
    state.notice = read_field(&value, "notice", false);
    state.earth_noise = read_field(&value, "earthNoise", false);
    state.black_noise = read_field(&value, "blackNoise", false);
    state.hell_noise = read_field(&value, "hellNoise", false);
    state.regeneration = read_field(&value, "regeneration", false);
    *upsert(runes, actor) = state;
    qa_save_reader_destroy(&ref); qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void respawn_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1mp_rogue_runes *runes = ctx;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = spawn_point(runes);
  patch.has_velocity = true; patch.velocity = rune_velocity(runes);
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  qa_q1mp_world_later(game, entity, 120, "rogue:rune_respawn");
}
static void touch_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                     const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1mp_rogue_runes *runes = ctx;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_q1mp_rune_state *state = state_of(runes, other);
  if (state->rune != 0) {
    if (state->notice < game->time) emit_message(game, other, "$qc_already_have_rune");
    state->notice = game->time + 5;
    return;
  }
  state->rune = qa_js_bit_or(state->rune, qa_q1_actor_number(entity, "rune"));
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner)) return;
  qa_q1_sound(game, owner.id, "weapons/pkup.wav", "item", 1, 1);
  const int32_t rune = bits(state->rune);
  emit_message(game, other, (rune & 1) != 0 ? "$qc_rune_resistance" : (rune & 2) != 0 ? "$qc_rune_strength"
                                : (rune & 4) != 0 ? "$qc_rune_haste" : "$qc_rune_regeneration");
  qa_q1_remove(game, entity);
}
static void spawn_all_cb(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1mp_rogue_runes *runes = ctx;
  qa_q1_remove(game, entity);
  static const int32_t RUNES[] = {1, 2, 4, 8};
  for (size_t i = 0; i < 4; i++) (void)spawn_rune(runes, RUNES[i], spawn_point(runes));
}
static bool after_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                       const qa_combat_state *attacker, qa_damage_preparation *out) {
  (void)target; (void)attacker;
  qa_q1mp_rogue_runes *runes = ctx;
  out->is_continue = true;
  out->amount = runes->game->options.deathmatch != 0 && request.attack.attacker != NULL
      ? qa_q1mp_runes_damage(runes, *request.attack.attacker, damage) : damage;
  return true;
}
static double after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target, const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  qa_q1mp_rogue_runes *runes = ctx;
  return runes->game->options.deathmatch != 0 ? qa_q1mp_runes_resistance(runes, request.target, take) : take;
}
static void before_fire(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game;
  qa_q1mp_runes_attack_sound(ctx, player->actor.id);
}
static double attack_delay(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double delay) {
  (void)game;
  static const char *const RATED[] = {"axe", "shotgun", "supershotgun", "grenadelauncher", "rocketlauncher",
                                      "rogue:multi-grenade", "rogue:multi-rocket", "rogue:plasma"};
  for (size_t i = 0; i < sizeof RATED / sizeof RATED[0]; i++)
    if (qa_q1mp_streq(player->weapon, RATED[i])) return qa_q1mp_runes_attack_delay(ctx, player->actor.id, delay);
  return delay;
}
void qa_q1mp_rogue_runes_init(qa_q1mp_rogue_runes *runes, qa_q1_services *game, double (*gamecfg)(void *ctx), void *gamecfg_ctx) {
  memset(runes, 0, sizeof *runes);
  runes->game = game; runes->gamecfg = gamecfg; runes->gamecfg_ctx = gamecfg_ctx;
  runes->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, runes);
  runes->state_extension = (qa_q1_state_extension){.ctx = runes, .id = "rogue:runes", .capture = capture, .restore = restore};
  qa_q1_register_state_extension(game, &runes->state_extension);
  qa_q1_callback_registry_register(game->named, "rogue:rune_respawn", (qa_q1_callback_handlers){.ctx = runes, .action = respawn_cb});
  qa_q1_callback_registry_register(game->named, "rogue:rune_touch", (qa_q1_callback_handlers){.ctx = runes, .touch = touch_cb});
  qa_q1_callback_registry_register(game->named, "rogue:rune_spawn", (qa_q1_callback_handlers){.ctx = runes, .action = spawn_all_cb});
  qa_q1_damage_source_effects effects = {0};
  effects.ctx = runes; effects.after_quad = after_quad; effects.after_armor = after_armor;
  qa_q1_register_damage_source_effects(game, "rogue:runes", effects);
  runes->weapon_rules = (qa_q1_weapon_rules){.ctx = runes, .id = "rogue:runes", .before_fire = before_fire, .attack_delay = attack_delay};
  qa_q1_register_weapon_rules(game, &runes->weapon_rules);
}
void qa_q1mp_rogue_runes_destroy(qa_q1mp_rogue_runes *runes) {
  qa_session_actor_registry_off_release(runes->game->host.actors, runes->release_subscription);
  free(runes->players);
  runes->players = NULL; runes->player_count = runes->player_capacity = 0;
}

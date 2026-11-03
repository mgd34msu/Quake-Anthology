/* Port of src/app/bootstrap/simulation/q3/runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_policy.h"
#include "qa/core/game_numeric.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/info_string.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/weapon.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
/* checkpoint value helpers are in persistence/value.h */
#include "qa/persistence/save_image.h"

static char *dup_str(const char *text) {
  size_t length = strlen(text ? text : "") + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text ? text : "", length);
  return copy;
}
void qa_app_q3_spawn_report_destroy(qa_app_q3_spawn_report *report) {
  if (!report) return;
  for (size_t i = 0; i < report->world_variable_count; i++) {
    free((void *)report->world_variables[i].key); free((void *)report->world_variables[i].value);
  }
  for (size_t i = 0; i < report->outcome_count; i++) free((void *)report->outcomes[i].classname);
  free(report->world_variables); free(report->outcomes);
  memset(report, 0, sizeof *report);
}
static qa_save_reader c_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
/* ---- Bootstrap construction: TS itemLifecycle/createCommands/load ---- */
static qa_app_q3_item_lifecycle_host *item_host(void *ctx) { return ctx; }
static qa_q3_base_server_trace_result ilhost_trace(void *ctx, qa_q3_base_server_trace_query query) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.trace(host->services.ctx, query);
}
static qa_q3_base_actor_trace_result ilhost_trace_actor(void *ctx, qa_q3_base_actor_trace_query query) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.trace_actor(host->services.ctx, query);
}
static const char *ilhost_handicap(void *ctx, double client) {
  /* TS: number => this.userinfo(number, "handicap"). */
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  char *info = host->services.get_userinfo ? host->services.get_userinfo(host->services.ctx, client) : NULL;
  char *value = qa_q3_ta_client_info_value(info ? info : "", "handicap");
  free(info);
  free(host->handicap);
  host->handicap = value;
  return host->handicap ? host->handicap : "";
}
static double ilhost_team_pickup(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *player) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.team_pickup(host->services.ctx, item, player);
}
static void ilhost_use_targets(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *activator) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  host->services.use_targets(host->services.ctx, item, activator);
}
static double ilhost_sound_index(void *ctx, const char *path) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.sound_index(host->services.ctx, path);
}
static double ilhost_rand_value(void *ctx) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.rand_value(host->services.ctx);
}
static double ilhost_random(void *ctx) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  return host->services.random(host->services.ctx);
}
static void ilhost_log(void *ctx, const char *message) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  host->services.log(host->services.ctx, message);
}
static void ilhost_warn(void *ctx, const char *message) {
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  host->services.warn(host->services.ctx, message);
}
static qa_q3_source_pickup_preview ilhost_preview(void *ctx, qa_q3_source_pickup_descriptor descriptor) {
  /* TS: item => host.previewPickup?.(item) ?? { kind: "native" }. */
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  qa_q3_source_pickup_preview preview; memset(&preview, 0, sizeof preview);
  preview.kind = QA_Q3_SOURCE_PREVIEW_NATIVE;
  if (host->services.preview_pickup
      && !host->services.preview_pickup(host->services.ctx, &descriptor, &preview)) {
    memset(&preview, 0, sizeof preview);
    preview.kind = QA_Q3_SOURCE_PREVIEW_NATIVE;
  }
  return preview;
}
static qa_q3_source_pickup_admission ilhost_admit(void *ctx, qa_q3_source_pickup_descriptor descriptor) {
  /* TS: item => host.admitPickup?.(item) ?? { kind: "native" }. */
  qa_app_q3_item_lifecycle_host *host = item_host(ctx);
  qa_q3_source_pickup_admission admission; memset(&admission, 0, sizeof admission);
  admission.kind = QA_Q3_SOURCE_PICKUP_NATIVE;
  if (host->services.admit_pickup
      && !host->services.admit_pickup(host->services.ctx, &descriptor, &admission)) {
    memset(&admission, 0, sizeof admission);
    admission.kind = QA_Q3_SOURCE_PICKUP_NATIVE;
  }
  return admission;
}
static void ilhost_touch_cb(void *ctx, qa_q3_game_entity *entity, qa_q3_damage_participant other,
    qa_touch_contact contact) {
  qa_q3_touch_item(entity, other, contact, qa_app_q3_item_lifecycle_host_context(item_host(ctx)));
}
static void ilhost_respawn_cb(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_respawn_item(entity, qa_app_q3_item_lifecycle_host_context(item_host(ctx)));
}
static qa_q3_entity_callback ilhost_callback(const void *fn, void *ctx) {
  qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb;
}
qa_q3_item_lifecycle_context qa_app_q3_item_lifecycle_host_context(qa_app_q3_item_lifecycle_host *host) {
  if (host->services.settings_integer) {
    void *ctx = host->services.ctx;
    host->context.game_type = host->services.settings_integer(ctx, "g_gametype");
    host->context.weapon_respawn_seconds = host->services.settings_integer(ctx, "g_weaponrespawn");
    host->context.team_weapon_respawn_seconds = host->services.settings_integer(ctx, "g_weaponTeamRespawn");
  }
  return host->context;
}
void qa_app_q3_item_lifecycle_host_init(qa_app_q3_item_lifecycle_host *host,
    qa_q3_entity_pool *entities, qa_q3_base_product product,
    qa_app_q3_item_lifecycle_services services) {
  memset(host, 0, sizeof *host);
  host->services = services;
  qa_q3_item_lifecycle_context *context = &host->context;
  context->ctx = host;
  qa_q3_item_registry_init(&context->registry, product); /* TS new ItemRegistry(options.product) */
  context->entities = entities;
  context->product = product;
  context->has_callbacks = true;
  qa_q3_entity_touch_fn touch = ilhost_touch_cb;
  qa_q3_entity_think_fn respawn = ilhost_respawn_cb;
  context->touch_callback = qa_q3_callback_family_register(&entities->callbacks.touch,
      "q3.item.touch", ilhost_callback(&touch, host));
  context->respawn_callback = qa_q3_callback_family_register(&entities->callbacks.think,
      "q3.item.respawn", ilhost_callback(&respawn, host));
  context->has_original_pickups = services.has_original_pickups;
  context->original_pickups = services.original_pickups;
  context->preview_pickup = ilhost_preview;
  context->admit_pickup = ilhost_admit;
  context->trace = ilhost_trace;
  context->trace_actor = ilhost_trace_actor;
  context->handicap_for_client = ilhost_handicap;
  context->team_pickup = ilhost_team_pickup;
  context->use_targets = ilhost_use_targets;
  context->sound_index = ilhost_sound_index;
  context->rand_value = ilhost_rand_value;
  context->random = ilhost_random;
  context->log = ilhost_log;
  context->warn = ilhost_warn;
  (void)qa_app_q3_item_lifecycle_host_context(host);
  qa_q3_bind_item_save_callbacks(host->context);
}
void qa_app_q3_item_lifecycle_host_destroy(qa_app_q3_item_lifecycle_host *host) {
  if (!host) return;
  qa_q3_item_registry_destroy(&host->context.registry);
  free(host->handicap);
  memset(host, 0, sizeof *host);
}

typedef struct spawn_disabled { qa_cvar_registry *cvars; const char *cvar_name; } spawn_disabled;
static bool spawn_is_disabled(void *ctx) {
  /* TS: () => gameAtoi(this.engineCvar(`disable_${item.className}`)) !== 0. */
  spawn_disabled *check = ctx;
  if (!check->cvars) return false;
  char *value = qa_cvar_registry_variable_string(check->cvars, check->cvar_name);
  bool disabled = value && qa_game_atoi(value) != 0;
  free(value);
  return disabled;
}
static void spawn_item_hook(void *ctx, qa_q3_game_entity *entity,
    const qa_q3_base_item_definition *item, const qa_q3_spawn_variables *variables) {
  qa_app_q3_spawn_item_host *host = ctx;
  const char *classname = item && item->class_name ? item->class_name : "";
  size_t length = strlen("disable_") + strlen(classname) + 1;
  char *cvar_name = malloc(length);
  if (!cvar_name) qa_range_error("Out of memory");
  (void)snprintf(cvar_name, length, "disable_%s", classname);
  spawn_disabled check = {host->cvars, cvar_name};
  qa_q3_spawn_item(entity, item, variables, spawn_is_disabled, &check,
      qa_app_q3_item_lifecycle_host_context(host->items));
  free(cvar_name);
}
void qa_app_q3_wire_spawn_item(qa_q3_spawn_context *spawn, qa_app_q3_spawn_item_host *host) {
  spawn->ctx = host;
  spawn->spawn_item = spawn_item_hook;
}

qa_q3_ta_game_command_host qa_app_q3_build_game_command_host(qa_app_q3_game_command_host_parts parts) {
  qa_q3_ta_game_command_host host; memset(&host, 0, sizeof host);
  host.pool = parts.pool;
  host.state = parts.state;
  host.team_scores = parts.team_scores;
  host.settings = parts.settings;
  host.imports = parts.imports;
  host.team = parts.team;
  host.spawn = parts.spawn;
  host.admission = parts.admission;
  host.match = parts.match;
  host.items = qa_app_q3_item_lifecycle_host_context(parts.items);
  host.teleport = parts.teleport;
  host.ctx = parts.ctx;
  host.grant_selected_arsenal = parts.grant_selected_arsenal;
  host.give_selected_item = parts.give_selected_item;
  host.player_die = parts.player_die;
  /* host.spawn_item stays NULL: TS GameCommandHost has no such member. */
  return host;
}

/* ---- Subsystems aggregate: TS Q3SourceRuntime constructor ---- */
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/save_level.h"
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/game/targets.h"
#include "qa/content/q3/base/game/save_reader.h"
#include "qa/content/q3/base/game/save_state.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/movement/q3/constants.h"

struct qa_app_q3_source_subsystems_owner {
  qa_app_q3_source_subsystems_parts parts;
  qa_q3_game_level level;
  qa_game_random random;
  qa_q3_ta_client_spawn_state client_spawn_state;
  qa_q3_ta_match_module_state match_module_state;
  qa_q3_shader_remap_registry remaps;
  qa_q3_base_game_settings *settings;
  bool settings_ready;
  qa_q3_base_entity_records *records;
  qa_q3_base_world_adapter world;
  qa_q3_base_server_world server_world;
  qa_q3_base_actor_spatial_queries spatial;
  qa_q3_entity_pool pool;
  qa_q3_game_memory memory;
  qa_q3_config_string_registry config;
  qa_app_q3_item_lifecycle_host items;
  qa_q3_base_combat_bridge bridge;
  qa_q3_missile_runtime missiles;
  qa_q3_weapon_runtime weapons;
  qa_q3_drop_item_context drops;
  qa_q3_ta_team_runtime team;
  qa_q3_death_runtime death;
  qa_q3_ta_client_think_runtime think;
  qa_q3_ta_client_spawn_runtime spawns;
  qa_q3_ta_session_world_state session_world;
  qa_q3_ta_game_session_manager session;
  qa_q3_ta_match_runtime match;
  qa_q3_ta_arena_runtime arenas;
  qa_q3_mover_runtime movers;
  qa_q3_mover_spawn_runtime mover_spawns;
  qa_q3_personal_portal_runtime portal;
  qa_q3_ta_client_admission_runtime admission;
  qa_q3_ta_game_command_runtime commands;
  qa_q3_ta_game_server_command_state server_command_state;
  qa_q3_ta_game_server_command_runtime server_commands;
  qa_q3_target_runtime targets;
  qa_q3_ta_command_settings command_settings;
  qa_app_q3_spawn_item_host spawn_item_host;
  char *cvar_scratch, *userinfo_scratch, *handicap_scratch;
};

typedef qa_app_q3_source_subsystems_owner owner_t;
static qa_q3_base_product owner_game_product(const owner_t *o) {
  return o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}
static double owner_integer(owner_t *o, const char *name) {
  if (!o->settings_ready) return 0; /* TS getters are lazy until register() */
  return qa_q3_base_game_settings_integer(o->settings, name);
}
static double owner_number(owner_t *o, const char *name) {
  if (!o->settings_ready) return 0;
  return qa_q3_base_game_settings_number(o->settings, name);
}
static const char *owner_string(owner_t *o, const char *name) {
  if (!o->settings_ready) return "";
  const char *value = qa_q3_base_game_settings_string(o->settings, name);
  return value ? value : "";
}
static double owner_game_type(owner_t *o) { return owner_integer(o, "g_gametype"); }
static double owner_time(owner_t *o) { return o->level.base.time; }
static bool owner_single_player(owner_t *o) {
  return o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      && owner_integer(o, "ui_singlePlayerActive") != 0;
}
static void owner_set_cvar(owner_t *o, const char *name, const char *value) {
  qa_cvar_registry *cvars = o->parts.settings_host.cvars;
  if (!cvars) return;
  qa_cvar_snapshot result;
  if (qa_cvar_registry_set(cvars, name, value, true, &result)) qa_cvar_read_free(&result);
}
static double fn_time(void *ctx) { return owner_time(ctx); }
static double fn_gravity(void *ctx) { return owner_number(ctx, "g_gravity"); }
static double fn_sound_index(void *ctx, const char *path) {
  return qa_q3_config_string_registry_sound_index(&((owner_t *)ctx)->config, path);
}
static double fn_model_index(void *ctx, const char *path) {
  return qa_q3_config_string_registry_model_index(&((owner_t *)ctx)->config, path);
}
static double fn_rand(void *ctx) { return (double)qa_game_random_rand(&((owner_t *)ctx)->random); }
static double fn_random(void *ctx) { return (double)qa_game_random_random(&((owner_t *)ctx)->random); }
static double fn_crandom(void *ctx) { return (double)qa_game_random_crandom(&((owner_t *)ctx)->random); }
static void fn_remap_teams_at(owner_t *o, double level_time) {
  /* TS remapTeams (490-496). */
  if (o->parts.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) return;
  double time = (double)(float)((float)level_time * (float)0.001);
  char buffer[512];
  const char *suffixes[2] = {"01", "02"};
  for (int i = 0; i < 2; i++) {
    char old_name[64];
    snprintf(old_name, sizeof old_name, "textures/ctf2/redteam%s", suffixes[i]);
    snprintf(buffer, sizeof buffer, "team_icon/%s_red", owner_string(o, "g_redteam"));
    qa_q3_shader_remap_registry_add(&o->remaps, old_name, buffer, time);
  }
  for (int i = 0; i < 2; i++) {
    char old_name[64];
    snprintf(old_name, sizeof old_name, "textures/ctf2/blueteam%s", suffixes[i]);
    snprintf(buffer, sizeof buffer, "team_icon/%s_blue", owner_string(o, "g_blueteam"));
    qa_q3_shader_remap_registry_add(&o->remaps, old_name, buffer, time);
  }
  char *config = qa_q3_shader_remap_registry_build_config(&o->remaps);
  if (o->config.store.set) o->config.store.set(o->config.store.ctx, 24, config ? config : "");
  free(config);
}
static void fn_remap_shader(void *ctx, const char *old_name, const char *new_name, double time) {
  /* TS remapShader (522-525). */
  owner_t *o = ctx;
  qa_q3_shader_remap_registry_add(&o->remaps, old_name, new_name, time);
  char *config = qa_q3_shader_remap_registry_build_config(&o->remaps);
  if (o->config.store.set) o->config.store.set(o->config.store.ctx, 24, config ? config : "");
  free(config);
}
static void fn_settings_remap_teams(void *ctx) { owner_t *o = ctx; fn_remap_teams_at(o, o->level.base.time); }
static double fn_settings_integer(void *ctx, const char *name) { return owner_integer(ctx, name); }
static double fn_memory_debug(void *ctx) { return owner_integer(ctx, "g_debugAlloc"); }
static bool fn_curves(void *ctx) {
  owner_t *o = ctx;
  if (!o->parts.settings_host.cvars) return true;
  return qa_cvar_registry_variable_value(o->parts.settings_host.cvars, "cm_noCurves") == 0;
}
static bool fn_player_curve_clip(void *ctx) {
  owner_t *o = ctx;
  if (!o->parts.settings_host.cvars) return true;
  return qa_cvar_registry_variable_value(o->parts.settings_host.cvars, "cm_playerCurveClip") != 0;
}
static bool fn_bridge_damage_call(void *ctx, qa_q3_damage_call *out) {
  return qa_q3_base_combat_bridge_current_call(&((owner_t *)ctx)->bridge, out);
}
static qa_q3_use_participant owner_participant(owner_t *o, const qa_actor_id *actor) {
  if (!actor) return qa_q3_participant_entity(qa_q3_entity_pool_at(&o->pool, 1022));
  qa_q3_use_participant out;
  if (qa_q3_base_entity_records_use_participant(o->records, true, *actor, &out)) return out;
  return qa_q3_participant_entity(qa_q3_entity_pool_at(&o->pool, 1022));
}
static qa_q3_base_admission fn_admit_damage(void *ctx, qa_q3_game_entity *entity, qa_damage_request request) {
  /* TS admitDamage closure (138-140) over landed qa_q3_admit_target_damage. */
  owner_t *o = ctx;
  qa_q3_use_participant inflictor = owner_participant(o, request.attack.inflictor);
  qa_q3_use_participant attacker = owner_participant(o, request.attack.attacker);
  (void)qa_q3_base_combat_bridge_context(&o->bridge);
  qa_q3_game_admit_result result = qa_q3_admit_target_damage(
      o->level.base.intermission_queued, entity, &inflictor, &attacker);
  return result == QA_Q3_GAME_ADMIT_HANDLED ? QA_Q3_BASE_ADMIT_HANDLED : QA_Q3_BASE_ADMIT_CONTINUE;
}
static void fn_pool_link(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_base_world_adapter_link(&((owner_t *)ctx)->world, entity);
}
static void fn_pool_unlink(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_base_world_adapter_unlink(&((owner_t *)ctx)->world, entity->slot);
}
static void *fn_weapon_unlink(void *ctx, qa_actor_id actor) {
  return (void *)qa_q3_base_world_adapter_unlink_actor(&((owner_t *)ctx)->world, actor);
}
static void fn_weapon_restore(void *ctx, void *token) {
  (void)ctx;
  qa_q3_base_relink_closure *closure = token;
  if (closure) { qa_q3_base_relink_closure_call(closure); qa_q3_base_relink_closure_destroy(closure); }
}
static qa_q3_base_server_trace_result fn_world_trace(void *ctx, qa_q3_base_server_trace_query q) {
  return qa_q3_base_world_adapter_trace(&((owner_t *)ctx)->world, q);
}
static qa_q3_base_actor_trace_result fn_world_trace_actor(void *ctx, qa_q3_base_actor_trace_query q) {
  return qa_q3_base_world_adapter_trace_actor(&((owner_t *)ctx)->world, q);
}
static double fn_point_contents(void *ctx, qa_vec3 point, double pass) {
  return qa_q3_base_world_adapter_point_contents(&((owner_t *)ctx)->world, point, pass);
}
static double fn_bridge_intermission(void *ctx) { return ((owner_t *)ctx)->level.base.intermission_queued; }
static double fn_bridge_game_type(void *ctx) { return owner_game_type(ctx); }
static bool fn_bridge_friendly(void *ctx) { return owner_integer(ctx, "g_friendlyFire") != 0; }
static double fn_bridge_knockback(void *ctx) { return owner_number(ctx, "g_knockback"); }
static void fn_bridge_hurt_carrier(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) {
  qa_q3_ta_team_runtime_check_hurt_carrier(&((owner_t *)ctx)->team, t, a);
}
static bool fn_bridge_log_accuracy(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) {
  return qa_q3_log_accuracy_hit(owner_game_type(ctx), t, a);
}
static bool fn_bridge_obelisk(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) {
  return qa_q3_ta_team_runtime_check_obelisk_attack(&((owner_t *)ctx)->team, t, a);
}
static void fn_bridge_invuln(void *ctx, qa_q3_game_entity *t, qa_vec3 d, qa_vec3 p) {
  owner_t *o = ctx;
  (void)qa_q3_invulnerability_effect(&o->pool, t, d, p);
}
static qa_q3_game_combat_context *fn_combat(void *ctx) {
  return qa_q3_base_combat_bridge_context(&((owner_t *)ctx)->bridge);
}
static void fwd_pool_print(void *ctx, const char *m);
static qa_q3_target_use_context owner_targets(owner_t *o) {
  qa_q3_target_use_context context; memset(&context, 0, sizeof context);
  context.pool = &o->pool; context.time = o->level.base.time; context.ctx = o;
  context.remap_shader = fn_remap_shader;
  context.warn = o->parts.pool_options.print ? fwd_pool_print : NULL;
  return context;
}
static void fn_use_targets_item(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *activator) {
  owner_t *o = ctx;
  qa_q3_use_participant participant = qa_q3_participant_entity(activator);
  qa_q3_use_targets(owner_targets(o), item, activator ? &participant : NULL);
}
static double fn_team_pickup(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *player) {
  return qa_q3_ta_team_runtime_pickup_team(&((owner_t *)ctx)->team, item, player);
}
static void fn_calculate_ranks(void *ctx) { qa_q3_ta_match_runtime_calculate_ranks(&((owner_t *)ctx)->match); }
static void fn_respawn_item(void *ctx, qa_q3_game_entity *item) {
  owner_t *o = ctx;
  qa_q3_respawn_item(item, qa_app_q3_item_lifecycle_host_context(&o->items));
}
static void fn_add_score(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, double score) {
  qa_q3_death_add_score(&((owner_t *)ctx)->death, e, origin, score);
}
static void fn_send_scoreboard(void *ctx, qa_q3_game_entity *e) {
  qa_q3_ta_game_command_runtime_scoreboard(&((owner_t *)ctx)->commands, e);
}
static void fn_team_frag_bonuses(void *ctx, qa_q3_game_entity *v, qa_q3_game_entity *a) {
  qa_q3_ta_team_runtime_frag_bonuses(&((owner_t *)ctx)->team, v, a);
}
static void fn_return_flag_d(void *ctx, double team) {
  qa_q3_ta_team_runtime_return_flag(&((owner_t *)ctx)->team, (qa_q3_base_team)(int)team);
}
static qa_q3_death_frame fn_death_frame(void *ctx) {
  owner_t *o = ctx;
  qa_q3_death_frame frame = {o->level.base.time, owner_game_type(o), o->level.base.warmup_time,
      o->level.base.intermission_time, owner_integer(o, "com_blood") != 0};
  return frame;
}
static qa_q3_game_entity *fn_neutral_obelisk(void *ctx) { return ((owner_t *)ctx)->team.neutral_obelisk; }
static double fn_cube_timeout(void *ctx) { return owner_integer(ctx, "g_cubeTimeout"); }
static void fn_start_kamikaze(void *ctx, qa_q3_game_entity *timer) {
  (void)qa_q3_weapon_start_kamikaze(&((owner_t *)ctx)->weapons, timer);
}
static void fn_player_die_cb(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  qa_q3_death_player_die(&((owner_t *)ctx)->death, self, &inflictor, &attacker, damage, method);
}
static void fn_body_die_cb(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  (void)inflictor; (void)attacker; (void)damage; (void)method;
  qa_q3_death_body_die(&((owner_t *)ctx)->death, self);
}
static qa_q3_entity_callback owner_callback(const void *fn, void *ctx) {
  qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb;
}
static void fn_dropped_flag_think(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_team_runtime_dropped_flag_think(&((owner_t *)ctx)->team, entity);
}
static void fn_check_dropped_item(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_team_runtime_check_dropped_item(&((owner_t *)ctx)->team, entity);
}
static void fn_free_team_entity(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_team_runtime_free_entity(&((owner_t *)ctx)->team, entity);
}
static qa_q3_ta_client_think_frame fn_think_frame(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_client_think_frame frame = {o->level.base.time, o->level.base.intermission_time,
      o->level.base.intermission_queued};
  return frame;
}
static qa_q3_ta_client_think_settings fn_think_settings(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_client_think_settings s; memset(&s, 0, sizeof s);
  s.debug_move = owner_integer(o, "g_debugMove");
  s.ladders = owner_integer(o, "pmove_ladders") != 0;
  s.synchronous_clients = owner_integer(o, "g_synchronousClients") != 0;
  s.pmove_fixed = owner_integer(o, "pmove_fixed") != 0;
  s.pmove_msec = owner_integer(o, "pmove_msec");
  s.gravity = owner_number(o, "g_gravity");
  s.speed = owner_number(o, "g_speed");
  s.dmflags = owner_integer(o, "dmflags");
  s.smooth_clients = owner_integer(o, "g_smoothClients") != 0;
  s.force_respawn_seconds = owner_integer(o, "g_forcerespawn");
  s.single_player = owner_single_player(o);
  return s;
}
static void fn_set_pmove_msec(void *ctx, double value) {
  char buffer[32];
  snprintf(buffer, sizeof buffer, "%s", QA_JS_NUMBER(value));
  owner_set_cvar(ctx, "pmove_msec", buffer);
}
static bool fn_check_gauntlet(void *ctx, qa_q3_game_entity *entity) {
  /* host.primaryAttackAllowed?.(entity.actor.id) !== false && weapons.checkGauntletAttack(entity). */
  owner_t *o = ctx; const qa_app_q3_source_host *h = o->parts.host;
  if (h && h->primary_attack_allowed && !h->primary_attack_allowed(h->ctx, qa_q3_game_entity_actor(entity).id)) return false;
  return qa_q3_weapon_check_gauntlet_attack(&o->weapons, entity);
}
static void fn_respawn_entity(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_client_spawn_runtime_respawn(&((owner_t *)ctx)->spawns, entity);
}
static bool fn_is_door_trigger(void *ctx, qa_q3_game_entity *entity) {
  return qa_q3_mover_spawn_is_door_trigger(&((owner_t *)ctx)->mover_spawns, entity);
}
static qa_q3_game_entity *fn_touch_native(void *ctx, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(((owner_t *)ctx)->records, true, actor);
}
static qa_q3_ta_client_spawn_frame fn_spawn_frame(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_client_spawn_frame frame = {o->level.base.time, owner_game_type(o),
      owner_integer(o, "g_inactivity"), o->level.base.intermission_time};
  return frame;
}
static qa_q3_ta_spawn_pose fn_find_intermission(void *ctx) {
  return qa_q3_ta_match_runtime_find_intermission_point(&((owner_t *)ctx)->match);
}
static void fn_move_to_intermission(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_match_runtime_move_client_to_intermission(&((owner_t *)ctx)->match, entity);
}
static void fn_kill_box(void *ctx, qa_q3_game_entity *entity) {
  owner_t *o = ctx;
  qa_q3_game_combat_context * context = fn_combat(o);
  qa_q3_kill_box(context, o->parts.kill_box, entity);
}
static void fn_client_think_fwd(void *ctx, double client_num, qa_q3_base_user_command command) {
  qa_q3_ta_client_think_runtime_client_think(&((owner_t *)ctx)->think, client_num, command);
}
static void fn_broadcast_team_change(void *ctx, double client_num, double old_team) {
  qa_q3_ta_game_command_runtime_broadcast_team_change(&((owner_t *)ctx)->commands, client_num, old_team);
}
static void fn_stop_following(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_game_command_runtime_stop_following(&((owner_t *)ctx)->commands, entity);
}
static void fn_set_team_str(void *ctx, qa_q3_game_entity *entity, const char *request) {
  qa_q3_ta_game_command_runtime_set_team(&((owner_t *)ctx)->commands, entity, request);
}
static void fn_userinfo_changed(void *ctx, double client_num) {
  qa_q3_ta_client_admission_runtime_userinfo_changed(&((owner_t *)ctx)->admission, client_num);
}
static void fn_write_session_data(void *ctx) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_game_session_manager_write_world(&o->session);
}
static void fn_set_cvar_named(void *ctx, const char *name, const char *value) {
  owner_set_cvar(ctx, name, value);
}
static void fn_update_tournament(void *ctx) { qa_q3_ta_arena_runtime_update_tournament_info(&((owner_t *)ctx)->arenas); }
static void fn_victory_pads(void *ctx) { qa_q3_ta_arena_runtime_spawn_models_on_victory_pads(&((owner_t *)ctx)->arenas); }
static bool fn_single_player(void *ctx) { return owner_single_player(ctx); }
static qa_q3_ta_match_settings fn_match_settings(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_match_settings s; memset(&s, 0, sizeof s);
  s.game_type = owner_game_type(o);
  s.time_limit = owner_integer(o, "timelimit");
  s.frag_limit = owner_integer(o, "fraglimit");
  s.capture_limit = owner_integer(o, "capturelimit");
  s.warmup_seconds = owner_integer(o, "g_warmup");
  s.warmup_modification_count = o->settings_ready
      ? qa_q3_base_game_settings_snapshot(o->settings, "g_warmup")->modification_count : 0;
  s.password = owner_string(o, "g_password");
  s.password_modification_count = o->settings_ready
      ? qa_q3_base_game_settings_snapshot(o->settings, "g_password")->modification_count : 0;
  return s;
}
static qa_q3_ta_spawn_pose fn_select_spawn_point(void *ctx, qa_vec3 avoid) {
  owner_t *o = ctx;
  qa_q3_ta_spawn_point point = qa_q3_ta_client_spawn_runtime_select_spawn_point(&o->spawns, avoid);
  qa_q3_ta_spawn_pose pose = {point.origin, point.angles};
  return pose;
}
static bool fn_new_session(void *ctx) { return ((owner_t *)ctx)->level.new_session; }
static void fn_client_spawn_fwd(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_ta_client_spawn_runtime_client_spawn(&((owner_t *)ctx)->spawns, entity);
}
static void fn_toss_items(void *ctx, qa_q3_game_entity *e) { qa_q3_death_toss_client_items(&((owner_t *)ctx)->death, e); }
static void fn_toss_powerups(void *ctx, qa_q3_game_entity *e) {
  qa_q3_death_toss_client_persistant_powerups(&((owner_t *)ctx)->death, e);
}
static void fn_toss_cubes(void *ctx, qa_q3_game_entity *e) { qa_q3_death_toss_client_cubes(&((owner_t *)ctx)->death, e); }
static qa_q3_ta_client_admission_settings fn_admission_settings(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_client_admission_settings s = {owner_game_type(o), owner_string(o, "g_password")};
  return s;
}
static bool fn_filter_packet(void *ctx, const char *address) {
  return qa_q3_ta_game_server_command_runtime_filter_packet(&((owner_t *)ctx)->server_commands, address);
}
static void fn_memory_status_cap(void *ctx, const char *const *argv, size_t argc) {
  (void)argv; (void)argc;
  qa_q3_game_memory_status(&((owner_t *)ctx)->memory);
}
static void fn_podium_cap(void *ctx, const char *const *argv, size_t argc) {
  (void)argv; (void)argc;
  qa_q3_ta_arena_runtime_abort_podium(&((owner_t *)ctx)->arenas);
}
static qa_cvar_snapshot fn_read_vm_cvar(void *ctx, const char *name) {
  owner_t *o = ctx;
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (!o->settings_ready) return out;
  const qa_cvar_snapshot *snap = qa_q3_base_game_settings_snapshot(o->settings, name);
  out = *snap;
  out.name = dup_str(snap->name ? snap->name : "");
  out.value = dup_str(snap->value ? snap->value : "");
  out.reset_value = dup_str(snap->reset_value ? snap->reset_value : "");
  out.latched_value = snap->latched_value ? dup_str(snap->latched_value) : NULL;
  return out;
}
static const char *fn_session_cvar_get(void *ctx, const char *name) {
  owner_t *o = ctx;
  free(o->cvar_scratch);
  o->cvar_scratch = o->parts.settings_host.cvars
      ? qa_cvar_registry_variable_string(o->parts.settings_host.cvars, name) : NULL;
  return o->cvar_scratch ? o->cvar_scratch : "";
}
static void fn_session_cvar_set(void *ctx, const char *name, const char *value) {
  owner_set_cvar(ctx, name, value);
}
static void fn_register_item(void *ctx, const qa_q3_base_item_definition *item) {
  owner_t *o = ctx;
  qa_q3_item_registry_register(&o->items.context.registry, item);
}
static void fn_config_set(void *ctx, double index, const char *value) {
  owner_t *o = ctx;
  if (o->config.store.set) o->config.store.set(o->config.store.ctx, index, value);
}

void qa_app_q3_source_subsystems_owner_refresh(qa_app_q3_source_subsystems_owner *o) {
  /* TS live getters flattened to snapshot host fields in landed hosts.
   * TS level IS the match state: level.base is the one record the match,
   * command and admission hosts share. */
  o->missiles.host.previous_time = o->level.previous_time;
  o->weapons.host.quad_factor = owner_number(o, "g_quadfactor");
  o->team.host.game_type = owner_game_type(o);
  o->team.host.time = o->level.base.time;
  o->team.host.location_head = o->targets.location.head;
  if (o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && o->team.host.has_obelisk) {
    o->team.host.obelisk.health = owner_integer(o, "g_obeliskHealth");
    o->team.host.obelisk.regen_period_seconds = owner_integer(o, "g_obeliskRegenPeriod");
    o->team.host.obelisk.regen_amount = owner_integer(o, "g_obeliskRegenAmount");
    o->team.host.obelisk.respawn_delay_seconds = owner_integer(o, "g_obeliskRespawnDelay");
  }
  o->drops.launch.game_type = owner_game_type(o);
  o->drops.launch.time = o->level.base.time;
  qa_q3_ta_session_world_state *world = &o->session_world;
  world->clients = o->pool.clients; world->client_count = QA_Q3_MAX_CLIENTS;
  world->max_clients = o->pool.max_clients;
  world->team_scores = &o->level.team_scores;
  world->game_type = owner_game_type(o);
  world->team_auto_join = owner_integer(o, "g_teamAutoJoin") != 0;
  world->max_game_clients = owner_integer(o, "g_maxGameClients");
  world->time = o->level.base.time;
  world->num_non_spectator_clients = o->level.base.num_non_spectator_clients;
  /* new_session is written back by the session manager into level. */
  o->level.new_session = world->new_session;
  if (!o->parts.command_parts.settings) {
    o->command_settings.game_type = owner_game_type(o);
    o->command_settings.cheats = owner_integer(o, "sv_cheats") != 0;
    o->command_settings.team_force_balance = owner_integer(o, "g_teamForceBalance") != 0;
    o->command_settings.max_game_clients = owner_integer(o, "g_maxGameClients");
    o->command_settings.dedicated = owner_integer(o, "dedicated") != 0;
    o->command_settings.allow_vote = owner_integer(o, "g_allowVote") != 0;
    o->commands.host.settings = &o->command_settings;
  }
}

/* Forwarders: hosts whose ctx the owner overwrites keep reaching the
 * caller's leaf fns with the caller's original ctx (o->parts). A
 * forwarder is installed only when the caller supplied the fn, so
 * TS-undefined NULL fields stay NULL. */
static void fwd_pool_print(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.pool_options.print(o->parts.pool_options.ctx, m);
}
static double fwd_memory_debug(void *ctx) {
  owner_t *o = ctx; return o->parts.memory_debug_integer(o->parts.memory_ctx);
}
static void fwd_settings_send(void *ctx, double client, const char *command) {
  owner_t *o = ctx; o->parts.settings_host.send_server_command(o->parts.settings_host.ctx, client, command);
}
static void fwd_ammo_timer(void *ctx, qa_actor_id a, double w, double v) {
  owner_t *o = ctx; o->parts.records_host.ammo_timer_stored(o->parts.records_host.ctx, a, w, v);
}
static void fwd_schedule(void *ctx, qa_owned_actor a, bool has_due, double due) {
  owner_t *o = ctx; o->parts.records_host.schedule(o->parts.records_host.ctx, a, has_due, due);
}
static void fwd_run_think_host(void *ctx, qa_owned_actor a, double t) {
  owner_t *o = ctx; o->parts.records_host.run_think(o->parts.records_host.ctx, a, t);
}
static qa_q3_game_entity *fwd_foreign(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.records_host.foreign(o->parts.records_host.ctx, a);
}
static bool fwd_records_is_player(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.records_host.is_player(o->parts.records_host.ctx, a);
}
static void fwd_collision(void *ctx, qa_owned_actor a, qa_actor_collision c) {
  owner_t *o = ctx; o->parts.world_host.collision(o->parts.world_host.ctx, a, c);
}
static void fwd_item_log(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.item_services.log(o->parts.item_services.ctx, m);
}
static void fwd_item_warn(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.item_services.warn(o->parts.item_services.ctx, m);
}
static char *fwd_item_userinfo(void *ctx, double client) {
  owner_t *o = ctx; return o->parts.item_services.get_userinfo(o->parts.item_services.ctx, client);
}
static bool fwd_item_preview(void *ctx, const void *d, void *out) {
  owner_t *o = ctx; return o->parts.item_services.preview_pickup(o->parts.item_services.ctx, d, out);
}
static bool fwd_item_admit(void *ctx, const void *d, void *out) {
  owner_t *o = ctx; return o->parts.item_services.admit_pickup(o->parts.item_services.ctx, d, out);
}
static qa_victim_armor_context fwd_armor_context(void *ctx, qa_damage_request r) {
  owner_t *o = ctx; return o->parts.bridge_host.armor_context(o->parts.bridge_host.ctx, r);
}
static void fwd_debug_damage(void *ctx, qa_q3_damage_diagnostic d) {
  owner_t *o = ctx; o->parts.bridge_host.debug_damage(o->parts.bridge_host.ctx, d);
}
static bool fwd_bridge_is_live(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.bridge_host.is_live(o->parts.bridge_host.ctx, a);
}
static void fwd_apply_damage(void *ctx, qa_actor_id t, double amt, double kb, qa_vec3 d, qa_vec3 p, bool r) {
  owner_t *o = ctx; o->parts.bridge_host.apply_damage(o->parts.bridge_host.ctx, t, amt, kb, d, p, r);
}
static void fwd_damage_feedback(void *ctx, qa_q3_damage_call c, qa_damage_decision d) {
  owner_t *o = ctx; o->parts.bridge_host.damage_feedback(o->parts.bridge_host.ctx, c, d);
}
static void fwd_foreign_feedback(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a, qa_damage_decision d) {
  owner_t *o = ctx; o->parts.bridge_host.foreign_damage_feedback(o->parts.bridge_host.ctx, t, a, d);
}
static bool fwd_projectile_parent(void *ctx, qa_actor_id a, qa_actor_id *out) {
  owner_t *o = ctx; return o->parts.bridge_host.projectile_parent(o->parts.bridge_host.ctx, a, out);
}
#define FWD_WEAPON_FIRE(name, field) \
  static qa_q3_game_entity *name(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, qa_q3_missile_direction d) { \
    owner_t *o = ctx; return o->parts.weapon_host.field(o->parts.weapon_host.ctx, e, origin, d); }
FWD_WEAPON_FIRE(fwd_fire_grenade, fire_grenade)
FWD_WEAPON_FIRE(fwd_fire_rocket, fire_rocket)
FWD_WEAPON_FIRE(fwd_fire_plasma, fire_plasma)
FWD_WEAPON_FIRE(fwd_fire_bfg, fire_bfg)
FWD_WEAPON_FIRE(fwd_fire_grapple, fire_grapple)
FWD_WEAPON_FIRE(fwd_fire_prox, fire_prox)
#undef FWD_WEAPON_FIRE
static qa_q3_game_entity *fwd_fire_nail(void *ctx, qa_q3_game_entity *e, qa_vec3 origin,
    qa_q3_missile_direction d, qa_vec3 right, qa_vec3 up) {
  owner_t *o = ctx; return o->parts.weapon_host.fire_nail(o->parts.weapon_host.ctx, e, origin, d, right, up);
}
static double fwd_damage_factor(void *ctx, qa_q3_game_entity *e) {
  owner_t *o = ctx; return o->parts.weapon_host.damage_factor(o->parts.weapon_host.ctx, e);
}
static qa_q3_base_actor_trace_result fwd_weapon_trace(void *ctx, qa_vec3 s, qa_vec3 e, const qa_actor_id *pass) {
  owner_t *o = ctx; return o->parts.weapon_host.trace(o->parts.weapon_host.ctx, s, e, pass);
}
static bool fwd_authority_read(void *ctx, qa_actor_id a, double *h, bool *d, const char **team) {
  owner_t *o = ctx; return o->parts.weapon_host.authority_read(o->parts.weapon_host.ctx, a, h, d, team);
}
static qa_q3_damage_participant fwd_weapon_participant(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.weapon_host.participant(o->parts.weapon_host.ctx, a);
}
static qa_q3_game_entity *fwd_weapon_native(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.weapon_host.native_by_actor(o->parts.weapon_host.ctx, a);
}
static bool fwd_weapon_is_player(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.weapon_host.is_player(o->parts.weapon_host.ctx, a);
}
static size_t fwd_area_entities(void *ctx, qa_bounds b, double *out, size_t cap) {
  owner_t *o = ctx; return o->parts.weapon_host.area_entities(o->parts.weapon_host.ctx, b, out, cap);
}
static bool fwd_link_bounds(void *ctx, double n, qa_bounds *out) {
  owner_t *o = ctx; return o->parts.weapon_host.link_bounds(o->parts.weapon_host.ctx, n, out);
}
static void fwd_team_send(void *ctx, double client, const char *text) {
  owner_t *o = ctx; o->parts.team_host.send_server_command(o->parts.team_host.ctx, client, text);
}
static void fwd_team_set_cs(void *ctx, double index, const char *text) {
  owner_t *o = ctx; o->parts.team_host.set_configstring(o->parts.team_host.ctx, index, text);
}
static void fwd_team_warn(void *ctx, const char *text) {
  owner_t *o = ctx; o->parts.team_host.warn(o->parts.team_host.ctx, text);
}
static bool fwd_in_pvs(void *ctx, qa_vec3 a, qa_vec3 b) {
  owner_t *o = ctx; return o->parts.team_host.in_pvs(o->parts.team_host.ctx, a, b);
}
static void fwd_hook_free_death(void *ctx, qa_q3_game_entity *hook) {
  owner_t *o = ctx; o->parts.death_host.hook_free(o->parts.death_host.ctx, hook);
}
static void fwd_death_log(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.death_host.log(o->parts.death_host.ctx, m);
}
static bool fwd_timer_ownership(void *ctx, qa_actor_id a, qa_q3_ta_client_timer_ownership *out) {
  owner_t *o = ctx; return o->parts.think_host.timer_ownership(o->parts.think_host.ctx, a, out);
}
static bool fwd_speed_multiplier(void *ctx, qa_actor_id a, double *out) {
  owner_t *o = ctx; return o->parts.think_host.speed_multiplier(o->parts.think_host.ctx, a, out);
}
static void fwd_intermission_think(void *ctx, qa_q3_game_client *c) {
  owner_t *o = ctx; o->parts.think_host.intermission_think(o->parts.think_host.ctx, c);
}
static void fwd_spectator_think(void *ctx, qa_q3_game_entity *e, qa_q3_base_user_command cmd) {
  owner_t *o = ctx; o->parts.think_host.spectator_think(o->parts.think_host.ctx, e, cmd);
}
static bool fwd_check_inactivity(void *ctx, qa_q3_game_client *c) {
  owner_t *o = ctx; return o->parts.think_host.check_inactivity(o->parts.think_host.ctx, c);
}
static void fwd_free_hook(void *ctx, qa_q3_game_entity *hook) {
  owner_t *o = ctx; o->parts.think_host.free_hook(o->parts.think_host.ctx, hook);
}
static void fwd_client_events(void *ctx, qa_q3_game_entity *e, double seq) {
  owner_t *o = ctx; o->parts.think_host.client_events(o->parts.think_host.ctx, e, seq);
}
static void fwd_append_think(void *ctx, const char *command) {
  owner_t *o = ctx; o->parts.think_host.append_console_command(o->parts.think_host.ctx, command);
}
static void fwd_bot_test_aas(void *ctx, qa_vec3 origin) {
  owner_t *o = ctx; o->parts.think_host.bot_test_aas(o->parts.think_host.ctx, origin);
}
static bool fwd_touch_is_trigger(void *ctx, qa_actor_id a) {
  owner_t *o = ctx;
  return o->parts.think_host.touches.is_trigger(o->parts.think_host.touches.ctx, a);
}
static void fwd_touch_touch(void *ctx, qa_actor_id self, qa_actor_id other) {
  owner_t *o = ctx;
  o->parts.think_host.touches.touch(o->parts.think_host.touches.ctx, self, other);
}
static bool fwd_spawn_is_player(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.client_spawn_host.is_player(o->parts.client_spawn_host.ctx, a);
}
static qa_q3_base_user_command fwd_user_command(void *ctx, double client) {
  owner_t *o = ctx; return o->parts.client_spawn_host.user_command(o->parts.client_spawn_host.ctx, client);
}
static const char *fwd_handicap(void *ctx, double client) {
  owner_t *o = ctx; return o->parts.client_spawn_host.handicap(o->parts.client_spawn_host.ctx, client);
}
static void fwd_selected_player(void *ctx, qa_q3_game_entity *e, qa_q3_ta_spawn_pose pose) {
  owner_t *o = ctx; o->parts.client_spawn_host.selected_player(o->parts.client_spawn_host.ctx, e, pose);
}
static void fwd_selected_weapon_after_targets(void *ctx, qa_q3_game_entity *e) {
  owner_t *o = ctx; o->parts.client_spawn_host.selected_weapon_after_targets(o->parts.client_spawn_host.ctx, e);
}
static qa_q3_ta_client_effects_context fwd_spawn_effects(void *ctx) {
  owner_t *o = ctx; return o->parts.client_spawn_host.effects(o->parts.client_spawn_host.ctx);
}
static qa_q3_target_use_context fwd_spawn_targets(void *ctx) {
  owner_t *o = ctx; return o->parts.client_spawn_host.targets(o->parts.client_spawn_host.ctx);
}
static void fwd_session_print(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.session_services.print(o->parts.session_services.ctx, m);
}
static void fwd_match_append(void *ctx, const char *text) {
  owner_t *o = ctx; o->parts.match_host.append_console_command(o->parts.match_host.ctx, text);
}
static void fwd_match_send(void *ctx, double client, const char *text) {
  owner_t *o = ctx; o->parts.match_host.send_server_command(o->parts.match_host.ctx, client, text);
}
static void fwd_match_set_cs(void *ctx, double index, const char *text) {
  owner_t *o = ctx; o->parts.match_host.set_configstring(o->parts.match_host.ctx, index, text);
}
static void fwd_match_set_cvar(void *ctx, const char *name, const char *value) {
  owner_t *o = ctx; o->parts.match_host.set_cvar(o->parts.match_host.ctx, name, value);
}
static void fwd_match_log(void *ctx, const char *text) {
  owner_t *o = ctx; o->parts.match_host.log(o->parts.match_host.ctx, text);
}
static void fwd_match_warn(void *ctx, const char *text) {
  owner_t *o = ctx; o->parts.match_host.warn(o->parts.match_host.ctx, text);
}
static void fwd_bot_interbreed(void *ctx) {
  owner_t *o = ctx; o->parts.match_host.bot_interbreed_end_match(o->parts.match_host.ctx);
}
static void fwd_set_brush_model_ms(void *ctx, qa_q3_game_entity *e, const char *name) {
  owner_t *o = ctx; o->parts.mover_spawn_host.set_brush_model(o->parts.mover_spawn_host.ctx, e, name);
}
static void fwd_ms_warn(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.mover_spawn_host.warn(o->parts.mover_spawn_host.ctx, m);
}
static void fwd_initialize_binary(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  owner_t *o = ctx; o->parts.mover_spawn_host.initialize_binary(o->parts.mover_spawn_host.ctx, e, v);
}
static void fwd_use_binary(void *ctx, qa_q3_game_entity *p, qa_q3_game_entity *e, qa_q3_damage_participant other) {
  owner_t *o = ctx; o->parts.mover_spawn_host.use_binary(o->parts.mover_spawn_host.ctx, p, e, other);
}
static void fwd_blocked_door(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other) {
  owner_t *o = ctx; o->parts.mover_spawn_host.blocked_door(o->parts.mover_spawn_host.ctx, self, other);
}
static void fwd_match_team(void *ctx, qa_q3_game_entity *e, double state, double time) {
  owner_t *o = ctx; o->parts.mover_spawn_host.match_team(o->parts.mover_spawn_host.ctx, e, state, time);
}
static void fwd_set_state(void *ctx, qa_q3_game_entity *e, double state, double time) {
  owner_t *o = ctx; o->parts.mover_spawn_host.set_state(o->parts.mover_spawn_host.ctx, e, state, time);
}
static void fwd_ms_remap(void *ctx, const char *a, const char *b, double t) {
  owner_t *o = ctx; o->parts.mover_spawn_host.remap_shader(o->parts.mover_spawn_host.ctx, a, b, t);
}
static void fwd_map_drop_flag(void *ctx, qa_q3_game_entity *p) {
  owner_t *o = ctx; o->parts.portal_host.map_drop_carried_flag(o->parts.portal_host.ctx, p);
}
static void fwd_map_teleport(void *ctx, qa_q3_game_entity *p, qa_vec3 origin, qa_vec3 angles) {
  owner_t *o = ctx; o->parts.portal_host.map_teleport(o->parts.portal_host.ctx, p, origin, angles);
}
static const char *fwd_admission_userinfo(void *ctx, double client) {
  owner_t *o = ctx; return o->parts.admission_host.get_userinfo(o->parts.admission_host.ctx, client);
}
static void fwd_admission_set_cs(void *ctx, double index, const char *value) {
  owner_t *o = ctx; o->parts.admission_host.set_configstring(o->parts.admission_host.ctx, index, value);
}
static void fwd_admission_send(void *ctx, double client, const char *value) {
  owner_t *o = ctx; o->parts.admission_host.send_server_command(o->parts.admission_host.ctx, client, value);
}
static void fwd_admission_log(void *ctx, const char *value) {
  owner_t *o = ctx; o->parts.admission_host.log(o->parts.admission_host.ctx, value);
}
static bool fwd_grant_arsenal(void *ctx, qa_actor_id a, const char *category) {
  owner_t *o = ctx;
  return o->parts.command_parts.grant_selected_arsenal(o->parts.command_parts.ctx, a, category);
}
static bool fwd_give_item(void *ctx, qa_actor_id a, const char *const *args, size_t count) {
  owner_t *o = ctx;
  return o->parts.command_parts.give_selected_item(o->parts.command_parts.ctx, a, args, count);
}
static qa_cvar_snapshot fwd_read_vm_cvar(void *ctx, const char *name) {
  owner_t *o = ctx;
  return o->parts.server_command_host.read_vm_cvar(o->parts.server_command_host.ctx, name);
}
static void fwd_server_print(void *ctx, const char *text) {
  owner_t *o = ctx; o->parts.server_command_host.print(o->parts.server_command_host.ctx, text);
}
static void fwd_server_send(void *ctx, double client, const char *command) {
  owner_t *o = ctx;
  o->parts.server_command_host.send_server_command(o->parts.server_command_host.ctx, client, command);
}
static void fwd_server_exec(void *ctx, const char *text) {
  owner_t *o = ctx;
  o->parts.server_command_host.execute_console_now(o->parts.server_command_host.ctx, text);
}
static void fwd_target_send(void *ctx, double client, const char *command) {
  owner_t *o = ctx; o->parts.target_runtime.send_server_command(o->parts.target_runtime.ctx, client, command);
}
static void fwd_target_touch_item(void *ctx, qa_q3_game_entity *t, qa_q3_game_entity *a) {
  owner_t *o = ctx; o->parts.target_runtime.touch_item(o->parts.target_runtime.ctx, t, a);
}
#define FWD_MISC_FIRE(name, field) \
  static void name(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, qa_q3_missile_direction d) { \
    owner_t *o = ctx; o->parts.misc_spawn_host.field(o->parts.misc_spawn_host.ctx, e, origin, d); }
FWD_MISC_FIRE(fwd_misc_grenade, fire_grenade)
FWD_MISC_FIRE(fwd_misc_rocket, fire_rocket)
FWD_MISC_FIRE(fwd_misc_plasma, fire_plasma)
#undef FWD_MISC_FIRE
static void fwd_misc_warn(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.misc_spawn_host.warn(o->parts.misc_spawn_host.ctx, m);
}
static void fwd_trigger_brush(void *ctx, qa_q3_game_entity *e, const char *name) {
  owner_t *o = ctx; o->parts.trigger_host.set_brush_model(o->parts.trigger_host.ctx, e, name);
}
static bool fwd_can_take_damage(void *ctx, qa_actor_id a) {
  owner_t *o = ctx; return o->parts.trigger_host.can_take_damage(o->parts.trigger_host.ctx, a);
}
/* qa_app_q3_wire_spawn_item makes the spawn item host the spawn context's ctx. */
static void fwd_spawn_warn(void *ctx, const char *m) {
  owner_t *o = (owner_t *)((char *)ctx - offsetof(owner_t, spawn_item_host));
  o->parts.pool_options.print(o->parts.pool_options.ctx, m);
}
static void fwd_world_log(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.worldspawn.log(o->parts.worldspawn.ctx, m);
}
static void fwd_trigger_warn(void *ctx, const char *m) {
  owner_t *o = ctx; o->parts.trigger_host.warn(o->parts.trigger_host.ctx, m);
}

/* ---- Q3SourceRuntime constructor wiring (runtime.ts 112-233 and the
 * create* helpers): with parts.host set, every leaf service the caller left
 * NULL is the TS closure over the host and the owned subsystems. Each runs
 * with the owner as ctx; the owner's forwarders above route through them. ---- */
#define HOST(o) ((o)->parts.host)
static void h_print(void *ctx, const char *text) { const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.print(h->engine.ctx, text); }
static void h_log(void *ctx, const char *text) { const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.log(h->engine.ctx, text); }
static void h_send(void *ctx, double client, const char *text) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.send_server_command(h->engine.ctx, client, text);
}
static void h_append(void *ctx, const char *text) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.append_console_command(h->engine.ctx, text);
}
static void h_exec(void *ctx, const char *text) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.execute_console_now(h->engine.ctx, text);
}
static void h_drop(void *ctx, double client, const char *reason) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.drop_client(h->engine.ctx, client, reason);
}
static char *h_userinfo_owned(void *ctx, double client) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->engine.get_userinfo(h->engine.ctx, client);
}
/* Borrowed userinfo for hosts that read it as const char *: the owner keeps the
 * latest copy (TS strings are values; each caller reads it before the next call). */
static const char *h_userinfo(void *ctx, double client) {
  owner_t *o = ctx;
  free(o->userinfo_scratch);
  o->userinfo_scratch = h_userinfo_owned(ctx, client);
  return o->userinfo_scratch;
}
static void h_set_userinfo(void *ctx, double client, const char *value) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->engine.set_userinfo(h->engine.ctx, client, value);
}
static qa_q3_base_user_command h_user_command(void *ctx, double client) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->engine.get_user_command(h->engine.ctx, client);
}
static const char *h_cs_get(void *ctx, double index) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->configstring_get(h->configstrings_ctx, index);
}
static void h_cs_set(void *ctx, double index, const char *value) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->configstring_set(h->configstrings_ctx, index, value);
}
/* engineCvar(name) = host.cvars.variableString(name), borrowed like h_userinfo. */
static const char *h_engine_cvar(void *ctx, const char *name) {
  owner_t *o = ctx;
  free(o->cvar_scratch);
  o->cvar_scratch = qa_cvar_registry_variable_string(HOST(o)->cvars, name);
  return o->cvar_scratch ? o->cvar_scratch : "";
}
static void h_ammo_timer(void *ctx, qa_actor_id a, double w, double v) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->ammo_timer_stored(h->ctx, a, w, v);
}
static void h_schedule(void *ctx, qa_owned_actor a, bool has_due, double due) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->schedule(h->ctx, a, has_due, due);
}
static void h_run_think(void *ctx, qa_owned_actor a, double t) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->run_think(h->ctx, a, t);
}
static qa_q3_game_entity *h_foreign(void *ctx, qa_actor_id a) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->foreign ? h->foreign(h->ctx, a) : NULL;
}
static bool h_is_player(void *ctx, qa_actor_id a) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->is_player(h->ctx, a);
}
static void h_collision(void *ctx, qa_owned_actor a, qa_actor_collision c) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->collision(h->ctx, a, c);
}
static qa_victim_armor_context h_armor(void *ctx, qa_damage_request r) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->armor_context(h->ctx, r);
}
static bool h_is_live(void *ctx, qa_actor_id a) { return qa_session_actor_registry_is_live(HOST((owner_t *)ctx)->actors, a); }
/* previewPickup: item => host.previewPickup?.(item) ?? { kind: "native" } (the item
 * lifecycle host reads false as the native result). */
static bool h_preview(void *ctx, const void *descriptor, void *out) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx);
  *(qa_q3_source_pickup_preview *)out = h->preview_pickup(h->ctx, *(const qa_q3_source_pickup_descriptor *)descriptor);
  return true;
}
static bool h_admit(void *ctx, const void *descriptor, void *out) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx);
  *(qa_q3_source_pickup_admission *)out = h->admit_pickup(h->ctx, *(const qa_q3_source_pickup_descriptor *)descriptor);
  return true;
}
static bool h_projectile_parent(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  return qa_q3_missile_runtime_owner_of(&((owner_t *)ctx)->missiles, actor, out);
}
/* WeaponRuntime host over this.missiles (fire*), missiles.host.combat and world. */
#define H_FIRE(name, fn) \
  static qa_q3_game_entity *name(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, qa_q3_missile_direction d) { \
    return fn(&((owner_t *)ctx)->missiles, e, origin, &d); }
H_FIRE(h_fire_grenade, qa_q3_missile_runtime_fire_grenade)
H_FIRE(h_fire_rocket, qa_q3_missile_runtime_fire_rocket)
H_FIRE(h_fire_plasma, qa_q3_missile_runtime_fire_plasma)
H_FIRE(h_fire_bfg, qa_q3_missile_runtime_fire_bfg)
H_FIRE(h_fire_grapple, qa_q3_missile_runtime_fire_grapple)
H_FIRE(h_fire_prox, qa_q3_missile_runtime_fire_prox)
#undef H_FIRE
static qa_q3_game_entity *h_fire_nail(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, qa_q3_missile_direction d,
    qa_vec3 right, qa_vec3 up) {
  qa_vec3 forward = {d.x, d.y, d.z};
  return qa_q3_missile_runtime_fire_nail(&((owner_t *)ctx)->missiles, e, origin, forward, right, up);
}
#define H_MISC_FIRE(name, fn) \
  static void name(void *ctx, qa_q3_game_entity *e, qa_vec3 origin, qa_q3_missile_direction d) { \
    (void)fn(&((owner_t *)ctx)->missiles, e, origin, &d); }
H_MISC_FIRE(h_misc_grenade, qa_q3_missile_runtime_fire_grenade)
H_MISC_FIRE(h_misc_rocket, qa_q3_missile_runtime_fire_rocket)
H_MISC_FIRE(h_misc_plasma, qa_q3_missile_runtime_fire_plasma)
#undef H_MISC_FIRE
/* combat.spatial.traceActor({ start, end, shape: point, passActor, mask: MASK_SHOT }). */
static qa_q3_base_actor_trace_result h_shot_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass) {
  qa_q3_base_actor_trace_query query; memset(&query, 0, sizeof query);
  query.start = start; query.end = end; query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  query.mask = 0x6000001; query.pass_actor = pass;
  return qa_q3_base_world_adapter_trace_actor(&((owner_t *)ctx)->world, query);
}
/* combat.authority.read(actor): health, canTakeDamage, team (false = null). */
static bool h_authority_read(void *ctx, qa_actor_id actor, double *health, bool *can, const char **team) {
  owner_t *o = ctx;
  qa_combat_state_result state = qa_gameplay_authority_read(fn_combat(o)->authority, actor);
  if (!state.has_value) return false;
  *health = state.value.health; *can = state.value.can_take_damage; *team = state.value.team;
  return true;
}
static qa_q3_damage_participant h_participant(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *c = fn_combat(ctx); return c->participant(c->services_ctx, actor);
}
static qa_q3_game_entity *h_native(void *ctx, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(((owner_t *)ctx)->records, true, actor);
}
static bool h_combat_is_player(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *c = fn_combat(ctx); return c->is_player(c->services_ctx, actor);
}
static size_t h_area_entities(void *ctx, qa_bounds bounds, double *out, size_t cap) {
  owner_t *o = ctx; size_t count = 0;
  const double *numbers = o->server_world.area_entities(o->server_world.ctx, bounds, false, 0, &count);
  for (size_t i = 0; i < count && i < cap; i++) out[i] = numbers[i];
  return count;
}
static bool h_link_bounds(void *ctx, double number, qa_bounds *out) {
  owner_t *o = ctx; qa_q3_base_link_state state;
  if (!o->server_world.link_state(o->server_world.ctx, number, &state)) return false;
  *out = state.absbounds; return true;
}
/* killBox(this.combat, entity): context.spatial.areaActors / actors.*. */
static size_t h_kill_area_actors(void *ctx, qa_bounds bounds, double maximum, qa_actor_id *out, size_t cap) {
  owner_t *o = ctx; size_t count = 0;
  const qa_actor_id *actors = o->spatial.area_actors(o->spatial.ctx, bounds, maximum, &count);
  for (size_t i = 0; i < count && i < cap; i++) out[i] = actors[i];
  return count;
}
static bool h_kill_linked(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *c = fn_combat(ctx); qa_bounds bounds;
  return c->linked_bounds(c->services_ctx, actor, &bounds);
}
static void h_hook_free(void *ctx, qa_q3_game_entity *hook) { qa_q3_missile_runtime_hook_free(&((owner_t *)ctx)->missiles, hook); }
static bool h_in_pvs(void *ctx, qa_vec3 first, qa_vec3 second) {
  qa_shared_scene_queries *scene = HOST((owner_t *)ctx)->scene;
  double a = qa_shared_point_leaf(scene, first), b = qa_shared_point_leaf(scene, second);
  return qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, a), qa_shared_leaf_cluster(scene, b), "pvs")
      && qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, a), qa_shared_leaf_area(scene, b));
}
/* setBrushModel (runtime.ts 398-406). */
static void h_set_brush_model(void *ctx, qa_q3_game_entity *entity, const char *name) {
  owner_t *o = ctx;
  if (name == NULL || name[0] != '*') qa_range_error("SV_SetBrushModel: %s is not a brush model", name ? name : "null");
  double index = qa_game_atoi(name + 1);
  entity->s.modelindex = index;
  qa_bounds bounds = qa_shared_model_bounds(HOST(o)->scene, index);
  qa_q3_base_entity_shared_set_mins(&entity->r, bounds.min);
  qa_q3_base_entity_shared_set_maxs(&entity->r, bounds.max);
  entity->r.model.kind = QA_Q3_BASE_MODEL_INLINE; entity->r.model.index = index;
  entity->r.contents = -1;
  qa_q3_base_world_adapter_link(&o->world, entity);
}
/* MoverSpawnRuntime core calls (mover-spawn.ts this.core.*). */
static void h_initialize_binary(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  qa_q3_mover_initialize_binary(&((owner_t *)ctx)->movers, e, v);
}
static void h_use_binary(void *ctx, qa_q3_game_entity *parent, qa_q3_game_entity *e, qa_q3_damage_participant other) {
  qa_q3_use_participant entity = qa_q3_participant_entity(e);
  qa_q3_mover_use_binary(&((owner_t *)ctx)->movers, parent, &entity, &other);
}
static void h_blocked_door(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other) {
  qa_q3_mover_blocked_door(&((owner_t *)ctx)->movers, self, other);
}
static void h_match_team(void *ctx, qa_q3_game_entity *e, double state, double time) {
  qa_q3_mover_match_team(&((owner_t *)ctx)->movers, e, (qa_q3_mover_state)state, time);
}
static void h_set_state(void *ctx, qa_q3_game_entity *e, double state, double time) {
  qa_q3_mover_set_state(&((owner_t *)ctx)->movers, e, (qa_q3_mover_state)state, time);
}
/* ClientThinkRuntime touches (runtime.ts 180-183). */
static bool h_touch_is_trigger(void *ctx, qa_actor_id actor) {
  qa_spatial_actor spatial;
  return qa_shared_linked_actor(HOST((owner_t *)ctx)->scene, actor, &spatial)
      && spatial.collision.role != NULL && strcmp(spatial.collision.role, "trigger") == 0;
}
static void h_touch(void *ctx, qa_actor_id self, qa_actor_id other) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(h->actors, self, &actor)) return;
  qa_touch_contact contact; memset(&contact, 0, sizeof contact);
  contact.self = actor; contact.other = other;
  (void)qa_actor_callback_table_touch(h->callbacks, contact);
}
static bool h_timer_ownership(void *ctx, qa_actor_id actor, qa_q3_ta_client_timer_ownership *out) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->timer_ownership(h->ctx, actor, out);
}
static bool h_speed_multiplier(void *ctx, qa_actor_id actor, double *out) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); *out = h->speed_multiplier(h->ctx, actor); return true;
}
static void h_intermission_think(void *ctx, qa_q3_game_client *client) { (void)ctx; qa_q3_ta_client_intermission_think(client); }
static void h_policy_touch_triggers(void *ctx, qa_q3_game_entity *e) { qa_q3_ta_client_think_runtime_touch_triggers(&((owner_t *)ctx)->think, e); }
static void h_policy_follow_cycle(void *ctx, qa_q3_game_entity *e, double direction) {
  qa_q3_ta_game_command_runtime_follow_cycle(&((owner_t *)ctx)->commands, e, direction);
}
static void h_policy_begin(void *ctx, double client) { qa_q3_ta_client_admission_runtime_begin(&((owner_t *)ctx)->admission, client); }
/* policy() (runtime.ts 376-382): a fresh context per call. */
static qa_q3_ta_client_policy_context owner_policy(owner_t *o) {
  qa_q3_ta_client_policy_context p; memset(&p, 0, sizeof p);
  p.movement.ctx = HOST(o)->ctx; p.movement.move_client = HOST(o)->move_client;
  p.pool = &o->pool; p.world = &o->server_world;
  p.time = o->level.base.time; p.inactivity_seconds = owner_integer(o, "g_inactivity");
  p.follow1 = o->level.base.follow1; p.follow2 = o->level.base.follow2;
  p.ctx = o; p.touch_triggers = h_policy_touch_triggers; p.follow_cycle = h_policy_follow_cycle;
  p.client_begin = h_policy_begin; p.drop_client = h_drop; p.send_server_command = h_send;
  return p;
}
static void h_spectator_think(void *ctx, qa_q3_game_entity *e, qa_q3_base_user_command command) {
  qa_q3_ta_client_policy_context p = owner_policy(ctx); qa_q3_ta_spectator_think(&p, e, command);
}
static bool h_check_inactivity(void *ctx, qa_q3_game_client *client) {
  qa_q3_ta_client_policy_context p = owner_policy(ctx); return qa_q3_ta_client_inactivity_timer(&p, client);
}
static void h_spectator_end_frame(void *ctx, qa_q3_game_entity *e) {
  qa_q3_ta_client_policy_context p = owner_policy(ctx); qa_q3_ta_spectator_client_end_frame(&p, e);
}
/* effects() (runtime.ts 383-388). */
static void h_effect_sound(void *ctx, qa_q3_game_entity *entity, double channel, double sound) {
  (void)channel;
  owner_t *o = ctx;
  qa_q3_game_entity *event = qa_q3_entity_pool_temp_entity(&o->pool, qa_q3_base_entity_shared_current_origin(&entity->r), QA_Q3_EV_GENERAL_SOUND);
  event->s.event_parm = sound;
}
static qa_q3_ta_client_effects_context h_effects(void *ctx) {
  owner_t *o = ctx;
  qa_q3_ta_client_effects_context e; memset(&e, 0, sizeof e);
  e.combat = fn_combat(o); e.intermission_time = o->level.base.intermission_time;
  e.smooth_clients = owner_integer(o, "g_smoothClients") != 0; e.fry_sound = o->level.fry_sound;
  e.ctx = o; e.random_int = fn_rand; e.sound_index = fn_sound_index; e.sound = h_effect_sound;
  e.spectator_end_frame = h_spectator_end_frame;
  return e;
}
static qa_q3_target_use_context h_targets(void *ctx) { return owner_targets(ctx); }
/* runClientEvents (runtime.ts 431-440). */
static void h_events_fire(void *ctx, qa_q3_game_entity *e) { qa_q3_weapon_fire(&((owner_t *)ctx)->weapons, e); }
static void h_events_kamikaze(void *ctx, qa_q3_game_entity *e) { (void)qa_q3_weapon_start_kamikaze(&((owner_t *)ctx)->weapons, e); }
static qa_q3_game_entity *h_events_drop(void *ctx, qa_q3_game_entity *e, const qa_q3_base_item_definition *item, double angle) {
  owner_t *o = ctx; qa_app_q3_source_subsystems_owner_refresh(o); return qa_q3_drop_item(o->drops, e, item, angle);
}
static void h_events_portal_source(void *ctx, qa_q3_game_entity *e) { qa_q3_personal_portal_drop_source(&((owner_t *)ctx)->portal, e); }
static void h_events_portal_destination(void *ctx, qa_q3_game_entity *e) { qa_q3_personal_portal_drop_destination(&((owner_t *)ctx)->portal, e); }
static bool h_primary_allowed(void *ctx, qa_actor_id actor) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx);
  return h->primary_attack_allowed == NULL || h->primary_attack_allowed(h->ctx, actor);
}
static void h_client_events(void *ctx, qa_q3_game_entity *e, double old_sequence) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_client_events_context c; memset(&c, 0, sizeof c);
  c.product = o->parts.product; c.combat = fn_combat(o); c.world = &o->server_world;
  c.teleport.combat = fn_combat(o); c.teleport.kill_box = o->parts.kill_box; c.teleport.world = o->server_world;
  c.dmflags = owner_integer(o, "dmflags");
  c.ctx = o; c.fire_weapon = h_events_fire; c.start_kamikaze = h_events_kamikaze;
  c.select_spawn_point = fn_select_spawn_point; c.drop_item = h_events_drop;
  if (o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    c.drop_portal_source = h_events_portal_source; c.drop_portal_destination = h_events_portal_destination;
  }
  c.primary_attack_allowed = h_primary_allowed;
  qa_q3_ta_client_events(&c, e, old_sequence);
}
static void h_bot_test_aas(void *ctx, qa_vec3 origin) {
  const qa_app_q3_source_bots *bots = &HOST((owner_t *)ctx)->bots;
  if (strcmp(bots->kind, "available") == 0) bots->test_aas(bots->ctx, origin);
}
static void h_bot_interbreed(void *ctx) {
  const qa_app_q3_source_bots *bots = &HOST((owner_t *)ctx)->bots;
  if (strcmp(bots->kind, "available") == 0) bots->interbreed_end_match(bots->ctx);
}
static void h_bot_console(void *ctx, const char *const *argv, size_t argc) {
  const qa_app_q3_source_bots *bots = &HOST((owner_t *)ctx)->bots;
  if (strcmp(bots->kind, "available") == 0) bots->console_command(bots->ctx, argv, argc);
}
/* ClientSpawnRuntime host (runtime.ts 197-205). */
static bool h_spawn_is_player(void *ctx, qa_actor_id actor) { return h_combat_is_player(ctx, actor); }
static const char *h_handicap(void *ctx, double client) {
  owner_t *o = ctx;
  char *info = h_userinfo_owned(ctx, client);
  free(o->handicap_scratch);
  o->handicap_scratch = qa_q3_ta_client_info_value(info ? info : "", "handicap");
  free(info);
  return o->handicap_scratch ? o->handicap_scratch : "";
}
static void h_selected_player(void *ctx, qa_q3_game_entity *e, qa_q3_ta_spawn_pose pose) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->spawn_player(h->ctx, e, pose);
}
static void h_selected_weapon(void *ctx, qa_q3_game_entity *e) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); h->select_spawn_weapon(h->ctx, e);
}
static bool h_grant(void *ctx, qa_actor_id actor, const char *category) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->grant_selected_arsenal(h->ctx, actor, category);
}
static bool h_give(void *ctx, qa_actor_id actor, const char *const *args, size_t count) {
  const qa_app_q3_source_host *h = HOST((owner_t *)ctx); return h->give_selected_item(h->ctx, actor, args, count);
}
static bool h_can_take_damage(void *ctx, qa_actor_id actor) {
  qa_combat_state_result state = qa_gameplay_authority_read(fn_combat(ctx)->authority, actor);
  return state.has_value && state.value.can_take_damage;
}
static void h_target_touch_item(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *activator) {
  owner_t *o = ctx;
  qa_touch_contact contact; memset(&contact, 0, sizeof contact);
  contact.self = qa_q3_game_entity_actor(target); contact.other = qa_q3_game_entity_actor(activator).id;
  qa_q3_touch_item(target, qa_q3_participant_entity(activator), contact, qa_app_q3_item_lifecycle_host_context(&o->items));
}
/* useParticipants() (runtime.ts 496-503). */
static bool h_up_live(void *ctx, qa_actor_id actor) { return h_is_live(ctx, actor); }
static void h_up_event(void *ctx, qa_actor_id actor, double event, double parameter) {
  owner_t *o = ctx; const qa_app_q3_source_host *h = HOST(o);
  qa_body_state_result body = qa_shared_body_table_read(h->bodies, actor);
  if (!body.has_value) return;
  qa_app_q3_source_entity_event published; memset(&published, 0, sizeof published);
  published.kind = "entity-event"; published.actor = actor;
  qa_q3_entity_state_init(&published.state);
  published.state.event = event; published.state.event_parm = parameter;
  published.origin = body.value.origin; published.time = o->level.base.time;
  h->entity_event(h->entity_event_ctx, published);
}
static qa_q3_use_participant_services owner_participants(owner_t *o) {
  qa_q3_use_participant_services p = {o, h_up_live, h_is_player, h_native, h_up_event};
  return p;
}

/* adjustAreaPortalState (runtime.ts 692-703). */
static void h_adjust_area_portal_state(void *ctx, qa_q3_game_entity *entity, bool open) {
  owner_t *o = ctx; const qa_app_q3_source_host *h = HOST(o);
  if (o->parts.foreign_world) return;
  qa_linked_body_result linked = qa_shared_body_table_linked(h->bodies, qa_q3_game_entity_actor(entity).id);
  if (!linked.has_value) qa_range_error("Area portal mover must be linked");
  qa_leaf_query_result leaves = qa_shared_box_leaves(h->scene, linked.value.absolute_bounds, 128);
  double areas[2]; size_t count = 0;
  for (size_t i = 0; i < leaves.leaves.count && count < 2; i++) {
    double area = qa_shared_leaf_area(h->scene, leaves.leaves.values[i]);
    if (area == -1) continue;
    bool seen = false;
    for (size_t j = 0; j < count; j++) if (areas[j] == area) seen = true;
    if (!seen) areas[count++] = area;
  }
  if (count == 2) qa_shared_adjust_area_portal_pair(h->scene, areas[0], areas[1], open);
}
#define FILL(slot, fn) do { if ((slot) == NULL) (slot) = (fn); } while (0)
static void owner_fill_from_host(owner_t *o) {
  const qa_app_q3_source_host *h = HOST(o);
  qa_app_q3_source_subsystems_parts *p = &o->parts;
  p->settings_host.ctx = o;
  if (!p->settings_host.cvars) p->settings_host.cvars = h->cvars;
  FILL(p->settings_host.send_server_command, h_send);
  p->records_host.ctx = o;
  if (!p->records_host.actors) p->records_host.actors = h->actors;
  if (!p->records_host.bodies) p->records_host.bodies = h->bodies;
  if (!p->records_host.combat) p->records_host.combat = h->combat;
  if (!p->records_host.inventory) p->records_host.inventory = h->inventory;
  if (!p->records_host.callbacks) p->records_host.callbacks = h->callbacks;
  if (h->ammo_timer_stored) FILL(p->records_host.ammo_timer_stored, h_ammo_timer);
  FILL(p->records_host.schedule, h_schedule);
  FILL(p->records_host.run_think, h_run_think);
  FILL(p->records_host.foreign, h_foreign);
  FILL(p->records_host.is_player, h_is_player);
  p->world_host.ctx = o;
  if (!p->world_host.queries) p->world_host.queries = h->scene;
  if (!p->world_host.bodies) p->world_host.bodies = h->bodies;
  FILL(p->world_host.collision, h_collision);
  p->pool_options.ctx = o;
  FILL(p->pool_options.print, h_print);
  if (!p->config_store.get) { p->config_store.ctx = o; p->config_store.get = h_cs_get; p->config_store.set = h_cs_set; }
  p->item_services.ctx = o;
  FILL(p->item_services.log, h_log);
  FILL(p->item_services.warn, h_print);
  FILL(p->item_services.get_userinfo, h_userinfo_owned);
  if (h->preview_pickup) FILL(p->item_services.preview_pickup, h_preview);
  if (h->admit_pickup) FILL(p->item_services.admit_pickup, h_admit);
  if (h->original_pickups && !p->item_services.has_original_pickups) {
    p->item_services.has_original_pickups = true; p->item_services.original_pickups = *h->original_pickups;
  }
  p->bridge_host.ctx = o;
  if (!p->bridge_host.authority) p->bridge_host.authority = h->combat;
  FILL(p->bridge_host.armor_context, h_armor);
  FILL(p->bridge_host.is_live, h_is_live);
  if (p->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) FILL(p->bridge_host.projectile_parent, h_projectile_parent);
  p->weapon_host.ctx = o;
  FILL(p->weapon_host.fire_grenade, h_fire_grenade); FILL(p->weapon_host.fire_rocket, h_fire_rocket);
  FILL(p->weapon_host.fire_plasma, h_fire_plasma); FILL(p->weapon_host.fire_bfg, h_fire_bfg);
  FILL(p->weapon_host.fire_grapple, h_fire_grapple); FILL(p->weapon_host.fire_prox, h_fire_prox);
  FILL(p->weapon_host.fire_nail, h_fire_nail);
  FILL(p->weapon_host.trace, h_shot_trace);
  FILL(p->weapon_host.authority_read, h_authority_read);
  FILL(p->weapon_host.participant, h_participant);
  FILL(p->weapon_host.native_by_actor, h_native);
  FILL(p->weapon_host.is_player, h_combat_is_player);
  FILL(p->weapon_host.area_entities, h_area_entities);
  FILL(p->weapon_host.link_bounds, h_link_bounds);
  p->kill_box.ctx = o;
  FILL(p->kill_box.area_actors, h_kill_area_actors);
  FILL(p->kill_box.has_linked_bounds, h_kill_linked);
  FILL(p->kill_box.is_player, h_combat_is_player);
  FILL(p->kill_box.participant, h_participant);
  p->team_host.ctx = o;
  FILL(p->team_host.send_server_command, h_send);
  FILL(p->team_host.set_configstring, h_cs_set);
  FILL(p->team_host.warn, h_print);
  FILL(p->team_host.in_pvs, h_in_pvs);
  p->death_host.ctx = o;
  if (!p->death_host.death_animations) p->death_host.death_animations = h->death_animations;
  FILL(p->death_host.hook_free, h_hook_free);
  FILL(p->death_host.log, h_log);
  if (!p->think_host.movement.move_client) { p->think_host.movement.ctx = h->ctx; p->think_host.movement.move_client = h->move_client; }
  p->think_host.ctx = o;
  p->think_host.touches.ctx = o;
  FILL(p->think_host.touches.is_trigger, h_touch_is_trigger);
  FILL(p->think_host.touches.touch, h_touch);
  if (h->timer_ownership) FILL(p->think_host.timer_ownership, h_timer_ownership);
  if (h->speed_multiplier) FILL(p->think_host.speed_multiplier, h_speed_multiplier);
  FILL(p->think_host.intermission_think, h_intermission_think);
  FILL(p->think_host.spectator_think, h_spectator_think);
  FILL(p->think_host.check_inactivity, h_check_inactivity);
  FILL(p->think_host.free_hook, h_hook_free);
  FILL(p->think_host.client_events, h_client_events);
  FILL(p->think_host.append_console_command, h_append);
  FILL(p->think_host.bot_test_aas, h_bot_test_aas);
  p->client_spawn_host.ctx = o;
  FILL(p->client_spawn_host.is_player, h_spawn_is_player);
  FILL(p->client_spawn_host.user_command, h_user_command);
  FILL(p->client_spawn_host.handicap, h_handicap);
  FILL(p->client_spawn_host.selected_player, h_selected_player);
  FILL(p->client_spawn_host.selected_weapon_after_targets, h_selected_weapon);
  FILL(p->client_spawn_host.effects, h_effects);
  FILL(p->client_spawn_host.targets, h_targets);
  p->session_services.ctx = o;
  FILL(p->session_services.print, h_print);
  p->match_host.ctx = o;
  FILL(p->match_host.append_console_command, h_append);
  FILL(p->match_host.send_server_command, h_send);
  FILL(p->match_host.set_configstring, h_cs_set);
  FILL(p->match_host.log, h_log);
  FILL(p->match_host.warn, h_print);
  FILL(p->match_host.bot_interbreed_end_match, h_bot_interbreed);
  p->mover_host.ctx = o;
  if (!p->mover_host.actors.observe) {
    p->mover_host.actors = h->mover_actors;
  }
  p->mover_spawn_host.ctx = o;
  FILL(p->mover_spawn_host.set_brush_model, h_set_brush_model);
  FILL(p->mover_spawn_host.warn, h_print);
  FILL(p->mover_spawn_host.initialize_binary, h_initialize_binary);
  FILL(p->mover_spawn_host.use_binary, h_use_binary);
  FILL(p->mover_spawn_host.blocked_door, h_blocked_door);
  FILL(p->mover_spawn_host.match_team, h_match_team);
  FILL(p->mover_spawn_host.set_state, h_set_state);
  p->admission_host.ctx = o;
  if (p->admission_host.bots.connect == NULL && p->admission_host.bots.reason == NULL) {
    p->admission_host.bots = h->bots.client;
    if (strcmp(h->bots.kind, "unavailable") == 0) {
      p->admission_host.bots.kind = QA_Q3_TA_BOTS_UNAVAILABLE; p->admission_host.bots.reason = h->bots.reason;
    } else p->admission_host.bots.kind = QA_Q3_TA_BOTS_AVAILABLE;
  }
  FILL(p->admission_host.get_userinfo, h_userinfo);
  FILL(p->admission_host.set_configstring, h_cs_set);
  FILL(p->admission_host.send_server_command, h_send);
  FILL(p->admission_host.log, h_log);
  p->command_parts.ctx = o;
  if (!p->command_parts.imports.send_server_command) {
    p->command_parts.imports.ctx = o;
    p->command_parts.imports.send_server_command = h_send; p->command_parts.imports.set_configstring = h_cs_set;
    p->command_parts.imports.append_console_command = h_append; p->command_parts.imports.get_cvar = h_engine_cvar;
    p->command_parts.imports.get_userinfo = h_userinfo; p->command_parts.imports.set_userinfo = h_set_userinfo;
    p->command_parts.imports.log = h_log; p->command_parts.imports.print = h_print;
  }
  if (h->grant_selected_arsenal) FILL(p->command_parts.grant_selected_arsenal, h_grant);
  if (h->give_selected_item) FILL(p->command_parts.give_selected_item, h_give);
  p->server_command_host.ctx = o;
  FILL(p->server_command_host.print, h_print);
  FILL(p->server_command_host.send_server_command, h_send);
  FILL(p->server_command_host.execute_console_now, h_exec);
  if (p->server_command_host.bots.run == NULL && p->server_command_host.bots.reason == NULL) {
    if (strcmp(h->bots.kind, "unavailable") == 0) {
      p->server_command_host.bots.kind = QA_Q3_TA_CAP_UNAVAILABLE; p->server_command_host.bots.reason = h->bots.reason;
    } else {
      p->server_command_host.bots.kind = QA_Q3_TA_CAP_AVAILABLE; p->server_command_host.bots.ctx = o;
      p->server_command_host.bots.run = h_bot_console;
    }
  }
  p->misc_spawn_host.ctx = o;
  FILL(p->misc_spawn_host.fire_grenade, h_misc_grenade);
  FILL(p->misc_spawn_host.fire_rocket, h_misc_rocket);
  FILL(p->misc_spawn_host.fire_plasma, h_misc_plasma);
  FILL(p->misc_spawn_host.warn, h_print);
  p->trigger_host.ctx = o;
  FILL(p->trigger_host.set_brush_model, h_set_brush_model);
  FILL(p->trigger_host.can_take_damage, h_can_take_damage);
  FILL(p->trigger_host.warn, h_print);
  if (!p->trigger_host.has_participants) { p->trigger_host.has_participants = true; p->trigger_host.participants = owner_participants(o); }
  p->target_runtime.ctx = o;
  FILL(p->target_runtime.send_server_command, h_send);
  FILL(p->target_runtime.set_configstring, h_cs_set);
  FILL(p->target_runtime.warn, h_print);
  FILL(p->target_runtime.touch_item, h_target_touch_item);
  if (!p->target_runtime.has_participants) { p->target_runtime.has_participants = true; p->target_runtime.participants = owner_participants(o); }
  p->worldspawn.ctx = o;
  FILL(p->worldspawn.log, h_log);
  if (!p->worldspawn.motd) p->worldspawn.motd = "";
  FILL(p->mover_host.adjust_area_portal_state, h_adjust_area_portal_state);
}
#undef FILL

/* Install a forwarder only when the caller supplied the leaf, so TS-undefined
 * NULL fields stay NULL. */
#define FWD(host, field, forwarder) do { if ((host).field) (host).field = (forwarder); } while (0)

qa_app_q3_source_subsystems_owner *qa_app_q3_source_subsystems_owner_create(
    qa_app_q3_source_subsystems_parts parts) {
  owner_t *o = calloc(1, sizeof *o);
  if (!o) return NULL;
  o->parts = parts;
  if (parts.host) { owner_fill_from_host(o); parts = o->parts; }
  qa_q3_base_product game_product = owner_game_product(o);
  /* TS field initializers (73-77). */
  qa_q3_game_level_init(&o->level);
  qa_game_random_init(&o->random, 0);
  qa_q3_ta_client_spawn_state_init(&o->client_spawn_state);
  qa_q3_ta_match_module_state_init(&o->match_module_state);
  /* remaps (123): print is engine.print via pool options print. */
  qa_q3_shader_remap_registry_init(&o->remaps, parts.pool_options.ctx, parts.pool_options.print);
  /* settings (124). */
  qa_q3_base_settings_host settings_host = parts.settings_host;
  settings_host.ctx = o;
  FWD(settings_host, send_server_command, fwd_settings_send);
  settings_host.remap_teams = fn_settings_remap_teams;
  o->settings = qa_q3_base_game_settings_create(settings_host, parts.product);
  if (!o->settings) { free(o); return NULL; }
  /* records (135-141). */
  qa_q3_base_record_host records_host = parts.records_host;
  records_host.ctx = o;
  FWD(records_host, ammo_timer_stored, fwd_ammo_timer);
  FWD(records_host, schedule, fwd_schedule);
  FWD(records_host, run_think, fwd_run_think_host);
  FWD(records_host, foreign, fwd_foreign);
  FWD(records_host, is_player, fwd_records_is_player);
  records_host.damage_call = fn_bridge_damage_call;
  records_host.admit_damage = fn_admit_damage;
  o->records = qa_q3_base_entity_records_create(records_host, parts.map_provider, parts.product);
  if (!o->records) { qa_q3_base_game_settings_destroy(o->settings); free(o); return NULL; }
  /* world (142-143). */
  qa_q3_base_world_adapter_host world_host = parts.world_host;
  world_host.ctx = o;
  FWD(world_host, collision, fwd_collision);
  if (parts.settings_host.cvars) {
    world_host.curves = fn_curves;
    world_host.player_curve_clip = fn_player_curve_clip;
  }
  qa_q3_base_world_adapter_init(&o->world, world_host, o->records);
  o->server_world = qa_q3_base_world_adapter_server_world(&o->world);
  o->spatial = qa_q3_base_world_adapter_spatial_queries(&o->world);
  /* pool (144-146). */
  qa_q3_entity_pool_options pool_options = parts.pool_options;
  pool_options.records = qa_q3_base_entity_records_pool_table(o->records);
  pool_options.product = game_product;
  pool_options.max_clients = parts.max_clients;
  pool_options.map_start_time = o->level.base.start_time;
  pool_options.ctx = o;
  FWD(pool_options, print, fwd_pool_print);
  pool_options.time = fn_time;
  pool_options.link = fn_pool_link;
  pool_options.unlink = fn_pool_unlink;
  qa_q3_entity_pool_init(&o->pool, pool_options);
  /* memory (147) + config (148). */
  qa_q3_game_memory_init(&o->memory, o,
      parts.memory_debug_integer ? fwd_memory_debug : fn_memory_debug,
      parts.pool_options.print ? fwd_pool_print : NULL);
  qa_q3_config_string_registry_init(&o->config, parts.config_store);
  /* itemLifecycle + registeredItems (149, 160-174). */
  qa_app_q3_item_lifecycle_services item_services = parts.item_services;
  item_services.ctx = o;
  FWD(item_services, log, fwd_item_log);
  FWD(item_services, warn, fwd_item_warn);
  FWD(item_services, get_userinfo, fwd_item_userinfo);
  FWD(item_services, preview_pickup, fwd_item_preview);
  FWD(item_services, admit_pickup, fwd_item_admit);
  item_services.trace = fn_world_trace;
  item_services.trace_actor = fn_world_trace_actor;
  item_services.team_pickup = fn_team_pickup;
  item_services.use_targets = fn_use_targets_item;
  item_services.sound_index = fn_sound_index;
  item_services.rand_value = fn_rand;
  item_services.random = fn_random;
  item_services.settings_integer = fn_settings_integer;
  qa_app_q3_item_lifecycle_host_init(&o->items, &o->pool, game_product, item_services);
  /* bridge/combat (150-152, createCombatBridge). */
  qa_q3_base_combat_bridge_host bridge_host = parts.bridge_host;
  bridge_host.product = parts.product;
  bridge_host.entities = &o->pool;
  bridge_host.records = o->records;
  bridge_host.world = &o->server_world;
  bridge_host.spatial = &o->spatial;
  bridge_host.ctx = o;
  FWD(bridge_host, armor_context, fwd_armor_context);
  FWD(bridge_host, debug_damage, fwd_debug_damage);
  FWD(bridge_host, is_live, fwd_bridge_is_live);
  FWD(bridge_host, apply_damage, fwd_apply_damage);
  FWD(bridge_host, damage_feedback, fwd_damage_feedback);
  FWD(bridge_host, foreign_damage_feedback, fwd_foreign_feedback);
  FWD(bridge_host, projectile_parent, fwd_projectile_parent);
  bridge_host.time = fn_time;
  bridge_host.intermission_queued = fn_bridge_intermission;
  bridge_host.game_type = fn_bridge_game_type;
  bridge_host.friendly_fire = fn_bridge_friendly;
  bridge_host.knockback = fn_bridge_knockback;
  bridge_host.check_hurt_carrier = fn_bridge_hurt_carrier;
  bridge_host.log_accuracy_hit = fn_bridge_log_accuracy;
  if (parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    bridge_host.check_obelisk_attack = fn_bridge_obelisk;
    bridge_host.invulnerability_effect = fn_bridge_invuln;
  }
  qa_q3_base_combat_bridge_init(&o->bridge, bridge_host);
  /* missiles (154-158) + weapons (159). Missionpack missile extras
   * (proxMineTimeout/random/soundIndex/invulnerabilityImpact) have no
   * landed missile_host fields; the host is retained verbatim. */
  qa_q3_missile_host missile_host = parts.missile_host;
  missile_host.product = game_product;
  missile_host.previous_time = o->level.previous_time;
  qa_q3_missile_runtime_init(&o->missiles, missile_host);
  qa_q3_weapon_host weapon_host = parts.weapon_host;
  weapon_host.combat = fn_combat(o);
  weapon_host.world = o->server_world;
  weapon_host.random = &o->random;
  weapon_host.quad_factor = owner_number(o, "g_quadfactor");
  weapon_host.ctx = o;
  FWD(weapon_host, fire_grenade, fwd_fire_grenade);
  FWD(weapon_host, fire_rocket, fwd_fire_rocket);
  FWD(weapon_host, fire_plasma, fwd_fire_plasma);
  FWD(weapon_host, fire_bfg, fwd_fire_bfg);
  FWD(weapon_host, fire_grapple, fwd_fire_grapple);
  FWD(weapon_host, fire_prox, fwd_fire_prox);
  FWD(weapon_host, fire_nail, fwd_fire_nail);
  FWD(weapon_host, damage_factor, fwd_damage_factor);
  FWD(weapon_host, trace, fwd_weapon_trace);
  FWD(weapon_host, authority_read, fwd_authority_read);
  FWD(weapon_host, participant, fwd_weapon_participant);
  FWD(weapon_host, native_by_actor, fwd_weapon_native);
  FWD(weapon_host, is_player, fwd_weapon_is_player);
  FWD(weapon_host, area_entities, fwd_area_entities);
  FWD(weapon_host, link_bounds, fwd_link_bounds);
  weapon_host.unlink = fn_weapon_unlink;
  weapon_host.restore = fn_weapon_restore;
  qa_q3_weapon_runtime_init(&o->weapons, weapon_host);
  /* drops (170-174). */
  memset(&o->drops, 0, sizeof o->drops);
  o->drops.launch.entities = &o->pool;
  o->drops.launch.product = game_product;
  o->drops.launch.touch_item = o->items.context.touch_callback;
  { qa_q3_entity_think_fn dropped = fn_dropped_flag_think;
    qa_q3_entity_think_fn check = fn_check_dropped_item;
    o->drops.launch.dropped_flag_think = qa_q3_callback_family_register(&o->pool.callbacks.think,
        "q3.item.droppedFlag", owner_callback(&dropped, o));
    o->drops.launch.check_dropped_team_item = owner_callback(&check, o); }
  o->drops.random_ctx = o;
  o->drops.random = fn_random;
  qa_q3_bind_launch_save_callbacks(o->drops.launch);
  /* team (createTeam) + death (createDeath). */
  qa_q3_ta_team_host team_host = parts.team_host;
  team_host.product = parts.product;
  team_host.has_obelisk = parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  team_host.pool = &o->pool;
  team_host.world = &o->server_world;
  team_host.spatial = &o->spatial;
  team_host.team_scores = &o->level.team_scores;
  team_host.sorted_clients = o->level.base.sorted_clients;
  team_host.sorted_client_count = QA_Q3_MAX_CLIENTS;
  team_host.ctx = o;
  FWD(team_host, send_server_command, fwd_team_send);
  FWD(team_host, set_configstring, fwd_team_set_cs);
  FWD(team_host, warn, fwd_team_warn);
  FWD(team_host, in_pvs, fwd_in_pvs);
  team_host.add_score = fn_add_score;
  team_host.calculate_ranks = fn_calculate_ranks;
  team_host.respawn_item = fn_respawn_item;
  qa_q3_ta_team_runtime_init(&o->team, team_host);
  qa_q3_death_host death_host = parts.death_host;
  death_host.ctx = o;
  FWD(death_host, hook_free, fwd_hook_free_death);
  FWD(death_host, log, fwd_death_log);
  death_host.product = game_product;
  death_host.character_death_selected = true;
  death_host.pool = &o->pool;
  death_host.world = o->server_world;
  death_host.point_contents = fn_point_contents;
  death_host.random_value = fn_random;
  death_host.crandom = fn_crandom;
  death_host.team_scores = &o->level.team_scores;
  death_host.frame = fn_death_frame;
  death_host.calculate_ranks = fn_calculate_ranks;
  death_host.send_scoreboard = fn_send_scoreboard;
  death_host.team_frag_bonuses = fn_team_frag_bonuses;
  death_host.return_flag = fn_return_flag_d;
  death_host.touch_item = o->drops.launch.touch_item;
  death_host.dropped_flag_think = o->drops.launch.dropped_flag_think;
  death_host.check_dropped_team_item = o->drops.launch.check_dropped_team_item;
  if (parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    death_host.neutral_obelisk = fn_neutral_obelisk;
    death_host.cube_timeout_seconds = fn_cube_timeout;
    death_host.start_kamikaze = fn_start_kamikaze;
  }
  qa_q3_death_runtime_init(&o->death, death_host);
  /* think (177-195). Policy/effects/clientEvents leaf fns stay external:
   * TS builds fresh policy()/effects() contexts per call, which the
   * landed think host receives as fn+ctx from parts. */
  qa_q3_ta_client_think_host think_host = parts.think_host;
  think_host.pool = &o->pool;
  think_host.world = &o->server_world;
  think_host.spatial = &o->spatial;
  think_host.touches.ctx = o;
  FWD(think_host.touches, is_trigger, fwd_touch_is_trigger);
  FWD(think_host.touches, touch, fwd_touch_touch);
  think_host.touches.native = fn_touch_native;
  think_host.effects.combat = fn_combat(o);
  think_host.ctx = o;
  FWD(think_host, timer_ownership, fwd_timer_ownership);
  FWD(think_host, speed_multiplier, fwd_speed_multiplier);
  FWD(think_host, intermission_think, fwd_intermission_think);
  FWD(think_host, spectator_think, fwd_spectator_think);
  FWD(think_host, check_inactivity, fwd_check_inactivity);
  FWD(think_host, free_hook, fwd_free_hook);
  FWD(think_host, client_events, fwd_client_events);
  FWD(think_host, append_console_command, fwd_append_think);
  FWD(think_host, bot_test_aas, fwd_bot_test_aas);
  think_host.frame = fn_think_frame;
  think_host.settings = fn_think_settings;
  think_host.set_pmove_msec = fn_set_pmove_msec;
  think_host.check_gauntlet_attack = fn_check_gauntlet;
  think_host.respawn = fn_respawn_entity;
  think_host.is_door_trigger = fn_is_door_trigger;
  qa_q3_ta_client_think_runtime_init(&o->think, think_host);
  /* spawns (196-203). */
  qa_q3_ta_client_spawn_host spawn_host = parts.client_spawn_host;
  spawn_host.pool = &o->pool;
  spawn_host.world = &o->server_world;
  spawn_host.spatial = &o->spatial;
  spawn_host.random = &o->random;
  spawn_host.think_pool = &o->pool;
  spawn_host.think_world = &o->server_world;
  { qa_q3_entity_die_fn pd = fn_player_die_cb, bd = fn_body_die_cb;
    spawn_host.player_die = owner_callback(&pd, o);
    spawn_host.body_die = owner_callback(&bd, o); }
  spawn_host.ctx = o;
  FWD(spawn_host, is_player, fwd_spawn_is_player);
  FWD(spawn_host, user_command, fwd_user_command);
  FWD(spawn_host, handicap, fwd_handicap);
  FWD(spawn_host, selected_player, fwd_selected_player);
  FWD(spawn_host, selected_weapon_after_targets, fwd_selected_weapon_after_targets);
  FWD(spawn_host, effects, fwd_spawn_effects);
  FWD(spawn_host, targets, fwd_spawn_targets);
  spawn_host.frame = fn_spawn_frame;
  spawn_host.find_intermission_point = fn_find_intermission;
  spawn_host.move_to_intermission = fn_move_to_intermission;
  spawn_host.kill_box = fn_kill_box;
  spawn_host.client_think = fn_client_think_fwd;
  qa_q3_ta_client_spawn_runtime_init(&o->spawns, spawn_host, &o->client_spawn_state);
  /* session (204-205, sessionState 365-372). */
  /* The manager validates its world at init; refresh keeps it current after. */
  memset(&o->session_world, 0, sizeof o->session_world);
  o->session_world.clients = o->pool.clients;
  o->session_world.client_count = QA_Q3_MAX_CLIENTS;
  o->session_world.max_clients = o->pool.max_clients;
  o->session_world.team_scores = &o->level.team_scores;
  qa_q3_ta_session_services session_services = parts.session_services;
  if (!session_services.cvars.get) {
    session_services.cvars.ctx = o;
    session_services.cvars.get = fn_session_cvar_get;
    session_services.cvars.set = fn_session_cvar_set;
  }
  session_services.ctx = o;
  FWD(session_services, print, fwd_session_print);
  session_services.broadcast_team_change = fn_broadcast_team_change;
  qa_q3_ta_game_session_manager_init(&o->session, &o->session_world, session_services);
  /* match (createMatch). */
  qa_q3_ta_match_host match_host = parts.match_host;
  match_host.product = parts.product;
  match_host.state = &o->level.base;
  match_host.pool = &o->pool;
  match_host.team_scores = &o->level.team_scores;
  match_host.random = &o->random;
  match_host.ctx = o;
  FWD(match_host, append_console_command, fwd_match_append);
  FWD(match_host, send_server_command, fwd_match_send);
  FWD(match_host, set_configstring, fwd_match_set_cs);
  FWD(match_host, set_cvar, fwd_match_set_cvar);
  FWD(match_host, log, fwd_match_log);
  FWD(match_host, warn, fwd_match_warn);
  FWD(match_host, bot_interbreed_end_match, fwd_bot_interbreed);
  match_host.settings = fn_match_settings;
  match_host.select_spawn_point = fn_select_spawn_point;
  match_host.respawn = fn_respawn_entity;
  match_host.set_team = fn_set_team_str;
  match_host.stop_following = fn_stop_following;
  match_host.send_scoreboard = fn_send_scoreboard;
  match_host.client_userinfo_changed = fn_userinfo_changed;
  match_host.write_session_data = fn_write_session_data;
  if (!match_host.set_cvar) match_host.set_cvar = fn_set_cvar_named;
  match_host.update_tournament_info = fn_update_tournament;
  if (parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) match_host.single_player = fn_single_player;
  else match_host.spawn_models_on_victory_pads = fn_victory_pads;
  qa_q3_ta_match_runtime_init(&o->match, match_host, &o->match_module_state);
  /* arenas (207). */
  qa_q3_ta_arena_host arena_host = {&o->match, &o->server_world, parts.settings_host.cvars, &o->config};
  qa_q3_ta_arena_runtime_init(&o->arenas, arena_host);
  /* movers (208-210) + moverSpawns (211-213). */
  qa_q3_mover_host mover_host = parts.mover_host;
  mover_host.product = game_product;
  qa_q3_mover_runtime_init(&o->movers, mover_host);
  qa_q3_mover_spawn_host mover_spawn_host = parts.mover_spawn_host;
  mover_spawn_host.ctx = o;
  FWD(mover_spawn_host, set_brush_model, fwd_set_brush_model_ms);
  FWD(mover_spawn_host, warn, fwd_ms_warn);
  FWD(mover_spawn_host, initialize_binary, fwd_initialize_binary);
  FWD(mover_spawn_host, use_binary, fwd_use_binary);
  FWD(mover_spawn_host, blocked_door, fwd_blocked_door);
  FWD(mover_spawn_host, match_team, fwd_match_team);
  FWD(mover_spawn_host, set_state, fwd_set_state);
  FWD(mover_spawn_host, remap_shader, fwd_ms_remap);
  mover_spawn_host.movers = &o->movers;
  mover_spawn_host.pool = &o->pool;
  mover_spawn_host.world = o->server_world;
  mover_spawn_host.teleport.combat = fn_combat(o);
  mover_spawn_host.teleport.kill_box = parts.kill_box;
  mover_spawn_host.teleport.world = o->server_world;
  mover_spawn_host.time = fn_time;
  mover_spawn_host.gravity = fn_gravity;
  mover_spawn_host.sound_index = fn_sound_index;
  if (!mover_spawn_host.remap_shader) mover_spawn_host.remap_shader = fn_remap_shader;
  qa_q3_mover_spawn_runtime_init(&o->mover_spawns, mover_spawn_host);
  /* personalPortal (createPersonalPortal: missionpack only). */
  if (parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_q3_personal_portal_host portal_host = parts.portal_host;
    portal_host.ctx = o;
    FWD(portal_host, map_drop_carried_flag, fwd_map_drop_flag);
    FWD(portal_host, map_teleport, fwd_map_teleport);
    portal_host.combat = fn_combat(o);
    portal_host.world = o->server_world;
    /* combat.spatial and world are the one owned world adapter (TS identity check). */
    portal_host.spatial_is_world = true;
    portal_host.kill_box = parts.kill_box;
    portal_host.model_index = fn_model_index;
    portal_host.random_value = fn_random;
    portal_host.touch_item = o->drops.launch.touch_item;
    portal_host.dropped_flag_think = o->drops.launch.dropped_flag_think;
    portal_host.check_dropped_team_item = o->drops.launch.check_dropped_team_item;
    qa_q3_personal_portal_runtime_init(&o->portal, portal_host);
  }
  /* admission (createAdmission). */
  qa_q3_ta_client_admission_host admission_host = parts.admission_host;
  admission_host.product = parts.product;
  admission_host.pool = &o->pool;
  admission_host.team_scores = &o->level.team_scores;
  admission_host.world = &o->server_world;
  admission_host.state = &o->level.base;
  admission_host.session = &o->session;
  admission_host.ctx = o;
  FWD(admission_host, get_userinfo, fwd_admission_userinfo);
  FWD(admission_host, set_configstring, fwd_admission_set_cs);
  FWD(admission_host, send_server_command, fwd_admission_send);
  FWD(admission_host, log, fwd_admission_log);
  admission_host.new_session = fn_new_session;
  admission_host.client_spawn = fn_client_spawn_fwd;
  admission_host.toss_client_items = fn_toss_items;
  admission_host.toss_client_persistant_powerups = fn_toss_powerups;
  admission_host.toss_client_cubes = fn_toss_cubes;
  admission_host.calculate_ranks = fn_calculate_ranks;
  admission_host.broadcast_team_change = fn_broadcast_team_change;
  admission_host.stop_following = fn_stop_following;
  admission_host.settings = fn_admission_settings;
  admission_host.filter_packet = fn_filter_packet;
  qa_q3_ta_client_admission_runtime_init(&o->admission, admission_host);
  /* commands (createCommands via the landed host builder). */
  qa_app_q3_game_command_host_parts command_parts = parts.command_parts;
  command_parts.pool = &o->pool;
  command_parts.state = &o->level.base;
  command_parts.team_scores = &o->level.team_scores;
  command_parts.team = &o->team;
  command_parts.spawn = &o->spawns;
  command_parts.admission = &o->admission;
  command_parts.match = &o->match;
  command_parts.items = &o->items;
  command_parts.teleport.combat = fn_combat(o);
  command_parts.teleport.kill_box = parts.kill_box;
  command_parts.teleport.world = o->server_world;
  command_parts.ctx = o;
  FWD(command_parts, grant_selected_arsenal, fwd_grant_arsenal);
  FWD(command_parts, give_selected_item, fwd_give_item);
  command_parts.player_die = fn_player_die_cb;
  if (!command_parts.settings) command_parts.settings = &o->command_settings;
  qa_q3_ta_game_command_runtime_init(&o->commands, qa_app_q3_build_game_command_host(command_parts));
  /* serverCommands (218-225). */
  qa_q3_ta_game_server_command_state_init(&o->server_command_state);
  qa_q3_ta_game_server_command_host server_host = parts.server_command_host;
  server_host.ctx = o;
  FWD(server_host, read_vm_cvar, fwd_read_vm_cvar);
  FWD(server_host, print, fwd_server_print);
  FWD(server_host, send_server_command, fwd_server_send);
  FWD(server_host, execute_console_now, fwd_server_exec);
  if (!server_host.read_vm_cvar) server_host.read_vm_cvar = fn_read_vm_cvar;
  server_host.set_team = fn_set_team_str;
  server_host.memory.kind = QA_Q3_TA_CAP_AVAILABLE;
  server_host.memory.ctx = o;
  server_host.memory.run = fn_memory_status_cap;
  server_host.podium.kind = QA_Q3_TA_CAP_AVAILABLE;
  server_host.podium.ctx = o;
  server_host.podium.run = fn_podium_cap;
  qa_q3_ta_game_server_command_runtime_init(&o->server_commands, &o->pool,
      parts.settings_host.cvars, server_host, &o->server_command_state);
  /* targets runtime for spawnHandlers (shared fields from 463-477). */
  /* init zeroes the runtime, so it must run before the caller's fields land. */
  qa_q3_target_runtime_init(&o->targets, o, &o->pool);
  { qa_q3_target_location_state location = o->targets.location;
    o->targets = parts.target_runtime;
    o->targets.ctx = o; o->targets.entities = &o->pool; o->targets.location = location; }
  /* TS passes this.itemLifecycle, built over this same pool and world. */
  o->targets.item_lifecycle_matches = true;
  FWD(o->targets, send_server_command, fwd_target_send);
  FWD(o->targets, touch_item, fwd_target_touch_item);
  o->targets.world = o->server_world;
  o->targets.kill_box = parts.kill_box;
  o->targets.combat = fn_combat;
  o->targets.gravity = fn_gravity;
  o->targets.rand_value = fn_rand;
  o->targets.crandom = fn_crandom;
  o->targets.sound_index = fn_sound_index;
  o->targets.add_score = fn_add_score;
  o->targets.return_flag = fn_return_flag_d;
  o->targets.remap_shader = fn_remap_shader;
  if (!o->targets.set_configstring) o->targets.set_configstring = fn_config_set;
  if (!o->targets.warn && parts.pool_options.print) o->targets.warn = fwd_pool_print;
  o->spawn_item_host.items = &o->items;
  o->spawn_item_host.cvars = parts.settings_host.cvars;
  qa_app_q3_source_subsystems_owner_refresh(o);
  return o;
}
void qa_app_q3_source_subsystems_owner_destroy(qa_app_q3_source_subsystems_owner *o) {
  if (!o) return;
  qa_q3_ta_game_server_command_runtime_destroy(&o->server_commands);
  qa_q3_ta_game_server_command_state_destroy(&o->server_command_state);
  qa_app_q3_item_lifecycle_host_destroy(&o->items);
  qa_q3_base_combat_bridge_destroy(&o->bridge);
  qa_q3_base_world_adapter_destroy(&o->world);
  qa_q3_entity_pool_destroy(&o->pool);
  qa_q3_base_entity_records_free(o->records);
  qa_q3_base_game_settings_destroy(o->settings);
  qa_q3_shader_remap_registry_destroy(&o->remaps);
  qa_q3_game_level_destroy(&o->level);
  free(o->cvar_scratch); free(o->userinfo_scratch); free(o->handicap_scratch);
  free(o);
}
qa_q3_game_level *qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_subsystems_owner *o) { return &o->level; }
qa_q3_entity_pool *qa_app_q3_source_subsystems_owner_pool(qa_app_q3_source_subsystems_owner *o) { return &o->pool; }
qa_q3_base_entity_records *qa_app_q3_source_subsystems_owner_records(qa_app_q3_source_subsystems_owner *o) { return o->records; }
qa_q3_base_world_adapter *qa_app_q3_source_subsystems_owner_world(qa_app_q3_source_subsystems_owner *o) { return &o->world; }
qa_q3_base_game_settings *qa_app_q3_source_subsystems_owner_settings(qa_app_q3_source_subsystems_owner *o) { return o->settings; }
qa_q3_game_combat_context *qa_app_q3_source_subsystems_owner_combat(qa_app_q3_source_subsystems_owner *o) { return fn_combat(o); }
qa_q3_ta_game_command_runtime *qa_app_q3_source_subsystems_owner_commands(qa_app_q3_source_subsystems_owner *o) { return &o->commands; }
qa_q3_ta_game_server_command_runtime *qa_app_q3_source_subsystems_owner_server_commands(qa_app_q3_source_subsystems_owner *o) { return &o->server_commands; }
qa_q3_ta_client_spawn_runtime *qa_app_q3_source_subsystems_owner_spawns(qa_app_q3_source_subsystems_owner *o) { return &o->spawns; }
qa_q3_ta_client_admission_runtime *qa_app_q3_source_subsystems_owner_admission(qa_app_q3_source_subsystems_owner *o) { return &o->admission; }
qa_q3_ta_team_runtime *qa_app_q3_source_subsystems_owner_team(qa_app_q3_source_subsystems_owner *o) { return &o->team; }
qa_q3_ta_match_runtime *qa_app_q3_source_subsystems_owner_match(qa_app_q3_source_subsystems_owner *o) { return &o->match; }
qa_app_q3_item_lifecycle_host *qa_app_q3_source_subsystems_owner_items(qa_app_q3_source_subsystems_owner *o) { return &o->items; }
qa_q3_base_product qa_app_q3_source_subsystems_owner_product(qa_app_q3_source_subsystems_owner *o) { return o->parts.product; }
double qa_app_q3_source_subsystems_owner_game_type(qa_app_q3_source_subsystems_owner *o) { return owner_game_type(o); }
qa_game_random *qa_app_q3_source_subsystems_owner_random(qa_app_q3_source_subsystems_owner *o) { return &o->random; }
qa_q3_game_memory *qa_app_q3_source_subsystems_owner_memory(qa_app_q3_source_subsystems_owner *o) { return &o->memory; }
qa_q3_config_string_registry *qa_app_q3_source_subsystems_owner_config(qa_app_q3_source_subsystems_owner *o) { return &o->config; }
qa_q3_missile_runtime *qa_app_q3_source_subsystems_owner_missiles(qa_app_q3_source_subsystems_owner *o) { return &o->missiles; }
qa_q3_ta_arena_runtime *qa_app_q3_source_subsystems_owner_arenas(qa_app_q3_source_subsystems_owner *o) { return &o->arenas; }

/* ---- qa_app_q3_source_subsystems table over the owner ----
 * capture_part builds each native part's captureSaveState() value from
 * the module's struct capture (t_capture_part); restore_part reads them
 * back (missiles restore through their own reader in finishNativeRestore). */
static qa_app_q3_runtime_entity_view owner_view(owner_t *o, qa_q3_game_entity *entity) {
  qa_app_q3_runtime_entity_view view; memset(&view, 0, sizeof view);
  view.next_slot = -1;
  if (!entity) return view;
  view.present = true;
  view.inuse = qa_q3_game_entity_inuse(entity);
  view.has_client = entity->client != NULL;
  view.never_free = entity->never_free;
  view.physics_object = entity->physics_object;
  view.slot = entity->slot;
  view.e_type = entity->s.e_type;
  view.event = entity->s.event;
  view.event_time = entity->event_time;
  view.flags = entity->flags;
  view.weapon = entity->s.weapon;
  qa_q3_entity_state_record *record = &view.state;
  const qa_q3_entity_state_record *s = &entity->s;
  record->number = qa_js_to_int32(s->number); record->e_type = qa_js_to_int32(s->e_type);
  record->e_flags = qa_js_to_int32(s->e_flags); record->time = qa_js_to_int32(s->time);
  record->time2 = qa_js_to_int32(s->time2); record->origin = s->origin; record->origin2 = s->origin2;
  record->angles = s->angles; record->angles2 = s->angles2;
  record->other_entity_num = qa_js_to_int32(s->other_entity_num);
  record->other_entity_num2 = qa_js_to_int32(s->other_entity_num2);
  record->ground_entity_num = qa_js_to_int32(s->ground_entity_num);
  record->constant_light = qa_js_to_int32(s->constant_light); record->loop_sound = qa_js_to_int32(s->loop_sound);
  record->modelindex = qa_js_to_int32(s->modelindex); record->modelindex2 = qa_js_to_int32(s->modelindex2);
  record->client_num = qa_js_to_int32(s->client_num); record->frame = qa_js_to_int32(s->frame);
  record->solid = qa_js_to_int32(s->solid); record->event = qa_js_to_int32(s->event);
  record->event_parm = qa_js_to_int32(s->event_parm); record->powerups = qa_js_to_int32(s->powerups);
  record->weapon = qa_js_to_int32(s->weapon); record->legs_anim = qa_js_to_int32(s->legs_anim);
  record->torso_anim = qa_js_to_int32(s->torso_anim); record->generic1 = qa_js_to_int32(s->generic1);
  record->pos.type = qa_js_to_int32(s->pos.type); record->pos.time = qa_js_to_int32(s->pos.time);
  record->pos.duration = qa_js_to_int32(s->pos.duration); record->pos.base = s->pos.base; record->pos.delta = s->pos.delta;
  record->apos.type = qa_js_to_int32(s->apos.type); record->apos.time = qa_js_to_int32(s->apos.time);
  record->apos.duration = qa_js_to_int32(s->apos.duration); record->apos.base = s->apos.base; record->apos.delta = s->apos.delta;
  view.actor = qa_q3_game_entity_actor(entity);
  view.current_origin = qa_q3_base_entity_shared_current_origin(&entity->r);
  view.classname = qa_q3_game_entity_classname(entity);
  view.next_slot = entity->teamchain ? entity->teamchain->slot : -1;
  (void)o;
  return view;
}
static void t_settings_register(void *ctx, const char *build_date) {
  owner_t *o = ctx;
  qa_q3_base_game_settings_register(o->settings, build_date);
  o->settings_ready = true;
  qa_app_q3_source_subsystems_owner_refresh(o);
}
static void t_settings_update(void *ctx) { qa_q3_base_game_settings_update(((owner_t *)ctx)->settings); }
static double t_settings_integer(void *ctx, const char *n) { return qa_q3_base_game_settings_integer(((owner_t *)ctx)->settings, n); }
static double t_settings_number(void *ctx, const char *n) { return qa_q3_base_game_settings_number(((owner_t *)ctx)->settings, n); }
static const char *t_settings_string(void *ctx, const char *n) { return qa_q3_base_game_settings_string(((owner_t *)ctx)->settings, n); }
static double t_settings_modcount(void *ctx, const char *n) {
  return qa_q3_base_game_settings_snapshot(((owner_t *)ctx)->settings, n)->modification_count;
}
static void t_settings_restore(void *ctx, const qa_save_value *v) {
  owner_t *o = ctx;
  qa_q3_base_game_settings_restore_save_state(o->settings, v);
  o->settings_ready = true;
  qa_app_q3_source_subsystems_owner_refresh(o);
}
static double t_random_seed(void *ctx) { return (double)((owner_t *)ctx)->random.seed; }
static void t_random_reset(void *ctx, double seed) { qa_game_random_reset(&((owner_t *)ctx)->random, (int64_t)seed); }
static qa_app_q3_runtime_entity_view t_by_actor(void *ctx, qa_actor_id actor) {
  owner_t *o = ctx;
  return owner_view(o, qa_q3_base_entity_records_by_actor(o->records, true, actor));
}
static qa_app_q3_runtime_entity_view t_at(void *ctx, double slot) {
  owner_t *o = ctx;
  return owner_view(o, qa_q3_entity_pool_get(&o->pool, slot));
}
static double t_pool_max_clients(void *ctx) { return ((owner_t *)ctx)->pool.max_clients; }
static double t_pool_num_entities(void *ctx) { return ((owner_t *)ctx)->pool.num_entities; }
static void t_pool_init_clients(void *ctx, double max_clients) {
  qa_q3_entity_pool_initialize_clients(&((owner_t *)ctx)->pool, max_clients);
}
static void t_records_activate(void *ctx, double slot) {
  (void)qa_q3_base_entity_records_activate(((owner_t *)ctx)->records, slot);
}
static void t_records_close(void *ctx) { qa_q3_base_entity_records_close(((owner_t *)ctx)->records); }
static qa_app_q3_runtime_entity_view t_records_attach(void *ctx, double slot, qa_owned_actor actor) {
  owner_t *o = ctx;
  return owner_view(o, qa_q3_base_entity_records_attach(o->records, slot, actor, true));
}
static const char *t_expire_events(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (!entity) return "inactive";
  return qa_q3_entity_event_status_name(qa_q3_entity_pool_expire_events(&o->pool, entity));
}
static bool t_linked(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_base_link_state state;
  return qa_q3_base_world_adapter_link_state(&o->world, slot, &state) && state.linked;
}
/* Checkpoint-value builders for the captureSaveState() objects of the
 * native parts (TS key order). put() takes ownership of child and clears
 * *ok on failure. */
static void put(qa_save_value *object, const char *key, qa_save_value *child, bool *ok) {
  if (!*ok || object == NULL || child == NULL || !qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    *ok = false;
  }
}
static void push(qa_save_value *array, qa_save_value *child, bool *ok) {
  if (!*ok || array == NULL || child == NULL || !qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    *ok = false;
  }
}
static qa_save_value *finish(qa_save_value *value, bool ok) {
  if (ok) return value;
  qa_save_value_free(value);
  return NULL;
}
static qa_save_value *nullable_slot(bool present, double slot) {
  return present ? qa_save_value_number(slot) : qa_save_value_null();
}
static qa_save_value *vector_value(qa_vec3 v) {
  bool ok = true;
  qa_save_value *object = qa_save_value_object();
  put(object, "x", qa_save_value_number(v.x), &ok);
  put(object, "y", qa_save_value_number(v.y), &ok);
  put(object, "z", qa_save_value_number(v.z), &ok);
  return finish(object, ok);
}
static qa_save_value *number_list(const double *values, size_t count) {
  bool ok = true;
  qa_save_value *array = qa_save_value_array();
  for (size_t i = 0; ok && i < count; i++) push(array, qa_save_value_number(values[i]), &ok);
  return finish(array, ok);
}
/* save-level.ts captureQ3Level over save-values.ts captureLevelValues. */
static qa_save_value *capture_level_value(qa_q3_game_level *level) {
  qa_q3_level_capture capture = qa_q3_capture_level(level);
  const qa_q3_level_values *v = &capture.values;
  bool ok = true;
  qa_save_value *root = qa_save_value_object(), *values = qa_save_value_object();
  put(values, "time", qa_save_value_number(v->time), &ok);
  put(values, "startTime", qa_save_value_number(v->start_time), &ok);
  put(values, "warmupTime", qa_save_value_number(v->warmup_time), &ok);
  put(values, "warmupModificationCount", qa_save_value_number(v->warmup_modification_count), &ok);
  put(values, "restarted", qa_save_value_boolean(v->restarted), &ok);
  put(values, "numConnectedClients", qa_save_value_number(v->num_connected_clients), &ok);
  put(values, "numNonSpectatorClients", qa_save_value_number(v->num_non_spectator_clients), &ok);
  put(values, "numPlayingClients", qa_save_value_number(v->num_playing_clients), &ok);
  put(values, "numVotingClients", qa_save_value_number(v->num_voting_clients), &ok);
  put(values, "follow1", qa_save_value_number(v->follow1), &ok);
  put(values, "follow2", qa_save_value_number(v->follow2), &ok);
  put(values, "intermissionTime", qa_save_value_number(v->intermission_time), &ok);
  put(values, "intermissionQueued", qa_save_value_number(v->intermission_queued), &ok);
  put(values, "intermissionOrigin", vector_value(v->intermission_origin), &ok);
  put(values, "intermissionAngle", vector_value(v->intermission_angle), &ok);
  put(values, "changemap", v->changemap ? qa_save_value_string(v->changemap) : qa_save_value_null(), &ok);
  put(values, "readyToExit", qa_save_value_boolean(v->ready_to_exit), &ok);
  put(values, "exitTime", qa_save_value_number(v->exit_time), &ok);
  put(values, "frameNum", qa_save_value_number(v->frame_num), &ok);
  put(values, "previousTime", qa_save_value_number(v->previous_time), &ok);
  put(values, "newSession", qa_save_value_boolean(v->new_session), &ok);
  put(values, "frySound", qa_save_value_number(v->fry_sound), &ok);
  put(root, "values", values, &ok);
  put(root, "teamScores", number_list(capture.team_scores, capture.team_score_count), &ok);
  put(root, "numTeamVotingClients", number_list(capture.num_team_voting_clients, 2), &ok);
  put(root, "sortedClients", number_list(capture.sorted_clients, capture.sorted_count), &ok);
  qa_save_value *vote = qa_save_value_object();
  put(vote, "time", qa_save_value_number(capture.vote.time), &ok);
  put(vote, "yes", qa_save_value_number(capture.vote.yes), &ok);
  put(vote, "no", qa_save_value_number(capture.vote.no), &ok);
  put(vote, "string", qa_save_value_string(capture.vote.string ? capture.vote.string : ""), &ok);
  put(vote, "displayString", qa_save_value_string(capture.vote.display_string ? capture.vote.display_string : ""), &ok);
  put(vote, "executeTime", qa_save_value_number(capture.vote.execute_time), &ok);
  put(root, "vote", vote, &ok);
  qa_save_value *team_votes = qa_save_value_array();
  for (size_t i = 0; i < 2; i++) {
    qa_save_value *entry = qa_save_value_object();
    put(entry, "time", qa_save_value_number(capture.team_votes[i].time), &ok);
    put(entry, "yes", qa_save_value_number(capture.team_votes[i].yes), &ok);
    put(entry, "no", qa_save_value_number(capture.team_votes[i].no), &ok);
    put(entry, "string", qa_save_value_string(capture.team_votes[i].string ? capture.team_votes[i].string : ""), &ok);
    push(team_votes, entry, &ok);
  }
  put(root, "teamVotes", team_votes, &ok);
  qa_q3_level_capture_destroy(&capture);
  return finish(root, ok);
}
static qa_save_value *t_capture_part(void *ctx, const char *name) {
  owner_t *o = ctx;
  bool ok = true;
  if (strcmp(name, "level") == 0) return capture_level_value(&o->level);
  if (strcmp(name, "locations") == 0) {
    /* targets.ts TargetLocationState.captureSaveState */
    const qa_q3_target_location_save saved = qa_q3_target_location_capture(&o->targets.location);
    qa_save_value *root = qa_save_value_object();
    put(root, "linked", qa_save_value_boolean(saved.linked), &ok);
    put(root, "head", nullable_slot(saved.has_head, saved.head_slot), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "spawns") == 0) {
    /* client-spawn.ts ClientSpawnRuntime.captureSaveState */
    const qa_q3_ta_client_spawn_save_state saved = qa_q3_ta_client_spawn_runtime_capture_save_state(&o->spawns);
    qa_save_value *root = qa_save_value_object();
    if (!saved.has_body_queue) put(root, "bodyQueue", qa_save_value_null(), &ok);
    else {
      qa_save_value *queue = qa_save_value_object();
      put(queue, "entities", number_list(saved.entity_slots, QA_Q3_TA_BODY_QUEUE_SIZE), &ok);
      put(queue, "index", qa_save_value_number(saved.index), &ok);
      put(root, "bodyQueue", queue, &ok);
    }
    return finish(root, ok);
  }
  if (strcmp(name, "match") == 0) {
    const qa_q3_ta_match_save_state saved = qa_q3_ta_match_runtime_capture_save_state(&o->match);
    qa_save_value *root = qa_save_value_object();
    put(root, "passwordModificationCount", qa_save_value_number(saved.password_modification_count), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "remaps") == 0) {
    qa_q3_shader_remap_state saved = qa_q3_shader_remap_registry_capture(&o->remaps);
    qa_save_value *root = qa_save_value_array();
    for (size_t i = 0; i < saved.count; i++) {
      qa_save_value *entry = qa_save_value_object();
      put(entry, "oldName", qa_save_value_string(saved.remaps[i].old_name), &ok);
      put(entry, "newName", qa_save_value_string(saved.remaps[i].new_name), &ok);
      put(entry, "timeOffset", qa_save_value_number(saved.remaps[i].time_offset), &ok);
      push(root, entry, &ok);
    }
    qa_q3_shader_remap_state_destroy(&saved);
    return finish(root, ok);
  }
  if (strcmp(name, "settings") == 0) {
    /* settings.ts captureSaveState over save-module-values.ts captureModuleCvar */
    size_t count = 0;
    qa_q3_module_cvar_capture *rows = qa_q3_base_game_settings_capture_save_state(o->settings, &count);
    qa_save_value *root = qa_save_value_array();
    for (size_t i = 0; i < count; i++) {
      const qa_q3_module_cvar_capture *row = &rows[i];
      qa_save_value *entry = qa_save_value_object();
      put(entry, "name", qa_save_value_string(row->name), &ok);
      put(entry, "value", qa_save_value_string(row->value), &ok);
      put(entry, "resetValue", qa_save_value_string(row->reset_value), &ok);
      put(entry, "latchedValue", row->latched_value ? qa_save_value_string(row->latched_value) : qa_save_value_null(), &ok);
      put(entry, "flags", qa_save_value_number(row->flags), &ok);
      put(entry, "modified", qa_save_value_boolean(row->modified), &ok);
      put(entry, "modificationCount", qa_save_value_number(row->modification_count), &ok);
      put(entry, "numericValue", qa_save_value_number(row->numeric_value), &ok);
      put(entry, "integerValue", qa_save_value_number(row->integer_value), &ok);
      push(root, entry, &ok);
    }
    free(rows);
    return finish(root, ok);
  }
  if (strcmp(name, "memory") == 0) {
    qa_q3_game_memory_save_state saved = qa_q3_game_memory_capture_save_state(&o->memory);
    qa_save_value *root = qa_save_value_object();
    put(root, "pool", qa_save_value_bytes(saved.pool, saved.pool_length), &ok);
    put(root, "allocPoint", qa_save_value_number(saved.alloc_point), &ok);
    qa_q3_game_memory_save_state_destroy(&saved);
    return finish(root, ok);
  }
  if (strcmp(name, "registeredItems") == 0)
    return qa_q3_item_registry_capture_save_state(&o->items.context.registry);
  if (strcmp(name, "bridge") == 0) {
    const qa_q3_base_combat_bridge_save_state saved = qa_q3_base_combat_bridge_capture_save_state(&o->bridge);
    qa_save_value *root = qa_save_value_object();
    put(root, "sequence", qa_save_value_number(saved.sequence), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "missiles") == 0) return qa_q3_missile_runtime_capture_save_state(&o->missiles);
  if (strcmp(name, "team") == 0) {
    const qa_q3_ta_team_save_state saved = qa_q3_ta_team_runtime_capture_save_state(&o->team);
    const qa_q3_ta_team_game_state *t = &saved.state;
    qa_save_value *root = qa_save_value_object(), *state = qa_save_value_object();
    put(state, "lastFlagCapture", qa_save_value_number(t->last_flag_capture), &ok);
    put(state, "lastCaptureTeam", qa_save_value_number(t->last_capture_team), &ok);
    put(state, "redStatus", qa_save_value_number(t->red_status), &ok);
    put(state, "blueStatus", qa_save_value_number(t->blue_status), &ok);
    put(state, "flagStatus", qa_save_value_number(t->flag_status), &ok);
    put(state, "redTakenTime", qa_save_value_number(t->red_taken_time), &ok);
    put(state, "blueTakenTime", qa_save_value_number(t->blue_taken_time), &ok);
    put(state, "redObeliskAttackedTime", qa_save_value_number(t->red_obelisk_attacked_time), &ok);
    put(state, "blueObeliskAttackedTime", qa_save_value_number(t->blue_obelisk_attacked_time), &ok);
    put(root, "state", state, &ok);
    put(root, "neutralObelisk", nullable_slot(saved.has_neutral_obelisk, saved.neutral_obelisk_slot), &ok);
    put(root, "lastTeamLocationTime", qa_save_value_number(saved.last_team_location_time), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "arenas") == 0) {
    const qa_q3_ta_arena_save_state saved = qa_q3_ta_arena_runtime_capture_save_state(&o->arenas);
    qa_save_value *root = qa_save_value_object();
    put(root, "podium1", nullable_slot(saved.has_podium1, saved.podium1_slot), &ok);
    put(root, "podium2", nullable_slot(saved.has_podium2, saved.podium2_slot), &ok);
    put(root, "podium3", nullable_slot(saved.has_podium3, saved.podium3_slot), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "personalPortal") == 0) {
    qa_save_value *root = qa_save_value_object();
    put(root, "portalSequence", qa_save_value_number(o->portal.portal_sequence), &ok);
    return finish(root, ok);
  }
  if (strcmp(name, "serverCommands") == 0) {
    qa_q3_ta_server_command_save_state saved = qa_q3_ta_game_server_command_runtime_capture(&o->server_commands);
    qa_save_value *root = qa_save_value_object(), *filters = qa_save_value_array();
    for (size_t i = 0; i < saved.filter_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      put(entry, "mask", qa_save_value_number(saved.filters[i].mask), &ok);
      put(entry, "compare", qa_save_value_number(saved.filters[i].compare), &ok);
      push(filters, entry, &ok);
    }
    put(root, "filters", filters, &ok);
    if (!saved.has_ban_vm_string) put(root, "banVmString", qa_save_value_null(), &ok);
    else {
      qa_save_value *ban = qa_save_value_object();
      put(ban, "modificationCount", qa_save_value_number(saved.ban_modification_count), &ok);
      put(ban, "value", qa_save_value_string(saved.ban_value ? saved.ban_value : ""), &ok);
      put(root, "banVmString", ban, &ok);
    }
    qa_q3_ta_server_command_save_state_destroy(&saved);
    return finish(root, ok);
  }
  qa_range_error("Unknown Q3 native save part %s", name);
}
static void t_restore_part(void *ctx, const char *name, const qa_save_value *value) {
  owner_t *o = ctx;
  if (strcmp(name, "level") == 0) qa_q3_restore_level(&o->level, value);
  else if (strcmp(name, "graph") == 0) {
    qa_q3_graph_state graph = qa_q3_read_graph(value);
    qa_q3_restore_graph(&o->pool, &graph);
    qa_q3_graph_state_destroy(&graph);
  }
  else if (strcmp(name, "spawns") == 0) qa_q3_ta_client_spawn_runtime_restore_save_state(&o->spawns, value);
  else if (strcmp(name, "match") == 0) qa_q3_ta_match_runtime_restore_save_state(&o->match, value);
  else if (strcmp(name, "remaps") == 0) qa_q3_shader_remap_registry_restore(&o->remaps, value);
  else if (strcmp(name, "memory") == 0) qa_q3_game_memory_restore_save_state(&o->memory, value);
  else if (strcmp(name, "registeredItems") == 0) qa_q3_item_registry_restore_save_state(&o->items.context.registry, value);
  else if (strcmp(name, "bridge") == 0) qa_q3_base_combat_bridge_restore_save_state(&o->bridge, value);
  else if (strcmp(name, "team") == 0) qa_q3_ta_team_runtime_restore_save_state(&o->team, value);
  else if (strcmp(name, "arenas") == 0) qa_q3_ta_arena_runtime_restore_save_state(&o->arenas, value);
  else if (strcmp(name, "serverCommands") == 0) qa_q3_ta_game_server_command_runtime_restore(&o->server_commands, value);
  else if (strcmp(name, "locations") == 0) {
    /* targets.ts TargetLocationState.restoreSaveState(value, pool) */
    qa_save_reader reader;
    if (!qa_save_reader_init(&reader, value, "q3.locations")) qa_range_error("Out of memory");
    qa_save_reader linked = c_field(&reader, "linked"), head = c_field(&reader, "head");
    qa_q3_target_location_save saved = {qa_save_reader_boolean(&linked), false, 0};
    if (!qa_save_reader_is_null(&head)) {
      qa_q3_game_entity *entity = qa_q3_read_module_entity(&head, &o->pool);
      saved.has_head = entity != NULL;
      if (entity != NULL) saved.head_slot = entity->slot;
    }
    qa_q3_target_location_restore(&o->targets.location, saved, &o->pool);
    qa_save_reader_destroy(&head);
    qa_save_reader_destroy(&linked);
    qa_save_reader_destroy(&reader);
  } else if (strcmp(name, "personalPortal") == 0) {
    /* personal-portal.ts restoreSaveState */
    qa_save_reader reader;
    if (!qa_save_reader_init(&reader, value, "q3.portals")) qa_range_error("Out of memory");
    qa_save_reader sequence = c_field(&reader, "portalSequence");
    qa_q3_personal_portal_restore_sequence(&o->portal, qa_save_reader_integer_min(&sequence, -2147483648.0));
    qa_save_reader_destroy(&sequence);
    qa_save_reader_destroy(&reader);
  }
}
static void t_memory_initialize(void *ctx) { qa_q3_game_memory_initialize(&((owner_t *)ctx)->memory); }
static double t_sound_index(void *ctx, const char *p) { return fn_sound_index(ctx, p); }
static double t_model_index(void *ctx, const char *p) { return fn_model_index(ctx, p); }
static void t_session_init_world(void *ctx) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_game_session_manager_initialize_world(&o->session);
  o->level.new_session = o->session_world.new_session;
}
static void t_process_ip_bans(void *ctx) {
  qa_q3_ta_game_server_command_runtime_process_ip_bans(&((owner_t *)ctx)->server_commands);
}
static void t_init_body_queue(void *ctx) { qa_q3_ta_client_spawn_runtime_init_body_queue(&((owner_t *)ctx)->spawns); }
static void t_items_clear(void *ctx, double game_type) {
  qa_q3_item_registry_clear(&((owner_t *)ctx)->items.context.registry, game_type);
}
static void t_items_save(void *ctx) {
  owner_t *o = ctx;
  /* TS registeredItems.save(setConfigstring, engine.print-as-log). */
  /* TS registeredItems.save(configstrings.set, host.engine.print). */
  (void)qa_q3_item_registry_save(&o->items.context.registry, o, fn_config_set,
      o->parts.host ? fwd_pool_print
      : o->items.services.log ? o->items.services.log : (o->parts.pool_options.print ? fwd_pool_print : NULL));
}
static qa_app_q3_spawn_report t_spawn_entities(void *ctx) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_misc_spawn_host misc = o->parts.misc_spawn_host;
  misc.ctx = o; misc.pool = &o->pool; misc.world = o->server_world;
  FWD(misc, fire_grenade, fwd_misc_grenade);
  FWD(misc, fire_rocket, fwd_misc_rocket);
  FWD(misc, fire_plasma, fwd_misc_plasma);
  FWD(misc, warn, fwd_misc_warn);
  misc.item_product = owner_game_product(o); misc.missile_product = owner_game_product(o);
  misc.time = fn_time; misc.rand_value = fn_rand; misc.crandom = fn_crandom;
  misc.register_item = fn_register_item;
  if (!misc.warn && o->parts.pool_options.print) misc.warn = fwd_pool_print;
  qa_q3_trigger_host triggers = o->parts.trigger_host;
  triggers.ctx = o; triggers.entities = &o->pool; triggers.world = o->server_world;
  FWD(triggers, set_brush_model, fwd_trigger_brush);
  FWD(triggers, can_take_damage, fwd_can_take_damage);
  FWD(triggers, warn, fwd_trigger_warn);
  triggers.kill_box = o->parts.kill_box;
  triggers.combat = fn_combat; triggers.gravity = fn_gravity;
  triggers.rand_value = fn_rand; triggers.crandom = fn_crandom;
  triggers.sound_index = fn_sound_index; triggers.remap_shader = fn_remap_shader;
  if (!triggers.warn && o->parts.pool_options.print) triggers.warn = fwd_pool_print;
  qa_q3_base_spawn_handlers_host handlers_host;
  handlers_host.product = o->parts.product;
  handlers_host.misc = misc;
  handlers_host.movers = &o->mover_spawns;
  handlers_host.triggers = triggers;
  handlers_host.targets = &o->targets;
  handlers_host.team = &o->team;
  size_t capacity = 256;
  qa_q3_spawn_handler_entry *handlers = malloc(capacity * sizeof *handlers);
  if (!handlers) qa_range_error("Out of memory");
  size_t handler_count = qa_q3_base_create_spawn_handlers(handlers_host, handlers, capacity);
  if (handler_count > capacity) {
    qa_q3_spawn_handler_entry *grown = realloc(handlers, handler_count * sizeof *grown);
    if (!grown) { free(handlers); qa_range_error("Out of memory"); }
    handlers = grown;
    handler_count = qa_q3_base_create_spawn_handlers(handlers_host, handlers, handler_count);
  }
  qa_q3_spawn_map_context map; memset(&map, 0, sizeof map);
  map.spawn.pool = &o->pool; map.spawn.memory = &o->memory;
  map.spawn.product = owner_game_product(o);
  map.spawn.game_type = owner_game_type(o);
  map.spawn.handlers = handlers; map.spawn.handler_count = handler_count;

  map.spawn.warn = o->parts.pool_options.print ? fwd_spawn_warn : NULL;
  qa_app_q3_wire_spawn_item(&map.spawn, &o->spawn_item_host);
  map.world = o->parts.worldspawn;
  /* TS: configstrings/cvars are owner services, reached with the owner's ctx. */
  map.world.ctx = o;
  map.world.set_configstring = fn_config_set;
  map.world.set_cvar = fn_set_cvar_named;
  FWD(map.world, log, fwd_world_log);
  map.world.pool = &o->pool;
  map.world.start_time = o->level.base.start_time;
  if (o->parts.host) {
    /* TS load(): motd/restarted/doWarmup read from settings at spawn time. */
    map.world.motd = owner_string(o, "g_motd");
    map.world.restarted = owner_integer(o, "g_restarted");
    map.world.do_warmup = owner_integer(o, "g_doWarmup");
  }
  map.world.warmup_time = o->level.base.warmup_time;
  qa_q3_spawn_report spawned = qa_q3_spawn_entities(o->parts.spawn_text ? o->parts.spawn_text : "", &map, NULL);
  o->level.base.warmup_time = map.world.warmup_time;
  free(handlers);
  qa_app_q3_spawn_report report; memset(&report, 0, sizeof report);
  report.world_variable_count = spawned.world_variables.count;
  report.world_variables = calloc(report.world_variable_count ? report.world_variable_count : 1,
      sizeof *report.world_variables);
  report.outcome_count = spawned.outcome_count;
  report.outcomes = calloc(report.outcome_count ? report.outcome_count : 1, sizeof *report.outcomes);
  if (!report.world_variables || !report.outcomes) qa_range_error("Out of memory");
  for (size_t i = 0; i < report.world_variable_count; i++) {
    report.world_variables[i].key = dup_str(spawned.world_variables.entries[i].key);
    report.world_variables[i].value = dup_str(spawned.world_variables.entries[i].value);
  }
  for (size_t i = 0; i < report.outcome_count; i++) {
    const qa_q3_spawn_outcome *outcome = &spawned.outcomes[i];
    qa_app_q3_spawn_outcome *out = &report.outcomes[i];
    out->kind = outcome->kind == QA_Q3_SPAWN_DISPATCHED ? "dispatched"
        : outcome->kind == QA_Q3_SPAWN_FILTERED ? "filtered" : "unknown";
    out->route = outcome->route == QA_Q3_ROUTE_ITEM ? "item" : "handler";
    /* classname dup'd (the outcome borrows entity storage a filtered
     * spawn may already have freed); qa_app_q3_spawn_report_destroy frees it. */
    out->classname = outcome->classname ? dup_str(outcome->classname) : NULL;
    out->reason = outcome->kind == QA_Q3_SPAWN_FILTERED ? qa_q3_spawn_filter_name(outcome->reason) : NULL;
    out->slot = outcome->slot;
  }
  qa_q3_spawn_report_destroy(&spawned);
  return report;
}
static void t_find_entity_teams(void *ctx) { (void)qa_q3_base_find_entity_teams(&((owner_t *)ctx)->pool); }
static void t_check_team_items(void *ctx) {
  /* TS checkTeamItems (497-508). */
  owner_t *o = ctx;
  qa_q3_ta_team_runtime_init_game(&o->team);
  double game_type = owner_game_type(o);
  const char *flags[3]; size_t flag_count = 0;
  if (game_type == QA_Q3_BASE_GT_CTF) { flags[0] = "Red"; flags[1] = "Blue"; flag_count = 2; }
  else if (o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && game_type == QA_Q3_BASE_GT_1FCTF) {
    flags[0] = "Red"; flags[1] = "Blue"; flags[2] = "Neutral"; flag_count = 3;
  }
  void (*print)(void *, const char *) = o->parts.pool_options.print;
  for (size_t i = 0; i < flag_count; i++) {
    char name[64], warning[128];
    snprintf(name, sizeof name, "%s Flag", flags[i]);
    const qa_q3_base_item_definition *item = qa_q3_base_find_item(o->parts.product, name);
    if (!item || !qa_q3_item_registry_is_registered(&o->items.context.registry, item)) {
      char lower[32];
      snprintf(lower, sizeof lower, "%s", flags[i]);
      for (char *p = lower; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
      snprintf(warning, sizeof warning, "^3WARNING: No team_CTF_%sflag in map", lower);
      if (print) print(o->parts.pool_options.ctx, warning);
    }
  }
  const char *obelisks[3]; size_t obelisk_count = 0;
  if (o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && game_type == QA_Q3_BASE_GT_OBELISK) {
    obelisks[0] = "team_redobelisk"; obelisks[1] = "team_blueobelisk"; obelisk_count = 2;
  } else if (o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && game_type == QA_Q3_BASE_GT_HARVESTER) {
    obelisks[0] = "team_redobelisk"; obelisks[1] = "team_blueobelisk"; obelisks[2] = "team_neutralobelisk";
    obelisk_count = 3;
  }
  for (size_t i = 0; i < obelisk_count; i++) {
    if (!qa_q3_find_entity(&o->pool, NULL, QA_Q3_FIELD_CLASSNAME, obelisks[i])) {
      char warning[128];
      snprintf(warning, sizeof warning, "^3WARNING: No %s in map", obelisks[i]);
      if (print) print(o->parts.pool_options.ctx, warning);
    }
  }
}
static void t_remap_teams(void *ctx, double level_time) { fn_remap_teams_at(ctx, level_time); }
static qa_q3_base_actor_trace_result fn_item_trace_actor(void *ctx, qa_q3_base_actor_trace_query query) {
  return qa_q3_base_world_adapter_trace_actor(&((owner_t *)ctx)->world, query);
}
static void t_run_item(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (!entity) return;
  qa_q3_run_item_context context; memset(&context, 0, sizeof context);
  context.frame.time = o->level.base.time;
  context.frame.previous_time = o->level.previous_time;
  context.entities = &o->pool;
  context.world.ctx = o;
  context.world.trace_actor = fn_item_trace_actor;
  context.world.point_contents = fn_point_contents;
  { qa_q3_entity_think_fn free_entity = fn_free_team_entity;
    context.free_team_entity = owner_callback(&free_entity, o); }
  qa_q3_run_item(entity, context);
}
static void t_run_client(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (entity) qa_q3_ta_client_think_runtime_run_client(&o->think, entity);
}
static void t_run_think(void *ctx, double slot, double time) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (entity) qa_q3_run_think(entity, time);
}
static void t_client_end_frame(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (!entity || !o->spawns.host.effects) return;
  /* TS endFrame: clientEndFrame(this.effects(), entity); the landed
   * effects builder is the client-spawn host's effects fn. */
  qa_q3_ta_client_effects_context effects = o->spawns.host.effects(o->spawns.host.ctx);
  qa_q3_ta_client_end_frame(&effects, entity);
}
static void t_match_checks(void *ctx) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_match_runtime_check_tournament(&o->match);
  qa_q3_ta_match_runtime_check_exit_rules(&o->match);
  qa_q3_ta_match_runtime_check_vote(&o->match);
  qa_q3_ta_match_runtime_check_team_vote(&o->match, QA_Q3_BASE_TEAM_RED);
  qa_q3_ta_match_runtime_check_team_vote(&o->match, QA_Q3_BASE_TEAM_BLUE);
  qa_q3_ta_match_runtime_check_cvars(&o->match);
}
static void t_team_check_status(void *ctx) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_team_runtime_check_team_status(&o->team);
}
static const char *t_admission_connect(void *ctx, double client, bool first, bool bot) {
  return qa_q3_ta_client_admission_runtime_connect(&((owner_t *)ctx)->admission, client, first, bot);
}
static void t_admission_begin(void *ctx, double client) {
  qa_q3_ta_client_admission_runtime_begin(&((owner_t *)ctx)->admission, client);
}
static void t_admission_disconnect(void *ctx, double slot) {
  qa_q3_ta_client_admission_runtime_disconnect(&((owner_t *)ctx)->admission, slot);
}
static void t_client_think(void *ctx, double slot, qa_q3_base_user_command command) {
  qa_q3_ta_client_think_runtime_client_think(&((owner_t *)ctx)->think, slot, command);
}
static void t_command_dispatch(void *ctx, double slot, const char *const *argv, size_t argc) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  /* Rebuild host.items so the settings-derived fields are current. */
  o->commands.host.items = qa_app_q3_item_lifecycle_host_context(&o->items);
  qa_q3_ta_game_command_runtime_dispatch(&o->commands, slot, argv, argc);
}
static void t_weapon_fire(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (!entity) return;
  qa_app_q3_source_subsystems_owner_refresh(o);
  o->weapons.host.combat = fn_combat(o);
  qa_q3_weapon_fire(&o->weapons, entity);
}
static void t_set_entity_weapon(void *ctx, double slot, double weapon) {
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&((owner_t *)ctx)->pool, slot);
  if (entity) entity->s.weapon = weapon;
}
static double t_client_weapon(void *ctx, double slot) {
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&((owner_t *)ctx)->pool, slot);
  return entity && entity->client ? entity->client->ps.weapon : 0;
}
static void t_session_write_world(void *ctx) { fn_write_session_data(ctx); }
static void t_session_write_client(void *ctx, double slot) {
  owner_t *o = ctx;
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_game_session_manager_write_client(&o->session, slot);
  o->level.new_session = o->session_world.new_session;
}
static bool t_client_connected(void *ctx, double slot) {
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(&((owner_t *)ctx)->pool, slot);
  return client && client->pers.connected != QA_Q3_CONN_DISCONNECTED;
}
static void t_bridge_before_reaction(void *ctx, qa_damage_decision decision) {
  qa_q3_base_combat_bridge_before_reaction(&((owner_t *)ctx)->bridge, decision);
}
static void t_player_die(void *ctx, double slot, double damage, double means) {
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_get(&o->pool, slot);
  if (entity) qa_q3_death_player_die(&o->death, entity, NULL, NULL, damage, means);
}
static double t_observe_supply(void *ctx, qa_actor_id pickup, qa_actor_id recipient) {
  /* Landed table flattens the TS observation-or-null to 1/0. */
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(o->records, true, pickup);
  qa_q3_game_entity *player = qa_q3_base_entity_records_native_by_actor(o->records, true, recipient);
  if (!entity || !player) return 0;
  qa_q3_supply_observation out;
  return qa_q3_observe_q3_supply(entity, player, qa_app_q3_item_lifecycle_host_context(&o->items), &out) ? 1 : 0;
}
typedef struct holdable_event_ctx { qa_q3_base_player_state *ps; } holdable_event_ctx;
static void fn_holdable_event(void *ctx, double event) {
  qa_q3_base_player_state_record_add_event(((holdable_event_ctx *)ctx)->ps, event, 0);
}
static bool t_step_holdable(void *ctx, qa_actor_id actor, bool pressed) {
  /* TS stepHoldable (416-427). */
  owner_t *o = ctx;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(o->records, true, actor);
  if (!entity || !entity->client) return true;
  qa_q3_base_player_state *ps = &entity->client->ps;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(o->parts.product);
  if (((qa_js_to_int32(ps->pm_flags)) & QA_Q3_FLAG_RESPAWNED) != 0 || ps->pm_type == QA_Q3_PM_SPECTATOR
      || qa_q3_game_entity_health(entity) <= 0) return true;
  double item = qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->holdable_item);
  const qa_q3_base_item_definition *definition = qa_q3_base_item_at(o->parts.product, item);
  qa_q3_holdable_state state; memset(&state, 0, sizeof state);
  state.product = o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? "missionpack" : "baseq3";
  state.pm_flags = ps->pm_flags;
  state.holdable_item = item;
  state.holdable_tag = definition ? definition->tag : 0;
  state.health = qa_q3_game_entity_health(entity);
  state.max_health = qa_q3_base_player_state_slots_get(&ps->stats, (size_t)schema->max_health);
  holdable_event_ctx event_ctx = {ps};
  bool consumed = qa_q3_step_holdable(&state, pressed, fn_holdable_event, &event_ctx);
  ps->pm_flags = state.pm_flags;
  qa_q3_base_player_state_slots_set(&ps->stats, (size_t)schema->holdable_item, state.holdable_item);
  return consumed;
}
static qa_q3_entity_pool *t_pool(void *ctx) { return &((owner_t *)ctx)->pool; }
static qa_q3_base_entity_records *t_records(void *ctx) { return ((owner_t *)ctx)->records; }
static qa_q3_ta_client_admission_runtime *t_admission(void *ctx) { return &((owner_t *)ctx)->admission; }
/* ---- MissileRuntime/MoverRuntime table entries (runtime.ts 154-158, 208-210, runMover). ---- */
static void t_damage_feedback(void *ctx, qa_q3_damage_call call, qa_damage_decision decision) { qa_q3_damage_feedback(fn_combat(ctx), call, decision); }
static void t_foreign_feedback(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *owner, qa_damage_decision decision) {
  qa_q3_foreign_damage_feedback(fn_combat(ctx), target, owner, decision);
}
static double t_prox_timeout(void *ctx) { return owner_integer(ctx, "g_proxMineTimeout"); }
static qa_q3_reflection_result t_missile_invuln(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  qa_q3_weapon_invuln_impact impact = qa_q3_invulnerability_effect(&((owner_t *)ctx)->pool, target, direction, point);
  qa_q3_reflection_result out; memset(&out, 0, sizeof out);
  out.kind = impact.kind == QA_Q3_WEAPON_INVULN_HIT ? QA_Q3_REFLECTION_HIT : QA_Q3_REFLECTION_MISS; out.bounce_direction = impact.bounce_direction;
  return out;
}
static void t_mv_use_targets(void *ctx, qa_q3_game_entity *entity, qa_q3_use_participant activator) { qa_q3_use_targets(owner_targets(ctx), entity, &activator); }
static void t_mv_portal(void *ctx, qa_q3_game_entity *entity, bool open) {
  owner_t *o = ctx; qa_q3_mover_host *h = &o->parts.mover_host;
  if (h->adjust_area_portal_state) h->adjust_area_portal_state(h->ctx, entity, open);
}
static void t_mv_flag(void *ctx, qa_q3_game_entity *entity) { qa_q3_ta_team_runtime_free_entity(&((owner_t *)ctx)->team, entity); }
static void t_mv_explode(void *ctx, qa_q3_game_entity *entity) { qa_q3_missile_runtime_explode(&((owner_t *)ctx)->missiles, entity); }
static qa_q3_game_entity *t_ma_native(void *ctx, qa_actor_id actor) { return qa_q3_base_entity_records_native_by_actor(((owner_t *)ctx)->records, true, actor); }
static qa_q3_damage_participant t_ma_participant(void *ctx, qa_actor_id actor) { return qa_q3_base_entity_records_damage_inflictor(((owner_t *)ctx)->records, true, actor); }
static const qa_q3_mover_actor_access *t_ma_host(owner_t *o) {
  if (!o->parts.mover_host.actors.observe) qa_range_error("Q3 source host supplies no moverActors");
  return &o->parts.mover_host.actors;
}
static bool t_ma_observe(void *ctx, qa_actor_id actor, qa_q3_shared_mover_body *out) { const qa_q3_mover_actor_access *a = t_ma_host(ctx); return a->observe(a->ctx, actor, out); }
static void t_ma_write(void *ctx, qa_owned_actor actor, qa_vec3 origin, const qa_actor_id *ground) { const qa_q3_mover_actor_access *a = t_ma_host(ctx); a->write(a->ctx, actor, origin, ground); }
static void t_ma_link(void *ctx, qa_owned_actor actor) { const qa_q3_mover_actor_access *a = t_ma_host(ctx); a->link(a->ctx, actor); }
static void t_ma_release(void *ctx, qa_owned_actor actor) { const qa_q3_mover_actor_access *a = t_ma_host(ctx); a->release(a->ctx, actor); }
static void owner_bind_game_runtimes(owner_t *o) {
  if (!o->bridge.host.authority) o->bridge.host.authority = o->parts.records_host.combat;
  if (!o->bridge.host.damage_feedback) o->bridge.host.damage_feedback = t_damage_feedback;
  if (!o->bridge.host.foreign_damage_feedback) o->bridge.host.foreign_damage_feedback = t_foreign_feedback;
  qa_q3_base_combat_bridge_bind_game_services(&o->bridge);
  qa_q3_missile_host *m = &o->missiles.host;
  m->ctx = o; m->combat = fn_combat; m->actors = o->parts.records_host.actors; m->bodies = o->parts.records_host.bodies; m->spatial = &o->spatial; m->world = o->server_world;
  m->has_missionpack = o->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (m->has_missionpack) {
    m->missionpack.ctx = o; m->missionpack.prox_mine_timeout = t_prox_timeout; m->missionpack.random = qa_q3_game_random_pick_from(&o->random);
    m->missionpack.sound_index = fn_sound_index; m->missionpack.invulnerability_impact = t_missile_invuln;
  }
  qa_q3_missile_runtime_bind(&o->missiles);
  qa_q3_mover_host *v = &o->movers.host;
  v->ctx = o; v->combat = fn_combat; v->world = o->server_world; v->spatial = &o->spatial; v->config = &o->config; v->bodies = o->parts.records_host.bodies;
  v->previous_time = o->level.previous_time; v->use_targets = t_mv_use_targets; v->adjust_area_portal_state = t_mv_portal; v->return_dropped_flag = t_mv_flag;
  v->actors.ctx = o; v->actors.native = t_ma_native; v->actors.participant = t_ma_participant; v->actors.observe = t_ma_observe; v->actors.write = t_ma_write; v->actors.link = t_ma_link; v->actors.release = t_ma_release;
  v->has_missionpack = m->has_missionpack; v->explode_missile = t_mv_explode;
  qa_q3_mover_runtime_bind(&o->movers);
}
static bool t_missiles_run_owned(void *ctx, qa_owned_actor actor) { return qa_q3_missile_runtime_run_owned(&((owner_t *)ctx)->missiles, actor); }
static void t_missiles_run(void *ctx, double slot) { owner_t *o = ctx; qa_q3_missile_runtime_run(&o->missiles, qa_q3_entity_pool_at(&o->pool, slot)); }
static void t_missiles_close(void *ctx) { qa_q3_missile_runtime_close(&((owner_t *)ctx)->missiles); }
static void t_run_mover(void *ctx, double slot) {
  owner_t *o = ctx; qa_q3_game_entity *entity = qa_q3_entity_pool_at(&o->pool, slot); qa_shared_body_table *bodies = o->parts.records_host.bodies;
  if ((qa_js_to_int32(entity->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0) return;
  size_t count = 0; for (qa_q3_game_entity *part = entity; part; part = part->teamchain) count++;
  qa_owned_actor *actors = malloc(count * sizeof *actors); qa_vec3 *origins = malloc(count * sizeof *origins);
  if (!actors || !origins) qa_range_error("out of memory");
  size_t index = 0;
  for (qa_q3_game_entity *part = entity; part; part = part->teamchain, index++) { actors[index] = qa_q3_game_entity_actor(part); origins[index] = qa_q3_base_entity_shared_current_origin(&part->r); }
  o->movers.host.previous_time = o->level.previous_time;
  qa_q3_mover_run(&o->movers, entity);
  double elapsed = (float)((float)(o->level.base.time - o->level.previous_time) * (float)0.001);
  for (size_t i = 0; i < count; i++) {
    qa_body_state_result body = qa_shared_body_table_read(bodies, actors[i].id);
    if (!body.has_value) continue;
    body.value.velocity.x = elapsed <= 0 ? 0 : (float)((float)(body.value.origin.x - origins[i].x) / elapsed);
    body.value.velocity.y = elapsed <= 0 ? 0 : (float)((float)(body.value.origin.y - origins[i].y) / elapsed);
    body.value.velocity.z = elapsed <= 0 ? 0 : (float)((float)(body.value.origin.z - origins[i].z) / elapsed);
    qa_shared_body_table_write(bodies, actors[i], body.value);
  }
  free(actors); free(origins);
}

qa_app_q3_source_subsystems qa_app_q3_source_subsystems_owner_table(
    qa_app_q3_source_subsystems_owner *owner) {
  qa_app_q3_source_subsystems table; memset(&table, 0, sizeof table);
  owner_bind_game_runtimes(owner);
  table.missiles_run_owned = t_missiles_run_owned;
  table.missiles_run = t_missiles_run;
  table.missiles_close = t_missiles_close;
  table.run_mover = t_run_mover;
  table.ctx = owner;
  table.settings_register = t_settings_register;
  table.settings_update = t_settings_update;
  table.settings_integer = t_settings_integer;
  table.settings_number = t_settings_number;
  table.settings_string = t_settings_string;
  table.settings_modification_count = t_settings_modcount;
  table.settings_restore = t_settings_restore;
  table.random_seed = t_random_seed;
  table.random_reset = t_random_reset;
  table.entity_by_actor = t_by_actor;
  table.entity_at = t_at;
  table.pool_max_clients = t_pool_max_clients;
  table.pool_num_entities = t_pool_num_entities;
  table.pool_initialize_clients = t_pool_init_clients;
  table.records_activate = t_records_activate;
  table.records_close = t_records_close;
  table.records_attach = t_records_attach;
  table.expire_events = t_expire_events;
  table.linked = t_linked;
  table.capture_part = t_capture_part;
  table.restore_part = t_restore_part;
  table.has_personal_portal = owner->parts.product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  table.memory_initialize = t_memory_initialize;
  table.sound_index = t_sound_index;
  table.model_index = t_model_index;
  table.session_initialize_world = t_session_init_world;
  table.process_ip_bans = t_process_ip_bans;
  table.init_body_queue = t_init_body_queue;
  table.registered_items_clear = t_items_clear;
  table.registered_items_save = t_items_save;
  table.spawn_entities = t_spawn_entities;
  table.find_entity_teams = t_find_entity_teams;
  table.check_team_items = t_check_team_items;
  table.remap_teams = t_remap_teams;
  table.run_item = t_run_item;
  table.run_client = t_run_client;
  table.run_think = t_run_think;
  table.client_end_frame = t_client_end_frame;
  table.match_checks = t_match_checks;
  table.team_check_status = t_team_check_status;
  table.admission_connect = t_admission_connect;
  table.admission_begin = t_admission_begin;
  table.admission_disconnect = t_admission_disconnect;
  table.client_think = t_client_think;
  table.command_dispatch = t_command_dispatch;
  table.weapon_fire = t_weapon_fire;
  table.set_entity_weapon = t_set_entity_weapon;
  table.client_weapon = t_client_weapon;
  table.session_write_world = t_session_write_world;
  table.session_write_client = t_session_write_client;
  table.client_connected = t_client_connected;
  table.bridge_before_reaction = t_bridge_before_reaction;
  table.player_die = t_player_die;
  table.observe_supply = t_observe_supply;
  table.step_arsenal = t_step_holdable;
  table.pool = t_pool;
  table.records = t_records;
  table.admission = t_admission;
  table.content = owner->parts.content;
  return table;
}

/* ---- Q3SourceRuntime (runtime.ts 70-704) ---- */
typedef struct published_entry { qa_owned_actor actor; double event, time; } published_entry;
struct qa_app_q3_source_runtime {
  qa_app_q3_source_options options;
  qa_app_q3_source_host host;
  qa_app_q3_source_runtime_mode mode;
  owner_t *owner;
  qa_app_q3_source_subsystems table; /* the owner's table (save parts) */
  bool loaded, retired;
  bool has_loaded_game_type;
  double loaded_game_type;
  qa_app_q3_spawn_report map_report;
  bool has_report;
  /* carriedSlots: slots whose session carried over (first admission reconnects). */
  bool carried[QA_Q3_MAX_CLIENTS];
  bool unobserved;
  int64_t release_subscription;
  /* publishedEvents: Map<OwnedActor, { event, time }> in insertion order. */
  published_entry *published;
  size_t published_count, published_capacity;
  const char **setting_names; size_t setting_name_count; /* settings.definitions names (setting_names) */
};
static bool same_owned(qa_owned_actor a, qa_owned_actor b) { return a.token == b.token && qa_same_actor(a.id, b.id); }
static published_entry *published_find(qa_app_q3_source_runtime *runtime, qa_owned_actor actor) {
  for (size_t i = 0; i < runtime->published_count; i++) if (same_owned(runtime->published[i].actor, actor)) return &runtime->published[i];
  return NULL;
}
static void published_set(qa_app_q3_source_runtime *runtime, qa_owned_actor actor, double event, double time) {
  published_entry *entry = published_find(runtime, actor);
  if (!entry) {
    if (runtime->published_count == runtime->published_capacity) {
      size_t capacity = runtime->published_capacity ? runtime->published_capacity * 2 : 64;
      published_entry *grown = realloc(runtime->published, capacity * sizeof *grown);
      if (!grown) qa_range_error("Out of memory");
      runtime->published = grown; runtime->published_capacity = capacity;
    }
    entry = &runtime->published[runtime->published_count++];
    entry->actor = actor;
  }
  entry->event = event; entry->time = time;
}
static void published_release(void *ctx, qa_owned_actor actor) {
  qa_app_q3_source_runtime *runtime = ctx;
  published_entry *entry = published_find(runtime, actor);
  if (!entry) return;
  size_t index = (size_t)(entry - runtime->published);
  memmove(entry, entry + 1, (runtime->published_count - index - 1) * sizeof *entry);
  runtime->published_count--;
}
static double rt_integer(qa_app_q3_source_runtime *runtime, const char *name) { return owner_integer(runtime->owner, name); }
static void rt_set_cvar(qa_app_q3_source_runtime *runtime, const char *name, const char *value) {
  qa_cvar_snapshot result;
  if (qa_cvar_registry_set(runtime->host.cvars, name, value, true, &result)) qa_cvar_read_free(&result);
}
static void native_reader(qa_app_q3_source_runtime *runtime, const qa_save_value *value, qa_save_reader *out) {
  if (!qa_save_reader_init(out, value, "q3.native")) qa_range_error("Out of memory");
  qa_save_reader r = c_field(out, "schema"); qa_save_reader_literal_string(&r, "q3:native"); qa_save_reader_destroy(&r);
  r = c_field(out, "version"); qa_save_reader_literal_number(&r, 1); qa_save_reader_destroy(&r);
  r = c_field(out, "product"); qa_save_reader_literal_string(&r, runtime->options.product); qa_save_reader_destroy(&r);
  r = c_field(out, "entityText"); qa_save_reader_literal_string(&r, runtime->options.entities); qa_save_reader_destroy(&r);
}
static qa_q3_graph_services rt_graph_services(qa_app_q3_source_runtime *runtime) {
  qa_q3_graph_services services = {runtime->owner->records, &runtime->owner->pool, runtime->host.actors};
  return services;
}

qa_app_q3_source_runtime *qa_app_q3_source_runtime_new(qa_app_q3_source_options options,
    qa_app_q3_source_host host, qa_app_q3_source_runtime_mode mode) {
  qa_app_q3_source_runtime *runtime = calloc(1, sizeof *runtime);
  if (!runtime) return NULL;
  runtime->options = options; runtime->host = host; runtime->mode = mode;
  qa_q3_base_product product = options.product && strcmp(options.product, "missionpack") == 0
      ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  /* 115-123: carried sessions. */
  if (!mode.restore && options.session_carry) {
    rt_set_cvar(runtime, "session", options.session_carry->world);
    for (size_t i = 0; i < options.session_carry->client_count; i++) {
      const qa_app_q3_source_session_carry_client *client = &options.session_carry->clients[i];
      if (client->slot < 0 || client->slot >= options.max_clients) continue;
      char name[48];
      snprintf(name, sizeof name, "session%s", QA_JS_NUMBER(client->slot));
      rt_set_cvar(runtime, name, client->session);
      if (client->slot < QA_Q3_MAX_CLIENTS) runtime->carried[(size_t)client->slot] = true;
      host.engine.set_userinfo(host.engine.ctx, client->slot, client->userinfo);
    }
  }
  /* 124-231: the subsystems, built from the host (owner) in TS order. */
  qa_app_q3_source_subsystems_parts parts; memset(&parts, 0, sizeof parts);
  parts.host = &runtime->host;
  parts.foreign_world = options.foreign_world;
  parts.product = product;
  parts.max_clients = options.max_clients;
  parts.map_provider = options.recipe.map.entities.provider;
  parts.bridge_host.weapon_provider = options.weapon_provider.provider;
  parts.bridge_host.combat_provider = options.recipe.combat.provider;
  parts.bridge_host.inventory_provider = options.recipe.inventory.provider;
  parts.bridge_host.movement_provider = options.recipe.movement.provider;
  parts.missile_host.weapon_behavior = options.weapon_behavior;
  parts.spawn_text = options.entities;
  parts.content = options.recipe.map.entities.content;
  owner_t *o = qa_app_q3_source_subsystems_owner_create(parts);
  if (!o) { free(runtime); return NULL; }
  runtime->owner = o;
  runtime->table = qa_app_q3_source_subsystems_owner_table(o);
  if (!mode.restore) {
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%s", QA_JS_NUMBER(options.max_clients));
    rt_set_cvar(runtime, "sv_maxclients", buffer);
    runtime->table.settings_register(o, options.build_date);
  } else {
    qa_save_reader reader;
    native_reader(runtime, mode.state, &reader);
    qa_save_reader server = c_field(&reader, "server");
    qa_app_q3_server_state_restore_save_state(host.server_state, server.value);
    qa_save_reader_destroy(&server);
    qa_save_reader settings = c_field(&reader, "settings");
    runtime->table.settings_restore(o, settings.value);
    qa_save_reader_destroy(&settings);
    qa_save_reader_destroy(&reader);
  }
  qa_game_random_reset(&o->random, (int64_t)options.seed);
  runtime->release_subscription = qa_session_actor_registry_on_release(host.actors, published_release, runtime);
  if (mode.restore) {
    /* 228-231: prepareQ3Graph(records, readQ3Graph(graph), actors); spawnHandlers(). */
    qa_save_reader reader;
    native_reader(runtime, mode.state, &reader);
    qa_save_reader graph = c_field(&reader, "graph");
    qa_q3_graph_state state = qa_q3_read_complete_graph(graph.value);
    qa_q3_prepare_q3_graph(rt_graph_services(runtime), &state);
    qa_q3_graph_state_destroy(&state);
    qa_save_reader_destroy(&graph);
    qa_save_reader_destroy(&reader);
  }
  return runtime;
}
void qa_app_q3_source_runtime_destroy(qa_app_q3_source_runtime *runtime) {
  if (!runtime) return;
  qa_app_q3_source_runtime_close(runtime);
  qa_app_q3_spawn_report_destroy(&runtime->map_report);
  qa_app_q3_source_subsystems_owner_destroy(runtime->owner);
  free(runtime->published);
  free(runtime->setting_names);
  free(runtime);
}
/* TS settings.definitions.map(definition => definition.name); borrowed until destroy. */
const char *const *qa_app_q3_source_runtime_setting_names(qa_app_q3_source_runtime *runtime, size_t *out_count) {
  if (runtime->setting_names == NULL) {
    size_t count = 0;
    const qa_q3_base_cvar_definition *definitions = qa_q3_base_game_settings_definitions(runtime->owner->settings, &count);
    runtime->setting_names = calloc(count == 0 ? 1 : count, sizeof *runtime->setting_names);
    if (runtime->setting_names == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) runtime->setting_names[i] = definitions[i].name;
    runtime->setting_name_count = count;
  }
  *out_count = runtime->setting_name_count;
  return runtime->setting_names;
}
qa_app_q3_source_subsystems_owner *qa_app_q3_source_runtime_owner(qa_app_q3_source_runtime *runtime) { return runtime->owner; }
const qa_app_q3_source_options *qa_app_q3_source_runtime_options(const qa_app_q3_source_runtime *runtime) { return &runtime->options; }
qa_app_q3_source_host *qa_app_q3_source_runtime_host(qa_app_q3_source_runtime *runtime) { return &runtime->host; }
double qa_app_q3_source_runtime_level_time(const qa_app_q3_source_runtime *runtime) { return runtime->owner->level.base.time; }
double qa_app_q3_source_runtime_frame_num(const qa_app_q3_source_runtime *runtime) { return runtime->owner->level.frame_num; }
double qa_app_q3_source_runtime_intermission_time(const qa_app_q3_source_runtime *runtime) { return runtime->owner->level.base.intermission_time; }
void qa_app_q3_source_runtime_respawn(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity) {
  qa_q3_ta_client_spawn_runtime_respawn(&runtime->owner->spawns, entity);
}
bool qa_app_q3_source_runtime_cheats_ok(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity) {
  qa_app_q3_source_subsystems_owner_refresh(runtime->owner);
  return qa_q3_ta_game_command_runtime_cheats_ok(&runtime->owner->commands, entity);
}
struct qa_q3_base_combat_bridge *qa_app_q3_source_runtime_bridge(qa_app_q3_source_runtime *runtime) { return &runtime->owner->bridge; }
double qa_app_q3_source_runtime_intermission_queued(const qa_app_q3_source_runtime *runtime) { return runtime->owner->level.base.intermission_queued; }
struct qa_q3_ta_team_runtime *qa_app_q3_source_runtime_team(qa_app_q3_source_runtime *runtime) { return &runtime->owner->team; }
qa_q3_base_server_world qa_app_q3_source_runtime_world(qa_app_q3_source_runtime *runtime) { return runtime->owner->server_world; }
qa_q3_ta_spawn_pose qa_app_q3_source_runtime_select_spawn_point(qa_app_q3_source_runtime *runtime, qa_vec3 origin) {
  return fn_select_spawn_point(runtime->owner, origin);
}
qa_q3_game_entity *qa_app_q3_source_runtime_drop_item(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity,
    const qa_q3_base_item_definition *item, double angle) {
  return h_events_drop(runtime->owner, entity, item, angle);
}

static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) { qa_save_value_free(object); return NULL; }
  return object;
}
/* captureNativeState (243-258). Parts with no checkpoint-tree writer in their
 * module come from the owner table (capture_part); graph uses the complete
 * captureQ3Graph tree. */
qa_save_value *qa_app_q3_source_runtime_capture_native_state(qa_app_q3_source_runtime *runtime) {
  if (!runtime->loaded || !runtime->has_report) qa_range_error("Q3 native save requires a loaded source");
  if (qa_actor_callback_table_current(runtime->host.callbacks) != NULL) qa_range_error("Cannot save Q3 during an actor callback");
  owner_t *o = runtime->owner;
  qa_save_value *root = qa_save_value_object(), *events = qa_save_value_array(), *report = qa_save_value_object();
  qa_save_value *variables = qa_save_value_array(), *outcomes = qa_save_value_array();
  qa_q3_graph_state graph = qa_q3_capture_q3_graph(rt_graph_services(runtime));
  qa_save_value *graph_value = qa_q3_graph_state_to_value(&graph);
  qa_q3_graph_state_destroy(&graph);
  static const char *PARTS[] = {"level", "locations", "spawns", "match", "remaps", "settings",
      "memory", "registeredItems", "bridge", "missiles", "team", "arenas"};
  bool ok = root && events && report && variables && outcomes
      && qa_save_value_set(root, "schema", qa_save_value_string("q3:native"))
      && qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "product", qa_save_value_string(runtime->options.product))
      && qa_save_value_set(root, "entityText", qa_save_value_string(runtime->options.entities))
      && qa_save_value_set(root, "server", qa_app_q3_server_state_capture_save_state(runtime->host.server_state))
      && qa_save_value_set(root, "graph", graph_value);
  for (size_t i = 0; ok && i < sizeof PARTS / sizeof *PARTS; i++) {
    if (i == 1) ok = qa_save_value_set(root, "random", qa_save_value_number((double)o->random.seed));
    qa_save_value *part = runtime->table.capture_part(o, PARTS[i]);
    ok = ok && qa_save_value_set(root, PARTS[i], part ? part : qa_save_value_null());
  }
  if (ok) {
    qa_save_value *portal = runtime->table.has_personal_portal ? runtime->table.capture_part(o, "personalPortal") : qa_save_value_null();
    ok = portal != NULL && qa_save_value_set(root, "personalPortal", portal);
    if (!ok) qa_save_value_free(portal);
  }
  qa_save_value *server_commands = runtime->table.capture_part(o, "serverCommands");
  ok = ok && qa_save_value_set(root, "serverCommands", server_commands ? server_commands : qa_save_value_null());
  for (size_t i = 0; ok && i < runtime->published_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(runtime->published[i].actor.id)))
        && qa_save_value_set(entry, "event", qa_save_value_number(runtime->published[i].event))
        && qa_save_value_set(entry, "time", qa_save_value_number(runtime->published[i].time))
        && qa_save_value_push(events, entry);
    if (!ok) qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < runtime->map_report.world_variable_count; i++) {
    qa_save_value *pair = qa_save_value_object();
    ok = pair && qa_save_value_set(pair, "key", qa_save_value_string(runtime->map_report.world_variables[i].key))
        && qa_save_value_set(pair, "value", qa_save_value_string(runtime->map_report.world_variables[i].value))
        && qa_save_value_push(variables, pair);
    if (!ok) qa_save_value_free(pair);
  }
  for (size_t i = 0; ok && i < runtime->map_report.outcome_count; i++) {
    qa_app_q3_spawn_outcome *outcome = &runtime->map_report.outcomes[i];
    qa_save_value *entry = qa_save_value_object();
    bool dispatched = strcmp(outcome->kind, "dispatched") == 0, filtered = strcmp(outcome->kind, "filtered") == 0;
    ok = entry && qa_save_value_set(entry, "kind", qa_save_value_string(outcome->kind));
    if (ok && dispatched) ok = qa_save_value_set(entry, "route", qa_save_value_string(outcome->route));
    ok = ok && qa_save_value_set(entry, "slot", qa_save_value_number(outcome->slot));
    if (ok && filtered) ok = qa_save_value_set(entry, "reason", qa_save_value_string(outcome->reason));
    if (ok && !filtered) ok = qa_save_value_set(entry, "classname", outcome->classname ? qa_save_value_string(outcome->classname) : qa_save_value_null());
    ok = ok && qa_save_value_push(outcomes, entry);
    if (!ok) qa_save_value_free(entry);
  }
  ok = ok && qa_save_value_set(root, "publishedEvents", events)
      && qa_save_value_set(report, "worldVariables", variables)
      && qa_save_value_set(report, "outcomes", outcomes)
      && qa_save_value_set(root, "report", report);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
uint8_t *qa_app_q3_source_runtime_capture_native_bytes(qa_app_q3_source_runtime *runtime, size_t *out_length) {
  qa_save_value *state = qa_app_q3_source_runtime_capture_native_state(runtime);
  if (!state) return NULL;
  uint8_t *encoded = qa_encode_checkpoint_value(state, out_length);
  qa_save_value_free(state);
  return encoded;
}
static qa_actor_id rt_reference_saved(void *ctx, qa_saved_actor_id saved) {
  qa_app_q3_source_runtime *runtime = ctx;
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(runtime->host.actors, saved, &actor)) qa_range_error("Saved Q3 actor has no session owner");
  return actor.id;
}
/* finishNativeRestore (262-292). */
void qa_app_q3_source_runtime_finish_native_restore(qa_app_q3_source_runtime *runtime) {
  if (runtime->loaded || !runtime->mode.restore) qa_range_error("Q3 native hydration requires a prepared restore source");
  owner_t *o = runtime->owner;
  qa_save_reader reader;
  native_reader(runtime, runtime->mode.state, &reader);
  qa_save_reader part = c_field(&reader, "level");
  runtime->table.restore_part(o, "level", part.value); qa_save_reader_destroy(&part);
  part = c_field(&reader, "random");
  qa_game_random_reset(&o->random, (int64_t)qa_save_reader_integer(&part)); qa_save_reader_destroy(&part);
  part = c_field(&reader, "graph");
  { qa_q3_graph_state state = qa_q3_read_complete_graph(part.value);
    qa_q3_restore_q3_graph(rt_graph_services(runtime), &state);
    qa_q3_graph_state_destroy(&state); }
  qa_save_reader_destroy(&part);
  static const char *PARTS[] = {"locations", "spawns", "match", "remaps", "memory", "registeredItems", "bridge"};
  for (size_t i = 0; i < sizeof PARTS / sizeof *PARTS; i++) {
    part = c_field(&reader, PARTS[i]);
    runtime->table.restore_part(o, PARTS[i], part.value);
    qa_save_reader_destroy(&part);
  }
  part = c_field(&reader, "missiles");
  qa_q3_missile_runtime_restore_save_state(&o->missiles, part.value, rt_reference_saved, runtime);
  qa_save_reader_destroy(&part);
  part = c_field(&reader, "team"); runtime->table.restore_part(o, "team", part.value); qa_save_reader_destroy(&part);
  part = c_field(&reader, "arenas"); runtime->table.restore_part(o, "arenas", part.value); qa_save_reader_destroy(&part);
  part = c_field(&reader, "personalPortal");
  if (!runtime->table.has_personal_portal) {
    if (!qa_save_reader_is_null(&part)) qa_save_reader_fail(&part, "baseq3 has no personal portal state");
  } else runtime->table.restore_part(o, "personalPortal", part.value);
  qa_save_reader_destroy(&part);
  part = c_field(&reader, "serverCommands"); runtime->table.restore_part(o, "serverCommands", part.value); qa_save_reader_destroy(&part);
  runtime->published_count = 0;
  qa_save_reader events = c_field(&reader, "publishedEvents");
  size_t event_count = qa_save_reader_list_count(&events);
  for (size_t i = 0; i < event_count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&events, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader actor_reader = c_field(&entry, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_save_reader event_reader = c_field(&entry, "event"), time_reader = c_field(&entry, "time");
    double event = qa_save_reader_number(&event_reader), time = qa_save_reader_number(&time_reader);
    qa_save_reader_destroy(&event_reader); qa_save_reader_destroy(&time_reader);
    qa_save_reader_destroy(&entry);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(runtime->host.actors, saved, &actor) || published_find(runtime, actor))
      qa_save_reader_fail(&reader, "Invalid published Q3 event actor");
    published_set(runtime, actor, event, time);
  }
  qa_save_reader_destroy(&events);
  qa_save_reader report = c_field(&reader, "report");
  qa_app_q3_spawn_report_destroy(&runtime->map_report);
  qa_save_reader variables = c_field(&report, "worldVariables");
  size_t variable_count = qa_save_reader_list_count(&variables);
  runtime->map_report.world_variables = calloc(variable_count ? variable_count : 1, sizeof *runtime->map_report.world_variables);
  if (!runtime->map_report.world_variables) qa_range_error("Out of memory");
  for (size_t i = 0; i < variable_count; i++) {
    qa_save_reader pair;
    if (!qa_save_reader_item(&variables, i, &pair)) qa_range_error("Out of memory");
    qa_save_reader key = c_field(&pair, "key"), value = c_field(&pair, "value");
    runtime->map_report.world_variables[i].key = dup_str(qa_save_reader_string(&key));
    runtime->map_report.world_variables[i].value = dup_str(qa_save_reader_string(&value));
    qa_save_reader_destroy(&key); qa_save_reader_destroy(&value); qa_save_reader_destroy(&pair);
  }
  runtime->map_report.world_variable_count = variable_count;
  qa_save_reader_destroy(&variables);
  qa_save_reader outcomes = c_field(&report, "outcomes");
  size_t outcome_count = qa_save_reader_list_count(&outcomes);
  runtime->map_report.outcomes = calloc(outcome_count ? outcome_count : 1, sizeof *runtime->map_report.outcomes);
  if (!runtime->map_report.outcomes) qa_range_error("Out of memory");
  for (size_t i = 0; i < outcome_count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&outcomes, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader kind_reader = c_field(&entry, "kind");
    const char *kind_choices[3] = {"dispatched", "filtered", "unknown"};
    const char *kind = qa_save_reader_choice_string(&kind_reader, kind_choices, 3);
    qa_save_reader_destroy(&kind_reader);
    qa_save_reader slot_reader = c_field(&entry, "slot");
    double slot = qa_save_reader_integer_min(&slot_reader, 0);
    qa_save_reader_destroy(&slot_reader);
    qa_app_q3_spawn_outcome *outcome = &runtime->map_report.outcomes[i];
    outcome->kind = kind; outcome->slot = slot;
    if (strcmp(kind, "dispatched") == 0) {
      qa_save_reader route = c_field(&entry, "route");
      const char *routes[2] = {"item", "handler"};
      outcome->route = qa_save_reader_choice_string(&route, routes, 2);
      qa_save_reader_destroy(&route);
      qa_save_reader classname = c_field(&entry, "classname");
      outcome->classname = dup_str(qa_save_reader_string(&classname));
      qa_save_reader_destroy(&classname);
    } else if (strcmp(kind, "filtered") == 0) {
      qa_save_reader reason = c_field(&entry, "reason");
      const char *reasons[6] = {"notsingle", "notteam", "notfree", "notta", "notq3a", "gametype"};
      outcome->reason = qa_save_reader_choice_string(&reason, reasons, 6);
      qa_save_reader_destroy(&reason);
    } else {
      qa_save_reader classname = c_field(&entry, "classname");
      outcome->classname = qa_save_reader_is_null(&classname) ? NULL : dup_str(qa_save_reader_string(&classname));
      qa_save_reader_destroy(&classname);
    }
    qa_save_reader_destroy(&entry);
  }
  runtime->map_report.outcome_count = outcome_count;
  qa_save_reader_destroy(&outcomes);
  qa_save_reader_destroy(&report);
  qa_save_reader_destroy(&reader);
  runtime->has_report = true;
  runtime->loaded = true;
  runtime->has_loaded_game_type = true;
  runtime->loaded_game_type = qa_app_q3_source_runtime_game_type(runtime);
}

double qa_app_q3_source_runtime_game_type(qa_app_q3_source_runtime *runtime) { return rt_integer(runtime, "g_gametype"); }
qa_q3_entity_pool *qa_app_q3_source_runtime_pool(qa_app_q3_source_runtime *runtime) { return &runtime->owner->pool; }
qa_app_q3_source_presentation_state *qa_app_q3_source_runtime_source_state(qa_app_q3_source_runtime *runtime) {
  return qa_app_q3_source_presentation_state_of(runtime);
}
size_t qa_app_q3_source_runtime_presentations(qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity) {
  return qa_app_q3_source_models(runtime, out, capacity);
}
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_spawn_report(qa_app_q3_source_runtime *runtime) {
  if (!runtime->has_report) qa_range_error("Q3 map not loaded");
  return &runtime->map_report;
}
const char *qa_app_q3_source_runtime_setting_string(qa_app_q3_source_runtime *runtime, const char *name) {
  return owner_string(runtime->owner, name);
}
/* observeSupply (411-414). */
bool qa_app_q3_source_runtime_observe_supply_value(qa_app_q3_source_runtime *runtime, qa_actor_id pickup,
    qa_actor_id recipient, qa_q3_supply_observation *out) {
  owner_t *o = runtime->owner;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(o->records, true, pickup);
  qa_q3_game_entity *player = qa_q3_base_entity_records_native_by_actor(o->records, true, recipient);
  if (!entity || !player) return false;
  return qa_q3_observe_q3_supply(entity, player, qa_app_q3_item_lifecycle_host_context(&o->items), out);
}
double qa_app_q3_source_runtime_observe_supply(qa_app_q3_source_runtime *runtime, qa_actor_id pickup, qa_actor_id recipient) {
  qa_q3_supply_observation out;
  return qa_app_q3_source_runtime_observe_supply_value(runtime, pickup, recipient, &out) ? 1 : 0;
}
double qa_app_q3_source_runtime_quad_damage_factor(qa_app_q3_source_runtime *runtime) { return owner_number(runtime->owner, "g_quadfactor"); }
/* stepHoldable (418-429). */
bool qa_app_q3_source_runtime_step_holdable(qa_app_q3_source_runtime *runtime, qa_actor_id actor, bool pressed) {
  return t_step_holdable(runtime->owner, actor, pressed);
}
/* cvars.find(name)?.modified === true. */
static bool rt_cvar_modified(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(cvars, name, &read)) return false;
  bool modified = read.modified;
  qa_cvar_read_free(&read);
  return modified;
}
/* restartCompatibility (513-518). */
const char *qa_app_q3_source_runtime_restart_compatibility(qa_app_q3_source_runtime *runtime) {
  if (!runtime->loaded || runtime->retired) qa_range_error("Q3 round is not active");
  qa_cvar_registry *cvars = runtime->host.cvars;
  if (rt_cvar_modified(cvars, "sv_maxclients") || qa_cvar_registry_variable_value(cvars, "sv_maxclients") != runtime->options.max_clients)
    return "client-capacity-changed";
  if (rt_cvar_modified(cvars, "g_gametype") || !runtime->has_loaded_game_type
      || qa_cvar_registry_variable_value(cvars, "g_gametype") != runtime->loaded_game_type) return "game-type-changed";
  return "compatible";
}
/* close (520-524). */
void qa_app_q3_source_runtime_close(qa_app_q3_source_runtime *runtime) {
  if (runtime->retired) return;
  runtime->retired = true; runtime->loaded = false;
  qa_q3_missile_runtime_close(&runtime->owner->missiles);
  qa_q3_base_entity_records_close(runtime->owner->records);
  if (!runtime->unobserved) {
    qa_session_actor_registry_off_release(runtime->host.actors, runtime->release_subscription);
    runtime->unobserved = true;
  }
  runtime->published_count = 0;
}
/* load (526-556). */
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_load(qa_app_q3_source_runtime *runtime) {
  if (runtime->retired || runtime->loaded || runtime->mode.restore)
    qa_range_error("Q3 source map cannot spawn in its current construction mode");
  owner_t *o = runtime->owner;
  o->level.base.time = runtime->host.now(runtime->host.ctx); o->level.base.start_time = o->level.base.time;
  o->level.base.warmup_modification_count = qa_q3_base_game_settings_snapshot(o->settings, "g_warmup")->modification_count;
  qa_q3_game_memory_initialize(&o->memory);
  qa_q3_entity_pool_initialize_clients(&o->pool, runtime->options.max_clients);
  (void)qa_q3_base_entity_records_activate(o->records, 1022);
  o->level.fry_sound = fn_sound_index(o, "sound/player/fry.wav");
  t_session_init_world(o);
  qa_q3_ta_game_server_command_runtime_process_ip_bans(&o->server_commands);
  qa_q3_ta_client_spawn_runtime_init_body_queue(&o->spawns);
  qa_q3_item_registry_clear(&o->items.context.registry, qa_app_q3_source_runtime_game_type(runtime));
  runtime->map_report = t_spawn_entities(o);
  runtime->has_report = true;
  (void)qa_q3_base_find_entity_teams(&o->pool);
  if (qa_app_q3_source_runtime_game_type(runtime) >= QA_Q3_BASE_GT_TEAM) t_check_team_items(o);
  t_items_save(o);
  char *build = qa_cvar_registry_variable_string(runtime->host.cvars, "com_buildScript");
  double build_script = qa_game_atoi(build ? build : "");
  free(build);
  if (qa_app_q3_source_runtime_game_type(runtime) == QA_Q3_BASE_GT_SINGLE_PLAYER || build_script != 0) {
    (void)fn_model_index(o, "models/mapobjects/podium/podium4.md3");
    (void)fn_sound_index(o, "sound/player/gurp1.wav"); (void)fn_sound_index(o, "sound/player/gurp2.wav");
  }
  fn_remap_teams_at(o, o->level.base.time);
  qa_q3_base_game_settings_update(o->settings);
  runtime->loaded = true;
  runtime->has_loaded_game_type = true;
  runtime->loaded_game_type = qa_app_q3_source_runtime_game_type(runtime);
  char *info = qa_app_q3_server_state_server_info(runtime->host.server_state);
  runtime->host.configstring_set(runtime->host.configstrings_ctx, 0, info ? info : "");
  char dashes[62]; memset(dashes, '-', 60); dashes[60] = '\n'; dashes[61] = 0;
  runtime->host.engine.log(runtime->host.engine.ctx, dashes);
  qa_q3_game_format_arg args[1] = {qa_q3_game_format_string(info ? info : "")};
  char *line = qa_q3_game_format("InitGame: %s\n", args, 1, 32000);
  runtime->host.engine.log(runtime->host.engine.ctx, line ? line : "");
  free(line); free(info);
  return &runtime->map_report;
}
/* foreignSpawns (560-563). */
size_t qa_app_q3_source_runtime_foreign_spawns(qa_app_q3_source_runtime *runtime, qa_app_q3_foreign_spawn *out, size_t capacity) {
  if (!runtime->has_report) return 0;
  size_t count = 0;
  for (size_t i = 0; i < runtime->map_report.outcome_count; i++) {
    if (strcmp(runtime->map_report.outcomes[i].kind, "unknown") != 0) continue;
    if (count < capacity) { out[count].ordinal = (double)(i + 1); out[count].classname = runtime->map_report.outcomes[i].classname; }
    count++;
  }
  return count;
}
/* beginFrame (565-570). */
void qa_app_q3_source_runtime_begin_frame(qa_app_q3_source_runtime *runtime, qa_frame_context frame) {
  owner_t *o = runtime->owner;
  o->level.frame_num = frame.frame;
  o->level.previous_time = o->level.base.time;
  o->level.base.time = frame.time.kind == QA_SOURCE_TIME_MILLISECONDS ? frame.time.value : trunc(frame.time.value * 1000);
  qa_q3_base_game_settings_update(o->settings);
}
/* runActor (572-583); runMover (585-601) is the table's run_mover. */
void qa_app_q3_source_runtime_run_actor(qa_app_q3_source_runtime *runtime, qa_owned_actor actor) {
  owner_t *o = runtime->owner;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(o->records, true, actor.id);
  if (entity && qa_q3_missile_runtime_run_owned(&o->missiles, actor)) return;
  if (!entity || entity->slot == 1022 || qa_q3_entity_pool_expire_events(&o->pool, entity) != QA_Q3_EVENT_ACTIVE) return;
  if (entity->never_free) {
    qa_q3_base_link_state state;
    if (!qa_q3_base_world_adapter_link_state(&o->world, entity->slot, &state) || !state.linked) return;
  }
  qa_app_q3_source_subsystems_owner_refresh(o);
  if (entity->s.e_type == QA_Q3_BASE_ET_MISSILE) qa_q3_missile_runtime_run(&o->missiles, entity);
  else if (entity->s.e_type == QA_Q3_BASE_ET_ITEM || entity->physics_object) t_run_item(o, entity->slot);
  else if (entity->s.e_type == QA_Q3_BASE_ET_MOVER) t_run_mover(o, entity->slot);
  else if (entity->slot < QA_Q3_MAX_CLIENTS) qa_q3_ta_client_think_runtime_run_client(&o->think, entity);
  else qa_q3_run_think(entity, o->level.base.time);
}
/* endFrame (603-615) and publishEvents (617-628). */
void qa_app_q3_source_runtime_end_frame(qa_app_q3_source_runtime *runtime) {
  owner_t *o = runtime->owner;
  for (double index = 0; index < o->pool.max_clients; index++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(&o->pool, index);
    if (qa_q3_game_entity_inuse(entity)) {
      qa_q3_ta_client_effects_context effects = o->spawns.host.effects(o->spawns.host.ctx);
      qa_q3_ta_client_end_frame(&effects, entity);
    }
  }
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_match_runtime_check_tournament(&o->match);
  qa_q3_ta_match_runtime_check_exit_rules(&o->match);
  qa_app_q3_source_subsystems_owner_refresh(o);
  qa_q3_ta_team_runtime_check_team_status(&o->team);
  qa_q3_ta_match_runtime_check_vote(&o->match);
  qa_q3_ta_match_runtime_check_team_vote(&o->match, QA_Q3_BASE_TEAM_RED);
  qa_q3_ta_match_runtime_check_team_vote(&o->match, QA_Q3_BASE_TEAM_BLUE);
  qa_q3_ta_match_runtime_check_cvars(&o->match);
  if (rt_integer(runtime, "g_listEntity") != 0) {
    for (double index = 0; index < QA_Q3_MAX_GENTITIES; index++) {
      const char *classname = qa_q3_game_entity_classname(qa_q3_entity_pool_at(&o->pool, index));
      qa_q3_game_format_arg format_args[2] = {qa_q3_game_format_number(index), qa_q3_game_format_string(classname ? classname : "")};
      char *text = qa_q3_game_format("%4i: %s\n", format_args, 2, 32000);
      runtime->host.engine.print(runtime->host.engine.ctx, text ? text : "");
      free(text);
    }
    rt_set_cvar(runtime, "g_listEntity", "0");
  }
  char *info = qa_app_q3_server_state_server_info(runtime->host.server_state);
  runtime->host.configstring_set(runtime->host.configstrings_ctx, 0, info ? info : "");
  free(info);
  for (double slot = 0; slot < o->pool.num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(&o->pool, slot);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    double event = entity->s.e_type >= QA_Q3_BASE_ET_EVENTS ? entity->s.e_type - QA_Q3_BASE_ET_EVENTS : entity->s.event;
    if (event == 0) continue;
    qa_owned_actor actor = qa_q3_game_entity_actor(entity);
    published_entry *previous = published_find(runtime, actor);
    if (previous && previous->event == event && previous->time == entity->event_time) continue;
    published_set(runtime, actor, event, entity->event_time);
    qa_app_q3_source_entity_event published; memset(&published, 0, sizeof published);
    published.kind = "entity-event"; published.actor = actor.id;
    published.state = entity->s; published.origin = qa_q3_base_entity_shared_current_origin(&entity->r);
    published.time = o->level.base.time;
    runtime->host.entity_event(runtime->host.entity_event_ctx, published);
  }
}
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_prepare_client(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client) {
  if (!runtime->loaded) qa_range_error("Q3 source map must be loaded before admitting players");
  return owner_view(runtime->owner, qa_q3_base_entity_records_attach(runtime->owner->records, client, actor, true));
}
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_admit_player(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client) {
  qa_app_q3_runtime_entity_view entity = qa_app_q3_source_runtime_prepare_client(runtime, actor, client);
  bool restored = client >= 0 && client < QA_Q3_MAX_CLIENTS && runtime->carried[(size_t)client];
  if (restored) runtime->carried[(size_t)client] = false;
  qa_app_q3_source_subsystems_owner_refresh(runtime->owner);
  const char *rejected = qa_q3_ta_client_admission_runtime_connect(&runtime->owner->admission, client, !restored, false);
  if (rejected) qa_raise(QA_APP_Q3_CLIENT_ADMISSION_DENIED, 0, 0, "%s", rejected);
  qa_q3_ta_client_admission_runtime_begin(&runtime->owner->admission, client);
  return entity;
}
qa_q3_game_entity *qa_app_q3_source_runtime_record_by_actor(qa_app_q3_source_runtime *runtime, qa_actor_id actor) {
  return qa_q3_base_entity_records_by_actor(runtime->owner->records, true, actor);
}
struct qa_q3_base_entity_records *qa_app_q3_source_runtime_records(qa_app_q3_source_runtime *runtime) { return runtime->owner->records; }
void qa_app_q3_source_runtime_userinfo_changed(qa_app_q3_source_runtime *runtime, double client) {
  qa_q3_ta_client_admission_runtime_userinfo_changed(&runtime->owner->admission, client);
}
/* captureSession (644-654). */
qa_app_q3_captured_session qa_app_q3_source_runtime_capture_session(qa_app_q3_source_runtime *runtime) {
  owner_t *o = runtime->owner;
  fn_write_session_data(o);
  qa_app_q3_captured_session session; memset(&session, 0, sizeof session);
  session.clients = calloc(QA_Q3_MAX_CLIENTS, sizeof *session.clients);
  if (!session.clients) qa_range_error("Out of memory");
  size_t count = 0;
  for (double slot = 0; slot < o->pool.max_clients; slot++) {
    if (qa_q3_entity_pool_client_at(&o->pool, slot)->pers.connected == QA_Q3_CONN_DISCONNECTED) continue;
    t_session_write_client(o, slot);
    char name[48];
    snprintf(name, sizeof name, "session%s", QA_JS_NUMBER(slot));
    char *saved = qa_cvar_registry_variable_string(runtime->host.cvars, name);
    char *userinfo = runtime->host.engine.get_userinfo(runtime->host.engine.ctx, slot);
    session.clients[count].slot = slot;
    session.clients[count].session = saved ? saved : dup_str("");
    session.clients[count].userinfo = userinfo ? userinfo : dup_str("");
    count++;
  }
  char *world = qa_cvar_registry_variable_string(runtime->host.cvars, "session");
  session.carry.kind = "q3";
  session.carry.world = world ? world : dup_str("");
  session.carry.clients = session.clients;
  session.carry.client_count = count;
  return session;
}
void qa_app_q3_captured_session_destroy(qa_app_q3_captured_session *session) {
  if (!session) return;
  free((void *)session->carry.world);
  for (size_t i = 0; i < session->carry.client_count; i++) {
    free((void *)session->clients[i].session); free((void *)session->clients[i].userinfo);
  }
  free(session->clients);
  memset(session, 0, sizeof *session);
}
/* disconnectPlayer / playerThink / playerCommand / playerWeapon (656-678). */
void qa_app_q3_source_runtime_disconnect_player(qa_app_q3_source_runtime *runtime, qa_actor_id actor) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(runtime->owner->records, true, actor);
  if (entity && entity->client) {
    qa_app_q3_source_subsystems_owner_refresh(runtime->owner);
    qa_q3_ta_client_admission_runtime_disconnect(&runtime->owner->admission, entity->slot);
  }
}
void qa_app_q3_source_runtime_player_think(qa_app_q3_source_runtime *runtime, qa_actor_command input) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(runtime->owner->records, true, input.actor);
  if (!entity || !entity->client) qa_range_error("Q3 client command has no admitted actor");
  qa_q3_base_user_command command = runtime->host.source_command(runtime->host.source_command_ctx, input);
  qa_q3_ta_client_think_runtime_client_think(&runtime->owner->think, entity->slot, command);
}
void qa_app_q3_source_runtime_player_command(qa_app_q3_source_runtime *runtime, qa_actor_id actor,
    const char *name, const char *const *args, size_t argc) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(runtime->owner->records, true, actor);
  if (!entity || !entity->client) qa_range_error("Q3 client command has no admitted actor");
  qa_q3_base_game_settings_update(runtime->owner->settings);
  const char **argv = malloc((argc + 1) * sizeof *argv);
  if (!argv) qa_range_error("Out of memory");
  argv[0] = name;
  for (size_t i = 0; i < argc; i++) argv[i + 1] = args[i];
  t_command_dispatch(runtime->owner, entity->slot, argv, argc + 1);
  free(argv);
}
void qa_app_q3_source_runtime_player_weapon(qa_app_q3_source_runtime *runtime, qa_actor_id actor) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(runtime->owner->records, true, actor);
  if (!entity || !entity->client) qa_range_error("Q3 weapon fire has no admitted actor");
  entity->s.weapon = entity->client->ps.weapon;
  qa_app_q3_source_subsystems_owner_refresh(runtime->owner);
  qa_q3_weapon_fire(&runtime->owner->weapons, entity);
}
/* beforeReaction (679-690). */
void qa_app_q3_source_runtime_before_reaction(qa_app_q3_source_runtime *runtime, qa_owned_actor actor,
    qa_damage_decision decision) {
  owner_t *o = runtime->owner;
  qa_q3_base_combat_bridge_before_reaction(&o->bridge, decision);
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(o->records, true, actor.id);
  if (decision.reaction && strcmp(decision.reaction, "death") == 0 && entity && entity->client) {
    const qa_attack_provenance *attack = &decision.request.attack;
    double means = attack->cause.kind == QA_ATTACK_CAUSE_Q3 ? attack->cause.means_of_death : 0;
    /* Native G_Damage dispatches through ent->die: player_die first, then body_die. */
    qa_q3_damage_inflictor inflictor = qa_q3_base_entity_records_damage_inflictor(o->records, attack->inflictor != NULL, attack->inflictor ? *attack->inflictor : actor.id);
    qa_q3_damage_inflictor attacker = qa_q3_base_entity_records_damage_inflictor(o->records, attack->attacker != NULL, attack->attacker ? *attack->attacker : actor.id);
    if (entity->die.fn) {
      qa_q3_entity_die_fn die; memcpy(&die, &entity->die.fn, sizeof die);
      die(entity->die.ctx, entity, inflictor, attacker, decision.applied_damage, means);
    } else qa_q3_death_player_die(&o->death, entity, &inflictor, &attacker, decision.applied_damage, means);
  }
}
void qa_app_q3_source_runtime_adjust_area_portal_state(qa_app_q3_source_runtime *runtime, double slot, bool open) {
  h_adjust_area_portal_state(runtime->owner, qa_q3_entity_pool_at(&runtime->owner->pool, slot), open);
}

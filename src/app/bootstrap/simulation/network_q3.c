/* Port of src/app/bootstrap/simulation/network-q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q3_downloads.h"
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/content/q3/team_arena/client_policy.h"
#include "qa/core/cvars/index.h"
#include "qa/core/cvars/info.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/adapters.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/pak_references.h"
#include "qa/network/q3/server.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/visibility.h"

typedef struct q3_host {
  qa_app_sim_q3_server_binding_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_app_q3_qvm_server_game *guest_source;  /* the guest source; NULL for native */
  qa_app_q3_source_runtime *native_round;   /* TS nativeRound */
  bool guest, restore;
  qa_app_q3_server_state *state;
  qa_cvar_registry *cvars;
  const char *product;   /* "baseq3" | "missionpack" */
  qa_q3_product wire_product;
  double max_clients;
  qa_app_q3_packages *packages;  /* TS packages / preparing: opened once */
  bool touched_cgame;
  /* Storage for what host members return. */
  char *strings[3];               /* privatePassword, gameDirectory, strictAuth */
  char *reason;                   /* rejected admission reason */
  qa_q3_pure_server pure; int64_t *pure_checksums;
  qa_q3_server_rate rate;
  qa_wire_admission support;
  double *occupied; size_t occupied_cap;
  qa_q3_gamestate gamestate;
  qa_q3_player_state_record snapshot_player;
  qa_q3_entity_state_record *wire; size_t wire_cap;    /* wireEntity(number) per number */
  int64_t *clusters; size_t cluster_cap;
  qa_q3_visible_entities visible; bool has_visible;
  const void **snapshot_entities;
  qa_actor_command **commands; size_t command_count;    /* input() results */
  qa_q3_download_read_file download;
  char *status;
} q3_host;

static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *q = realloc(p, size ? size : 1);
  if (!q) qa_range_error("Out of memory");
  return q;
}
static char *xstrdup(const char *text) {
  size_t n = strlen(text) + 1;
  char *out = xmalloc(n);
  memcpy(out, text, n);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void host_print(q3_host *h, const char *text) {
  if (h->options.print) h->options.print(h->options.print_ctx, text);
}
static void print_forward(void *ctx, const char *text) { host_print(ctx, text); }

/* ---- cvars (TS cvars.get(name)?.integerValue ?? fallback, variableValue, variableString). ---- */
static double cvar_integer(q3_host *h, const char *name, double fallback) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(h->cvars, name, &read)) return fallback;
  double value = read.integer_value;
  qa_cvar_read_free(&read);
  return value;
}
static double cvar_value(q3_host *h, const char *name) { return qa_cvar_registry_variable_value(h->cvars, name); }
static char *cvar_string(q3_host *h, const char *name) {
  char *value = qa_cvar_registry_variable_string(h->cvars, name);
  if (!value) qa_range_error("Out of memory");
  return value;
}
static void cvar_set(q3_host *h, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  qa_cvar_registry_set(h->cvars, name, value, true, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static const char *kept_string(q3_host *h, size_t index, char *value) {
  free(h->strings[index]);
  h->strings[index] = value;
  return value;
}

/* ---- source access (TS source / nativeSource / sharedEntity / linked). ---- */
static qa_app_q3_source_runtime *native_source(q3_host *h) {
  if (h->native_round == NULL) qa_raise("Error", 0, 0, "Native source round binding is unavailable");
  return h->native_round;
}
static qa_app_q3_guest_records *guest_records(q3_host *h) { return qa_app_q3_qvm_server_game_records(h->guest_source); }
static qa_qvm_game_data *guest_data(q3_host *h) { return qa_app_q3_guest_records_data(guest_records(h)); }
static double entity_count(q3_host *h) {
  return h->guest ? qa_qvm_game_data_get_num_entities(guest_data(h))
                  : qa_app_q3_source_runtime_pool(native_source(h))->num_entities;
}
static qa_q3_game_entity *pool_at(q3_host *h, double number) {
  return qa_q3_entity_pool_at(qa_app_q3_source_runtime_pool(native_source(h)), number);
}
static bool linked(q3_host *h, double number) {
  if (h->guest) return qa_qvm_shared_entity_get_linked(qa_app_q3_guest_records_entity(guest_records(h), number));
  qa_q3_game_entity *entity = pool_at(h, number);
  return qa_q3_game_entity_inuse(entity) && qa_q3_base_entity_shared_linked(&entity->r);
}
/* TS wireEntity: new EntityStateRecord<number>(0).copyFrom(sharedEntity(number).s). */
static void wire_entity(q3_host *h, double number, qa_q3_entity_state_record *out) {
  if (!h->guest) { qa_q3_entity_state_record_copy_from(out, &pool_at(h, number)->s); return; }
  qa_qvm_entity_state_fields f;
  qa_qvm_get_entity_state_fields(&qa_app_q3_guest_records_entity(guest_records(h), number)->s, &f);
  qa_q3_entity_state_record_init(out, 0);
  out->number = f.number; out->e_type = f.e_type; out->e_flags = f.e_flags;
  out->pos.type = f.pos.type; out->pos.time = f.pos.time; out->pos.duration = f.pos.duration;
  out->pos.base = f.pos.base; out->pos.delta = f.pos.delta;
  out->apos.type = f.apos.type; out->apos.time = f.apos.time; out->apos.duration = f.apos.duration;
  out->apos.base = f.apos.base; out->apos.delta = f.apos.delta;
  out->time = f.time; out->time2 = f.time2;
  out->origin = f.origin; out->origin2 = f.origin2; out->angles = f.angles; out->angles2 = f.angles2;
  out->other_entity_num = f.other_entity_num; out->other_entity_num2 = f.other_entity_num2;
  out->ground_entity_num = f.ground_entity_num; out->constant_light = f.constant_light;
  out->loop_sound = f.loop_sound; out->modelindex = f.modelindex; out->modelindex2 = f.modelindex2;
  out->client_num = f.client_num; out->frame = f.frame; out->solid = f.solid; out->event = f.event;
  out->event_parm = f.event_parm; out->powerups = f.powerups; out->weapon = f.weapon;
  out->legs_anim = f.legs_anim; out->torso_anim = f.torso_anim; out->generic1 = f.generic1;
}
static void entity_flags(q3_host *h, double number, double *sv_flags, double *single_client) {
  if (h->guest) {
    qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(guest_records(h), number);
    *sv_flags = qa_qvm_shared_entity_get_sv_flags(entity);
    *single_client = qa_qvm_shared_entity_get_single_client(entity);
    return;
  }
  qa_q3_game_entity *entity = pool_at(h, number);
  *sv_flags = entity->r.sv_flags; *single_client = entity->r.single_client;
}
/* simulation.players().find(actor => simulation.movementPlayer(actor)?.client.equals(client)). */
static bool find_player_actor(q3_host *h, qa_client_id client, qa_actor_id *out) {
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, client)) { *out = players[i]; return true; }
  }
  return false;
}
static qa_q3_base_user_command base_command(const qa_q3_wire_user_command *wire) {
  qa_q3_base_user_command command;
  command.server_time = wire->server_time;
  command.angles.x = wire->angles[0]; command.angles.y = wire->angles[1]; command.angles.z = wire->angles[2];
  command.buttons = wire->buttons; command.weapon = wire->weapon;
  command.forwardmove = wire->forwardmove; command.rightmove = wire->rightmove; command.upmove = wire->upmove;
  return command;
}
static const char *configstring(q3_host *h, double index) {
  const char *value = qa_app_q3_server_state_configstring_get(h->state, index);
  return value != NULL ? value : "";
}

/* ---- playerFor / connect. ---- */
static qa_app_q3_player player_for(q3_host *h, qa_client_id client) {
  qa_app_q3_player player;
  if (h->guest) {
    if (!qa_app_q3_qvm_server_game_player(h->guest_source, client, &player))
      qa_raise("Error", 0, 0, "Application has not admitted the Q3 guest client");
    return player;
  }
  qa_actor_id actor;
  if (!find_player_actor(h, client, &actor)) qa_raise("Error", 0, 0, "Application has not admitted the Q3 client");
  qa_q3_game_entity *entity = qa_app_q3_source_runtime_record_by_actor(native_source(h), actor);
  if (entity == NULL) qa_raise("Error", 0, 0, "Q3 player has no source entity");
  player.client = client; player.actor = actor; player.source_entity = entity->slot;
  return player;
}
typedef struct connect_scope { q3_host *h; qa_client_id client; qa_app_q3_player player; qa_actor_id actor; } connect_scope;
static void admit_player_body(void *ctx) {
  connect_scope *c = ctx;
  const qa_app_sim_network_simulation *s = c->h->simulation;
  s->admit_player(s->ctx, c->client);
  c->player = player_for(c->h, c->client);
}
static void disconnect_actor_body(void *ctx) {
  connect_scope *c = ctx;
  c->h->simulation->disconnect_player(c->h->simulation->ctx, c->actor);
}
static qa_app_q3_admission connect_client(q3_host *h, qa_client_id client, const char *userinfo) {
  if (h->guest) return qa_app_q3_qvm_server_game_connect(h->guest_source, client, userinfo);
  qa_app_q3_server_state_set_userinfo(h->state, (double)client.slot, userinfo);
  connect_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.client = client;
  qa_caught_error error;
  qa_app_q3_admission result;
  memset(&result, 0, sizeof result);
  if (qa_try(admit_player_body, &scope, &error)) { result.accepted = true; result.player = scope.player; return result; }
  if (find_player_actor(h, client, &scope.actor)) {
    qa_caught_error cleanup;
    if (!qa_try(disconnect_actor_body, &scope, &cleanup))
      qa_raise("AggregateError", 0, 0, "Q3 client admission and cleanup failed");
  }
  if (is(error.name, QA_APP_Q3_CLIENT_ADMISSION_DENIED)) {
    free(h->reason);
    h->reason = xstrdup(error.message);
    result.reason = h->reason;
    return result;
  }
  qa_rethrow(&error);
}
static qa_app_q3_admission authority_connect(void *ctx, qa_client_id client, const char *userinfo) {
  return connect_client(ctx, client, userinfo);
}

/* ---- admission settings. ---- */
static int64_t adm_private_clients(const void *ctx) { return (int64_t)cvar_integer((q3_host *)ctx, "sv_privateClients", 0); }
static const char *adm_private_password(const void *ctx) {
  q3_host *h = (q3_host *)ctx;
  return kept_string(h, 0, cvar_string(h, "sv_privatePassword"));
}
static double adm_reconnect_limit(const void *ctx) { return cvar_integer((q3_host *)ctx, "sv_reconnectlimit", 3); }
static double adm_minimum_ping(const void *ctx) { return cvar_value((q3_host *)ctx, "sv_minPing"); }
static double adm_maximum_ping(const void *ctx) { return cvar_value((q3_host *)ctx, "sv_maxPing"); }
static bool adm_demo_restricted(const void *ctx) { return ((const q3_host *)ctx)->options.content->q3_demo_restricted; }
static bool adm_enabled(const void *ctx) { return cvar_value((q3_host *)ctx, "g_gametype") != 2; }
static const char *adm_game_directory(const void *ctx) {
  q3_host *h = (q3_host *)ctx;
  return kept_string(h, 1, cvar_string(h, "fs_game"));
}
static const char *adm_strict_auth(const void *ctx) {
  q3_host *h = (q3_host *)ctx;
  return kept_string(h, 2, cvar_string(h, "sv_strictAuth"));
}
static bool adm_flood_protect(const void *ctx) { return cvar_value((q3_host *)ctx, "sv_floodProtect") != 0; }

/* ---- sourceRound (native only). ---- */
static void round_preflight(void *ctx) {
  q3_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_sim_network_restart_plan plan = s->source_restart_plan(s->ctx);
  if (!plan.source_reset) qa_raise("Error", 0, 0, "Q3 network fast restart is incompatible: %s", plan.reason ? plan.reason : "");
  if (s->q3_source(s->ctx) != native_source(h)) qa_raise("Error", 0, 0, "Q3 network source round is already retired");
}
static void round_rebind(void *ctx) {
  q3_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_app_q3_source_runtime *next = s->q3_source(s->ctx);
  if (next == NULL || next == native_source(h) || qa_app_q3_source_runtime_host(next)->server_state != h->state
      || !is(qa_app_q3_source_runtime_options(next)->product, h->product))
    qa_raise("Error", 0, 0, "Q3 network requires a new round with retained server state and product");
  h->native_round = next;
}
static qa_app_q3_admission round_reconnect(void *ctx, qa_client_id client, const char *userinfo, const void *last_command) {
  q3_host *h = ctx;
  qa_app_q3_server_state_set_user_command(qa_app_q3_source_runtime_host(native_source(h))->server_state,
      (double)client.slot, base_command(last_command));
  return connect_client(h, client, userinfo);
}

/* ---- prepare / pure / downloads. ---- */
static void require_saved_server_id(q3_host *h, double server_id) {
  if (h->restore && server_id != cvar_value(h, "sv_serverid"))
    qa_raise("Error", 0, 0, "Restored Q3 client authority must retain the saved server id");
}
static qa_app_q3_packages *archive_state(q3_host *h) {
  if (h->packages == NULL) qa_raise("Error", 0, 0, "Q3 package metadata has not been prepared");
  return h->packages;
}
static qa_q3_pak_references *references(q3_host *h) {
  return qa_q3_content_references_get_references(qa_app_q3_packages_references(archive_state(h)));
}
static qa_app_q3_packages *open_packages(q3_host *h, double checksum_feed) {
  const qa_app_sim_network_content *content = h->options.content;
  size_t count = 0;
  const qa_app_q3_package_mount *mounts = content->q3_package_mounts ? content->q3_package_mounts(content->ctx, &count) : NULL;
  qa_app_q3_packages *packages = qa_app_q3_packages_open(mounts, count, checksum_feed);
  if (packages == NULL) qa_range_error("Out of memory");
  return packages;
}
typedef struct collect_visit_ctx { q3_host *h; } collect_visit_ctx;
static bool collect_flag(void *ctx, const qa_resolved_resource_reference *reference) { (void)reference; return *(const bool *)ctx; }
static void collect_visit(void *ctx, const qa_resolved_resource_reference *reference, bool is_q3) {
  collect_visit_ctx *c = ctx;
  qa_app_q3_packages_collect(c->h->packages, &reference, 1, collect_flag, &is_q3);
}
typedef struct prepare_entry { double index; char *value; } prepare_entry;
static void host_prepare(void *ctx, double checksum_feed, double server_id,
    void (*configstring_cb)(void *, double, const char *), void *configstring_ctx) {
  q3_host *h = ctx;
  require_saved_server_id(h, server_id);
  if (h->packages == NULL) h->packages = open_packages(h, checksum_feed);
  if ((double)qa_q3_pak_references_checksum_feed(references(h)) != (double)qa_js_to_uint32(checksum_feed))
    qa_raise("Error", 0, 0, "Q3 world changed checksum feed without replacing content host");
  const qa_app_sim_network_content *content = h->options.content;
  if (cvar_value(h, "sv_pure") != 0 && !h->touched_cgame) {
    qa_resolved_resource_reference ignored;
    if (content->resolve_path) content->resolve_path(content->ctx, "vm/cgame.qvm", &ignored);
    h->touched_cgame = true;
  }
  collect_visit_ctx visit = {h};
  if (content->q3_collect_opened) content->q3_collect_opened(content->ctx, collect_visit, &visit);
  if (h->restore) return;
  qa_q3_pak_references *refs = references(h);
  char server_id_text[40];
  snprintf(server_id_text, sizeof server_id_text, "%s", QA_JS_NUMBER(server_id));
  cvar_set(h, "sv_serverid", server_id_text);
  bool pure = cvar_value(h, "sv_pure") != 0;
  char *values[4];
  values[0] = pure ? qa_q3_pak_references_loaded_pak_checksums(refs) : xstrdup("");
  values[1] = pure ? qa_q3_pak_references_loaded_pak_names(refs) : xstrdup("");
  values[2] = qa_q3_pak_references_referenced_pak_checksums(refs);
  values[3] = qa_q3_pak_references_referenced_pak_names(refs);
  static const char *const names[4] = {"sv_paks", "sv_pakNames", "sv_referencedPaks", "sv_referencedPakNames"};
  for (size_t i = 0; i < 4; i++) {
    if (!values[i]) qa_range_error("Out of memory");
    cvar_set(h, names[i], values[i]);
    free(values[i]);
  }
  /* [[1, systeminfo], [0, serverInfo()]]: both values are read before either is stored. */
  prepare_entry entries[2];
  entries[0].index = 1; entries[0].value = qa_cvar_registry_info_string(h->cvars, QA_CVAR_FLAG_SYSTEM_INFO, 8192);
  entries[1].index = 0; entries[1].value = qa_app_q3_server_state_server_info(h->state);
  if (!entries[0].value || !entries[1].value) { free(entries[0].value); free(entries[1].value); qa_range_error("Out of memory"); }
  qa_cleanup guards[2];
  qa_cleanup_push(&guards[0], free, entries[0].value);
  qa_cleanup_push(&guards[1], free, entries[1].value);
  for (size_t i = 0; i < 2; i++) {
    if (!h->guest) {
      qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(native_source(h));
      host->configstring_set(host->configstrings_ctx, entries[i].index, entries[i].value);
    } else if (strcmp(configstring(h, entries[i].index), entries[i].value) != 0) {
      qa_app_q3_server_state_configstring_set(h->state, entries[i].index, entries[i].value);
      if (configstring_cb) configstring_cb(configstring_ctx, entries[i].index, entries[i].value);
    }
  }
  qa_cleanup_pop(&guards[1]);
  qa_cleanup_pop(&guards[0]);
  free(entries[0].value); free(entries[1].value);
}
static const void *host_pure(void *ctx, double server_id, const double *checksum_feed_server_id) {
  q3_host *h = ctx;
  require_saved_server_id(h, server_id);
  qa_app_q3_packages *packages = archive_state(h);
  qa_q3_pure_server *pure = &h->pure;
  memset(pure, 0, sizeof *pure);
  pure->enabled = cvar_value(h, "sv_pure") != 0;
  pure->checksum_feed = qa_js_to_int32((double)qa_q3_pak_references_checksum_feed(references(h)));
  pure->checksum_feed_server_id = (int64_t)(checksum_feed_server_id ? *checksum_feed_server_id : server_id);
  double checksum = 0;
  pure->has_cgame_checksum = qa_app_q3_packages_pure_checksum(packages, "vm/cgame.qvm", &checksum);
  pure->cgame_checksum = (int64_t)checksum;
  pure->has_ui_checksum = qa_app_q3_packages_pure_checksum(packages, "vm/ui.qvm", &checksum);
  pure->ui_checksum = (int64_t)checksum;
  size_t count = 0;
  const qa_q3_mounted_pak *packs = qa_app_q3_packages_packs(packages, &count);
  free(h->pure_checksums);
  h->pure_checksums = xmalloc(count * sizeof *h->pure_checksums);
  for (size_t i = 0; i < count; i++) h->pure_checksums[i] = packs[i].pack.pure_checksum;
  pure->loaded_pure_checksums = h->pure_checksums; pure->loaded_count = count;
  return pure;
}
static bool host_downloads_enabled(void *ctx) { return cvar_value(ctx, "sv_allowDownload") != 0; }
static const void *host_open_download(void *ctx, const char *name) {
  q3_host *h = ctx;
  if (!qa_app_q3_packages_open_download(archive_state(h), name, &h->download)) return NULL;
  return &h->download;
}

/* ---- rate / wire support / time / slots. ---- */
static char *player_userinfo(q3_host *h, qa_app_q3_player player) {
  if (h->guest) {
    const char *info = qa_app_q3_server_state_get_userinfo(h->state, player.source_entity);
    return xstrdup(info != NULL ? info : "");
  }
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(native_source(h));
  char *info = host->engine.get_userinfo(host->engine.ctx, player.source_entity);
  if (!info) qa_range_error("Out of memory");
  return info;
}
static char *info_value(const char *info, const char *key) {
  char *value = qa_q3_info_value(info, key);
  if (!value) qa_range_error("Out of memory");
  return value;
}
static const void *host_rate(void *ctx, qa_app_q3_player player) {
  q3_host *h = ctx;
  char *info = player_userinfo(h, player);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, info);
  double fps = qa_js_max(1, cvar_value(h, "sv_fps"));
  char *rate_text = info_value(info, "rate");
  double requested_rate = qa_js_parse_int(rate_text, 10);
  free(rate_text);
  char *snaps_text = info_value(info, "snaps");
  double requested_snaps = qa_js_parse_int(snaps_text, 10);
  free(snaps_text);
  qa_q3_server_rate *rate = &h->rate;
  rate->rate = isnan(requested_rate) ? 3000 : qa_js_max(1000, qa_js_min(90000, requested_rate));
  rate->max_rate = cvar_value(h, "sv_maxRate");
  rate->snapshot_msec = trunc(1000 / (isnan(requested_snaps) ? fps : qa_js_max(1, qa_js_min(fps, requested_snaps))));
  char *ip = info_value(info, "ip");
  rate->local = strcmp(ip, "localhost") == 0;
  free(ip);
  rate->force_lan = false; rate->lan = false;
  qa_cleanup_pop(&guard);
  free(info);
  return rate;
}
static bool starts_with(const char *text, const char *prefix) {
  return text != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}
static const void *host_supports_source_wire(void *ctx) {
  q3_host *h = ctx;
  const qa_executable_recipe *recipe = h->simulation->recipe;
  qa_wire_admission_destroy(&h->support);
  memset(&h->support, 0, sizeof h->support);
  h->support.reasons = xmalloc(2 * sizeof *h->support.reasons);
  if (!starts_with(recipe->movement.provider, "q3:"))
    h->support.reasons[h->support.reason_count++] = xstrdup("Native Q3 wire requires Q3 movement");
  if (!starts_with(recipe->character.definition.provider, "q3:"))
    h->support.reasons[h->support.reason_count++] = xstrdup("Native Q3 wire requires a Q3 character");
  h->support.supported = h->support.reason_count == 0;
  return &h->support;
}
static double host_time(void *ctx) {
  q3_host *h = ctx;
  if (h->guest) return trunc(h->simulation->time_seconds(h->simulation->ctx) * 1000);
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(native_source(h));
  return host->now(host->ctx);
}
/* source.players().map(player => player.sourceEntity), or the native records' slots. */
static size_t occupied(q3_host *h) {
  if (h->guest) {
    qa_app_q3_player players[64];
    size_t count = qa_app_q3_qvm_server_game_players(h->guest_source, players, 64);
    if (count > 64) count = 64;
    if (h->occupied_cap < count) { h->occupied = xrealloc(h->occupied, count * sizeof *h->occupied); h->occupied_cap = count; }
    for (size_t i = 0; i < count; i++) h->occupied[i] = players[i].source_entity;
    return count;
  }
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  if (h->occupied_cap < count) { h->occupied = xrealloc(h->occupied, count * sizeof *h->occupied); h->occupied_cap = count; }
  for (size_t i = 0; i < count; i++) {
    qa_q3_game_entity *entity = qa_app_q3_source_runtime_record_by_actor(native_source(h), players[i]);
    h->occupied[i] = entity != NULL ? entity->slot : -1;
  }
  return count;
}
static const double *host_occupied_slots(void *ctx, size_t *out_count) {
  q3_host *h = ctx;
  *out_count = occupied(h);
  return h->occupied;
}

/* ---- admit / carriedPlayer / disconnect. ---- */
typedef struct admit_scope { q3_host *h; qa_client_id client; const char *userinfo; qa_app_q3_admission admitted; } admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  a->admitted = connect_client(a->h, a->client, a->userinfo);
}
static qa_app_q3_admission host_admit(void *ctx, const void *request_value) {
  q3_host *h = ctx;
  const qa_q3_accepted_connect *request = request_value;
  qa_session_client *client = qa_engine_session_create_client(h->options.session, request->slot);
  qa_session_client_connect(client, request->address.kind == QA_Q3_ADDR_LOOPBACK ? "loopback" : "remote");
  admit_scope scope = {h, qa_session_client_id(client), request->userinfo ? request->userinfo : "", {0}};
  qa_caught_error error;
  if (!qa_try(admit_body, &scope, &error)) {
    qa_engine_session_close_client(h->options.session, scope.client);
    qa_rethrow(&error);
  }
  if (!scope.admitted.accepted) qa_engine_session_close_client(h->options.session, scope.client);
  return scope.admitted;
}
static qa_app_q3_player host_carried_player(void *ctx, qa_client_id client) { return player_for(ctx, client); }
typedef struct disconnect_scope { q3_host *h; qa_app_q3_player player; } disconnect_scope;
static void disconnect_body(void *ctx) {
  disconnect_scope *d = ctx;
  if (d->h->guest) qa_app_q3_qvm_server_game_disconnect(d->h->guest_source, d->player);
  else d->h->simulation->disconnect_player(d->h->simulation->ctx, d->player.actor);
}
static void host_disconnect(void *ctx, qa_app_q3_player player, const char *reason) {
  (void)reason; /* TS disconnect(player) */
  q3_host *h = ctx;
  disconnect_scope scope = {h, player};
  qa_caught_error error;
  bool ok = qa_try(disconnect_body, &scope, &error);
  qa_engine_session_close_client(h->options.session, player.client); /* finally */
  if (!ok) qa_rethrow(&error);
}

/* ---- gameState. ---- */
static void gamestate_clear(q3_host *h) {
  for (size_t i = 0; i < h->gamestate.entry_count; i++) free(h->gamestate.entries[i].value);
  free(h->gamestate.entries);
  memset(&h->gamestate, 0, sizeof h->gamestate);
}
static qa_q3_gamestate_entry *gamestate_push(q3_host *h, size_t *cap) {
  if (h->gamestate.entry_count == *cap) {
    *cap = *cap ? *cap * 2 : 64;
    h->gamestate.entries = xrealloc(h->gamestate.entries, *cap * sizeof *h->gamestate.entries);
  }
  qa_q3_gamestate_entry *entry = &h->gamestate.entries[h->gamestate.entry_count++];
  memset(entry, 0, sizeof *entry);
  return entry;
}
static const void *host_game_state(void *ctx, qa_app_q3_player player, double server_id) {
  q3_host *h = ctx;
  require_saved_server_id(h, server_id);
  gamestate_clear(h);
  size_t cap = 0;
  /* configEntries().filter(...) is read first; a new world then sets sv_serverid and
   * unshifts the serverinfo and systeminfo strings. */
  if (!h->restore) { gamestate_push(h, &cap); gamestate_push(h, &cap); }
  for (int64_t index = 0; index < 1024; index++) {
    const char *value = configstring(h, (double)index);
    if (value[0] == '\0' || (!h->restore && (index == 0 || index == 1))) continue;
    qa_q3_gamestate_entry *entry = gamestate_push(h, &cap);
    entry->kind = QA_Q3_GS_CONFIGSTRING; entry->index = index; entry->value = xstrdup(value);
  }
  if (!h->restore) {
    char text[40];
    snprintf(text, sizeof text, "%s", QA_JS_NUMBER(server_id));
    cvar_set(h, "sv_serverid", text);
    qa_q3_gamestate_entry *info = &h->gamestate.entries[0], *system = &h->gamestate.entries[1];
    info->kind = QA_Q3_GS_CONFIGSTRING; info->index = 0; info->value = qa_app_q3_server_state_server_info(h->state);
    system->kind = QA_Q3_GS_CONFIGSTRING; system->index = 1;
    system->value = qa_cvar_registry_info_string(h->cvars, QA_CVAR_FLAG_SYSTEM_INFO, 8192);
    if (!info->value || !system->value) qa_range_error("Out of memory");
  }
  double count = entity_count(h);
  for (double number = 1; number < count; number++)
    if (linked(h, number)) {
      qa_q3_gamestate_entry *entry = gamestate_push(h, &cap);
      entry->kind = QA_Q3_GS_BASELINE; entry->number = (int64_t)number;
      wire_entity(h, number, &entry->entity);
    }
  h->gamestate.command_sequence = 0;
  h->gamestate.client_number = (int64_t)player.source_entity;
  h->gamestate.checksum_feed = qa_js_to_int32((double)qa_q3_pak_references_checksum_feed(references(h)));
  return &h->gamestate;
}

/* ---- snapshot. ---- */
static qa_q3_game_client *client_at(void *ctx, double number) {
  return qa_q3_entity_pool_client_at(qa_app_q3_source_runtime_pool(native_source(ctx)), number);
}
/* new PlayerStateRecord(product, ...).copyFrom({...ps, origin, velocity, stats and ammo of view}):
 * the record's slots are Int32Array (ToInt32 stores). */
static void native_player_record(q3_host *h, const qa_q3_base_player_state *ps, const qa_q3_base_player_state *view,
    qa_q3_player_state_record *out) {
  qa_q3_player_state_record_init(out, h->wire_product, qa_js_to_int32(ps->pm_type), qa_js_to_int32(ps->weapon),
      qa_js_to_int32(ps->weapon_state));
  out->product = qa_q3_base_player_state_record_product(ps) == QA_Q3_BASE_PRODUCT_MISSIONPACK
      ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  out->command_time = qa_js_to_int32(ps->command_time); out->pm_type = qa_js_to_int32(ps->pm_type);
  out->bob_cycle = qa_js_to_int32(ps->bob_cycle); out->pm_flags = qa_js_to_int32(ps->pm_flags);
  out->pm_time = qa_js_to_int32(ps->pm_time);
  out->origin = qa_q3_base_player_state_record_origin(view);
  out->velocity = qa_q3_base_player_state_record_velocity(view);
  out->weapon_time = qa_js_to_int32(ps->weapon_time); out->gravity = qa_js_to_int32(ps->gravity);
  out->speed = qa_js_to_int32(ps->speed); out->delta_angles = ps->delta_angles;
  out->ground_entity_num = qa_js_to_int32(ps->ground_entity_num); out->legs_timer = qa_js_to_int32(ps->legs_timer);
  out->legs_anim = qa_js_to_int32(ps->legs_anim); out->torso_timer = qa_js_to_int32(ps->torso_timer);
  out->torso_anim = qa_js_to_int32(ps->torso_anim); out->movement_dir = qa_js_to_int32(ps->movement_dir);
  out->grapple_point = ps->grapple_point;
  out->e_flags = qa_js_to_int32(ps->e_flags); out->event_sequence = qa_js_to_int32(ps->event_sequence);
  for (int i = 0; i < 2; i++) {
    out->events[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&ps->events, i));
    out->event_parms[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&ps->event_parms, i));
  }
  out->external_event = qa_js_to_int32(ps->external_event);
  out->external_event_parm = qa_js_to_int32(ps->external_event_parm);
  out->external_event_time = qa_js_to_int32(ps->external_event_time); out->client_num = qa_js_to_int32(ps->client_num);
  out->weapon = qa_js_to_int32(ps->weapon); out->weapon_state = qa_js_to_int32(ps->weapon_state);
  out->viewangles = ps->viewangles;
  out->viewheight = qa_js_to_int32(ps->viewheight); out->damage_event = qa_js_to_int32(ps->damage_event);
  out->damage_yaw = qa_js_to_int32(ps->damage_yaw); out->damage_pitch = qa_js_to_int32(ps->damage_pitch);
  out->damage_count = qa_js_to_int32(ps->damage_count);
  for (int i = 0; i < 16; i++) {
    out->stats[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&view->stats, i));
    out->persistant[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&ps->persistant, i));
    out->powerups[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&ps->powerups, i));
    out->ammo[i] = qa_js_to_int32(qa_q3_base_player_state_slots_get(&view->ammo, i));
  }
  out->generic1 = qa_js_to_int32(ps->generic1); out->loop_sound = qa_js_to_int32(ps->loop_sound);
  out->jumppad_ent = qa_js_to_int32(ps->jumppad_ent); out->ping = qa_js_to_int32(ps->ping);
  out->pmove_framecount = qa_js_to_int32(ps->pmove_framecount); out->jumppad_frame = qa_js_to_int32(ps->jumppad_frame);
  out->entity_event_sequence = qa_js_to_int32(ps->entity_event_sequence);
}
static void guest_player_record(q3_host *h, double slot, qa_q3_player_state_record *out) {
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(guest_records(h), slot, &state, stats, persistent, powerups, ammo);
  qa_q3_from_player_state(&state, h->wire_product, out);
}
static qa_q3_visibility_entity vis_entity(void *ctx, int64_t number) {
  q3_host *h = ctx;
  qa_q3_visibility_entity entity;
  qa_q3_entity_state_record *state = &h->wire[number];
  wire_entity(h, (double)number, state);
  entity.state = state;
  entity.linked = linked(h, (double)number);
  double sv_flags, single_client;
  entity_flags(h, (double)number, &sv_flags, &single_client);
  entity.flags = qa_js_to_int32(sv_flags);
  entity.single_client = qa_js_to_int32(single_client);
  return entity;
}
static bool vis_link(void *ctx, int64_t number, qa_q3_visibility_link *out) {
  q3_host *h = ctx;
  if (h->guest) {
    qa_app_q3_visibility_link link;
    if (!qa_app_q3_guest_records_visibility(guest_records(h), (double)number, &link)) return false;
    out->areanum = link.areanum; out->areanum2 = link.areanum2; out->clusters = link.clusters;
    out->cluster_count = link.cluster_count; out->last_cluster = link.last_cluster;
    return true;
  }
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_q3_game_entity *item = pool_at(h, (double)number);
  qa_linked_body_result body = qa_shared_body_table_linked(s->bodies, qa_q3_game_entity_actor(item).id);
  if (!body.has_value) return false;
  qa_leaf_query_result query = qa_shared_box_leaves(s->scene, body.value.absolute_bounds, 128);
  /* [...new Set(leaves.map(leafCluster).filter(cluster >= 0))]: first-seen order. */
  size_t leaf_count = query.leaves.count;
  if (h->cluster_cap < leaf_count) { h->clusters = xrealloc(h->clusters, leaf_count * sizeof *h->clusters); h->cluster_cap = leaf_count; }
  double *leaves = xmalloc(leaf_count * sizeof *leaves);
  memcpy(leaves, query.leaves.values, leaf_count * sizeof *leaves);
  size_t cluster_count = 0;
  for (size_t i = 0; i < leaf_count; i++) {
    double cluster = qa_shared_leaf_cluster(s->scene, leaves[i]);
    if (!(cluster >= 0)) continue;
    bool seen = false;
    for (size_t k = 0; k < cluster_count && !seen; k++) seen = (double)h->clusters[k] == cluster;
    if (!seen) h->clusters[cluster_count++] = (int64_t)cluster;
  }
  double areanum = -1, areanum2 = -1;
  for (size_t i = 0; i < leaf_count; i++) {
    double area = qa_shared_leaf_area(s->scene, leaves[i]);
    if (area == -1) continue;
    if (areanum != -1 && areanum != area) areanum2 = area;
    else areanum = area;
  }
  free(leaves);
  out->areanum = (int64_t)areanum; out->areanum2 = (int64_t)areanum2;
  out->clusters = h->clusters; out->cluster_count = cluster_count; out->last_cluster = 0;
  return true;
}
static int64_t vis_point_leaf(void *ctx, qa_vec3 point) {
  return (int64_t)qa_shared_point_leaf(((q3_host *)ctx)->simulation->scene, point);
}
static int64_t vis_leaf_area(void *ctx, int64_t leaf) {
  return (int64_t)qa_shared_leaf_area(((q3_host *)ctx)->simulation->scene, (double)leaf);
}
static int64_t vis_leaf_cluster(void *ctx, int64_t leaf) {
  return (int64_t)qa_shared_leaf_cluster(((q3_host *)ctx)->simulation->scene, (double)leaf);
}
static bool vis_areas_connected(void *ctx, int64_t a, int64_t b) {
  q3_host *h = ctx;
  return cvar_value(h, "cm_noAreas") != 0
      || (a >= 0 && b >= 0 && qa_shared_areas_connected(h->simulation->scene, (double)a, (double)b));
}
static int64_t vis_write_area_bits(void *ctx, uint8_t *bytes, int64_t area) {
  q3_host *h = ctx;
  size_t length = 0;
  const uint8_t *bits = qa_shared_area_bits(h->simulation->scene, (double)area, &length);
  /* Uint8Array stores past the 32-byte mask are dropped, as in TS. */
  for (size_t i = 0; i < length && i < 32; i++) bytes[i] = (uint8_t)(bytes[i] | bits[i]);
  return (int64_t)length;
}
static int64_t vis_pvs_byte(void *ctx, int64_t cluster, int64_t index) {
  q3_host *h = ctx;
  int64_t value = 0;
  for (int bit = 0; bit < 8; bit++)
    if (qa_shared_cluster_visible(h->simulation->scene, (double)cluster, (double)(index * 8 + bit), "pvs")) value |= 1 << bit;
  return value;
}
static void vis_print(void *ctx, const char *text) { host_print(ctx, text); }
static qa_app_q3_snapshot host_snapshot(void *ctx, qa_app_q3_player player) {
  q3_host *h = ctx;
  qa_q3_player_state_record *state = &h->snapshot_player;
  if (h->guest) guest_player_record(h, player.source_entity, state);
  else {
    qa_q3_game_entity *entity = qa_app_q3_source_runtime_record_by_actor(native_source(h), player.actor);
    if (entity == NULL || entity->client == NULL) qa_raise("Error", 0, 0, "Q3 snapshot player disappeared");
    const qa_q3_base_player_state *ps = &entity->client->ps;
    const qa_q3_base_player_state *view = qa_q3_ta_snapshot_view_source(ps, client_at, h);
    native_player_record(h, ps, view, state);
  }
  double count = entity_count(h);
  size_t slots = count > 0 ? (size_t)count : 0;
  if (h->wire_cap < slots) { h->wire = xrealloc(h->wire, slots * sizeof *h->wire); h->wire_cap = slots; }
  qa_q3_visibility_bindings bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.entity_count = (int64_t)count; bindings.dead = false;
  bindings.entity = vis_entity; bindings.link = vis_link; bindings.ctx = h;
  bindings.print = vis_print; bindings.print_ctx = h;
  bindings.collision.point_leafnum = vis_point_leaf; bindings.collision.leaf_area = vis_leaf_area;
  bindings.collision.leaf_cluster = vis_leaf_cluster; bindings.collision.areas_connected = vis_areas_connected;
  bindings.collision.write_area_bits = vis_write_area_bits; bindings.collision.pvs_byte_at = vis_pvs_byte;
  bindings.collision.ctx = h;
  if (h->has_visible) { qa_q3_visible_entities_destroy(&h->visible); h->has_visible = false; }
  if (!qa_q3_select_snapshot_entities(state, &bindings, &h->visible)) qa_range_error("Out of memory");
  h->has_visible = true;
  free(h->snapshot_entities);
  h->snapshot_entities = xmalloc(h->visible.entity_count * sizeof *h->snapshot_entities);
  for (size_t i = 0; i < h->visible.entity_count; i++) h->snapshot_entities[i] = h->visible.entities[i];
  qa_app_q3_snapshot snapshot;
  snapshot.player = state;
  snapshot.area_mask = h->visible.area_mask; snapshot.area_mask_length = h->visible.area_mask_len;
  snapshot.entities = h->snapshot_entities; snapshot.entity_count = h->visible.entity_count;
  return snapshot;
}

/* ---- begin / input / command / userinfo. ---- */
static void host_begin(void *ctx, qa_app_q3_player player, const void *command) {
  q3_host *h = ctx;
  qa_app_q3_qvm_server_game_begin(h->guest_source, player, base_command(command));
}
static const void *host_input(void *ctx, qa_app_q3_player player, const void *command_value, double sequence) {
  q3_host *h = ctx;
  const qa_q3_wire_user_command *command = command_value;
  qa_actor_command input;
  memset(&input, 0, sizeof input);
  input.actor = player.actor;
  input.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; input.source.client = player.client;
  input.sequence = sequence;
  input.command.kind = QA_USER_COMMAND_Q3;
  input.command.as.q3 = qa_q3_to_user_command(command);
  if (h->guest) {
    h->simulation->observe_client_command(h->simulation->ctx, &input);
    qa_app_q3_qvm_server_game_think(h->guest_source, player, base_command(command));
    return NULL;
  }
  qa_actor_command *out = xmalloc(sizeof *out);
  *out = input;
  h->commands = xrealloc(h->commands, (h->command_count + 1) * sizeof *h->commands);
  h->commands[h->command_count++] = out;
  return out;
}
static void host_command(void *ctx, qa_app_q3_player player, const char *name, const char *const *args, size_t count) {
  q3_host *h = ctx;
  if (!h->guest) { qa_app_q3_source_runtime_player_command(native_source(h), player.actor, name, args, count); return; }
  const char **argv = xmalloc((count + 1) * sizeof *argv);
  argv[0] = name;
  for (size_t i = 0; i < count; i++) argv[i + 1] = args[i];
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, (void *)argv);
  qa_app_q3_qvm_server_game_command(h->guest_source, player, argv, count + 1);
  qa_cleanup_pop(&guard);
  free((void *)argv);
}
static void host_userinfo(void *ctx, qa_app_q3_player player, const char *value) {
  q3_host *h = ctx;
  if (h->guest) { qa_app_q3_qvm_server_game_userinfo(h->guest_source, player, value); return; }
  qa_app_q3_server_state_set_userinfo(h->state, player.source_entity, value);
  qa_app_q3_source_runtime_userinfo_changed(native_source(h), player.source_entity);
  h->simulation->notify_client_event(h->simulation->ctx, "userinfo", player.actor);
}

/* ---- status. ---- */
typedef struct text_buffer { char *text; size_t length, cap; } text_buffer;
static void text_append(text_buffer *b, const char *text) {
  size_t n = strlen(text);
  if (b->length + n + 1 > b->cap) {
    b->cap = (b->length + n + 1) * 2;
    b->text = xrealloc(b->text, b->cap);
  }
  memcpy(b->text + b->length, text, n + 1);
  b->length += n;
}
static char *put(q3_host *h, char *info, const char *key, const char *value) {
  qa_info_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3; options.maximum_length = 1024;
  options.target = QA_CVAR_INFO_SERVER_INFO; options.server_high_characters = false;
  options.print = print_forward; options.print_ctx = h;
  char *next = qa_set_info_value(info, key, value, &options);
  if (!next) qa_range_error("Out of memory");
  free(info);
  return next;
}
static char *status_row(q3_host *h, double slot, qa_actor_id actor) {
  char row[512];
  if (h->guest) {
    qa_q3_player_state state;
    double stats[16], persistent[16], powerups[16], ammo[16];
    qa_app_q3_guest_records_player(guest_records(h), slot, &state, stats, persistent, powerups, ammo);
    const char *info = qa_app_q3_server_state_get_userinfo(h->state, slot);
    char *name = info_value(info != NULL ? info : "", "name");
    double ping = qa_qvm_game_data_player_ping(guest_data(h), slot);
    char score_text[40], ping_text[40];
    snprintf(score_text, sizeof score_text, "%s", QA_JS_NUMBER(persistent[0]));
    snprintf(ping_text, sizeof ping_text, "%s", QA_JS_NUMBER(ping));
    size_t n = strlen(score_text) + strlen(ping_text) + strlen(name) + 8;
    char *text = xmalloc(n);
    snprintf(text, n, "%s %s \"%s\"\n", score_text, ping_text, name);
    free(name);
    return text;
  }
  qa_q3_game_entity *entity = qa_app_q3_source_runtime_record_by_actor(native_source(h), actor);
  if (entity == NULL || entity->client == NULL) return xstrdup("");
  qa_q3_game_client *client = entity->client;
  char score_text[40], ping_text[40];
  snprintf(score_text, sizeof score_text, "%s", QA_JS_NUMBER(qa_js_to_int32(qa_q3_base_player_state_slots_get(&client->ps.persistant, 0))));
  snprintf(ping_text, sizeof ping_text, "%s", QA_JS_NUMBER(client->ps.ping));
  const char *name = client->pers.netname != NULL ? client->pers.netname : "";
  size_t n = strlen(score_text) + strlen(ping_text) + strlen(name) + 8;
  if (n <= sizeof row) { snprintf(row, sizeof row, "%s %s \"%s\"\n", score_text, ping_text, name); return xstrdup(row); }
  char *text = xmalloc(n);
  snprintf(text, n, "%s %s \"%s\"\n", score_text, ping_text, name);
  return text;
}
static const char *host_status(void *ctx, const char *challenge, bool detailed) {
  q3_host *h = ctx;
  if (cvar_value(h, "g_gametype") == 2 || (!detailed && cvar_value(h, "ui_singlePlayerActive") != 0)) return NULL;
  char *info = detailed ? qa_cvar_registry_info_string(h->cvars, QA_CVAR_FLAG_SERVER_INFO, 1024) : xstrdup("");
  if (!info) qa_range_error("Out of memory");
  info = put(h, info, "challenge", challenge);
  free(h->status); h->status = NULL;
  if (!detailed) {
    double private_clients = cvar_integer(h, "sv_privateClients", 0);
    size_t count = occupied(h);
    double clients = 0;
    for (size_t i = 0; i < count; i++)
      if (h->occupied[i] >= private_clients && h->occupied[i] < h->max_clients) clients++;
    char protocol[40], client_text[40], max_text[40], gametype[40], pure[40];
    snprintf(protocol, sizeof protocol, "%s", QA_JS_NUMBER((double)qa_q3_protocol.version));
    snprintf(client_text, sizeof client_text, "%s", QA_JS_NUMBER(clients));
    snprintf(max_text, sizeof max_text, "%s", QA_JS_NUMBER(h->max_clients - private_clients));
    snprintf(gametype, sizeof gametype, "%s", QA_JS_NUMBER(cvar_integer(h, "g_gametype", 0)));
    snprintf(pure, sizeof pure, "%s", QA_JS_NUMBER(cvar_integer(h, "sv_pure", 0)));
    char *hostname = cvar_string(h, "sv_hostname"), *mapname = cvar_string(h, "mapname");
    const char *const fields[7][2] = {{"protocol", protocol}, {"hostname", hostname}, {"mapname", mapname},
        {"clients", client_text}, {"sv_maxclients", max_text}, {"gametype", gametype}, {"pure", pure}};
    for (size_t i = 0; i < 7; i++) info = put(h, info, fields[i][0], fields[i][1]);
    free(hostname); free(mapname);
    static const char *const pings[2][2] = {{"minPing", "sv_minPing"}, {"maxPing", "sv_maxPing"}};
    for (size_t i = 0; i < 2; i++) {
      double value = cvar_integer(h, pings[i][1], 0);
      if (value != 0) {
        char text[40];
        snprintf(text, sizeof text, "%s", QA_JS_NUMBER(value));
        info = put(h, info, pings[i][0], text);
      }
    }
    char *game = cvar_string(h, "fs_game");
    info = put(h, info, "game", game);
    free(game);
    size_t n = strlen(info) + 16;
    h->status = xmalloc(n);
    snprintf(h->status, n, "infoResponse\n%s", info);
    free(info);
    return h->status;
  }
  if (h->options.content->q3_demo_restricted) {
    char *keywords = info_value(info, "sv_keywords");
    size_t n = strlen(keywords) + 8;
    char *value = xmalloc(n);
    snprintf(value, n, "demo %s", keywords);
    free(keywords);
    info = put(h, info, "sv_keywords", value);
    free(value);
  }
  /* Rows are built for every player first, then appended while players.length + row.length
   * stays under 16384 (UTF-16 units in TS; byte lengths here). */
  text_buffer players = {0};
  text_append(&players, "");
  if (h->guest) {
    qa_app_q3_player list[64];
    size_t count = qa_app_q3_qvm_server_game_players(h->guest_source, list, 64);
    if (count > 64) count = 64;
    char **rows = xmalloc(count * sizeof *rows);
    for (size_t i = 0; i < count; i++) rows[i] = status_row(h, list[i].source_entity, list[i].actor);
    bool full = false;
    for (size_t i = 0; i < count; i++) {
      if (!full && players.length + strlen(rows[i]) >= 16384) full = true;
      if (!full) text_append(&players, rows[i]);
      free(rows[i]);
    }
    free(rows);
  } else {
    const qa_app_sim_network_simulation *s = h->simulation;
    size_t count = 0;
    const qa_actor_id *actors = s->players(s->ctx, &count);
    qa_actor_id *copy = xmalloc(count * sizeof *copy);
    memcpy(copy, actors, count * sizeof *copy);
    char **rows = xmalloc(count * sizeof *rows);
    for (size_t i = 0; i < count; i++) rows[i] = status_row(h, 0, copy[i]);
    bool full = false;
    for (size_t i = 0; i < count; i++) {
      if (!full && players.length + strlen(rows[i]) >= 16384) full = true;
      if (!full) text_append(&players, rows[i]);
      free(rows[i]);
    }
    free(rows); free(copy);
  }
  size_t n = strlen(info) + players.length + 20;
  h->status = xmalloc(n);
  snprintf(h->status, n, "statusResponse\n%s\n%s", info, players.text);
  free(info); free(players.text);
  return h->status;
}
static void host_print_member(void *ctx, const char *text) { host_print(ctx, text); }

/* ---- construction. ---- */
qa_app_sim_q3_server_authority qa_app_sim_create_q3_application_server_host(qa_app_sim_q3_server_binding_options options) {
  const qa_app_sim_network_simulation *s = options.simulation;
  qa_app_q3_qvm_server_game *guest_source = s->q3_guest ? s->q3_guest(s->ctx) : NULL;
  qa_app_q3_source_runtime *native = guest_source == NULL && s->q3_source ? s->q3_source(s->ctx) : NULL;
  if (guest_source == NULL && native == NULL) qa_raise("Error", 0, 0, "Q3 network requires a Q3 game provider");
  bool guest = guest_source != NULL;
  const char *mode = options.mode != NULL ? options.mode : "new";
  if (is(mode, "restore") && !guest) qa_raise("Error", 0, 0, "Restored Q3 client authority requires a saved QVM source");
  q3_host *h = calloc(1, sizeof *h);
  if (!h) qa_range_error("Out of memory");
  h->options = options; h->simulation = s;
  h->guest = guest; h->guest_source = guest_source; h->native_round = guest ? NULL : native;
  h->restore = is(mode, "restore");
  h->state = guest ? qa_app_q3_qvm_server_game_state(guest_source) : qa_app_q3_source_runtime_host(native)->server_state;
  h->cvars = qa_app_q3_server_state_cvars(h->state);
  h->product = guest ? "baseq3" : qa_app_q3_source_runtime_options(native)->product;
  if (!qa_q3_product_parse(h->product, &h->wire_product)) h->wire_product = QA_Q3_PRODUCT_BASEQ3;
  h->max_clients = s->options->max_clients;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, h);
  if (!h->restore) {
    char *fs_game = guest ? cvar_string(h, "fs_game") : xstrdup(is(h->product, "missionpack") ? "missionpack" : "");
    qa_cvar_snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    qa_cvar_registry_register(h->cvars, "fs_game", fs_game, QA_CVAR_FLAG_SYSTEM_INFO, &snapshot);
    qa_cvar_read_free(&snapshot);
    free(fs_game);
  }
  qa_cleanup_pop(&guard);
  qa_app_sim_q3_server_authority out;
  memset(&out, 0, sizeof out);
  qa_app_q3_server_host *host = &out.host;
  host->ctx = h;
  host->has_admission = true;
  host->admission.ctx = h;
  host->admission.private_clients = adm_private_clients;
  host->admission.private_password = adm_private_password;
  host->admission.reconnect_limit_seconds = adm_reconnect_limit;
  host->admission.minimum_ping = adm_minimum_ping;
  host->admission.maximum_ping = adm_maximum_ping;
  host->admission.demo_restricted = adm_demo_restricted;
  host->admission.enabled = adm_enabled;
  host->admission.game_directory = adm_game_directory;
  host->admission.strict_auth = adm_strict_auth;
  host->admission.flood_protect = adm_flood_protect;
  host->administration = options.administration;
  host->product = &h->wire_product; /* q3_types: a qa_q3_product */
  host->max_clients = h->max_clients;
  if (!guest) {
    host->has_source_round = true;
    host->source_round.ctx = h;
    host->source_round.preflight = round_preflight;
    host->source_round.rebind = round_rebind;
    host->source_round.reconnect = round_reconnect;
  }
  host->prepare = host_prepare;
  host->pure = host_pure;
  host->downloads_enabled = host_downloads_enabled;
  host->open_download = host_open_download;
  host->rate = host_rate;
  host->supports_source_wire = host_supports_source_wire;
  host->time = host_time;
  host->occupied_slots = host_occupied_slots;
  host->admit = host_admit;
  host->carried_player = host_carried_player;
  host->disconnect = host_disconnect;
  host->game_state = host_game_state;
  host->snapshot = host_snapshot;
  if (guest) host->begin = host_begin;
  host->input = host_input;
  host->command = host_command;
  host->userinfo = host_userinfo;
  host->status = host_status;
  host->print = host_print_member;
  out.connect = authority_connect;
  return out;
}
void qa_app_sim_q3_application_server_authority_destroy(qa_app_sim_q3_server_authority authority) {
  q3_host *h = authority.host.ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < 3; i++) free(h->strings[i]);
  free(h->reason);
  free(h->pure_checksums);
  qa_wire_admission_destroy(&h->support);
  free(h->occupied);
  gamestate_clear(h);
  free(h->wire);
  free(h->clusters);
  if (h->has_visible) qa_q3_visible_entities_destroy(&h->visible);
  free(h->snapshot_entities);
  for (size_t i = 0; i < h->command_count; i++) free(h->commands[i]);
  free(h->commands);
  free(h->status);
  qa_app_q3_packages_destroy(h->packages);
  free(h);
}

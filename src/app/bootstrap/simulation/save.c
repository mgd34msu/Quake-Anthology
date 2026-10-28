/* Port of src/app/bootstrap/simulation/save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/save_image.h"
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static bool has_schema(qa_save_image image, const char *schema) {
  for (size_t i = 0; i < image.provider_count; i++) if (is(image.providers[i].schema, schema)) return true;
  return false;
}
static const qa_execution_module *server_execution(qa_save_image image) {
  for (size_t i = 0; i < image.recipe.execution_count; i++)
    if (is(image.recipe.execution[i].role, "server-game")) return &image.recipe.execution[i];
  return NULL;
}
qa_provider_checkpoint qa_app_sim_simulation_provider_checkpoint(qa_save_image image, const char *schema) {
  const qa_provider_checkpoint *value = NULL; size_t matches = 0;
  for (size_t i = 0; i < image.provider_count; i++)
    if (is(image.providers[i].schema, schema)) { if (!value) value = &image.providers[i]; matches++; }
  qa_provider_checkpoint_ref expected = qa_save_provider_contract(schema, image.recipe.map.entities.provider);
  if (value == NULL || matches != 1 || !is(value->provider, expected.provider) || value->version != expected.version)
    qa_range_error("Missing or unsupported saved provider %s", schema);
  return *value;
}
static qa_module_identity saved_native_module(qa_save_image image, const char *schema) {
  const qa_execution_module *execution = server_execution(image);
  if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_NATIVE)
    qa_range_error("Native saved identity has no source execution");
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, schema);
  qa_save_value *tree = qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
  qa_save_reader reader; qa_save_reader_init(&reader, tree, schema);
  qa_save_reader mf = fld(&reader, "module");
  qa_module_identity module = qa_read_module(&mf);
  qa_save_reader_destroy(&mf); qa_save_reader_destroy(&reader);
  const qa_resolved_resource_reference *artifact = execution->artifact;
  char prefix[1024];
  snprintf(prefix, sizeof prefix, "%s/native-compatibility.json/", artifact->digest);
  bool declared = false;
  if (module.revision && strncmp(module.revision, prefix, strlen(prefix)) == 0) {
    const char *rest = module.revision + strlen(prefix);
    declared = strncmp(rest, "sha256:", 7) == 0 && strlen(rest) == 71;
    for (const char *c = rest + 7; declared && *c; c++) declared = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f');
  }
  if (!is(module.id, execution->owner.provider) || !is(module.artifact_path, artifact->requested_path)
      || !is(module.digest, artifact->digest) || (!is(module.revision, artifact->digest) && !declared)) {
    qa_save_value_free(tree);
    qa_range_error("Native saved identity differs from the selected artifact");
  }
  (void)tree; /* identity strings borrow the tree; leak-free callers use short-lived images */
  return module;
}
bool qa_app_sim_native_q2_original_save(qa_save_image image, const qa_module_identity *expected, qa_q2_classic_original_save *out) {
  const qa_execution_module *execution = server_execution(image);
  if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_NATIVE) {
    if (has_schema(image, "q2:classic-native-original")) qa_range_error("Original API 3 save has no matching native execution");
    return false;
  }
  if (execution->api.kind == QA_GAME_API_Q2_GAME && execution->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE) {
    if (has_schema(image, "q2:classic-native-original")) qa_range_error("Classic native save has a rerelease execution");
    return false;
  }
  if (execution->api.kind != QA_GAME_API_Q2_GAME || execution->api.as.q2_game.kind != QA_Q2_GAME_API_CLASSIC || execution->api.as.q2_game.version != 3 || !is(execution->profile.kind, "windows-i386"))
    qa_range_error("Unsupported native original-save execution");
  if (has_schema(image, "q2:rerelease-native-original")) qa_range_error("Rerelease native save has a classic execution");
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "q2:classic-native-original");
  qa_q2_classic_original_identity identity;
  identity.module = expected ? *expected : saved_native_module(image, "q2:classic-native-original");
  identity.map = image.recipe.map.geometry.requested_path;
  if (!qa_decode_q2_classic_original_save(&record, identity, out)) qa_range_error("Out of memory");
  return true;
}
bool qa_app_sim_native_q2_rerelease_save(qa_save_image image, const qa_module_identity *expected, qa_app_sim_rerelease_native_save_view *out) {
  const qa_execution_module *execution = server_execution(image);
  if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_NATIVE || execution->api.kind != QA_GAME_API_Q2_GAME || execution->api.as.q2_game.kind != QA_Q2_GAME_API_RERELEASE) {
    if (has_schema(image, "q2:rerelease-native-original")) qa_range_error("Rerelease native save has no matching execution");
    return false;
  }
  if (execution->api.as.q2_game.version != 2023 || !is(execution->profile.kind, "windows-x86-64"))
    qa_range_error("Unsupported rerelease native save execution");
  if (has_schema(image, "q2:classic-native-original")) qa_range_error("Classic native save has a rerelease execution");
  qa_module_identity module = expected ? *expected : saved_native_module(image, "q2:rerelease-native-original");
  (void)module;
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "q2:rerelease-native-original");
  out->value = qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
  /* server.cvars borrowed from the decoded tree. */
  const qa_save_value *server = qa_save_value_get(out->value, "server");
  const qa_save_value *cvars = server ? qa_save_value_get(server, "cvars") : NULL;
  out->cvars.bytes = cvars ? cvars->bytes : NULL; out->cvars.length = cvars ? cvars->byte_length : 0;
  return true;
}
qa_save_value *qa_app_sim_saved_source_cvars(qa_save_image image) {
  qa_q2_classic_original_save classic; memset(&classic, 0, sizeof classic);
  qa_app_sim_rerelease_native_save_view rerelease; memset(&rerelease, 0, sizeof rerelease);
  bool has_classic = false, has_rerelease = false;
  /* nativeQ2OriginalSave(image) ?? nativeQ2RereleaseSave(image): only
   * probe when a native schema exists to avoid double decode errors. */
  if (has_schema(image, "q2:classic-native-original")) has_classic = qa_app_sim_native_q2_original_save(image, NULL, &classic);
  else if (has_schema(image, "q2:rerelease-native-original")) has_rerelease = qa_app_sim_native_q2_rerelease_save(image, NULL, &rerelease);
  if (has_classic) {
    qa_save_value *out = qa_decode_checkpoint_value(classic.server.cvars.bytes, classic.server.cvars.length);
    qa_q2_classic_original_save_free(&classic);
    return out;
  }
  if (has_rerelease) {
    qa_save_value *out = qa_decode_checkpoint_value(rerelease.cvars.bytes, rerelease.cvars.length);
    qa_save_value_free(rerelease.value);
    return out;
  }
  if (!has_schema(image, "world:source-cvars")) return NULL;
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "world:source-cvars");
  return qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
}
qa_save_reader qa_app_sim_simulation_save_reader(qa_save_image image, qa_save_value **keep_alive) {
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "world:simulation");
  qa_save_value *tree = qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
  *keep_alive = tree;
  qa_save_reader reader; qa_save_reader_init(&reader, tree, "world:simulation");
  return reader;
}
static size_t players_count(qa_save_image image, qa_save_value **keep) {
  qa_save_reader reader = qa_app_sim_simulation_save_reader(image, keep);
  qa_save_reader players = fld(&reader, "players");
  size_t n = qa_save_reader_list_count(&players);
  qa_save_reader_destroy(&players); qa_save_reader_destroy(&reader);
  return n;
}
qa_app_sim_saved_bots *qa_app_sim_saved_bot_checkpoint(qa_save_image image) {
  if (!has_schema(image, "world:bots")) return NULL;
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "world:bots");
  qa_app_sim_saved_bots *bots = calloc(1, sizeof *bots);
  if (!bots) qa_range_error("Out of memory");
  bots->value = qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
  /* Read transport.connections[{client.slot, actor}] from the tree. */
  const qa_save_value *transport = qa_save_value_get(bots->value, "transport");
  const qa_save_value *connections = transport ? qa_save_value_get(transport, "connections") : NULL;
  size_t count = connections && connections->kind == QA_SAVE_ARRAY ? connections->item_count : 0;
  bots->connections = count ? malloc(count * sizeof *bots->connections) : NULL;
  bots->connection_count = count;
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *c = connections->items[i];
    const qa_save_value *client = qa_save_value_get(c, "client");
    const qa_save_value *slot = client ? qa_save_value_get(client, "slot") : NULL;
    const qa_save_value *actor = qa_save_value_get(c, "actor");
    bots->connections[i].client_slot = slot ? slot->number : 0;
    const qa_save_value *as = actor ? qa_save_value_get(actor, "slot") : NULL;
    const qa_save_value *ag = actor ? qa_save_value_get(actor, "generation") : NULL;
    bots->connections[i].actor.slot = as ? as->number : 0;
    bots->connections[i].actor.generation = ag ? ag->number : 0;
  }
  /* Validate against saved players. */
  qa_save_value *keep = NULL;
  qa_save_reader reader = qa_app_sim_simulation_save_reader(image, &keep);
  qa_save_reader players = fld(&reader, "players");
  size_t player_count = qa_save_reader_list_count(&players);
  typedef struct { double slot; qa_saved_actor_id actor; } saved_player;
  saved_player *saved = player_count ? malloc(player_count * sizeof *saved) : NULL;
  for (size_t i = 0; i < player_count; i++) {
    qa_save_reader entry, sf, af;
    qa_save_reader_item(&players, i, &entry);
    sf = fld(&entry, "clientSlot"); saved[i].slot = qa_save_reader_integer_min(&sf, 0); qa_save_reader_destroy(&sf);
    af = fld(&entry, "actor"); saved[i].actor = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&players); qa_save_reader_destroy(&reader);
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < i; j++)
      if (bots->connections[j].client_slot == bots->connections[i].client_slot)
        qa_range_error("Duplicate saved bot client slot");
    bool owned = false;
    for (size_t k = 0; k < player_count; k++)
      if (saved[k].slot == bots->connections[i].client_slot
          && saved[k].actor.slot == bots->connections[i].actor.slot
          && saved[k].actor.generation == bots->connections[i].actor.generation) owned = true;
    if (!owned) qa_range_error("Saved bot connection does not own its shared player");
  }
  free(saved); qa_save_value_free(keep);
  return bots;
}
static bool same_module(qa_module_identity a, qa_module_identity b) {
  return is(a.id, b.id) && is(a.artifact_path, b.artifact_path) && is(a.digest, b.digest) && is(a.revision, b.revision);
}
static bool same_api(qa_game_api_identity a, qa_game_api_identity b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case QA_GAME_API_QUAKEC: return a.as.quakec.kind == b.as.quakec.kind && a.as.quakec.program_version == b.as.quakec.program_version && a.as.quakec.system_crc == b.as.quakec.system_crc;
    case QA_GAME_API_Q2_GAME: return a.as.q2_game.kind == b.as.q2_game.kind && a.as.q2_game.version == b.as.q2_game.version;
    case QA_GAME_API_Q2_CGAME: return a.as.q2_cgame.version == b.as.q2_cgame.version;
    case QA_GAME_API_Q3: return a.as.q3.kind == b.as.q3.kind && a.as.q3.version == b.as.q3.version;
  }
  return false;
}
static bool api_is(qa_game_api_identity api, const char *name) {
  if (is(name, "q1-netquake")) return api.kind == QA_GAME_API_QUAKEC && api.as.quakec.kind == QA_QUAKEC_API_Q1_NETQUAKE;
  if (is(name, "q1-quakeworld")) return api.kind == QA_GAME_API_QUAKEC && api.as.quakec.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  if (is(name, "q2-classic-game")) return api.kind == QA_GAME_API_Q2_GAME && api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC;
  if (is(name, "q2-rerelease-game")) return api.kind == QA_GAME_API_Q2_GAME && api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE;
  if (is(name, "q3-qagame")) return api.kind == QA_GAME_API_Q3 && api.as.q3.kind == QA_Q3_API_QAGAME;
  return false;
}
const qa_guest_checkpoint *qa_app_sim_simulation_guest_checkpoint(qa_save_image image) {
  const qa_execution_module *execution = NULL; size_t count = 0;
  for (size_t i = 0; i < image.recipe.execution_count; i++)
    if (is(image.recipe.execution[i].role, "server-game")) { execution = &image.recipe.execution[i]; count++; }
  if (count != 1 || execution == NULL) qa_range_error("Save requires exactly one selected server execution");
  if (execution->kind != QA_EXECUTION_MODULE_QUAKEC && execution->kind != QA_EXECUTION_MODULE_QVM) {
    if (image.guest_count != 0) qa_range_error("Selected source cannot restore guest memory providers");
    return NULL;
  }
  const char *name = execution->kind == QA_EXECUTION_MODULE_QUAKEC ? "QuakeC" : "QVM";
  const qa_guest_checkpoint *checkpoint = image.guest_count ? &image.guests[0] : NULL;
  qa_guest_checkpoint_kind want = execution->kind == QA_EXECUTION_MODULE_QUAKEC ? QA_GUEST_CHECKPOINT_QUAKEC : QA_GUEST_CHECKPOINT_QVM;
  if (image.guest_count != 1 || checkpoint == NULL || checkpoint->kind != want)
    qa_range_error("%s save requires exactly one complete guest checkpoint", name);
  if (execution->kind == QA_EXECUTION_MODULE_QVM) {
    double want_version = (!checkpoint->has_abi_profile || is(checkpoint->abi_profile, "q3-modern")) ? 8 : 7;
    if (execution->api.kind != QA_GAME_API_Q3 || execution->api.as.q3.kind != QA_Q3_API_QAGAME || execution->api.as.q3.version != want_version)
      qa_range_error("QVM save requires matching selected qagame API and ABI profile");
  }
  const qa_resolved_resource_reference *artifact = execution->artifact;
  char revision[1024];
  qa_module_identity expected;
  expected.id = execution->owner.provider;
  expected.artifact_path = artifact->requested_path;
  expected.digest = artifact->digest;
  if (execution->kind == QA_EXECUTION_MODULE_QUAKEC) expected.revision = artifact->digest;
  else {
    qa_mount_identity mount = artifact->provenance.archive_mount ? artifact->provenance.archive_mount->identity
        : artifact->provenance.loose_mount ? artifact->provenance.loose_mount->identity : (qa_mount_identity){0};
    snprintf(revision, sizeof revision, "%s:%lld", mount.id ? mount.id : "", (long long)mount.generation);
    expected.revision = revision;
  }
  qa_game_api_identity api; memset(&api, 0, sizeof api);
  if (checkpoint->kind == QA_GUEST_CHECKPOINT_QUAKEC) { api.kind = QA_GAME_API_QUAKEC; api.as.quakec = checkpoint->quakec_api; }
  else { api.kind = QA_GAME_API_Q3; api.as.q3 = checkpoint->q3_api; }
  if (!is(execution->owner.provider, image.recipe.map.entities.provider)
      || !same_module(checkpoint->module, expected) || !same_module(checkpoint->host_state.module, expected)
      || !same_api(api, execution->api))
    qa_range_error("%s checkpoint differs from the selected artifact and API", name);
  return checkpoint;
}
qa_app_sim_native_saved_client *qa_app_sim_native_q2_saved_clients(qa_save_image image, size_t *out_count, qa_save_value **keep_alive) {
  *out_count = 0; *keep_alive = NULL;
  bool any = has_schema(image, "q2:classic-native-original") || has_schema(image, "q2:rerelease-native-original");
  if (!any) {
    /* TS still calls the decoders (they validate execution match). */
    qa_q2_classic_original_save tmp; qa_app_sim_rerelease_native_save_view tmp2;
    bool a = qa_app_sim_native_q2_original_save(image, NULL, &tmp);
    if (a) qa_q2_classic_original_save_free(&tmp);
    bool b = qa_app_sim_native_q2_rerelease_save(image, NULL, &tmp2);
    if (b) qa_save_value_free(tmp2.value);
    if (!a && !b) return NULL;
  }
  qa_save_reader reader = qa_app_sim_simulation_save_reader(image, keep_alive);
  qa_save_reader clients = fld(&reader, "nativeClients");
  size_t count = qa_save_reader_list_count(&clients);
  qa_app_sim_native_saved_client *out = count ? malloc(count * sizeof *out) : NULL;
  static const char *const phases[] = {"connected", "active"};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, f;
    qa_save_reader_item(&clients, i, &entry);
    f = fld(&entry, "clientSlot"); out[i].client_slot = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
    f = fld(&entry, "phase"); out[i].phase = qa_save_reader_choice_string(&f, phases, 2); qa_save_reader_destroy(&f);
    f = fld(&entry, "userinfo"); out[i].userinfo = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&entry);
    for (size_t j = 0; j < i; j++) if (out[j].client_slot == out[i].client_slot) qa_range_error("Duplicate native saved client slot");
  }
  qa_save_reader_destroy(&clients); qa_save_reader_destroy(&reader);
  *out_count = count;
  return out;
}
const qa_guest_checkpoint *qa_app_sim_simulation_quakec_checkpoint(qa_save_image image) {
  const qa_guest_checkpoint *c = qa_app_sim_simulation_guest_checkpoint(image);
  return c && c->kind == QA_GUEST_CHECKPOINT_QUAKEC ? c : NULL;
}
const qa_guest_checkpoint *qa_app_sim_simulation_qvm_checkpoint(qa_save_image image) {
  const qa_guest_checkpoint *c = qa_app_sim_simulation_guest_checkpoint(image);
  return c && c->kind == QA_GUEST_CHECKPOINT_QVM ? c : NULL;
}
void qa_app_sim_validate_simulation_save(qa_save_image image) {
  for (size_t i = 0; i < image.provider_count; i++) {
    for (size_t j = 0; j < i; j++)
      if (is(image.providers[j].provider, image.providers[i].provider) && is(image.providers[j].schema, image.providers[i].schema))
        qa_range_error("Duplicate saved provider %s", image.providers[i].schema);
    qa_validate_save_provider_owner(image.providers[i], image.recipe.map.entities.provider);
  }
  qa_app_sim_simulation_provider_checkpoint(image, "world:simulation");
  qa_app_sim_simulation_provider_checkpoint(image, "world:source-slots");
  const qa_guest_checkpoint *guest = qa_app_sim_simulation_guest_checkpoint(image);
  bool native = has_schema(image, "q2:classic-native-original") || has_schema(image, "q2:rerelease-native-original");
  if (native) {
    /* decode to validate execution match */
    qa_q2_classic_original_save a; qa_app_sim_rerelease_native_save_view b;
    if (has_schema(image, "q2:classic-native-original")) { qa_app_sim_native_q2_original_save(image, NULL, &a); qa_q2_classic_original_save_free(&a); }
    else { qa_app_sim_native_q2_rerelease_save(image, NULL, &b); qa_save_value_free(b.value); }
  }
  qa_save_value *keep = NULL;
  size_t np = (native || (guest && guest->kind == QA_GUEST_CHECKPOINT_QVM)) ? players_count(image, &keep) : 0;
  if (native && np != 0) qa_range_error("Native Q2 players must remain source-owned");
  if (guest && guest->kind == QA_GUEST_CHECKPOINT_QVM && np != 0)
    qa_range_error("QVM players must remain owned by the saved guest client records");
  qa_save_value_free(keep);
  const qa_execution_module *execution = server_execution(image);
  qa_app_sim_saved_bots *bots = qa_app_sim_saved_bot_checkpoint(image);
  if (bots) {
    if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_TYPESCRIPT)
      qa_range_error("Saved bot services have no supported source owner");
    if (!api_is(execution->api, "q3-qagame")) qa_app_sim_simulation_provider_checkpoint(image, "world:source-cvars");
    qa_save_value_free(bots->value); free(bots->connections); free(bots);
  }
  if (has_schema(image, "world:source-cvars")) {
    if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_TYPESCRIPT
        || (!api_is(execution->api, "q1-netquake") && !api_is(execution->api, "q1-quakeworld")
            && !api_is(execution->api, "q2-classic-game") && !api_is(execution->api, "q2-rerelease-game")))
      qa_range_error("Saved common cvars have no matching source owner");
    qa_app_sim_simulation_provider_checkpoint(image, "world:source-cvars");
  }
  if (guest == NULL && execution && execution->kind == QA_EXECUTION_MODULE_TYPESCRIPT) {
    if (api_is(execution->api, "q1-netquake") || api_is(execution->api, "q1-quakeworld"))
      qa_app_sim_simulation_provider_checkpoint(image, "q1:foundation");
    else if (api_is(execution->api, "q2-classic-game") || api_is(execution->api, "q2-rerelease-game")) {
      static const char *const schemas[] = {"q2:composition", "q2:foundation", "q2:items", "q2:movers", "q2:monsters", "q2:weapons", "q2:players", "q2:base-entities"};
      for (size_t i = 0; i < 8; i++) qa_app_sim_simulation_provider_checkpoint(image, schemas[i]);
    } else if (api_is(execution->api, "q3-qagame")) {
      qa_app_sim_simulation_provider_checkpoint(image, "q3:native");
      qa_app_sim_simulation_provider_checkpoint(image, "world:q3-runtime");
    }
  }
  size_t clocks = 0; const qa_save_clock *clock = NULL;
  for (size_t i = 0; i < image.clock_count; i++)
    if (is(image.clocks[i].provider, image.recipe.map.entities.provider)) { clock = &image.clocks[i]; clocks++; }
  if (clocks != 1 || clock->time.kind != image.frame.time.kind || clock->time.value != image.frame.time.value)
    qa_range_error("Saved source clock disagrees with its frame");
  size_t randoms = 0;
  for (size_t i = 0; i < image.random_count; i++)
    if (is(image.random[i].provider, image.recipe.map.entities.provider)) randoms++;
  if (randoms != 1) qa_range_error("Save requires one matching source random stream");
  /* duplicate actor state across bodies/combat/inventories/configurations/thinks */
  for (size_t i = 0; i < image.body_count; i++) for (size_t j = 0; j < i; j++)
    if (image.bodies[i].actor.slot == image.bodies[j].actor.slot && image.bodies[i].actor.generation == image.bodies[j].actor.generation) qa_range_error("Duplicate saved actor state");
  for (size_t i = 0; i < image.combat_count; i++) for (size_t j = 0; j < i; j++)
    if (image.combat[i].actor.slot == image.combat[j].actor.slot && image.combat[i].actor.generation == image.combat[j].actor.generation) qa_range_error("Duplicate saved actor state");
  for (size_t i = 0; i < image.inventory_count; i++) for (size_t j = 0; j < i; j++)
    if (image.inventories[i].actor.slot == image.inventories[j].actor.slot && image.inventories[i].actor.generation == image.inventories[j].actor.generation) qa_range_error("Duplicate saved actor state");
  for (size_t i = 0; i < image.configuration_count; i++) for (size_t j = 0; j < i; j++)
    if (image.configurations[i].actor.slot == image.configurations[j].actor.slot && image.configurations[i].actor.generation == image.configurations[j].actor.generation) qa_range_error("Duplicate saved actor state");
  for (size_t i = 0; i < image.think_count; i++) for (size_t j = 0; j < i; j++)
    if (image.thinks[i].actor.slot == image.thinks[j].actor.slot && image.thinks[i].actor.generation == image.thinks[j].actor.generation) qa_range_error("Duplicate saved actor state");
}
qa_app_sim_native_q3_runtime qa_app_sim_native_q3_runtime_reader(qa_save_image image) {
  qa_provider_checkpoint record = qa_app_sim_simulation_provider_checkpoint(image, "world:q3-runtime");
  qa_app_sim_native_q3_runtime out; memset(&out, 0, sizeof out);
  out.value = qa_decode_checkpoint_value(record.bytes.bytes, record.bytes.length);
  qa_save_reader reader; qa_save_reader_init(&reader, out.value, "world:q3-runtime");
  qa_save_reader arsenals = fld(&reader, "arsenals");
  out.arsenal_count = qa_save_reader_list_count(&arsenals);
  out.arsenals = out.arsenal_count ? calloc(out.arsenal_count, sizeof *out.arsenals) : NULL;
  static const char *const products[] = {"baseq3", "missionpack"};
  static const char *const slots[] = {"active", "holster-requested", "dropping", "holstered", "resume-requested"};
  for (size_t i = 0; i < out.arsenal_count; i++) {
    qa_save_reader entry, s, f;
    qa_save_reader_item(&arsenals, i, &entry);
    f = fld(&entry, "actor"); out.arsenals[i].actor = qa_read_saved_actor(&f); qa_save_reader_destroy(&f);
    s = fld(&entry, "state");
    qa_q3_arsenal_runtime_state *st = &out.arsenals[i].state;
    f = fld(&s, "product"); st->product = qa_save_reader_choice_string(&f, products, 2); qa_save_reader_destroy(&f);
    f = fld(&s, "maxHealth"); st->max_health = qa_save_reader_number(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "spectator"); st->spectator = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "persistentPowerupTag"); st->persistent_powerup_tag = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "holdableItem"); st->holdable_item = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "holdableTag"); st->holdable_tag = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "respawned"); st->respawned = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "useItemHeld"); st->use_item_held = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "eventSequence"); st->event_sequence = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "fractionalMilliseconds"); st->fractional_milliseconds = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
    f = fld(&s, "externalSlot"); const char *slot = qa_save_reader_choice_string(&f, slots, 5); qa_save_reader_destroy(&f);
    st->external_slot = is(slot, "active") ? QA_Q3_WEAPON_SLOT_ACTIVE : is(slot, "holster-requested") ? QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED
        : is(slot, "dropping") ? QA_Q3_WEAPON_SLOT_DROPPING : is(slot, "holstered") ? QA_Q3_WEAPON_SLOT_HOLSTERED : QA_Q3_WEAPON_SLOT_RESUME_REQUESTED;
    f = fld(&s, "requestedWeapon");
    if (!qa_save_reader_is_null(&f)) { out.arsenals[i].has_requested_weapon = true; out.arsenals[i].requested_weapon = qa_save_reader_integer(&f); st->has_requested_weapon = true; st->requested_weapon = out.arsenals[i].requested_weapon; }
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&s); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&arsenals);
  qa_save_reader movement = fld(&reader, "movement");
  out.movement_count = qa_save_reader_list_count(&movement);
  out.movement = out.movement_count ? calloc(out.movement_count, sizeof *out.movement) : NULL;
  for (size_t i = 0; i < out.movement_count; i++) {
    qa_save_reader entry, s, f;
    qa_save_reader_item(&movement, i, &entry);
    f = fld(&entry, "actor"); out.movement[i].actor = qa_read_saved_actor(&f); qa_save_reader_destroy(&f);
    s = fld(&entry, "state");
    if (!qa_save_reader_is_null(&s)) {
      out.movement[i].has_state = true;
      f = fld(&s, "traceMask"); out.movement[i].trace_mask = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
      f = fld(&s, "fixedMsec");
      if (!qa_save_reader_is_null(&f)) { out.movement[i].has_fixed_msec = true; out.movement[i].fixed_msec = qa_save_reader_integer(&f); }
      qa_save_reader_destroy(&f);
      f = fld(&s, "noFootsteps"); out.movement[i].no_footsteps = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
      f = fld(&s, "gauntletHit"); out.movement[i].gauntlet_hit = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
      f = fld(&s, "debugLevel"); out.movement[i].debug_level = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    }
    qa_save_reader_destroy(&s); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&movement);
  qa_save_reader attacks = fld(&reader, "lastAttacks");
  out.last_attack_count = qa_save_reader_list_count(&attacks);
  out.last_attacks = out.last_attack_count ? calloc(out.last_attack_count, sizeof *out.last_attacks) : NULL;
  for (size_t i = 0; i < out.last_attack_count; i++) {
    qa_save_reader entry, f;
    qa_save_reader_item(&attacks, i, &entry);
    f = fld(&entry, "actor"); out.last_attacks[i].actor = qa_read_saved_actor(&f); qa_save_reader_destroy(&f);
    f = fld(&entry, "attack"); out.last_attacks[i].attack = qa_read_q2_attack_checkpoint(&f); qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&attacks);
  qa_save_reader_destroy(&reader);
  return out;
}
void qa_app_sim_native_q3_runtime_free(qa_app_sim_native_q3_runtime *runtime) {
  qa_save_value_free(runtime->value); free(runtime->arsenals); free(runtime->movement); free(runtime->last_attacks);
  memset(runtime, 0, sizeof *runtime);
}
qa_app_sim_saved_settings qa_app_sim_saved_simulation_settings(qa_save_image image) {
  qa_save_value *keep = NULL;
  qa_save_reader reader = qa_app_sim_simulation_save_reader(image, &keep);
  qa_save_reader settings = fld(&reader, "settings"), f;
  qa_app_sim_saved_settings out; memset(&out, 0, sizeof out);
  out.value = keep;
  static const double skills[] = {0, 1, 2, 3};
  static const char *const modes[] = {"singleplayer", "coop", "deathmatch"};
  f = fld(&settings, "skill"); out.skill = qa_save_reader_choice_number(&f, skills, 4); qa_save_reader_destroy(&f);
  f = fld(&settings, "mode"); out.mode = qa_save_reader_choice_string(&f, modes, 3); qa_save_reader_destroy(&f);
  f = fld(&settings, "maxClients"); out.max_clients = qa_save_reader_integer_min(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&settings, "seed"); out.seed = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  f = fld(&settings, "initialSpawnPoint"); out.initial_spawn_point = qa_save_reader_is_undefined(&f) ? "" : qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&settings, "startItems"); out.start_items = qa_save_reader_is_undefined(&f) ? "" : qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&reader, "hostMilliseconds"); out.host_milliseconds = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
  bool native = has_schema(image, "q2:classic-native-original") || has_schema(image, "q2:rerelease-native-original");
  if (native) {
    qa_save_value *clients_keep = NULL; size_t count = 0;
    qa_app_sim_native_saved_client *clients = qa_app_sim_native_q2_saved_clients(image, &count, &clients_keep);
    out.client_slots = count ? malloc(count * sizeof(double)) : NULL;
    for (size_t i = 0; i < count; i++) out.client_slots[i] = clients[i].client_slot;
    out.client_slot_count = count;
    free(clients); qa_save_value_free(clients_keep);
  } else {
    /* guest === null path reads players; QVM guest clients come from
     * the sibling q3 guest-runtime slice (no C export yet): fall back
     * to players list which is empty for QVM saves. */
    qa_save_reader players = fld(&reader, "players");
    size_t count = qa_save_reader_list_count(&players);
    out.client_slots = count ? malloc(count * sizeof(double)) : NULL;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry, sf;
      qa_save_reader_item(&players, i, &entry);
      sf = fld(&entry, "clientSlot"); out.client_slots[i] = qa_save_reader_integer_min(&sf, 0);
      qa_save_reader_destroy(&sf); qa_save_reader_destroy(&entry);
    }
    out.client_slot_count = count;
    qa_save_reader_destroy(&players);
  }
  qa_save_reader_destroy(&settings); qa_save_reader_destroy(&reader);
  return out;
}

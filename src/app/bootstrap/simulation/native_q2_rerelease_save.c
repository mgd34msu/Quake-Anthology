/* Port of src/app/bootstrap/simulation/native-q2-rerelease-save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

typedef qa_save_reader R;
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static const R *F(const R *reader, const char *name, R *out) { if (!qa_save_reader_field(reader, name, out)) qa_range_error("Out of memory"); return out; }
static void *xcalloc(size_t count, size_t size) { void *p = calloc(count == 0 ? 1 : count, size); if (p == NULL) qa_range_error("Out of memory"); return p; }
static double bounded_integer(const R *reader, double maximum) {
  double value = qa_save_reader_integer_min(reader, 0); if (value > maximum) fail("Native API 2023 value exceeds its public range"); return value;
}
static qa_compat_q2_rerelease_saved_actor read_reference(const R *reader) {
  static const char *const kinds[] = {"native", "shared"}; R k, a, s, g;
  const char *kind = qa_save_reader_choice_string(F(reader, "kind", &k), kinds, 2); qa_save_reader_destroy(&k);
  qa_compat_q2_rerelease_saved_actor out; memset(&out, 0, sizeof out);
  if (strcmp(kind, "shared") == 0) { out.shared = qa_read_saved_actor(F(reader, "actor", &a)); qa_save_reader_destroy(&a); }
  else { out.is_native = true; out.slot = qa_save_reader_integer_min(F(reader, "slot", &s), 0); out.generation = qa_save_reader_integer_min(F(reader, "generation", &g), 0); qa_save_reader_destroy(&s); qa_save_reader_destroy(&g); }
  return out;
}
static qa_compat_q2_rerelease_deferred_damage_save read_damage(const R *reader) {
  qa_compat_q2_rerelease_deferred_damage_save out; memset(&out, 0, sizeof out); R request, t, at, refs, v;
  F(reader, "request", &request);
  out.target = read_reference(F(reader, "target", &t)); qa_save_reader_destroy(&t);
  out.attack = qa_read_q2_attack_checkpoint(F(reader, "attack", &at)); qa_save_reader_destroy(&at);
  F(reader, "references", &refs); out.reference_count = qa_save_reader_list_count(&refs); out.references = xcalloc(out.reference_count, sizeof *out.references);
  for (size_t i = 0; i < out.reference_count; i++) { R item, actor, reference; qa_save_reader_item(&refs, i, &item);
    out.references[i].actor = qa_read_saved_actor(F(&item, "actor", &actor)); out.references[i].reference = read_reference(F(&item, "reference", &reference));
    qa_save_reader_destroy(&actor); qa_save_reader_destroy(&reference); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&refs);
  out.amount = qa_save_reader_finite(F(&request, "amount", &v)); qa_save_reader_destroy(&v);
  out.knockback = qa_save_reader_finite(F(&request, "knockback", &v)); qa_save_reader_destroy(&v);
  out.direction = qa_read_vector(F(&request, "direction", &v)); qa_save_reader_destroy(&v);
  out.point = qa_read_vector(F(&request, "point", &v)); qa_save_reader_destroy(&v);
  out.normal = qa_read_vector(F(&request, "normal", &v)); qa_save_reader_destroy(&v);
  static const char *const deliveries[] = {"direct", "radius"};
  out.delivery = qa_save_reader_choice_string(F(&request, "delivery", &v), deliveries, 2); qa_save_reader_destroy(&v); qa_save_reader_destroy(&request);
  out.blood = qa_save_reader_finite(F(reader, "blood", &v)); qa_save_reader_destroy(&v);
  out.save_knockback = qa_save_reader_finite(F(reader, "knockback", &v)); qa_save_reader_destroy(&v);
  out.save_point = qa_read_vector(F(reader, "point", &v)); qa_save_reader_destroy(&v);
  R mod; F(reader, "mod", &mod); size_t count = qa_save_reader_list_count(&mod); out.mod_count = count;
  for (size_t i = 0; i < count; i++) { R item; qa_save_reader_item(&mod, i, &item); double value = bounded_integer(&item, 255); if (i < 3) out.mod[i] = value; qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&mod);
  out.attacker_slot = qa_save_reader_integer_min(F(reader, "attackerSlot", &v), 0); qa_save_reader_destroy(&v);
  out.inflictor_slot = qa_save_reader_integer_min(F(reader, "inflictorSlot", &v), 0); qa_save_reader_destroy(&v);
  return out;
}
qa_compat_q2_rerelease_source_save qa_app_sim_read_rerelease_source_save(const R *reader) {
  qa_compat_q2_rerelease_source_save out; memset(&out, 0, sizeof out); R v;
  size_t length = 0; const uint8_t *native = qa_save_reader_bytes(F(reader, "native", &v), &length); qa_save_reader_destroy(&v);
  out.native = (uint8_t *)native; out.native_length = length;
  R damage; F(reader, "deferredDamage", &damage); out.deferred_damage_count = qa_save_reader_list_count(&damage);
  qa_compat_q2_rerelease_deferred_damage_save *damages = xcalloc(out.deferred_damage_count, sizeof *damages);
  for (size_t i = 0; i < out.deferred_damage_count; i++) { R item; qa_save_reader_item(&damage, i, &item); damages[i] = read_damage(&item); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&damage); out.deferred_damage = damages;
  R projections; F(reader, "projections", &projections); out.projection_count = qa_save_reader_list_count(&projections);
  qa_compat_q2_rerelease_projection_save *rows = xcalloc(out.projection_count, sizeof *rows);
  for (size_t i = 0; i < out.projection_count; i++) { R item, slot, actor; qa_save_reader_item(&projections, i, &item);
    rows[i].slot = qa_save_reader_integer_min(F(&item, "slot", &slot), 0); rows[i].actor = qa_read_saved_actor(F(&item, "actor", &actor));
    qa_save_reader_destroy(&slot); qa_save_reader_destroy(&actor); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&projections); out.projections = rows;
  if (length == 0 || memchr(native, 0, length) != NULL) fail("Native API 2023 save requires unterminated JSON bytes");
  qa_save_value *parsed = qa_parse_json_value(native, length, false);
  bool object = parsed != NULL && parsed->kind == QA_SAVE_OBJECT; qa_save_value_free(parsed);
  if (!object) fail("Native API 2023 save must contain a JSON object");
  for (size_t i = 0; i < out.projection_count; i++) for (size_t j = 0; j < i; j++)
    if (rows[i].slot == rows[j].slot || (rows[i].actor.slot == rows[j].actor.slot && rows[i].actor.generation == rows[j].actor.generation)) fail("Duplicate native API 2023 projection");
  for (size_t i = 0; i < out.deferred_damage_count; i++) if (damages[i].mod_count != 3) fail("Invalid native API 2023 deferred damage");
  return out;
}
void qa_app_sim_rerelease_source_save_free(qa_compat_q2_rerelease_source_save *save) {
  qa_compat_q2_rerelease_deferred_damage_save *damages = (qa_compat_q2_rerelease_deferred_damage_save *)save->deferred_damage;
  for (size_t i = 0; i < save->deferred_damage_count; i++) free(damages[i].references);
  free(damages); free((void *)save->projections); save->deferred_damage = NULL; save->projections = NULL;
}
static qa_app_sim_q2_rerelease_level_state read_level(const R *reader) {
  qa_app_sim_q2_rerelease_level_state out; memset(&out, 0, sizeof out); R list;
  F(reader, "configstrings", &list); out.configstring_count = qa_save_reader_list_count(&list); out.configstrings = xcalloc(out.configstring_count, sizeof *out.configstrings);
  for (size_t i = 0; i < out.configstring_count; i++) { R item, a, b; qa_save_reader_item(&list, i, &item);
    out.configstrings[i].index = bounded_integer(F(&item, "index", &a), 12447); out.configstrings[i].value = qa_save_reader_string(F(&item, "value", &b));
    qa_save_reader_destroy(&a); qa_save_reader_destroy(&b); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&list);
  F(reader, "portals", &list); out.portal_count = qa_save_reader_list_count(&list); out.portals = xcalloc(out.portal_count, sizeof *out.portals);
  for (size_t i = 0; i < out.portal_count; i++) { R item, a, b; qa_save_reader_item(&list, i, &item);
    out.portals[i].portal = qa_save_reader_integer_min(F(&item, "portal", &a), 0); out.portals[i].open = qa_save_reader_boolean(F(&item, "open", &b));
    qa_save_reader_destroy(&a); qa_save_reader_destroy(&b); qa_save_reader_destroy(&item); }
  qa_save_reader_destroy(&list);
  for (size_t i = 0; i < out.configstring_count; i++) for (size_t j = 0; j < i; j++) if (out.configstrings[i].index == out.configstrings[j].index) fail("Duplicate native API 2023 level metadata");
  for (size_t i = 0; i < out.portal_count; i++) for (size_t j = 0; j < i; j++) if (out.portals[i].portal == out.portals[j].portal) fail("Duplicate native API 2023 level metadata");
  return out;
}
static const char *map_path(const R *reader) {
  const char *path = qa_save_reader_string(reader); size_t length = strlen(path);
  bool bad = strncmp(path, "maps/", 5) != 0 || length < 4 || strcmp(path + length - 4, ".bsp") != 0 || length <= 9;
  for (size_t i = 0; i < length && !bad; i++) { unsigned char c = (unsigned char)path[i]; bad = c <= 0x1f || c == 0x7f || c == '\\' || c == ':'; }
  for (size_t start = 0; !bad && start <= length;) {
    const char *slash = strchr(path + start, '/'); size_t end = slash == NULL ? length : (size_t)(slash - path), n = end - start;
    bad = n == 0 || (n == 1 && path[start] == '.') || (n == 2 && path[start] == '.' && path[start + 1] == '.');
    start = end + 1; if (slash == NULL) break;
  }
  if (bad) fail("Invalid native API 2023 map path");
  return path;
}
static bool same_text(const char *a, const char *b) { return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0); }
static void free_level(qa_app_sim_q2_rerelease_level_state *state) { free(state->configstrings); free(state->portals); }
void qa_app_sim_q2_rerelease_native_save_free(qa_app_sim_q2_rerelease_native_save *save) {
  free_level(&save->server); qa_app_sim_rerelease_source_save_free(&save->game); qa_app_sim_rerelease_source_save_free(&save->level);
  for (size_t i = 0; i < save->visited_level_count; i++) { free_level(&save->visited_levels[i].state); qa_app_sim_rerelease_source_save_free(&save->visited_levels[i].level); }
  free(save->visited_levels); qa_save_value_free(save->root); memset(save, 0, sizeof *save);
}
void qa_app_sim_decode_q2_rerelease_native_save(const qa_provider_checkpoint *record, qa_app_sim_q2_rerelease_native_identity expected, qa_app_sim_q2_rerelease_native_save *out) {
  memset(out, 0, sizeof *out);
  if (!same_text(record->provider, expected.module.id) || !same_text(record->schema, "q2:rerelease-native-original") || record->version != 1)
    fail("Unsupported rerelease native source save provider");
  out->root = qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length);
  R reader, api, v; if (!qa_save_reader_init(&reader, out->root, "q2.rerelease-native-original")) qa_range_error("Out of memory");
  F(&reader, "api", &api); (void)qa_save_reader_literal_string(F(&api, "kind", &v), "q2-rerelease-game"); qa_save_reader_destroy(&v);
  (void)qa_save_reader_literal_number(F(&api, "version", &v), 2023); qa_save_reader_destroy(&v); qa_save_reader_destroy(&api);
  (void)qa_save_reader_literal_string(F(&reader, "abi", &v), "windows-x86-64"); qa_save_reader_destroy(&v);
  out->module = qa_read_module(F(&reader, "module", &v)); qa_save_reader_destroy(&v);
  out->map = map_path(F(&reader, "map", &v)); qa_save_reader_destroy(&v);
  if (!same_text(out->module.id, expected.module.id) || !same_text(out->module.artifact_path, expected.module.artifact_path) || !same_text(out->module.digest, expected.module.digest)
      || !same_text(out->module.revision, expected.module.revision) || !same_text(out->map, expected.map)) fail("Rerelease native save differs from the selected module, ABI or map");
  R state; F(&reader, "server", &state); size_t cvar_length = 0; const uint8_t *cvars = qa_save_reader_bytes(F(&state, "cvars", &v), &cvar_length); qa_save_reader_destroy(&v);
  out->server_cvars.bytes = cvars; out->server_cvars.length = cvar_length;
  qa_save_value_free(qa_decode_checkpoint_value(cvars, cvar_length));
  R visited; F(&reader, "visitedLevels", &visited); out->visited_level_count = qa_save_reader_list_count(&visited); out->visited_levels = xcalloc(out->visited_level_count, sizeof *out->visited_levels);
  for (size_t i = 0; i < out->visited_level_count; i++) {
    R item, a; qa_save_reader_item(&visited, i, &item); (void)qa_save_reader_literal_number(F(&item, "version", &a), 1); qa_save_reader_destroy(&a);
    qa_app_sim_q2_rerelease_visited_level *level = &out->visited_levels[i]; level->version = 1; level->state = read_level(&item);
    level->map = map_path(F(&item, "map", &a)); qa_save_reader_destroy(&a);
    level->level = qa_app_sim_read_rerelease_source_save(F(&item, "level", &a)); qa_save_reader_destroy(&a); qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&visited);
  for (size_t i = 0; i < out->visited_level_count; i++) {
    if (strcmp(out->visited_levels[i].map, out->map) == 0) fail("Duplicate native API 2023 visited map");
    for (size_t j = 0; j < i; j++) if (strcmp(out->visited_levels[i].map, out->visited_levels[j].map) == 0) fail("Duplicate native API 2023 visited map");
  }
  out->game = qa_app_sim_read_rerelease_source_save(F(&reader, "game", &v)); qa_save_reader_destroy(&v);
  if (out->game.deferred_damage_count != 0 || out->game.projection_count != 0) fail("Native game save cannot own level projections");
  out->autosave = qa_save_reader_boolean(F(&reader, "autosave", &v)); qa_save_reader_destroy(&v);
  out->server = read_level(&state); qa_save_reader_destroy(&state);
  out->level = qa_app_sim_read_rerelease_source_save(F(&reader, "level", &v)); qa_save_reader_destroy(&v);
  qa_save_reader_destroy(&reader);
}

static qa_save_value *checked(qa_save_value *value) { if (value == NULL) qa_range_error("Out of memory"); return value; }
static void put(qa_save_value *object, const char *key, qa_save_value *value) { if (!qa_save_value_set(object, key, checked(value))) qa_range_error("Out of memory"); }
static void push(qa_save_value *array, qa_save_value *value) { if (!qa_save_value_push(array, checked(value))) qa_range_error("Out of memory"); }
static qa_save_value *vector_value(qa_vec3 v) { qa_save_value *o = checked(qa_save_value_object()); put(o, "x", qa_save_value_number(v.x)); put(o, "y", qa_save_value_number(v.y)); put(o, "z", qa_save_value_number(v.z)); return o; }
static qa_save_value *saved_actor_value(qa_saved_actor_id a) { qa_save_value *o = checked(qa_save_value_object()); put(o, "slot", qa_save_value_number((double)a.slot)); put(o, "generation", qa_save_value_number((double)a.generation)); return o; }
static qa_save_value *reference_value(qa_compat_q2_rerelease_saved_actor r) {
  qa_save_value *o = checked(qa_save_value_object());
  if (r.is_native) { put(o, "kind", qa_save_value_string("native")); put(o, "slot", qa_save_value_number(r.slot)); put(o, "generation", qa_save_value_number(r.generation)); }
  else { put(o, "kind", qa_save_value_string("shared")); put(o, "actor", saved_actor_value(r.shared)); }
  return o;
}
static qa_save_value *source_save_value(const qa_compat_q2_rerelease_source_save *save) {
  qa_save_value *o = checked(qa_save_value_object()); put(o, "native", qa_save_value_bytes(save->native, save->native_length));
  qa_save_value *damage = checked(qa_save_value_array());
  for (size_t i = 0; i < save->deferred_damage_count; i++) {
    const qa_compat_q2_rerelease_deferred_damage_save *d = &save->deferred_damage[i]; qa_save_value *row = checked(qa_save_value_object());
    put(row, "target", reference_value(d->target)); put(row, "attack", qa_q2_attack_checkpoint_value(&d->attack));
    qa_save_value *refs = checked(qa_save_value_array());
    for (size_t r = 0; r < d->reference_count; r++) { qa_save_value *ref = checked(qa_save_value_object()); put(ref, "actor", saved_actor_value(d->references[r].actor)); put(ref, "reference", reference_value(d->references[r].reference)); push(refs, ref); }
    put(row, "references", refs);
    qa_save_value *request = checked(qa_save_value_object());
    put(request, "amount", qa_save_value_number(d->amount)); put(request, "knockback", qa_save_value_number(d->knockback)); put(request, "direction", vector_value(d->direction));
    put(request, "point", vector_value(d->point)); put(request, "normal", vector_value(d->normal)); put(request, "delivery", qa_save_value_string(d->delivery));
    put(row, "request", request); put(row, "blood", qa_save_value_number(d->blood)); put(row, "knockback", qa_save_value_number(d->save_knockback)); put(row, "point", vector_value(d->save_point));
    qa_save_value *mod = checked(qa_save_value_array()); for (size_t m = 0; m < d->mod_count && m < 3; m++) push(mod, qa_save_value_number(d->mod[m]));
    put(row, "mod", mod); put(row, "attackerSlot", qa_save_value_number(d->attacker_slot)); put(row, "inflictorSlot", qa_save_value_number(d->inflictor_slot));
    push(damage, row);
  }
  put(o, "deferredDamage", damage);
  qa_save_value *projections = checked(qa_save_value_array());
  for (size_t i = 0; i < save->projection_count; i++) { qa_save_value *row = checked(qa_save_value_object()); put(row, "slot", qa_save_value_number(save->projections[i].slot)); put(row, "actor", saved_actor_value(save->projections[i].actor)); push(projections, row); }
  put(o, "projections", projections);
  return o;
}
qa_save_value *qa_app_sim_rerelease_source_save_value(const qa_compat_q2_rerelease_source_save *save) { return source_save_value(save); }
static void level_values(qa_save_value *o, const qa_app_sim_q2_rerelease_level_state *state) {
  qa_save_value *strings = checked(qa_save_value_array());
  for (size_t i = 0; i < state->configstring_count; i++) { qa_save_value *row = checked(qa_save_value_object()); put(row, "index", qa_save_value_number(state->configstrings[i].index)); put(row, "value", qa_save_value_string(state->configstrings[i].value)); push(strings, row); }
  put(o, "configstrings", strings);
  qa_save_value *portals = checked(qa_save_value_array());
  for (size_t i = 0; i < state->portal_count; i++) { qa_save_value *row = checked(qa_save_value_object()); put(row, "portal", qa_save_value_number(state->portals[i].portal)); put(row, "open", qa_save_value_boolean(state->portals[i].open)); push(portals, row); }
  put(o, "portals", portals);
}
void qa_app_sim_encode_q2_rerelease_native_save(const qa_app_sim_q2_rerelease_native_save *save, qa_provider_checkpoint *out) {
  qa_save_value *root = checked(qa_save_value_object());
  qa_save_value *module = checked(qa_save_value_object());
  put(module, "id", qa_save_value_string(save->module.id)); put(module, "artifactPath", qa_save_value_string(save->module.artifact_path));
  put(module, "digest", qa_save_value_string(save->module.digest)); put(module, "revision", qa_save_value_string(save->module.revision));
  put(root, "module", module); put(root, "map", qa_save_value_string(save->map));
  qa_save_value *api = checked(qa_save_value_object()); put(api, "kind", qa_save_value_string("q2-rerelease-game")); put(api, "version", qa_save_value_number(2023));
  put(root, "api", api); put(root, "abi", qa_save_value_string("windows-x86-64")); put(root, "autosave", qa_save_value_boolean(save->autosave));
  qa_save_value *server = checked(qa_save_value_object()); put(server, "cvars", qa_save_value_bytes(save->server_cvars.bytes, save->server_cvars.length)); level_values(server, &save->server);
  put(root, "server", server); put(root, "game", source_save_value(&save->game)); put(root, "level", source_save_value(&save->level));
  qa_save_value *visited = checked(qa_save_value_array());
  for (size_t i = 0; i < save->visited_level_count; i++) {
    const qa_app_sim_q2_rerelease_visited_level *level = &save->visited_levels[i]; qa_save_value *row = checked(qa_save_value_object());
    put(row, "version", qa_save_value_number(1)); put(row, "map", qa_save_value_string(level->map)); put(row, "level", source_save_value(&level->level)); level_values(row, &level->state);
    push(visited, row);
  }
  put(root, "visitedLevels", visited);
  size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(root, &length); qa_save_value_free(root);
  if (bytes == NULL) qa_range_error("Out of memory");
  memset(out, 0, sizeof *out); out->provider = save->module.id; out->schema = "q2:rerelease-native-original"; out->version = 1; out->bytes.bytes = bytes; out->bytes.length = length;
  qa_app_sim_q2_rerelease_native_identity identity = {save->module, save->map};
  qa_app_sim_q2_rerelease_native_save check; qa_app_sim_decode_q2_rerelease_native_save(out, identity, &check); qa_app_sim_q2_rerelease_native_save_free(&check);
}

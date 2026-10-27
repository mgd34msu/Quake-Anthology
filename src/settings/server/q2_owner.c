/* Port of src/settings/server/q2-owner.ts. GPL-2.0-or-later. */
#include "qa/settings/server/q2_owner.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/settings/server/lmctf.h"
#include "qa/settings/server/lmctf_cvars.h"
#include "qa/settings/server/q2.h"

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}
static void *xalloc(void *pointer) {
  if (pointer == NULL) qa_error("Settings allocation failed");
  return pointer;
}

static void registry_register(qa_cvar_registry *cvars, const char *name,
                              const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_register(cvars, name, value, flags, &snapshot);
  qa_cvar_read_free(&snapshot);
}

/* TS rereleaseCombatDefaults. */
static const struct {
  const char *name;
  const char *value;
  int flags;
} rerelease_combat_defaults[] = {
    {"g_instant_weapon_switch", "0", QA_Q2_CVAR_FLAG_LATCH},
    {"g_weapon_respawn_time", "30", 0}, {"g_dm_weapons_stay", "0", 0},
    {"g_dm_instant_items", "1", 0}, {"g_dm_same_level", "0", 0},
    {"g_no_mines", "0", 0}, {"g_no_nukes", "0", 0}, {"g_no_spheres", "0", 0},
    {"g_dm_random_items", "0", 0}, {"g_dm_no_quadfire_drop", "0", 0},
    {"g_dm_no_quad_drop", "0", 0}, {"g_dm_no_stack_double", "0", 0},
    {"g_dm_strong_mines", "0", 0},
};
/* TS playerNumbers (cvar name, default, flags; rule keys only matter
 * to the unported binding functions). */
static const struct {
  const char *name;
  const char *value;
  int flags;
} player_numbers[] = {
    {"maxspectators", "4", QA_Q2_CVAR_FLAG_SERVER_INFO},
    {"flood_msgs", "4", 0}, {"flood_persecond", "4", 0},
    {"flood_waitdelay", "10", 0}, {"sv_rollspeed", "200", 0},
    {"sv_rollangle", "2", 0}, {"run_pitch", "0.002", 0},
    {"run_roll", "0.005", 0}, {"bob_up", "0.005", 0},
    {"bob_pitch", "0.002", 0}, {"bob_roll", "0.002", 0},
};
/* TS rereleaseBooleans + rereleaseNumbers (cvar name, default, flags). */
static const struct {
  const char *name;
  const char *value;
  int flags;
} rerelease_options[] = {
    {"g_coop_squad_respawn", "1", QA_Q2_CVAR_FLAG_LATCH},
    {"g_coop_instanced_items", "1", QA_Q2_CVAR_FLAG_LATCH},
    {"g_coop_enable_lives", "0", QA_Q2_CVAR_FLAG_LATCH},
    {"g_coop_player_collision", "0", QA_Q2_CVAR_FLAG_LATCH},
    {"g_dm_force_respawn", "0", 0}, {"g_dm_no_fall_damage", "0", 0},
    {"g_dm_spawn_farthest", "1", 0}, {"g_dm_allow_exit", "0", 0},
    {"g_coop_num_lives", "2", QA_Q2_CVAR_FLAG_LATCH},
    {"g_dm_force_respawn_time", "0", 0},
};

double qa_q2_source_deathmatch_flags(qa_cvar_registry *cvars) {
  double truncated = trunc(qa_cvar_registry_variable_value(cvars, "dmflags"));
  if (qa_cvar_registry_dialect(cvars) != QA_COMMAND_DIALECT_Q2_RERELEASE)
    return truncated;
  /* The TS bitwise folds coerce through int32 (JS ToInt32). */
  double modulo = fmod(fabs(truncated), 4294967296.0);
  if (truncated < 0) modulo = -modulo;
  if (modulo >= 2147483648.0) modulo -= 4294967296.0;
  if (modulo < -2147483648.0) modulo += 4294967296.0;
  int32_t flags = (int32_t)modulo;
  static const struct {
    const char *name;
    int32_t mask;
    bool inverted;
  } folds[] = {
      {"g_dm_weapons_stay", 4, false}, {"g_dm_instant_items", 16, false},
      {"g_dm_same_level", 32, false}, {"g_dm_no_quad_drop", 16384, true},
  };
  for (size_t i = 0; i < sizeof folds / sizeof *folds; i++) {
    bool enabled =
        (qa_cvar_registry_variable_value(cvars, folds[i].name) != 0) != folds[i].inverted;
    flags = enabled ? flags | folds[i].mask : flags & ~folds[i].mask;
  }
  return (double)flags;
}

static qa_cvar_registry *validated_registry(qa_cvar_registry *cvars,
                                            const qa_save_value *value) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = qa_cvar_registry_dialect(cvars);
  options.context = qa_cvar_registry_context(cvars);
  qa_cvar_registry *validated =
      (qa_cvar_registry *)xalloc(qa_cvar_registry_create(&options));
  qa_cvar_registry_restore_save_state(validated, value);
  return validated;
}

bool qa_saved_q2_server_cvars_have(qa_cvar_registry *cvars,
                                   const qa_save_value *value, const char *name) {
  qa_cvar_registry *validated = validated_registry(cvars, value);
  qa_cvar_read found;
  bool has = qa_cvar_registry_find(validated, name, &found);
  if (has) qa_cvar_read_free(&found);
  qa_cvar_registry_destroy(validated);
  return has;
}

static qa_save_value *clone_value(const qa_save_value *value);
static qa_save_value *clone_value(const qa_save_value *value) {
  if (value == NULL) return NULL;
  qa_save_value *copy = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: copy = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: copy = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: copy = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: copy = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: copy = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: copy = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES:
      copy = qa_save_value_bytes(value->bytes, value->byte_length);
      break;
    case QA_SAVE_ARRAY:
      copy = (qa_save_value *)xalloc(qa_save_value_array());
      for (size_t i = 0; i < value->item_count; i++)
        if (!qa_save_value_push(copy, clone_value(value->items[i])))
          qa_error("Settings allocation failed");
      break;
    case QA_SAVE_OBJECT:
      copy = (qa_save_value *)xalloc(qa_save_value_object());
      for (size_t i = 0; i < value->entry_count; i++)
        if (!qa_save_value_set(copy, value->entries[i].key,
                               clone_value(value->entries[i].value)))
          qa_error("Settings allocation failed");
      break;
  }
  return (qa_save_value *)xalloc(copy);
}

static qa_save_value *object_child(qa_save_value *object, const char *key) {
  for (size_t i = 0; i < object->entry_count; i++)
    if (strcmp(object->entries[i].key, key) == 0) return object->entries[i].value;
  return NULL;
}
static void object_replace(qa_save_value *object, const char *key,
                           qa_save_value *child) {
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) != 0) continue;
    qa_save_value_free(object->entries[i].value);
    object->entries[i].value = child;
    return;
  }
  if (!qa_save_value_set(object, key, child)) qa_error("Settings allocation failed");
}
static void entry_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child))
    qa_error("Settings allocation failed");
}

void qa_restore_q2_server_cvars(qa_cvar_registry *cvars,
                                const qa_save_value *value) {
  qa_cvar_registry *validated = validated_registry(cvars, value);
  qa_save_value *saved =
      (qa_save_value *)xalloc(qa_cvar_registry_capture_save_state(validated));
  qa_cvar_registry_destroy(validated);
  qa_save_value *merged = clone_value(saved);
  qa_save_value *variables = object_child(merged, "variables");
  qa_save_value *order = object_child(merged, "order");
  if (variables == NULL || order == NULL) qa_error("Settings allocation failed");
  size_t snapshot_count = 0;
  qa_cvar_snapshot *snapshots =
      qa_cvar_registry_snapshots(cvars, 0, &snapshot_count);
  qa_save_value *new_order = (qa_save_value *)xalloc(qa_save_value_array());
  for (size_t i = 0; i < snapshot_count; i++) {
    bool present = false;
    for (size_t j = 0; j < order->item_count; j++) {
      const qa_save_value *entry = order->items[j];
      if (entry != NULL && entry->kind == QA_SAVE_STRING && entry->text != NULL &&
          strcmp(entry->text, snapshots[i].name) == 0)
        present = true;
    }
    if (present) continue;
    /* {...snapshot, latchedValue: snapshot.latchedValue ?? null} in
     * the registry's saved-variable shape. */
    qa_save_value *entry = (qa_save_value *)xalloc(qa_save_value_object());
    entry_set(entry, "name", qa_save_value_string(snapshots[i].name));
    entry_set(entry, "value", qa_save_value_string(snapshots[i].value));
    entry_set(entry, "resetValue", qa_save_value_string(snapshots[i].reset_value));
    entry_set(entry, "latchedValue",
              snapshots[i].latched_value != NULL
                  ? qa_save_value_string(snapshots[i].latched_value)
                  : qa_save_value_null());
    entry_set(entry, "flags", qa_save_value_number((double)snapshots[i].flags));
    entry_set(entry, "modified", qa_save_value_boolean(snapshots[i].modified));
    entry_set(entry, "modificationCount",
              qa_save_value_number(snapshots[i].modification_count));
    entry_set(entry, "numericValue",
              qa_save_value_number(snapshots[i].numeric_value));
    entry_set(entry, "integerValue",
              qa_save_value_number(snapshots[i].integer_value));
    if (!qa_save_value_push(variables, entry)) qa_error("Settings allocation failed");
    if (!qa_save_value_push(new_order, qa_save_value_string(snapshots[i].name)))
      qa_error("Settings allocation failed");
  }
  for (size_t j = 0; j < order->item_count; j++)
    if (!qa_save_value_push(new_order, clone_value(order->items[j])))
      qa_error("Settings allocation failed");
  object_replace(merged, "order", new_order);
  for (size_t i = 0; i < snapshot_count; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots);
  qa_cvar_registry_restore_save_state(cvars, merged);
  qa_save_value_free(merged);
  qa_save_value_free(saved);
}

size_t qa_q2_server_setting_collections(
    const char *match, bool q2_combat, bool rerelease,
    qa_server_setting_collection out[6]) {
  size_t count = 0;
  out[count++] = qa_q2_spawn_settings();
  if (q2_combat) out[count++] = qa_q2_combat_settings();
  if (strcmp(match, "q2:lmctf") == 0) {
    out[count++] = qa_lmctf_limit_settings();
    out[count++] = qa_lmctf_rune_settings();
    out[count++] = qa_lmctf_weapon_settings();
  } else {
    out[count++] = qa_q2_limit_settings();
  }
  if (strcmp(match, "q2:ctf") == 0) out[count++] = qa_q2_ctf_capture_settings();
  if (strcmp(match, "q2:deathball") == 0) out[count++] = qa_q2_death_ball_settings();
  if (rerelease) out[count++] = qa_q2_rerelease_settings();
  return count;
}

void qa_register_q2_server_cvars(qa_cvar_registry *cvars, const char *match) {
  bool rerelease =
      qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q2_RERELEASE;
  if (strcmp(match, "q2:deathball") == 0) {
    registry_register(cvars, "dball_team1_skin", "male/ctf_r", 0);
    registry_register(cvars, "dball_team2_skin", "male/ctf_b", 0);
    registry_register(cvars, "goallimit", "0", 0);
  }
  if (rerelease)
    for (size_t i = 0; i < sizeof rerelease_combat_defaults /
                               sizeof *rerelease_combat_defaults;
         i++)
      registry_register(cvars, rerelease_combat_defaults[i].name,
                        rerelease_combat_defaults[i].value,
                        rerelease_combat_defaults[i].flags);
  /* createQ2PlayerRules() defaults: timeLimitMinutes 0, fragLimit 0. */
  registry_register(cvars, "sv_gravity", "800", 0);
  registry_register(cvars, "sv_airaccelerate", "0", 0);
  registry_register(cvars, "dmflags", "0", QA_Q2_CVAR_FLAG_SERVER_INFO);
  registry_register(cvars, "timelimit", "0", QA_Q2_CVAR_FLAG_SERVER_INFO);
  registry_register(cvars, "fraglimit", "0", QA_Q2_CVAR_FLAG_SERVER_INFO);
  for (size_t i = 0; i < sizeof player_numbers / sizeof *player_numbers; i++)
    registry_register(cvars, player_numbers[i].name, player_numbers[i].value,
                      player_numbers[i].flags);
  registry_register(cvars, "password", "", QA_Q2_CVAR_FLAG_USER_INFO);
  registry_register(cvars, "spectator_password", "", QA_Q2_CVAR_FLAG_USER_INFO);
  registry_register(cvars, "needpass", "0", QA_Q2_CVAR_FLAG_SERVER_INFO);
  registry_register(cvars, "cheats", "0",
                    QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_LATCH);
  registry_register(cvars, "gun_x", "0", 0);
  registry_register(cvars, "gun_y", "0", 0);
  registry_register(cvars, "gun_z", "0", 0);
  registry_register(cvars, rerelease ? "g_map_list" : "sv_maplist", "", 0);
  if (rerelease) registry_register(cvars, "g_map_list_shuffle", "0", 0);
  if (rerelease)
    for (size_t i = 0; i < sizeof rerelease_options / sizeof *rerelease_options; i++)
      registry_register(cvars, rerelease_options[i].name,
                        rerelease_options[i].value, rerelease_options[i].flags);
  /* createQ2CtfRules() default: captureLimit 0. */
  if (strcmp(match, "q2:ctf") == 0)
    registry_register(cvars, "capturelimit", "0", QA_Q2_CVAR_FLAG_SERVER_INFO);
  if (strcmp(match, "q2:lmctf") == 0) qa_register_lmctf_console_cvars(cvars);
}

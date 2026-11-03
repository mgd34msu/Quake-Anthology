/*
 * Port of src/bots/behavior/library/weapons.ts (botlib/be_ai_weap.c).
 * C accommodations: init selects defaults (owned BotMemory); the TS constructor
 * arguments go through qa_weapon_ai_bots_a_configure. WeaponInfo/ProjectileInfo
 * are decoded by value from the configuration bytes. Config structs stay
 * allocated until destroy because a retained TS config outlives the owner's
 * reference. destroy releases C bookkeeping and the owned memory only.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_WEAPONS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_WEAPONS_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_MAX_WEAPON_STATES 64
enum {
  QA_PROJECTILE_WINDOW_DAMAGE = 1,
  QA_PROJECTILE_RETURN = 2,
  QA_WEAPON_FIRE_RELEASED_BOTS_A = 1,
  QA_DAMAGE_TYPE_IMPACT = 1,
  QA_DAMAGE_TYPE_RADIAL = 2,
  QA_DAMAGE_TYPE_VISIBLE = 4
};
typedef enum qa_weapon_load_result {
  QA_WEAPON_LOAD_NO_ERROR = 0,
  QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_WEIGHTS = 11,
  QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG = 12
} qa_weapon_load_result;
/* TS ProjectileInfo/WeaponInfo hold JS numbers: the config decode stores the
 * exact float32/int32 cell values, and foreign arsenal knowledge (Q1/Q2) stores
 * computed doubles (109.5 rocket damage, atan spreads) unrounded. The int32
 * members are identities, indices and bit sets. */
typedef struct qa_projectile_info_bots_a {
  char name[80];
  char model[80];
  int32_t flags;
  double gravity;
  double damage;
  double radius;
  int32_t visible_damage, damage_type, health_increase;
  double push, detonation, bounce, bounce_friction, bounce_stop;
} qa_projectile_info_bots_a;
typedef struct qa_weapon_info_bots_a {
  bool valid;
  int32_t number;
  char name[80];
  char model[80];
  int32_t level, weapon_inventory_index, flags;
  char projectile[80];
  int32_t projectile_count;
  double horizontal_spread, vertical_spread, speed, acceleration;
  qa_vec3 recoil, offset, angle_offset;
  double extra_z_velocity;
  int32_t ammo_amount, ammo_inventory_index;
  double activate, reload, spin_up, spin_down;
  qa_projectile_info_bots_a projectile_info;
} qa_weapon_info_bots_a;
typedef enum qa_weapon_severity {
  QA_WEAPON_MESSAGE,
  QA_WEAPON_WARNING,
  QA_WEAPON_ERROR,
  QA_WEAPON_FATAL
} qa_weapon_severity;
/** TS WeaponDiagnostic (owned strings; the owner keeps them). */
typedef struct qa_weapon_diagnostic {
  bool origin_print; /* false: "source" */
  qa_weapon_severity severity;
  char *message;
  qa_ui_script_location location;
} qa_weapon_diagnostic;
typedef struct qa_weapon_config_bots_a qa_weapon_config_bots_a;
typedef struct qa_weapon_pointer_record {
  int64_t pointer;
  bool is_config;
  qa_weight_config_bots_a *config;
  int64_t references;
  qa_bot_memory_allocation allocation;
} qa_weapon_pointer_record;
typedef struct qa_weapon_config_identity {
  qa_weight_config_bots_a *config;
  int64_t pointer;
} qa_weapon_config_identity;
typedef struct qa_weapon_state_bots_a {
  bool used;
  uint64_t serial; /* TS object identity */
  qa_bot_memory_allocation allocation;
  int64_t revision;
} qa_weapon_state_bots_a;
/** TS `number | (() => number)` capacity: fn wins, else value when has_value. */
typedef struct qa_weapon_capacity {
  bool has_value;
  double value;
  double (*fn)(void *ctx);
  void *ctx;
} qa_weapon_capacity;
/** TS WeaponAiOptions; zero/NULL is the TS undefined field. */
typedef struct qa_weapon_ai_options {
  bool (*debug_has_file)(void *ctx); /* TS debug.log.filePointer() !== null */
  int64_t (*debug_file_write)(void *ctx, const char *text);
  void (*debug_flush)(void *ctx);
  void *debug_ctx;
  qa_bot_memory *memory;
  qa_weapon_capacity max_weapon_info;
  qa_weapon_capacity max_projectile_info;
  const qa_ui_script_preprocessor_options *preprocessor;
  void (*report)(void *ctx, const qa_weapon_diagnostic *diagnostic);
  void *report_ctx;
} qa_weapon_ai_options;
typedef struct qa_weapon_ai_bots_a {
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  qa_bot_script_reader resolver;
  bool has_resolver;
  qa_weight_config_store_bots_a *weights;
  qa_weapon_ai_options options;
  qa_ui_script_preprocessor_options preprocessor;
  qa_weapon_pointer_record *records;
  size_t record_count, record_capacity;
  qa_weapon_config_identity *identities;
  size_t identity_count, identity_capacity;
  int64_t next_pointer;
  qa_weapon_state_bots_a states[QA_MAX_WEAPON_STATES + 1];
  uint64_t next_serial;
  qa_weapon_diagnostic *reported;
  size_t reported_count, reported_capacity;
  qa_weapon_config_bots_a *current_config;
  qa_weapon_config_bots_a **configs;
  size_t config_count, config_capacity;
  int64_t generation, setup_revision;
} qa_weapon_ai_bots_a;
void qa_weapon_ai_bots_a_init(qa_weapon_ai_bots_a *w);
/* TS constructor(host, options): resolver is kept by value, weights is borrowed. */
void qa_weapon_ai_bots_a_configure(qa_weapon_ai_bots_a *w, const qa_bot_script_reader *resolver,
                                   qa_weight_config_store_bots_a *weights,
                                   const qa_weapon_ai_options *options);
void qa_weapon_ai_bots_a_destroy(qa_weapon_ai_bots_a *w);
const qa_weapon_diagnostic *qa_weapon_ai_diagnostics(const qa_weapon_ai_bots_a *w, size_t *out_count);
qa_weapon_config_bots_a *qa_weapon_ai_config(const qa_weapon_ai_bots_a *w);
/* path NULL is the TS default "weapons.c". */
qa_weapon_load_result qa_weapon_ai_setup(qa_weapon_ai_bots_a *w, const char *path);
int64_t qa_weapon_ai_allocate_state(qa_weapon_ai_bots_a *w);
void qa_weapon_ai_free_state(qa_weapon_ai_bots_a *w, int64_t handle);
void qa_weapon_ai_free_weights(qa_weapon_ai_bots_a *w, int64_t handle);
void qa_weapon_ai_reset_state(qa_weapon_ai_bots_a *w, int64_t handle);
qa_weapon_load_result qa_weapon_ai_load_weights(qa_weapon_ai_bots_a *w, int64_t handle, const char *path);
/* TS sourcePath thunk form; text is borrowed for the call. */
qa_weapon_load_result qa_weapon_ai_load_weights_fn(qa_weapon_ai_bots_a *w, int64_t handle,
                                                   const char *(*read)(void *ctx), void *ctx);
/* NULL is TS undefined; *out_length is the fixed 552-byte record. */
const uint8_t *qa_weapon_ai_weapon_info_bytes(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon,
                                              size_t *out_length);
bool qa_weapon_ai_get_weapon_info(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon,
                                  qa_weapon_info_bots_a *out);
/* False is TS null. */
bool qa_weapon_ai_evaluate_fight_weapon(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon,
                                        qa_weight_inventory inventory, float *out);
int64_t qa_weapon_ai_choose_best_fight_weapon(qa_weapon_ai_bots_a *w, int64_t handle,
                                              qa_weight_inventory inventory);
void qa_weapon_ai_shutdown(qa_weapon_ai_bots_a *w);
qa_save_value *qa_weapon_ai_checkpoint(qa_weapon_ai_bots_a *w, qa_bot_memory_capture *memory);
void qa_weapon_ai_restore(qa_weapon_ai_bots_a *w, const qa_save_value *value,
                          qa_bot_memory_restored *memory);
/* TS WeaponConfig surface. */
const char *qa_weapon_config_path(const qa_weapon_config_bots_a *c);
int64_t qa_weapon_config_capacity(const qa_weapon_config_bots_a *c);
int64_t qa_weapon_config_defined_count(const qa_weapon_config_bots_a *c);
size_t qa_weapon_config_projectile_count(const qa_weapon_config_bots_a *c);
/* TS weapons[index]: false is undefined. */
bool qa_weapon_config_weapon(const qa_weapon_config_bots_a *c, int64_t index, qa_weapon_info_bots_a *out);
void qa_weapon_config_projectile(const qa_weapon_config_bots_a *c, size_t index, qa_projectile_info_bots_a *out);
void qa_weapon_config_weapon_info(const qa_weapon_config_bots_a *c, int64_t index, qa_weapon_info_bots_a *out);
const uint8_t *qa_weapon_config_weapon_bytes(const qa_weapon_config_bots_a *c, int64_t index);
const uint8_t *qa_weapon_config_projectile_bytes(const qa_weapon_config_bots_a *c, int64_t index);
const qa_ui_script_diagnostic *qa_weapon_config_diagnostics(const qa_weapon_config_bots_a *c, size_t *out_count);
#endif

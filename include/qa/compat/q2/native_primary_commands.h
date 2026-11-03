/* Port of src/compat/q2/native-primary-commands.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_COMMANDS_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"

typedef struct qa_compat_q2_ammo_grant { double entry, join; const char *descriptor; const char *kind; } qa_compat_q2_ammo_grant;
typedef struct qa_compat_q2_native_primary_command_profile {
  qa_content_digest digest; qa_native_abi abi;
  double give_entry, give_weapons, give_ammo; double give_unknown_entry, give_unknown_join;
  const qa_compat_q2_ammo_grant *ammo_grants; size_t ammo_grant_count; double give_argc, give_argv;
  double drop_entry, drop_elig_entry, drop_elig_join;
  double client_pointer, client_weapon; bool has_client_ammo_index; double client_ammo_index; double client_inventory;
  double items_table, items_stride, items_count, items_classname, items_flags, items_weapon_flag, items_ammunition_flag, items_icon;
  const char *ammo_kind; double ammo_offset, ammo_label;
} qa_compat_q2_native_primary_command_profile;
typedef struct qa_compat_q2_native_primary_command_hooks {
  void *ctx; void (*give)(void *ctx, qa_actor_id a, const char *cat);
  bool (*give_item)(void *ctx, qa_actor_id a, const char *const *args, size_t n);
  void (*give_ammo)(void *ctx, qa_actor_id a, qa_item_id item, const char *kind, double amount);
  bool (*drop)(void *ctx, qa_actor_id a, qa_item_id *item, double *ammo);
} qa_compat_q2_native_primary_command_hooks;
typedef struct qa_compat_q2_native_primary_commands qa_compat_q2_native_primary_commands;
typedef struct qa_compat_q2_native_source_item { double index; qa_guest_address address; } qa_compat_q2_native_source_item;
typedef struct qa_compat_q2_native_inventory_slot { qa_item_id item; double index; bool weapon, ammunition; const char *icon; } qa_compat_q2_native_inventory_slot;
typedef struct qa_compat_q2_native_source_weapon { qa_item_id item; qa_item_id ammo; } qa_compat_q2_native_source_weapon;
/* Reads the original item table. Hook `drop` returns false for the TS null and
 * leaves *item NULL for a null item. Strings live as long as the object. */
qa_compat_q2_native_primary_commands *qa_compat_q2_native_primary_commands_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_command_profile *profile, qa_compat_q2_native_primary_command_hooks hooks);
/* False is the TS null. */
bool qa_compat_q2_native_primary_commands_source_item(const qa_compat_q2_native_primary_commands *s, qa_item_id item, qa_compat_q2_native_source_item *out);
/* NULL is the TS null. */
qa_item_id qa_compat_q2_native_primary_commands_item_at(const qa_compat_q2_native_primary_commands *s, qa_guest_address address);
const qa_compat_q2_native_inventory_slot *qa_compat_q2_native_primary_commands_inventory_slots(const qa_compat_q2_native_primary_commands *s, size_t *out_count);
const qa_compat_q2_native_source_weapon *qa_compat_q2_native_primary_commands_weapons(const qa_compat_q2_native_primary_commands *s, size_t *out_count);
/* TS withWeapon: run under the projected weapon pointer. item NULL = TS null. */
void qa_compat_q2_native_primary_commands_with_weapon(qa_compat_q2_native_primary_commands *s, qa_actor_id actor, qa_item_id item, void *ctx, void (*run)(void *ctx));
void qa_compat_q2_native_primary_commands_close(qa_compat_q2_native_primary_commands *s);
void qa_compat_q2_native_primary_commands_destroy(qa_compat_q2_native_primary_commands *s);
#endif

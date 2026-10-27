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
typedef struct qa_compat_q2_native_primary_commands {
  qa_compat_q2_native_primary_weapon_host host; const qa_compat_q2_native_primary_command_profile *profile;
  qa_compat_q2_native_primary_command_hooks hooks; bool closed;
} qa_compat_q2_native_primary_commands;
void qa_compat_q2_native_primary_commands_init(qa_compat_q2_native_primary_commands *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_command_profile *p, qa_compat_q2_native_primary_command_hooks h);
void qa_compat_q2_native_primary_commands_close(qa_compat_q2_native_primary_commands *s);
#endif

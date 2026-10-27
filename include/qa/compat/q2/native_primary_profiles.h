/* Port of src/compat/q2/native-primary-profiles.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_PROFILES_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_PROFILES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_pickups.h"
#include "qa/compat/q2/native_primary_commands.h"
#include "qa/compat/q2/native_primary_drop.h"
#include "qa/compat/q2/native_primary_inventory.h"
#include "qa/compat/q2/native_primary_player.h"
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/persistence/value.h"

/* Readers return false only on OOM; list arrays are malloc'd and
 * owned by the profile (release helpers below where lists exist). */
bool qa_compat_q2_read_native_primary_weapons(const qa_save_reader *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_weapon_profile *out);
bool qa_compat_q2_read_native_primary_player(const qa_save_reader *r, qa_content_digest d, qa_compat_q2_native_primary_player_profile *out);
bool qa_compat_q2_read_native_primary_commands(const qa_save_reader *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_command_profile *out);
bool qa_compat_q2_read_native_primary_inventory(const qa_save_reader *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_inventory_profile *out);
bool qa_compat_q2_read_native_primary_drop(const qa_save_reader *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_drop_profile *out);
bool qa_compat_q2_read_native_primary_pickups(const qa_save_reader *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_pickup_profile *out);
void qa_compat_q2_native_primary_weapon_profile_free(qa_compat_q2_native_primary_weapon_profile *p);
void qa_compat_q2_native_primary_command_profile_free(qa_compat_q2_native_primary_command_profile *p);
void qa_compat_q2_native_primary_inventory_profile_free(qa_compat_q2_native_primary_inventory_profile *p);
void qa_compat_q2_native_primary_drop_profile_free(qa_compat_q2_native_primary_drop_profile *p);
void qa_compat_q2_native_pickup_profile_free(qa_compat_q2_native_pickup_profile *p);
#endif

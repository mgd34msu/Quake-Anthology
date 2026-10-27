/* Port of src/compat/q2/native-primary.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_H
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

/* TS Classic/RereleasePrimaryWorldProfile (classic|rr world-profile.ts,
 * other slices) mirrored as opaque + provider fn+ctx. */
typedef struct qa_compat_q2_primary_world_providers {
  void *ctx;
  const void *(*pickups_classic)(void *ctx, qa_content_digest d, const qa_compat_q2_native_pickup_profile **out);
  const void *(*pickups_rerelease)(void *ctx, qa_content_digest d, const qa_compat_q2_native_pickup_profile **out);
  const void *(*world_classic)(void *ctx, qa_content_digest d);
  const void *(*world_rerelease)(void *ctx, qa_content_digest d);
  double classic_entity_bytes; double rerelease_edict_bytes;
} qa_compat_q2_primary_world_providers;
typedef struct qa_compat_q2_native_primary_profile {
  const qa_compat_q2_native_primary_weapon_profile *weapons;
  const qa_compat_q2_native_primary_player_profile *player;
  const qa_compat_q2_native_primary_command_profile *commands;
  const qa_compat_q2_native_primary_inventory_profile *inventory;
  const qa_compat_q2_native_primary_drop_profile *drop;
  const qa_compat_q2_native_pickup_profile *pickups;
  const char *edition; /* "classic"|"rerelease" */ const void *world;
} qa_compat_q2_native_primary_profile;
typedef struct qa_compat_q2_native_primary_declaration { qa_resolved_resource_reference declaration; qa_compat_q2_native_primary_profile profile; } qa_compat_q2_native_primary_declaration;
bool qa_compat_q2_builtin_native_primary(qa_content_digest digest, const char *edition, qa_compat_q2_primary_world_providers prov, qa_compat_q2_native_primary_profile *out);
#endif

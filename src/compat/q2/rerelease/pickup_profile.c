/* Port of src/compat/q2/rerelease/pickup-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/pickup_profile.h"
#include <string.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/client_profile.h"

static qa_compat_q2_native_pickup_profile profile_storage;
static qa_compat_q2_native_pickup_grant grants_storage[5];
static qa_compat_q2_pickup_consumer consumers_storage[1];
static double capacities_storage[12];
static bool built = false;
static void build(void) {
  if (built) return;
  built = true;
  qa_compat_q2_rerelease_layouts_ensure();
  static const qa_guest_value_layout p4b[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"uint8",{NULL,0,0,0,NULL,0}}};
  static const qa_guest_value_layout p2[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}}};
  static const qa_guest_value_layout p2i[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"int32",{NULL,0,0,0,NULL,0}}};
  qa_guest_value_layout bool_layout; memset(&bool_layout, 0, sizeof bool_layout); bool_layout.storage = "uint8";
  consumers_storage[0] = (qa_compat_q2_pickup_consumer){0x666c0,
      qa_compat_q2_rerelease_signature(p2, 1, true, qa_compat_q2_rr_int_layout), QA_PROTECTION_POWERED};
  grants_storage[0] = (qa_compat_q2_native_pickup_grant){0x67740, 0x677d4, 0x678e8, "regular", NULL, 0, false, NULL, 0, NULL, 0, 0, 0, 0};
  grants_storage[1] = (qa_compat_q2_native_pickup_grant){0x671e0, 0x67209, 0x67357, "inventory", consumers_storage, 1, true, "ammo", 0x67250, "rcx", 0, 0, 0, 0};
  grants_storage[2] = (qa_compat_q2_native_pickup_grant){0xefd80, 0xefe19, 0xefe31, "inventory", consumers_storage, 1, true, "weapon", 0, NULL, 0xefeac, 0xefeb3, 0xeff20, 0xeff33};
  grants_storage[3] = (qa_compat_q2_native_pickup_grant){0x667b0, 0x667c4, 0x66918, "inventory", consumers_storage, 1, false, NULL, 0, NULL, 0, 0, 0, 0};
  grants_storage[4] = (qa_compat_q2_native_pickup_grant){0x66960, 0x66974, 0x66ce4, "inventory", consumers_storage, 1, false, NULL, 0, NULL, 0, 0, 0, 0};
  for (int i = 0; i < 12; i++) capacities_storage[i] = 0xbd0 + i * 2;
  memset(&profile_storage, 0, sizeof profile_storage);
  profile_storage.touch = 0x67be0; profile_storage.grant_return = 0x67c95; profile_storage.targets_return = 0x67f27;
  profile_storage.touch_signature = qa_compat_q2_rerelease_signature(p4b, 4, true, bool_layout);
  profile_storage.grant_signature = qa_compat_q2_rerelease_signature(p2, 2, false, bool_layout);
  profile_storage.grants = grants_storage; profile_storage.grant_count = 5;
  profile_storage.items_table = 0x195320; profile_storage.items_stride = 192; profile_storage.items_count = 84;
  profile_storage.items_classname = 8; profile_storage.items_pickup = 16;
  profile_storage.entity_item = 0x860; profile_storage.entity_count = 0x7b8; profile_storage.entity_spawnflags = 0x5f0;
  profile_storage.entity_inuse = qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "inuse");
  profile_storage.entity_inuse_bytes = 1; profile_storage.has_entity_generation = true;
  profile_storage.entity_generation = qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_private_edict_prefix_layout, "spawn_count");
  profile_storage.time_address = 0x241b28; profile_storage.time_storage = "int64-milliseconds";
  profile_storage.supply_client = 0x78; profile_storage.supply_inventory = 0xa80; profile_storage.supply_flags = 0x7c;
  profile_storage.supply_weapon_flag = 1;
  profile_storage.ammo_entry = 0x670e0;
  profile_storage.ammo_signature = qa_compat_q2_rerelease_signature(p2i, 3, false, bool_layout);
  profile_storage.has_ammo_stop = true; profile_storage.ammo_stop = 0x6712e; profile_storage.ammo_tag = 0x90;
  profile_storage.ammo_capacities = capacities_storage; profile_storage.ammo_capacity_count = 12; profile_storage.ammo_capacity_bytes = 2;
}
const qa_compat_q2_native_pickup_profile *qa_compat_q2_rerelease_get_pickup_profile(qa_content_digest digest) {
  const qa_compat_q2_rerelease_client_profile *retail = qa_compat_q2_rerelease_get_retail_client_profile();
  if (!retail->authority_is_artifact || !digest || !retail->digest || strcmp(retail->digest, digest) != 0) return NULL;
  build();
  profile_storage.digest = digest;
  return &profile_storage;
}

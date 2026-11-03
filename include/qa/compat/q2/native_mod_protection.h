/* Port of src/compat/q2/native-mod-protection.ts. Component armor uses original
 * storage and original absorption code. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_PROTECTION_H
#define QA_COMPAT_Q2_NATIVE_MOD_PROTECTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_armor.h"
#include "qa/compat/q2/native_mod_pickups.h"
#include "qa/compat/q2/native_mod_region.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/mods.h"

/* TS NativeProtectionInventoryCommit. */
typedef struct qa_compat_q2_native_protection_inventory_commit {
  void *ctx; qa_actor_id actor; const qa_item_id *items; size_t item_count;
  void (*committed)(void *ctx, qa_inventory_committed_change change);
} qa_compat_q2_native_protection_inventory_commit;
/* TS Operations. slot's false is TS null; flush's inventory may be NULL.
 * invoke's region/authority may be NULL; its false is TS null. */
typedef struct qa_compat_q2_native_protection_operations {
  void *ctx;
  void (*current)(void *ctx);
  const qa_original_pickup_rule *(*pickups)(void *ctx, qa_actor_id actor, qa_protection_channel channel, size_t *out_count);
  bool (*slot)(void *ctx, qa_actor_id actor, double *out);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  double (*scalar)(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value);
  void (*transfer)(void *ctx, void (*invoke)(void *), void *invoke_ctx);
  void (*flush)(void *ctx, const qa_compat_q2_native_protection_inventory_commit *inventory);
  bool (*invoke)(void *ctx, const qa_native_mod_source_call *call, qa_compat_q2_native_pickup_inputs inputs, const qa_native_mod_protection_region *region, const qa_compat_q2_native_mod_region_authority *authority, double *out);
} qa_compat_q2_native_protection_operations;
typedef struct qa_compat_q2_native_mod_protection qa_compat_q2_native_mod_protection;
qa_compat_q2_native_mod_protection *qa_compat_q2_native_mod_protection_create(const qa_native_mod_protection_definition *definitions, size_t definition_count,
    const qa_native_mod_declaration *declaration, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_protection_operations operations);
void qa_compat_q2_native_mod_protection_attach(qa_compat_q2_native_mod_protection *s, const qa_compat_q2_native_mod_host *host);
void qa_compat_q2_native_mod_protection_reserve(qa_compat_q2_native_mod_protection *s);
void qa_compat_q2_native_mod_protection_reserve_actor(qa_compat_q2_native_mod_protection *s, qa_actor_id actor);
void qa_compat_q2_native_mod_protection_bind_actor(qa_compat_q2_native_mod_protection *s, qa_actor_id actor);
void qa_compat_q2_native_mod_protection_activate(qa_compat_q2_native_mod_protection *s);
void qa_compat_q2_native_mod_protection_prepare_restore(qa_compat_q2_native_mod_protection *s);
void qa_compat_q2_native_mod_protection_validate_restored_inventory(qa_compat_q2_native_mod_protection *s);
/* TS observe(actor, observer, invoke, assertCurrent?). assert_current may be NULL. */
void qa_compat_q2_native_mod_protection_observe(qa_compat_q2_native_mod_protection *s, qa_actor_id actor, qa_protection_observer observer, void *invoke_ctx, void (*invoke)(void *), void *assert_ctx, void (*assert_current)(void *));
void qa_compat_q2_native_mod_protection_release(qa_compat_q2_native_mod_protection *s, qa_actor_id actor);
void qa_compat_q2_native_mod_protection_close(qa_compat_q2_native_mod_protection *s);
void qa_compat_q2_native_mod_protection_destroy(qa_compat_q2_native_mod_protection *s);
#endif

/* Port of src/content/q3/equipment/inventory-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_INVENTORY_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_INVENTORY_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/contracts/execution.h"
/* Mirror of QvmMemory word reads (TS owner: src/compat/qvm/memory.ts). */
typedef struct qa_q3_equipment_qvm_memory { const uint8_t *bytes; size_t byte_length; } qa_q3_equipment_qvm_memory;
/* Mirror of QvmPublicInventoryProfile (TS owner:
 * src/compat/qvm/game-inventory.ts). The TS capacity() also receives a
 * module/client context this profile's closures never read, so the C
 * closure keeps only memory + weapon. */
typedef struct qa_q3_equipment_qvm_inventory_profile {
  qa_module_identity module; const char *abi_profile;
  double weapons_offset, ammo_offset;
  void *capacity_ctx;
  double (*capacity)(void *ctx, qa_q3_equipment_qvm_memory memory, double weapon);
} qa_q3_equipment_qvm_inventory_profile;
bool qa_q3_native_inventory_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_inventory_profile *out);
void qa_q3_equipment_qvm_inventory_profile_destroy(qa_q3_equipment_qvm_inventory_profile *profile);
#endif
